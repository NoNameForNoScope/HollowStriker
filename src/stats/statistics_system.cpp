#include "statistics_system.hpp"
#include "../sim/world.hpp"
#include "../sim/los.hpp"
#include "../core/ini.hpp"
#include <algorithm>
#include <cmath>
#include <ctime>

namespace bodycam {

namespace {
constexpr double kPi = 3.14159265358979323846;
double wrap(double a) {
    while (a > kPi) a -= 2.0 * kPi;
    while (a < -kPi) a += 2.0 * kPi;
    return a;
}
std::string now_stamp() {
    std::time_t now = std::time(nullptr);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &now);
#else
    localtime_r(&now, &tmv);
#endif
    char b[32];
    std::strftime(b, sizeof(b), "%Y-%m-%d %H:%M:%S", &tmv);
    return b;
}
// zona de impact (HitZone) -> categorie statistica: 0 cap, 1 trunchi, 2 brate, 3 picioare
int zone_category(int hit_zone) {
    switch (static_cast<HitZone>(hit_zone)) {
        case HitZone::Head: return 0;
        case HitZone::Torso: return 1;
        case HitZone::Arms: case HitZone::LeftArm: case HitZone::RightArm: return 2;
        case HitZone::Legs: case HitZone::LeftLeg: case HitZone::RightLeg: return 3;
        default: return 1;
    }
}
std::string weapon_label(const World& w, int idx) {
    if (idx < 0 || idx >= static_cast<int>(w.weapons.size())) return "";
    return w.weapons[idx].name;
}
}

bool StatsConfig::load(const std::string& path) {
    IniParser ini;
    if (!ini.load(path)) return false;
    sample_interval = ini.get_d("sampling", "interval", sample_interval);
    engagement_timeout = ini.get_d("engagement", "timeout", engagement_timeout);
    under_fire_window = ini.get_d("engagement", "under_fire_window", under_fire_window);
    aim_cone = ini.get_d("engagement", "aim_cone_deg", aim_cone * 180.0 / kPi) * kPi / 180.0;
    assist_window = ini.get_d("engagement", "assist_window", assist_window);
    sprint_memory = ini.get_d("engagement", "sprint_memory", sprint_memory);
    rounds_to_win = static_cast<int>(ini.get_d("match", "rounds_to_win", rounds_to_win));
    max_rounds = static_cast<int>(ini.get_d("match", "max_rounds", max_rounds));
    score_kill = static_cast<int>(ini.get_d("score", "kill", score_kill));
    score_assist = static_cast<int>(ini.get_d("score", "assist", score_assist));
    score_headshot = static_cast<int>(ini.get_d("score", "headshot", score_headshot));
    score_round = static_cast<int>(ini.get_d("score", "round_win", score_round));
    score_objective = static_cast<int>(ini.get_d("score", "objective", score_objective));
    score_damage = ini.get_d("score", "damage", score_damage);
    return true;
}

// ============================================================================
void StatisticsSystem::init(const std::string& data_dir, const std::string& config_path) {
    cfg.load(config_path);
    storage.dir = data_dir;
    matches = storage.load<MatchRecord>("matches.csv");
    rounds = storage.load<RoundRecord>("rounds.csv");
    shots = storage.load<ShotRecord>("shots.csv");
    reloads = storage.load<ReloadRecord>("reloads.csv");
    damage = storage.load<DamageRecord>("damage.csv");
    engagements = storage.load<EngagementRecord>("engagements.csv");
    range_sessions = storage.load<RangeSessionRecord>("range_sessions.csv");
    trials = storage.load<ExperimentTrial>("experiments.csv");
    next_id_ = 1;
    for (const auto& m : matches) next_id_ = std::max(next_id_, m.id + 1);
    for (const auto& r : range_sessions) next_id_ = std::max(next_id_, r.id + 1);
    version++;
}

