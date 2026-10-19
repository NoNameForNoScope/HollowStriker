#include "shooting_range.hpp"
#include "world.hpp"
#include "../core/ini.hpp"
#include "../core/rng.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace bodycam {

namespace {
constexpr double kPi = 3.14159265358979323846;

std::vector<double> parse_list(const std::string& s, const std::vector<double>& def) {
    if (s.empty()) return def;
    std::vector<double> out;
    std::stringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        try { out.push_back(std::stod(tok)); } catch (...) {}
    }
    return out.empty() ? def : out;
}
std::vector<int> to_int(const std::vector<double>& v) {
    std::vector<int> o;
    for (double d : v) o.push_back(static_cast<int>(d));
    return o;
}
std::vector<double> to_double(const std::vector<int>& v) {
    std::vector<double> o;
    for (int d : v) o.push_back(d);
    return o;
}
}

const char* range_mode_name(RangeMode m) {
    switch (m) {
        case RangeMode::Free: return "LIBER";
        case RangeMode::Accuracy: return "PRECIZIE";
        case RangeMode::Reaction: return "REACTIE";
        case RangeMode::Speed: return "VITEZA";
        case RangeMode::Distance: return "DISTANTA";
        case RangeMode::Stress: return "STRES";
        default: return "?";
    }
}

const char* range_phase_name(RangePhase p) {
    switch (p) {
        case RangePhase::Idle: return "INACTIV";
        case RangePhase::Countdown: return "PREGATIRE";
        case RangePhase::Calm: return "CALMARE";
        case RangePhase::RestShoot: return "TRAGERE IN REPAUS";
        case RangePhase::Sprint: return "SPRINT";
        case RangePhase::PostShoot: return "TRAGERE DUPA SPRINT";
        case RangePhase::Running: return "IN DESFASURARE";
        case RangePhase::Done: return "TERMINAT";
    }
    return "?";
}

bool RangeConfig::load(const std::string& path) {
    IniParser ini;
    if (!ini.load(path)) return false;
    firing_line_x = ini.get_d("lanes", "firing_line_x", firing_line_x);
    lane_y = parse_list(ini.get("lanes", "lane_y", ""), lane_y);
    distances = parse_list(ini.get("lanes", "distances", ""), distances);
    free_distance = to_int(parse_list(ini.get("lanes", "free_distance", ""), to_double(free_distance)));
    moving_lanes = to_int(parse_list(ini.get("lanes", "moving_lanes", ""), to_double(moving_lanes)));
    head_r = ini.get_d("target", "head_r", head_r);
    head_z = ini.get_d("target", "head_z", head_z);
    body_w = ini.get_d("target", "body_w", body_w);
    body_z0 = ini.get_d("target", "body_z0", body_z0);
    body_z1 = ini.get_d("target", "body_z1", body_z1);
    sizes[0] = ini.get_d("target", "size_s", sizes[0]);
    sizes[1] = ini.get_d("target", "size_m", sizes[1]);
    sizes[2] = ini.get_d("target", "size_l", sizes[2]);
    move_speed = ini.get_d("target", "move_speed", move_speed);
    move_amp = ini.get_d("target", "move_amp", move_amp);
    raise_time = ini.get_d("target", "raise_time", raise_time);
    reset_time = ini.get_d("target", "reset_time", reset_time);
    accuracy_rounds = static_cast<int>(ini.get_d("accuracy", "rounds", accuracy_rounds));
    reaction_trials = static_cast<int>(ini.get_d("reaction", "trials", reaction_trials));
    reaction_delay_min = ini.get_d("reaction", "delay_min", reaction_delay_min);
    reaction_delay_max = ini.get_d("reaction", "delay_max", reaction_delay_max);
    reaction_expose = ini.get_d("reaction", "expose", reaction_expose);
    speed_targets = static_cast<int>(ini.get_d("speed", "targets", speed_targets));
    distance_shots = static_cast<int>(ini.get_d("distance", "shots", distance_shots));
    stress_shots = static_cast<int>(ini.get_d("stress", "shots", stress_shots));
    stress_calm_hr = ini.get_d("stress", "calm_hr", stress_calm_hr);
    stress_sprint_hr = ini.get_d("stress", "sprint_hr", stress_sprint_hr);
    stress_sprint_delta = ini.get_d("stress", "sprint_hr_delta", stress_sprint_delta);
    stress_sprint_stamina = ini.get_d("stress", "sprint_stamina", stress_sprint_stamina);
    countdown = ini.get_d("session", "countdown", countdown);
    return true;
}