void StatisticsSystem::reset_match_acc() {
    live_samples_.clear();
    live_shots_.clear();
    live_rounds_.clear();
    live_eng_.clear();
    live_reloads_.clear();
    live_damage_.clear();
    pending_.clear();
    shot_index_.clear();
    damaged_.clear();
    hr_sum_ = stress_sum_ = stamina_sum_ = fatigue_sum_ = move_pen_sum_ = hand_pen_sum_ = speed_sum_ = 0.0;
    body_steps_ = 0;
    prev_stamina_ = prev_fatigue_ = -1.0;
    last_threat_ = last_sprint_ = last_fire_ = -100.0;
    reload_start_ = -1.0;
    eng_active_ = false;
    t_ = 0.0;
    next_sample_ = 0.0;
    round_ = 0;
    round_open_ = false;
}

void StatisticsSystem::begin_match(int map_id, const std::string& map_name, GameMode mode) {
    if (training_) persist_range_shots();
    reset_match_acc();
    match_active_ = true;
    training_ = false;
    map_id_ = map_id;
    map_name_ = map_name;
    mode_ = mode;
    live_ = MatchRecord{};
    live_.id = next_id_++;
    live_.timestamp = now_stamp();
    live_.map_id = map_id;
    live_.map_name = map_name;
    live_.mode = static_cast<int>(mode);
    live_.hr_min = live_.stress_min = live_.stamina_min = 1e9;
    version++;
}

void StatisticsSystem::begin_session(int map_id, const std::string& map_name, bool training) {
    if (training_) persist_range_shots();
    reset_match_acc();
    match_active_ = false;
    training_ = training;
    map_id_ = map_id;
    map_name_ = map_name;
    live_ = MatchRecord{};
    live_.map_id = map_id;
    live_.map_name = map_name;
    version++;
}

void StatisticsSystem::reset_round_acc(int round) {
    rnd_ = RoundRecord{};
    rnd_.match = live_.id;
    rnd_.round = round;
    rnd_.map_id = map_id_;
    r_hr_sum_ = r_stress_sum_ = 0.0;
    r_steps_ = 0;
}

void StatisticsSystem::begin_round(const World& w, int local) {
    (void)local;
    round_ = w.game.round;
    reset_round_acc(round_);
    round_open_ = match_active_;
    round_start_ = t_;
    prev_stamina_ = prev_fatigue_ = -1.0;
    reload_start_ = -1.0;
    pending_.clear();
    damaged_.clear();
    eliminated_.clear();
}

int StatisticsSystem::context_now(const World& w) const {
    if (!w.range.enabled) return static_cast<int>(StatContext::Match);
    if (w.range.session_active()) return 10 + static_cast<int>(w.range.mode);
    return static_cast<int>(StatContext::RangeFree);
}

double StatisticsSystem::aimed_distance(const World& w, int local) const {
    const Agent& me = w.agents[local];
    double best = -1.0, best_ang = cfg.aim_cone;
    if (w.range.enabled) {
        for (const auto& t : w.range.targets) {
            if (!t.hittable()) continue;
            double ang = std::abs(wrap(std::atan2(t.y - me.y, t.x - me.x) - me.facing));
            if (ang < best_ang) { best_ang = ang; best = std::hypot(t.x - me.x, t.y - me.y); }
        }
        return best;
    }
    for (const auto& o : w.agents) {
        if (o.faction == me.faction || !o.active()) continue;
        double d = std::hypot(o.x - me.x, o.y - me.y);
        double ang = std::abs(wrap(std::atan2(o.y - me.y, o.x - me.x) - me.facing));
        if (ang > cfg.aim_cone || d > 150.0) continue;
        if (!check_los(w.map, me.x, me.y, o.x, o.y)) continue;
        if (best < 0.0 || d < best) best = d;
    }
    return best;
}

void StatisticsSystem::snapshot(const World& w, int local, ShotRecord& r) const {
    const Agent& me = w.agents[local];
    r.hr = me.phys.heart_rate;
    r.stress = me.phys.stress;
    r.stamina = me.stamina;
    r.fatigue = me.phys.fatigue;
    r.tremor = me.phys.tremor_amp;
    r.arm_injury = me.health.arm_injury();
    r.fine_motor = me.phys.fine_motor;
    r.speed = me.speed;
    r.aiming = me.aim > 0.5 ? 1 : 0;
    r.stance = me.prone > 0.5 ? 2 : (me.crouch > 0.5 ? 1 : 0);
    r.recent_sprint = (t_ - last_sprint_) <= cfg.sprint_memory ? 1 : 0;
    r.map_id = map_id_;
}

void StatisticsSystem::engagement_activity(double now, double dist) {
    if (!match_active_) return;
    if (!eng_active_) {
        eng_active_ = true;
        eng_ = EngagementRecord{};
        eng_.match = live_.id;
        eng_.round = round_;
        eng_.t_start = now;
        eng_dist_.clear();
        eng_hr_ = eng_stress_ = 0.0;
        eng_n_ = 0;
        eng_detect_ = -1.0;
    }
    eng_last_ = now;
    if (dist > 0.0) eng_dist_.push_back(dist);
}

void StatisticsSystem::close_engagement() {
    if (!eng_active_) return;
    eng_active_ = false;
    eng_.duration = std::max(0.0, eng_last_ - eng_.t_start);
    if (!eng_dist_.empty()) {
        double s = 0.0;
        for (double d : eng_dist_) s += d;
        eng_.dist_mean = s / eng_dist_.size();
        eng_.dist_min = *std::min_element(eng_dist_.begin(), eng_dist_.end());
        eng_.dist_max = *std::max_element(eng_dist_.begin(), eng_dist_.end());
    }
    if (eng_n_ > 0) { eng_.hr_mean = eng_hr_ / eng_n_; eng_.stress_mean = eng_stress_ / eng_n_; }
    live_eng_.push_back(eng_);
}

void StatisticsSystem::finish_shot(Pending& p, bool training_ctx) {
    (void)training_ctx;
    live_shots_.push_back(p.rec);
    shot_index_[p.shot] = live_shots_.size() - 1;
    const ShotRecord& r = live_shots_.back();
    if (match_active_ && r.context == static_cast<int>(StatContext::Match)) {
        if (r.hit) {
            live_.hits++; rnd_.hits++;
            if (r.zone == 0) { live_.headshots++; rnd_.headshots++; }
            else live_.bodyshots++;
            if (eng_active_) eng_.hits++;
        }
    }
}

void StatisticsSystem::finish_round(int winner, int local_faction) {
    if (!round_open_) return;
    round_open_ = false;
    rnd_.winner = winner;
    rnd_.duration = t_ - round_start_;
    if (r_steps_ > 0) { rnd_.hr_avg = r_hr_sum_ / r_steps_; rnd_.stress_avg = r_stress_sum_ / r_steps_; }
    live_rounds_.push_back(rnd_);
    if (winner == local_faction) live_.rounds_won++;
    else if (winner != 2) live_.rounds_lost++;
    close_engagement();
}

void StatisticsSystem::persist_range_shots() {
    if (live_shots_.empty()) return;
    storage.append("shots.csv", live_shots_);
    shots.insert(shots.end(), live_shots_.begin(), live_shots_.end());
    live_shots_.clear();
    shot_index_.clear();
    version++;
}