// ============================================================================
int ShootingRange::target_index(int entity_id) {
    if (entity_id < World::kRangeTargetIdBase) return -1;
    return (entity_id - World::kRangeTargetIdBase) / 2;
}

void ShootingRange::setup(World& w, const std::string& config_path) {
    targets.clear();
    enabled = !config_path.empty() && cfg.load(config_path);
    phase = RangePhase::Idle;
    mode = RangeMode::Free;
    if (!enabled) return;
    for (size_t l = 0; l < cfg.lane_y.size(); ++l) {
        for (size_t d = 0; d < cfg.distances.size(); ++d) {
            RangeTarget t;
            t.id = static_cast<int>(targets.size());
            t.lane = static_cast<int>(l);
            t.dist_idx = static_cast<int>(d);
            t.distance = cfg.distances[d];
            t.x = cfg.firing_line_x + cfg.distances[d];
            t.y = t.base_y = cfg.lane_y[l];
            t.moving = std::find(cfg.moving_lanes.begin(), cfg.moving_lanes.end(), static_cast<int>(l)) != cfg.moving_lanes.end();
            t.move_phase = 0.7 * static_cast<double>(d) + 1.3 * static_cast<double>(l);
            int cx = static_cast<int>(std::floor(t.x)), cy = static_cast<int>(std::floor(t.y));
            if (cx <= 0 || cy <= 0 || cx >= w.map.width - 1 || cy >= w.map.height - 1 || w.map.is_wall(cx, cy)) continue;
            targets.push_back(t);
        }
    }
    for (size_t i = 0; i < targets.size(); ++i) targets[i].id = static_cast<int>(i);
    free_mode_targets();
    for (auto& t : targets) if (t.want_up) t.raise = 1.0;
}

int ShootingRange::find_target(int lane_i, int dist_i) const {
    for (const auto& t : targets) if (t.lane == lane_i && t.dist_idx == dist_i) return t.id;
    return -1;
}

void ShootingRange::lower_all() {
    for (auto& t : targets) { t.want_up = false; t.expose_until = -1.0; t.down_timer = 0.0; }
}

void ShootingRange::free_mode_targets() {
    lower_all();
    for (auto& t : targets) {
        int want = (t.lane < static_cast<int>(cfg.free_distance.size())) ? cfg.free_distance[t.lane] : 1;
        t.want_up = t.dist_idx == want;
        t.reactive = true;
        t.size = cfg.sizes[1];
    }
}

void ShootingRange::raise_target(World& w, int idx, double expose) {
    if (idx < 0 || idx >= static_cast<int>(targets.size())) return;
    RangeTarget& t = targets[idx];
    t.want_up = true;
    t.down_timer = 0.0;
    t.size = cfg.sizes[std::clamp(size_idx, 0, 2)];
    t.appear_time = -1.0;
    t.expose_until = expose > 0.0 ? w.current_time + cfg.raise_time + expose : -1.0;
    t.marks.clear();
    t.last_shot = -1;
}

void ShootingRange::set_phase(World& w, RangePhase p) {
    phase = p;
    phase_start = w.current_time;
    w.emit(EventType::RANGE, shooter, -1, 0.0, 0.0, static_cast<double>(mode), static_cast<double>(p), 0.0,
           static_cast<double>(trial));
}

void ShootingRange::begin_shoot_phase(World& w, RangePhase p) {
    phase_shots = 0;
    phase_hits_ = 0;
    phase_first_shot_ = -1.0;
    phase_hr_sum_ = phase_stress_sum_ = 0.0;
    phase_phys_n_ = 0;
    phase_mre_sum_ = 0.0;
    phase_mre_n_ = 0;
    set_phase(w, p);
}