// ============================================================================
void StatisticsSystem::on_step(const World& w, const std::vector<Event>& evs, int local) {
    if (local < 0 || local >= static_cast<int>(w.agents.size())) return;
    if (!match_active_ && !training_) return;
    const Agent& me = w.agents[local];
    t_ += w.dt;
    const int faction = static_cast<int>(me.faction);

    auto find_pending = [&](int shot) -> ShotRecord* {
        if (shot < 0) return nullptr;
        for (auto& p : pending_) if (p.shot == shot) return &p.rec;
        auto it = shot_index_.find(shot);
        if (it != shot_index_.end() && it->second < live_shots_.size()) return &live_shots_[it->second];
        return nullptr;
    };

    for (const auto& e : evs) {
        switch (e.type) {
            case EventType::SHOT:
                if (e.a == local && e.shot >= 0) {
                    Pending p;
                    p.shot = e.shot;
                    snapshot(w, local, p.rec);
                    p.rec.match = match_active_ ? live_.id : range_session_id_;
                    p.rec.round = round_;
                    p.rec.t = t_;
                    p.rec.context = context_now(w);
                    p.rec.weapon = weapon_label(w, e.weapon);
                    p.rec.distance = aimed_distance(w, local);
                    pending_.push_back(p);
                    last_fire_ = t_;
                    if (match_active_) {
                        live_.shots++; rnd_.shots++;
                        if (p.rec.distance > 0.0) engagement_activity(t_, p.rec.distance);
                        if (eng_active_) {
                            eng_.shots++;
                            if (eng_.reaction < 0.0 && eng_detect_ >= 0.0) eng_.reaction = t_ - eng_detect_;
                        }
                    }
                } else if (e.a != local && e.b == local) {
                    last_threat_ = t_;
                    engagement_activity(t_, e.v1);
                }
                break;
            case EventType::HIT:
                if (e.a == local && e.b != local && e.b >= 0 && e.b < static_cast<int>(w.agents.size())) {
                    if (ShotRecord* r = find_pending(e.shot)) {
                        int cat = zone_category(static_cast<int>(e.v2));
                        r->zone = r->hit ? std::min(r->zone, cat) : cat;
                        r->hit = 1;
                        r->damage += e.v1;
                        r->distance = std::hypot(w.agents[e.b].x - me.x, w.agents[e.b].y - me.y);
                    }
                    if (match_active_) { live_.dmg_dealt += e.v1; rnd_.dmg_dealt += e.v1; }
                    damaged_[e.b] = t_;
                } else if (e.b == local) {
                    DamageRecord d;
                    d.match = live_.id;
                    d.round = round_;
                    d.t = t_;
                    d.zone = zone_to_body(static_cast<HitZone>(static_cast<int>(e.v2)));
                    d.damage = e.v1;
                    d.source = e.weapon == -2 ? 1 : e.weapon == -3 ? 2 : 0;
                    if (e.a >= 0 && e.a < static_cast<int>(w.agents.size())) d.distance = std::hypot(w.agents[e.a].x - me.x, w.agents[e.a].y - me.y);
                    d.hr = me.phys.heart_rate;
                    d.stress = me.phys.stress;
                    last_threat_ = t_;
                    if (match_active_) {
                        live_damage_.push_back(d);
                        live_.dmg_taken += e.v1; rnd_.dmg_taken += e.v1;
                        live_.injuries++;
                        double* zs[6] = {&live_.dmg_head, &live_.dmg_torso, &live_.dmg_larm, &live_.dmg_rarm, &live_.dmg_lleg, &live_.dmg_rleg};
                        if (d.zone >= 0 && d.zone < 6) *zs[d.zone] += e.v1;
                        engagement_activity(t_, d.distance);
                        if (eng_active_) eng_.damage_taken += e.v1;
                    }
                }
                break;
            case EventType::KILL:
                if (e.a == local && e.b != local) {
                    if (ShotRecord* r = find_pending(e.shot)) r->kill = 1;
                    // eliminarea a fost deja numarata cand inamicul a cazut (incapacitat)
                    if (match_active_ && !eliminated_.count(e.b)) { live_.kills++; rnd_.kills++; if (eng_active_) eng_.kills++; }
                    eliminated_[e.b] = 1;
                } else if (e.b == local) {
                    if (match_active_) { live_.deaths++; rnd_.deaths++; }
                } else if (e.b >= 0 && e.b < static_cast<int>(w.agents.size()) && w.agents[e.b].faction != me.faction) {
                    auto it = damaged_.find(e.b);
                    if (it != damaged_.end() && t_ - it->second <= cfg.assist_window && match_active_) live_.assists++;
                }
                break;
            case EventType::DOWNED:
                if (e.b == local && match_active_) { live_.downs++; rnd_.downs++; }
                if (e.a == local && e.b != local && e.b >= 0 && e.b < static_cast<int>(w.agents.size())
                    && w.agents[e.b].faction != me.faction && !eliminated_.count(e.b)) {
                    eliminated_[e.b] = 1;
                    if (ShotRecord* r = find_pending(e.shot)) r->kill = 1;
                    if (match_active_) { live_.kills++; rnd_.kills++; if (eng_active_) eng_.kills++; }
                }
                break;
            case EventType::NEAR_MISS:
                if (e.a == local) {
                    last_threat_ = t_;
                    double d = (e.b >= 0 && e.b < static_cast<int>(w.agents.size())) ? std::hypot(w.agents[e.b].x - me.x, w.agents[e.b].y - me.y) : -1.0;
                    engagement_activity(t_, d);
                }
                break;
            case EventType::DETECT:
                if (e.a == local) {
                    engagement_activity(t_, e.v1);
                    if (eng_active_ && eng_detect_ < 0.0) eng_detect_ = t_;
                }
                break;
            case EventType::RELOAD_START:
                if (e.a == local) {
                    reload_start_ = t_;
                    reload_cur_ = ReloadRecord{};
                    reload_cur_.match = match_active_ ? live_.id : range_session_id_;
                    reload_cur_.round = round_;
                    reload_cur_.t = t_;
                    reload_cur_.context = context_now(w);
                    const WeaponSlot* sl = me.cur_slot();
                    reload_cur_.weapon = sl ? weapon_label(w, sl->weapon) : "";
                    reload_cur_.hr = me.phys.heart_rate;
                    reload_cur_.stress = me.phys.stress;
                    reload_cur_.fatigue = me.phys.fatigue;
                    reload_cur_.fine_motor = me.phys.fine_motor;
                    reload_cur_.arm_injury = me.health.arm_injury();
                }
                break;
            case EventType::RELOAD_END:
                if (e.a == local && reload_start_ >= 0.0) {
                    reload_cur_.duration = t_ - reload_start_;
                    reload_cur_.interrupted = e.v1 < 0.0 ? 1 : 0;
                    live_reloads_.push_back(reload_cur_);
                    reload_start_ = -1.0;
                }
                break;
            case EventType::WEAPON_SWITCH:
                if (e.a == local && match_active_) live_.switches++;
                break;
            case EventType::JUMP:
                if (e.a == local && match_active_) live_.jumps++;
                break;
            case EventType::OBJECTIVE_REACHED:
                if (match_active_) live_.objective = 1;
                break;
            case EventType::TARGET_HIT:
                if (e.a == local) {
                    if (ShotRecord* r = find_pending(e.shot)) {
                        r->hit = 1;
                        r->zone = e.v1 < 0.5 ? 0 : 1;
                        r->offset = e.v3;
                        r->distance = e.v2;
                    }
                }
                break;
            case EventType::RANGE:
                if (e.a == local) {
                    auto ph = static_cast<RangePhase>(static_cast<int>(e.v2));
                    if (ph == RangePhase::Countdown) range_session_id_ = next_id_++;
                    if (ph == RangePhase::Done) record_range_session(w);
                }
                break;
            case EventType::ROUND_END:
                finish_round(static_cast<int>(e.v1), faction);
                break;
            default: break;
        }
    }

    // --- stari continue ale corpului (doar cand jucatorul e in corp, viu) ---
    const bool body = me.alive() && me.possess == Possession::Body;
    const double dt = w.dt;
    if (body) {
        const Physiology& ph = me.phys;
        if (match_active_) {
            body_steps_++;
            hr_sum_ += ph.heart_rate; stress_sum_ += ph.stress; stamina_sum_ += me.stamina; fatigue_sum_ += ph.fatigue;
            speed_sum_ += me.speed;
            move_pen_sum_ += me.health.leg_injury();
            hand_pen_sum_ += 1.0 - ph.fine_motor;
            live_.hr_min = std::min(live_.hr_min, ph.heart_rate);
            live_.hr_max = std::max(live_.hr_max, ph.heart_rate);
            live_.stress_min = std::min(live_.stress_min, ph.stress);
            live_.stress_max = std::max(live_.stress_max, ph.stress);
            live_.stamina_min = std::min(live_.stamina_min, me.stamina);
            live_.fatigue_max = std::max(live_.fatigue_max, ph.fatigue);
            live_.distance += me.speed * dt;
            if (me.sprinting) { live_.sprint_distance += me.speed * dt; live_.sprint_time += dt; }
            else if (me.speed > 0.3) live_.walk_time += dt;
            if (me.prone > 0.5) live_.prone_time += dt;
            else if (me.crouch > 0.5) live_.crouch_time += dt;
            if (me.aim > 0.5) live_.aim_time += dt;
            if (me.speed < 0.15) live_.stationary_time += dt;
            live_.speed_max = std::max(live_.speed_max, me.speed);
            if (prev_stamina_ >= 0.0) {
                double ds = me.stamina - prev_stamina_;
                (ds < 0.0 ? live_.stamina_used : live_.stamina_recovered) += std::abs(ds);
            }
            if (prev_fatigue_ >= 0.0) {
                double df = ph.fatigue - prev_fatigue_;
                (df > 0.0 ? live_.fatigue_gain : live_.fatigue_recovered) += std::abs(df);
            }
            if (eng_active_) { live_.combat_time += dt; eng_hr_ += ph.heart_rate; eng_stress_ += ph.stress; eng_n_++; }
            if (t_ - last_threat_ <= cfg.under_fire_window) live_.time_under_fire += dt;
            if (round_open_) {
                r_hr_sum_ += ph.heart_rate; r_stress_sum_ += ph.stress; r_steps_++;
                rnd_.hr_max = std::max(rnd_.hr_max, ph.heart_rate);
                rnd_.stress_max = std::max(rnd_.stress_max, ph.stress);
                rnd_.fatigue_end = ph.fatigue;
            }
        }
        if (me.sprinting) last_sprint_ = t_;
        prev_stamina_ = me.stamina;
        prev_fatigue_ = ph.fatigue;
    }

    // --- esantionare la interval fix ---
    if (t_ >= next_sample_) {
        next_sample_ += cfg.sample_interval;
        if (next_sample_ < t_) next_sample_ = t_ + cfg.sample_interval;
        if (body) {
            const Physiology& ph = me.phys;
            PhysioSample s;
            s.match = match_active_ ? live_.id : 0;
            s.round = round_;
            s.t = t_;
            s.hr = ph.heart_rate; s.stress = ph.stress; s.stamina = me.stamina; s.fatigue = ph.fatigue;
            s.exertion = ph.exertion; s.tremor = ph.tremor_amp; s.speed = me.speed; s.adrenaline = ph.adrenaline;
            s.aiming = me.aim > 0.5 ? 1 : 0;
            s.firing = (t_ - last_fire_) < 1.0 ? 1 : 0;
            s.under_fire = (t_ - last_threat_) <= cfg.under_fire_window ? 1 : 0;
            s.sprinting = me.sprinting ? 1 : 0;
            s.in_combat = eng_active_ ? 1 : 0;
            live_samples_.push_back(s);
            // poligonul nu are sfarsit de meci: pastram ultimele ~20 de minute pentru grafice
            if (!match_active_ && live_samples_.size() > 2400) live_samples_.erase(live_samples_.begin(), live_samples_.begin() + 400);
        }
        // inamicii vizibili mentin angajamentul si dau distanta de angajare
        if (match_active_ && body) {
            double best = -1.0;
            for (const auto& o : w.agents) {
                if (o.faction == me.faction || !o.active()) continue;
                double d = std::hypot(o.x - me.x, o.y - me.y);
                if (d > 120.0 || (best > 0.0 && d >= best)) continue;
                if (check_los(w.map, me.x, me.y, o.x, o.y)) best = d;
            }
            if (best > 0.0 && (eng_active_ || best < 60.0)) engagement_activity(t_, best);
        }
    }

    // --- focurile al caror glont nu mai zboara: lovit / ratat stabilit ---
    for (size_t i = 0; i < pending_.size();) {
        if (!w.shot_in_flight(local, pending_[i].shot)) {
            finish_shot(pending_[i], training_);
            pending_.erase(pending_.begin() + static_cast<long>(i));
        } else {
            ++i;
        }
    }
    if (eng_active_ && t_ - eng_last_ > cfg.engagement_timeout) close_engagement();
}

void StatisticsSystem::record_range_session(const World& w) {
    const RangeResult& r = w.range.result;
    RangeSessionRecord s;
    s.id = range_session_id_ > 0 ? range_session_id_ : next_id_++;
    s.timestamp = now_stamp();
    s.mode = static_cast<int>(r.mode);
    s.weapon = weapon_label(w, r.weapon);
    s.distance = r.distance;
    s.size = r.size_idx;
    s.shots = r.shots; s.hits = r.hits; s.head = r.head; s.body = r.body;
    s.accuracy = r.accuracy();
    s.duration = r.duration;
    s.spm = r.duration > 0.0 ? r.shots / (r.duration / 60.0) : 0.0;
    if (!r.reactions.empty()) {
        std::vector<double> v = r.reactions;
        double sum = 0.0;
        for (double x : v) sum += x;
        s.react_mean = sum / v.size();
        std::sort(v.begin(), v.end());
        s.react_median = v.size() % 2 ? v[v.size() / 2] : 0.5 * (v[v.size() / 2 - 1] + v[v.size() / 2]);
        double ss = 0.0;
        for (double x : v) ss += (x - s.react_mean) * (x - s.react_mean);
        s.react_sd = v.size() > 1 ? std::sqrt(ss / (v.size() - 1)) : 0.0;
        s.react_n = static_cast<int>(v.size());
    }
    if (!r.hit_times.empty()) {
        double sum = 0.0;
        for (double x : r.hit_times) sum += x;
        s.hit_time_mean = sum / r.hit_times.size();
    }
    s.timeouts = r.timeouts;
    s.total_time = r.total_time;
    s.mre = r.mre;
    s.hr_mean = r.hr_mean; s.hr_max = r.hr_max; s.stress_mean = r.stress_mean;
    double* acc[4] = {&s.acc_d0, &s.acc_d1, &s.acc_d2, &s.acc_d3};
    double* mre[4] = {&s.mre_d0, &s.mre_d1, &s.mre_d2, &s.mre_d3};
    for (size_t d = 0; d < r.shots_d.size() && d < 4; ++d) {
        if (r.shots_d[d] > 0) *acc[d] = static_cast<double>(r.hits_d[d]) / r.shots_d[d];
        *mre[d] = r.mre_d[d];
    }
    s.shots_rest = r.shots_rest; s.hits_rest = r.hits_rest; s.shots_post = r.shots_post; s.hits_post = r.hits_post;
    s.hr_rest = r.hr_rest; s.hr_post = r.hr_post; s.stress_rest = r.stress_rest; s.stress_post = r.stress_post;
    s.react_rest = r.react_rest; s.react_post = r.react_post; s.mre_rest = r.mre_rest; s.mre_post = r.mre_post;
    range_sessions.push_back(s);
    storage.append_one("range_sessions.csv", s);
    // focurile sesiunii (si cele libere de dinainte) se salveaza acum; reincarcarile la fel
    for (auto& p : pending_) finish_shot(p, true);
    pending_.clear();
    persist_range_shots();
    if (!live_reloads_.empty()) {
        storage.append("reloads.csv", live_reloads_);
        reloads.insert(reloads.end(), live_reloads_.begin(), live_reloads_.end());
        live_reloads_.clear();
    }
    range_session_id_ = 0;
    version++;
}