void ShootingRange::end_shoot_phase(bool rest) {
    double hr = phase_phys_n_ ? phase_hr_sum_ / phase_phys_n_ : 0.0;
    double st = phase_phys_n_ ? phase_stress_sum_ / phase_phys_n_ : 0.0;
    double react = phase_first_shot_ >= 0.0 ? phase_first_shot_ : 0.0;
    double mre = phase_mre_n_ ? phase_mre_sum_ / phase_mre_n_ : 0.0;
    if (rest) {
        cur_.shots_rest = phase_shots; cur_.hits_rest = phase_hits_;
        cur_.hr_rest = hr; cur_.stress_rest = st; cur_.react_rest = react; cur_.mre_rest = mre;
    } else {
        cur_.shots_post = phase_shots; cur_.hits_post = phase_hits_;
        cur_.hr_post = hr; cur_.stress_post = st; cur_.react_post = react; cur_.mre_post = mre;
    }
}

void ShootingRange::start(World& w, RangeMode m, int agent, int dist_index, int size_index) {
    if (!enabled || agent < 0 || agent >= static_cast<int>(w.agents.size())) return;
    if (m == RangeMode::Free) { stop(w); return; }
    mode = m;
    shooter = agent;
    dist_idx = std::clamp(dist_index, 0, static_cast<int>(cfg.distances.size()) - 1);
    size_idx = std::clamp(size_index, 0, 2);
    // culoarul din dreptul tragatorului
    const Agent& a = w.agents[agent];
    lane = 0;
    double best = 1e9;
    for (size_t l = 0; l < cfg.lane_y.size(); ++l) {
        bool moving = std::find(cfg.moving_lanes.begin(), cfg.moving_lanes.end(), static_cast<int>(l)) != cfg.moving_lanes.end();
        double d = std::abs(cfg.lane_y[l] - a.y) + (moving ? 0.5 : 0.0);
        if (d < best) { best = d; lane = static_cast<int>(l); }
    }
    cur_ = RangeResult{};
    cur_.mode = m;
    cur_.distance = cfg.distances[dist_idx];
    cur_.size_idx = size_idx;
    cur_.shots_d.assign(cfg.distances.size(), 0);
    cur_.hits_d.assign(cfg.distances.size(), 0);
    cur_.mre_d.assign(cfg.distances.size(), 0.0);
    mre_sum_d_.assign(cfg.distances.size(), 0.0);
    mre_n_d_.assign(cfg.distances.size(), 0);
    hr_sum_ = stress_sum_ = 0.0;
    phys_n_ = 0;
    mre_sum_ = 0.0;
    mre_n_ = 0;
    trial = 0;
    phase_shots = 0;
    last_counted_shot_ = -1;
    last_hit_shot_ = -1;
    active_target_ = -1;
    trial_appear_ = -1.0;
    trial_shot_ = false;
    dist_step_ = 0;
    speed_left_ = 0;
    session_start = w.current_time;
    lower_all();
    for (auto& t : targets) t.reactive = false;
    begin_shoot_phase(w, RangePhase::Countdown);
}

void ShootingRange::stop(World& w) {
    if (session_active()) set_phase(w, RangePhase::Idle);
    phase = RangePhase::Idle;
    mode = RangeMode::Free;
    free_mode_targets();
}

void ShootingRange::finish(World& w) {
    cur_.duration = w.current_time - session_start;
    cur_.mre = mre_n_ ? mre_sum_ / mre_n_ : 0.0;
    cur_.hr_mean = phys_n_ ? hr_sum_ / phys_n_ : 0.0;
    cur_.stress_mean = phys_n_ ? stress_sum_ / phys_n_ : 0.0;
    for (size_t d = 0; d < cur_.mre_d.size(); ++d) cur_.mre_d[d] = mre_n_d_[d] ? mre_sum_d_[d] / mre_n_d_[d] : 0.0;
    result = cur_;
    result_seq++;
    set_phase(w, RangePhase::Done);
    free_mode_targets();
}

std::string ShootingRange::instruction(const World& w) const {
    char buf[96];
    const Agent* a = (shooter >= 0 && shooter < static_cast<int>(w.agents.size())) ? &w.agents[shooter] : nullptr;
    switch (phase) {
        case RangePhase::Idle: return "MOD LIBER - TINTELE SE RIDICA SINGURE";
        case RangePhase::Done: {
            std::snprintf(buf, sizeof(buf), "%s TERMINAT: %d/%d LOVITURI (%.0f%%)", range_mode_name(result.mode), result.hits,
                          result.shots, result.accuracy() * 100.0);
            return buf;
        }
        case RangePhase::Countdown:
            std::snprintf(buf, sizeof(buf), "%s: PREGATESTE-TE %.0f", range_mode_name(mode),
                          std::ceil(std::max(0.0, cfg.countdown - (w.current_time - phase_start))));
            return buf;
        case RangePhase::Calm:
            std::snprintf(buf, sizeof(buf), "STRES: CALMEAZA-TE  PULS %.0f > %.0f", a ? a->phys.heart_rate : 0.0, cfg.stress_calm_hr);
            return buf;
        case RangePhase::RestShoot:
            std::snprintf(buf, sizeof(buf), "STRES: TRAGE %d/%d IN REPAUS (%.0fM)", phase_shots, cfg.stress_shots, cfg.distances[dist_idx]);
            return buf;
        case RangePhase::Sprint:
            std::snprintf(buf, sizeof(buf), "STRES: SPRINT!  PULS %.0f/%.0f  STAMINA %.0f%%", a ? a->phys.heart_rate : 0.0,
                          sprint_target(), a ? a->stamina * 100.0 : 0.0);
            return buf;
        case RangePhase::PostShoot:
            std::snprintf(buf, sizeof(buf), "STRES: TRAGE %d/%d DUPA SPRINT", phase_shots, cfg.stress_shots);
            return buf;
        case RangePhase::Running:
            switch (mode) {
                case RangeMode::Accuracy:
                    std::snprintf(buf, sizeof(buf), "PRECIZIE %.0fM: %d/%d FOCURI  %d LOVITURI", cfg.distances[dist_idx], cur_.shots,
                                  cfg.accuracy_rounds, cur_.hits);
                    return buf;
                case RangeMode::Reaction:
                    std::snprintf(buf, sizeof(buf), "REACTIE: PROBA %d/%d", std::min(trial + 1, cfg.reaction_trials), cfg.reaction_trials);
                    return buf;
                case RangeMode::Speed:
                    std::snprintf(buf, sizeof(buf), "VITEZA: %d TINTE  %.2fS", speed_left_, w.current_time - phase_start);
                    return buf;
                case RangeMode::Distance:
                    std::snprintf(buf, sizeof(buf), "DISTANTA %.0fM: %d/%d FOCURI",
                                  cfg.distances[std::min(dist_step_, static_cast<int>(cfg.distances.size()) - 1)], phase_shots, cfg.distance_shots);
                    return buf;
                default: break;
            }
            break;
    }
    return range_mode_name(mode);
}