void StatisticsSystem::end_match(const World& w, int local, const std::string& result) {
    if (!match_active_) return;
    for (auto& p : pending_) finish_shot(p, false);
    pending_.clear();
    if (round_open_ && local >= 0 && local < static_cast<int>(w.agents.size()) && result == "A") round_open_ = false;
    close_engagement();
    MatchRecord& m = live_;
    m.result = result;
    m.rounds = static_cast<int>(live_rounds_.size());
    m.duration = t_;
    m.round_avg = m.rounds > 0 ? t_ / m.rounds : 0.0;
    if (body_steps_ > 0) {
        double n = static_cast<double>(body_steps_);
        m.hr_avg = hr_sum_ / n; m.stress_avg = stress_sum_ / n; m.stamina_avg = stamina_sum_ / n;
        m.fatigue_avg = fatigue_sum_ / n; m.speed_avg = speed_sum_ / n;
        m.move_penalty_avg = move_pen_sum_ / n; m.handling_penalty_avg = hand_pen_sum_ / n;
    }
    if (m.hr_min > 1e8) m.hr_min = 0.0;
    if (m.stress_min > 1e8) m.stress_min = 0.0;
    if (m.stamina_min > 1e8) m.stamina_min = 0.0;
    int nr = 0;
    double rs = 0.0;
    for (const auto& r : live_reloads_) if (!r.interrupted) { rs += r.duration; nr++; }
    m.reloads = static_cast<int>(live_reloads_.size());
    m.reload_avg = nr ? rs / nr : 0.0;
    m.engagements = static_cast<int>(live_eng_.size());
    if (!live_eng_.empty()) {
        double dur = 0.0, dist = 0.0, react = 0.0;
        int nd = 0, nre = 0;
        m.eng_dist_min = 1e9;
        for (const auto& e : live_eng_) {
            dur += e.duration;
            if (e.dist_mean > 0.0) {
                dist += e.dist_mean; nd++;
                m.eng_dist_min = std::min(m.eng_dist_min, e.dist_min);
                m.eng_dist_max = std::max(m.eng_dist_max, e.dist_max);
            }
            if (e.reaction >= 0.0) { react += e.reaction; nre++; }
        }
        m.eng_duration_avg = dur / live_eng_.size();
        m.eng_dist_avg = nd ? dist / nd : 0.0;
        if (!nd) m.eng_dist_min = 0.0;
        m.reaction_avg = nre ? react / nre : -1.0;
    }
    // arma folosita cel mai mult
    std::map<std::string, int> used;
    for (const auto& s : live_shots_) used[s.weapon]++;
    int best = 0;
    for (const auto& kv : used) if (kv.second > best) { best = kv.second; m.weapon_main = kv.first; }
    m.score = m.kills * cfg.score_kill + m.assists * cfg.score_assist + m.headshots * cfg.score_headshot
            + m.rounds_won * cfg.score_round + m.objective * cfg.score_objective
            + static_cast<int>(m.dmg_dealt * cfg.score_damage);

    // --- salvare (o singura data, la sfarsitul meciului) ---
    storage.append_one("matches.csv", m);
    storage.append("rounds.csv", live_rounds_);
    storage.append("shots.csv", live_shots_);
    storage.append("reloads.csv", live_reloads_);
    storage.append("damage.csv", live_damage_);
    storage.append("engagements.csv", live_eng_);
    storage.append("samples.csv", live_samples_);
    matches.push_back(m);
    rounds.insert(rounds.end(), live_rounds_.begin(), live_rounds_.end());
    shots.insert(shots.end(), live_shots_.begin(), live_shots_.end());
    reloads.insert(reloads.end(), live_reloads_.begin(), live_reloads_.end());
    damage.insert(damage.end(), live_damage_.begin(), live_damage_.end());
    engagements.insert(engagements.end(), live_eng_.begin(), live_eng_.end());
    sample_cache_[m.id] = live_samples_;
    match_active_ = false;
    version++;
}