// ============================================================================
void ShootingRange::update(World& w) {
    if (!enabled) return;
    const double dt = w.dt;
    const double now = w.current_time;
    // --- animatia tintelor: ridicare / coborare, miscarea laterala, resetarea celor reactive ---
    for (auto& t : targets) {
        if (t.down_timer > 0.0) {
            t.down_timer -= dt;
            if (t.down_timer <= 0.0 && phase == RangePhase::Idle) { t.want_up = true; t.marks.clear(); }
        }
        if (t.expose_until > 0.0 && now >= t.expose_until) { t.want_up = false; t.expose_until = -1.0; }
        double before = t.raise;
        double rate = dt / std::max(0.05, cfg.raise_time);
        t.raise = std::clamp(t.raise + (t.want_up ? rate : -rate), 0.0, 1.0);
        if (before <= 0.6 && t.raise > 0.6) {
            t.appear_time = now;
            w.emit(EventType::TARGET_UP, shooter, t.id, t.x, t.y, t.distance);
        }
        if (t.moving && t.raise > 0.0) {
            t.move_phase += dt * cfg.move_speed / std::max(0.2, cfg.move_amp);
            t.y = t.base_y + cfg.move_amp * std::sin(t.move_phase);
        }
    }
    if (!session_active()) return;
    if (shooter < 0 || shooter >= static_cast<int>(w.agents.size())) { stop(w); return; }
    const Agent& a = w.agents[shooter];
    if (!a.alive()) { stop(w); return; }
    bool shooting_phase = phase == RangePhase::Running || phase == RangePhase::RestShoot || phase == RangePhase::PostShoot;
    if (shooting_phase) {
        hr_sum_ += a.phys.heart_rate; stress_sum_ += a.phys.stress; phys_n_++;
        cur_.hr_max = std::max(cur_.hr_max, a.phys.heart_rate);
        phase_hr_sum_ += a.phys.heart_rate; phase_stress_sum_ += a.phys.stress; phase_phys_n_++;
    }
    const double pt = now - phase_start;
    const int nd = static_cast<int>(cfg.distances.size());
    switch (phase) {
        case RangePhase::Countdown:
            if (pt < cfg.countdown) break;
            if (mode == RangeMode::Stress) { set_phase(w, RangePhase::Calm); break; }
            begin_shoot_phase(w, RangePhase::Running);
            if (mode == RangeMode::Accuracy) {
                active_target_ = find_target(lane, dist_idx);
                raise_target(w, active_target_);
            } else if (mode == RangeMode::Distance) {
                dist_step_ = 0;
                active_target_ = find_target(lane, 0);
                raise_target(w, active_target_);
            } else if (mode == RangeMode::Reaction) {
                next_event = now + cfg.reaction_delay_min
                           + (cfg.reaction_delay_max - cfg.reaction_delay_min) * u01(w.seed, 501, trial, 0, 0);
            } else if (mode == RangeMode::Speed) {
                // tinte pe culoare diferite, la 10 / 25 / 50 m, ridicate simultan
                int lanes = static_cast<int>(cfg.lane_y.size());
                speed_left_ = 0;
                for (int k = 0; k < cfg.speed_targets; ++k) {
                    int li = (lane + k) % std::max(1, lanes);
                    int di = k % std::min(3, nd);
                    int id = find_target(li, di);
                    if (id < 0 || targets[id].want_up) continue;
                    raise_target(w, id);
                    targets[id].reactive = true;
                    speed_left_++;
                }
            }
            break;
        case RangePhase::Calm:
            if (a.phys.heart_rate <= cfg.stress_calm_hr) {
                begin_shoot_phase(w, RangePhase::RestShoot);
                active_target_ = find_target(lane, dist_idx);
                raise_target(w, active_target_);
            }
            break;
        case RangePhase::RestShoot:
            if (phase_shots >= cfg.stress_shots && !w.shot_in_flight(shooter, last_counted_shot_)) {
                end_shoot_phase(true);
                lower_all();
                set_phase(w, RangePhase::Sprint);
            }
            break;
        case RangePhase::Sprint:
            if (a.phys.heart_rate >= sprint_target() && a.stamina <= cfg.stress_sprint_stamina) {
                begin_shoot_phase(w, RangePhase::PostShoot);
                active_target_ = find_target(lane, dist_idx);
                raise_target(w, active_target_);
            }
            break;
        case RangePhase::PostShoot:
            if (phase_shots >= cfg.stress_shots && !w.shot_in_flight(shooter, last_counted_shot_)) {
                end_shoot_phase(false);
                cur_.distance = cfg.distances[dist_idx];
                finish(w);
            }
            break;
        case RangePhase::Running:
            if (mode == RangeMode::Accuracy) {
                if (cur_.shots >= cfg.accuracy_rounds && !w.shot_in_flight(shooter, last_counted_shot_)) finish(w);
            } else if (mode == RangeMode::Distance) {
                if (phase_shots >= cfg.distance_shots && !w.shot_in_flight(shooter, last_counted_shot_)) {
                    dist_step_++;
                    phase_shots = 0;
                    lower_all();
                    if (dist_step_ >= nd) { finish(w); break; }
                    active_target_ = find_target(lane, dist_step_);
                    raise_target(w, active_target_);
                }
            } else if (mode == RangeMode::Reaction) {
                if (active_target_ < 0) {
                    if (trial >= cfg.reaction_trials) { finish(w); break; }
                    if (now >= next_event) {
                        // culoar ales aleator (determinist), la distanta selectata
                        int lanes = static_cast<int>(cfg.lane_y.size());
                        int li = std::min(lanes - 1, static_cast<int>(u01(w.seed, 502, trial, 0, 0) * lanes));
                        active_target_ = find_target(li, dist_idx);
                        if (active_target_ < 0) active_target_ = find_target(lane, dist_idx);
                        raise_target(w, active_target_, cfg.reaction_expose);
                        targets[active_target_].reactive = true;
                        trial_shot_ = false;
                    }
                } else {
                    RangeTarget& t = targets[active_target_];
                    if (!t.want_up && t.hits == 0) {
                        // expirata fara lovitura
                        if (!trial_shot_) cur_.timeouts++;
                        trial++;
                        active_target_ = -1;
                        next_event = now + cfg.reaction_delay_min
                                   + (cfg.reaction_delay_max - cfg.reaction_delay_min) * u01(w.seed, 501, trial, 0, 0);
                    }
                }
            } else if (mode == RangeMode::Speed) {
                if (speed_left_ <= 0) {
                    cur_.total_time = now - phase_start;
                    finish(w);
                }
            }
            break;
        default: break;
    }
}

void ShootingRange::on_shot(World& w, const Agent& a, int shot) {
    if (!enabled || !session_active() || a.id != shooter) return;
    bool shooting_phase = phase == RangePhase::Running || phase == RangePhase::RestShoot || phase == RangePhase::PostShoot;
    if (!shooting_phase) return;
    if (mode == RangeMode::Accuracy && cur_.shots >= cfg.accuracy_rounds) return;
    if ((phase == RangePhase::RestShoot || phase == RangePhase::PostShoot) && phase_shots >= cfg.stress_shots) return;
    if (mode == RangeMode::Distance && phase_shots >= cfg.distance_shots) return;
    if (cur_.weapon < 0 && !a.slots.empty()) cur_.weapon = a.slots[a.slot].weapon;
    cur_.shots++;
    phase_shots++;
    last_counted_shot_ = shot;
    if (mode == RangeMode::Distance && dist_step_ < static_cast<int>(cur_.shots_d.size())) cur_.shots_d[dist_step_]++;
    // primul foc al fazei: reactia fata de aparitia tintei
    if (phase_first_shot_ < 0.0 && active_target_ >= 0 && targets[active_target_].appear_time >= 0.0)
        phase_first_shot_ = w.current_time - targets[active_target_].appear_time;
    if (mode == RangeMode::Reaction && active_target_ >= 0 && !trial_shot_) {
        const RangeTarget& t = targets[active_target_];
        if (t.appear_time >= 0.0 && t.hittable()) {
            cur_.reactions.push_back(w.current_time - t.appear_time);
            trial_shot_ = true;
        }
    }
}