void StatisticsSystem::shutdown(const World& w, int local) {
    if (match_active_) {
        if (!live_rounds_.empty()) end_match(w, local, "A");
        else match_active_ = false;
    }
    if (training_) {
        for (auto& p : pending_) finish_shot(p, true);
        pending_.clear();
        persist_range_shots();
        if (!live_reloads_.empty()) {
            storage.append("reloads.csv", live_reloads_);
            reloads.insert(reloads.end(), live_reloads_.begin(), live_reloads_.end());
            live_reloads_.clear();
        }
    }
}

void StatisticsSystem::add_trials(const std::vector<ExperimentTrial>& t) {
    if (t.empty()) return;
    storage.append("experiments.csv", t);
    trials.insert(trials.end(), t.begin(), t.end());
    version++;
}

const std::vector<PhysioSample>& StatisticsSystem::samples_of(int64_t match_id) {
    if (match_active_ && match_id == live_.id) return live_samples_;
    auto it = sample_cache_.find(match_id);
    if (it != sample_cache_.end()) return it->second;
    std::vector<PhysioSample> v = storage.load_if<PhysioSample>("samples.csv", [&](const PhysioSample& s) { return s.match == match_id; });
    return sample_cache_.emplace(match_id, std::move(v)).first->second;
}

std::vector<ShotRecord> StatisticsSystem::all_shots() const {
    std::vector<ShotRecord> v = shots;
    v.insert(v.end(), live_shots_.begin(), live_shots_.end());
    return v;
}

std::vector<ReloadRecord> StatisticsSystem::all_reloads() const {
    std::vector<ReloadRecord> v = reloads;
    v.insert(v.end(), live_reloads_.begin(), live_reloads_.end());
    return v;
}

}