void ShootingRange::on_impact(World& w, const Agent& shooter_agent, const BulletImpact& imp, int shot, int weapon) {
    int idx = target_index(imp.target_id);
    if (idx < 0 || idx >= static_cast<int>(targets.size())) return;
    RangeTarget& t = targets[idx];
    if (!t.hittable()) return;
    bool head = ((imp.target_id - World::kRangeTargetIdBase) & 1) != 0;
    if (shot >= 0 && t.last_shot == shot) return;      // alicele aceluiasi foc
    t.last_shot = shot;
    double u = imp.y - t.y;
    double v = imp.z;
    double cz = 0.5 * (cfg.body_z0 + cfg.body_z1) * t.size;
    double offset = std::hypot(u, v - cz);
    double dist = std::hypot(t.x - shooter_agent.x, t.y - shooter_agent.y);
    if (t.marks.size() < 60) t.marks.push_back({static_cast<float>(u / t.size), static_cast<float>(v / t.size), static_cast<uint8_t>(head ? 1 : 0)});
    t.hits++;
    if (head) t.head_hits++;
    w.emit(EventType::TARGET_HIT, shooter_agent.id, idx, imp.x, imp.y, head ? 0.0 : 1.0, dist, imp.z, offset, shot, weapon);

    // --- sesiunea ---
    bool counted = session_active() && shooter_agent.id == shooter && shot >= 0
                && (phase == RangePhase::Running || phase == RangePhase::RestShoot || phase == RangePhase::PostShoot)
                && (active_target_ < 0 || idx == active_target_ || mode == RangeMode::Speed || mode == RangeMode::Reaction);
    if (counted) {
        // un foc conteaza o singura data la precizie (alicele pot lovi doua tinte in modul viteza)
        if (shot != last_hit_shot_) {
            last_hit_shot_ = shot;
            cur_.hits++;
            (head ? cur_.head : cur_.body)++;
            phase_hits_++;
            mre_sum_ += offset; mre_n_++;
            phase_mre_sum_ += offset; phase_mre_n_++;
            if (mode == RangeMode::Distance && dist_step_ < static_cast<int>(cur_.hits_d.size())) {
                cur_.hits_d[dist_step_]++;
                mre_sum_d_[dist_step_] += offset;
                mre_n_d_[dist_step_]++;
            }
        }
        if (mode == RangeMode::Reaction && idx == active_target_) {
            if (t.appear_time >= 0.0) cur_.hit_times.push_back(w.current_time - t.appear_time);
            t.want_up = false;
            trial++;
            active_target_ = -1;
            next_event = w.current_time + cfg.reaction_delay_min
                       + (cfg.reaction_delay_max - cfg.reaction_delay_min) * u01(w.seed, 501, trial, 0, 0);
        }
        if (mode == RangeMode::Speed && t.want_up) {
            t.want_up = false;
            speed_left_--;
            cur_.splits.push_back(w.current_time - phase_start);
        }
    }
    // modul liber: tinta reactiva cade si revine dupa reset_time
    if (!session_active() && t.reactive) {
        t.want_up = false;
        t.down_timer = cfg.reset_time;
    }
}

int ShootingRange::show_only(int lane_i, int dist_i, int size_i) {
    phase = RangePhase::Idle;
    mode = RangeMode::Free;
    lower_all();
    for (auto& t : targets) t.raise = 0.0;
    int id = find_target(lane_i, dist_i);
    if (id < 0) return -1;
    RangeTarget& t = targets[id];
    t.want_up = true;
    t.raise = 1.0;
    t.reactive = false;
    t.size = cfg.sizes[std::clamp(size_i, 0, 2)];
    t.marks.clear();
    return id;
}

void ShootingRange::add_bodies(std::vector<TargetBody>& out) const {
    if (!enabled) return;
    for (const auto& t : targets) {
        if (!t.hittable()) continue;
        double s = t.size;
        TargetBody body{World::kRangeTargetIdBase + 2 * t.id, t.x, t.y, cfg.body_z0 * s, cfg.body_z1 * s, cfg.body_w * 0.5 * s, false, kPi};
        body.absorb = 1e9;   // placa de otel: glontul se opreste
        TargetBody headb{World::kRangeTargetIdBase + 2 * t.id + 1, t.x, t.y, (cfg.head_z - cfg.head_r) * s,
                         (cfg.head_z + cfg.head_r) * s, cfg.head_r * s, false, kPi};
        headb.absorb = 1e9;
        out.push_back(body);
        out.push_back(headb);
    }
}

}
