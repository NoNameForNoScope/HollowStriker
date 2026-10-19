#include "experiments.hpp"
#include "analytics.hpp"
#include "../game/scenario.hpp"
#include "../sim/player_sim.hpp"
#include "../core/ini.hpp"
#include "../core/rng.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <map>
#include <set>
#include <sstream>

namespace bodycam {

namespace {
constexpr double kPi = 3.14159265358979323846;
double wrap(double a) {
    while (a > kPi) a -= 2.0 * kPi;
    while (a < -kPi) a += 2.0 * kPi;
    return a;
}
std::vector<std::string> split_list(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        tok.erase(0, tok.find_first_not_of(" \t"));
        tok.erase(tok.find_last_not_of(" \t") + 1);
        if (!tok.empty()) out.push_back(tok);
    }
    return out;
}
std::string stamp() {
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
}

std::string ExperimentDef::level_label(double level) const {
    char b[48];
    if (factor == "weapon") {
        int i = static_cast<int>(level);
        if (i >= 0 && i < static_cast<int>(weapons.size())) {
            std::string w = weapons[i];
            for (char& c : w) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            return w;
        }
    }
    if (factor == "size") {
        const char* s[3] = {"S", "M", "L"};
        int i = std::clamp(static_cast<int>(level), 0, 2);
        return s[i];
    }
    if (std::abs(level - std::round(level)) < 1e-6) std::snprintf(b, sizeof(b), "%.0f", level);
    else std::snprintf(b, sizeof(b), "%.2f", level);
    return b;
}

std::vector<ExperimentDef> load_experiment_defs(const std::string& path) {
    std::vector<ExperimentDef> out;
    IniParser ini;
    if (!ini.load(path)) return out;
    for (const auto& sec : ini.sections()) {
        ExperimentDef d;
        d.key = sec;
        d.name = ini.get(sec, "name", sec);
        d.question = ini.get(sec, "question", "");
        d.factor = ini.get(sec, "factor", "stress");
        d.unit = ini.get(sec, "unit", "");
        for (const auto& t : split_list(ini.get(sec, "levels", ""))) {
            try { d.levels.push_back(std::stod(t)); } catch (...) {}
        }
        auto w = split_list(ini.get(sec, "weapons", ""));
        if (!w.empty()) d.weapons = w;
        std::string single = ini.get(sec, "weapon", "");
        if (!single.empty()) d.weapons = {single};
        d.map_file = ini.get(sec, "map", d.map_file);
        d.range_config = ini.get(sec, "range", d.range_config);
        d.distance = ini.get_d(sec, "distance", d.distance);
        d.size = static_cast<int>(ini.get_d(sec, "size", d.size));
        d.shots = static_cast<int>(ini.get_d(sec, "shots", d.shots));
        d.reloads = static_cast<int>(ini.get_d(sec, "reloads", d.reloads));
        d.reps = static_cast<int>(ini.get_d(sec, "reps", d.reps));
        d.fire_interval = ini.get_d(sec, "fire_interval", d.fire_interval);
        d.settle = ini.get_d(sec, "settle", d.settle);
        d.measure = ini.get(sec, "measure", d.measure);
        if (!d.levels.empty()) out.push_back(d);
    }
    return out;
}

// ============================================================================
ExperimentTrial run_trial(const ExperimentDef& def, double level, int rep, uint64_t seed, const std::string& asset_root) {
    ExperimentTrial tr;
    tr.experiment = def.key;
    tr.factor = def.factor;
    tr.level = level;
    tr.rep = rep;
    tr.seed = static_cast<int64_t>(seed);

    // --- conditiile fixe (si factorul) ---
    std::string weapon = def.weapons.empty() ? "mk18" : def.weapons[0];
    if (def.factor == "weapon") {
        int i = std::clamp(static_cast<int>(level), 0, static_cast<int>(def.weapons.size()) - 1);
        weapon = def.weapons[i];
    }
    double distance = def.factor == "distance" ? level : def.distance;
    int size = def.factor == "size" ? static_cast<int>(level) : def.size;

    RoundConfig cfg;
    cfg.asset_root = asset_root;
    cfg.map_file = def.map_file;
    cfg.mode = GameMode::Training;
    cfg.range_config = def.range_config;
    cfg.seed = seed;
    {
        std::string f = "weapons/" + weapon + ".ini";
        auto& v = cfg.weapon_files;
        v.erase(std::remove(v.begin(), v.end(), f), v.end());
        v.insert(v.begin(), f);
    }
    World w(seed);
    setup_round(w, cfg, nullptr);
    w.emit_fx_events = false;
    if (w.agents.empty() || !w.range.enabled) return tr;
    Agent& a = w.agents[0];
    tr.weapon = w.weapons.empty() ? weapon : w.weapons[0].name;
    tr.distance = distance;

    // culoarul din mijloc, distanta cea mai apropiata din configuratie
    const RangeConfig& rc = w.range.cfg;
    int lane = static_cast<int>(rc.lane_y.size()) / 2;
    while (std::find(rc.moving_lanes.begin(), rc.moving_lanes.end(), lane) != rc.moving_lanes.end() && lane > 0) lane--;
    int di = 0;
    for (size_t i = 0; i < rc.distances.size(); ++i)
        if (std::abs(rc.distances[i] - distance) < std::abs(rc.distances[di] - distance)) di = static_cast<int>(i);
    int tid = w.range.show_only(lane, di, size);
    if (tid < 0) return tr;
    const RangeTarget& target = w.range.targets[tid];
    tr.distance = target.distance;
    const double start_x = rc.firing_line_x - 1.2, start_y = rc.lane_y[lane];
    a.x = start_x;
    a.y = start_y;
    a.facing = 0.0;
    a.heading_hold = 0.0;
    if (def.factor == "arm_injury") {
        // bratul drept (tragaciul) ranit proportional cu nivelul
        a.health.zone[static_cast<int>(BodyZone::RightArm)] = a.health.zone_max[static_cast<int>(BodyZone::RightArm)] * (1.0 - std::clamp(level, 0.0, 0.95));
    }
    tr.arm_injury = a.health.arm_injury();

    auto intervene = [&]() {
        if (def.factor == "stress") {
            physiology_force_stress(a.phys, level, w.phys_cfg);
        } else if (def.factor == "hr") {
            a.phys.heart_rate = level;
        } else if (def.factor == "fatigue") {
            a.phys.fatigue = std::clamp(level, 0.0, 1.0);
        }
    };

    // --- pilotul automat ---
    const double sprint_time = def.factor == "sprint" ? level : 0.0;
    int phase = sprint_time > 0.0 ? 0 : 1;
    double phase_t = 0.0, next_fire = 0.0;
    int shots_counted = 0, reloads_done = 0, reload_shots = 0;
    bool reload_wanted = false;
    double reload_start = -1.0;
    std::set<int> counted;
    std::set<int> hit_shots;
    double hr_sum = 0.0, st_sum = 0.0, trem_sq = 0.0, sway_sq = 0.0, off_sum = 0.0;
    int phys_n = 0, sway_n = 0, off_n = 0;
    std::vector<double> reload_times;
    const double max_time = 160.0 + sprint_time;
    const double tz = 0.5 * (rc.body_z0 + rc.body_z1) * target.size;

    while (w.current_time < max_time && phase < 4) {
        intervene();
        AgentControl c;
        double eye = w.eye_z(a) - 0.1;
        double dx = target.x - a.x, dy = target.y - a.y;
        double want_yaw = std::atan2(dy, dx);
        double want_pitch = std::atan2(tz - eye, std::hypot(dx, dy));
        c.look_yaw = want_yaw;
        c.look_pitch = want_pitch;
        phase_t += w.dt;
        if (phase == 0) {
            // sprint: jumatate de timp spre aval, jumatate inapoi la linia de tragere
            bool out = phase_t < sprint_time * 0.5;
            c.look_yaw = out ? 0.0 : kPi;
            c.look_pitch = 0.0;
            c.moveForward = 1.0;
            c.sprint = true;
            if (phase_t >= sprint_time || (!out && a.x <= start_x)) { phase = 1; phase_t = 0.0; }
        } else if (phase == 1) {
            // revenire pe pozitie si ochire
            double back = std::hypot(start_x - a.x, start_y - a.y);
            if (back > 0.4) {
                double ang = wrap(std::atan2(start_y - a.y, start_x - a.x) - a.facing);
                c.moveForward = std::cos(ang);
                c.moveRight = std::sin(ang);
            }
            c.aim = true;
            if (a.aim > 0.95 && phase_t >= def.settle && back <= 0.6) { phase = 2; phase_t = 0.0; next_fire = 0.0; }
        } else if (phase == 2) {
            c.aim = true;
            const WeaponSlot* sl = a.cur_slot();
            bool empty = sl && sl->mag <= 0 && !sl->chambered;
            if (empty && a.reload_stage == 0) c.reload = true;
            if (phase_t >= next_fire && a.reload_stage == 0 && !empty && shots_counted < def.shots && a.fire_timer <= 0.0) {
                c.fire = true;
                next_fire = phase_t + def.fire_interval;
                // directia armei fata de tinta, inainte de foc (leganare + tremur + recul ramas)
                double ey = wrap(aim_yaw(a) - want_yaw), ep = aim_pitch(a) - want_pitch;
                sway_sq += ey * ey + ep * ep;
                sway_n++;
            }
            hr_sum += a.phys.heart_rate; st_sum += a.phys.stress;
            trem_sq += a.phys.tremor_amp * a.phys.tremor_amp;
            phys_n++;
            if (shots_counted >= def.shots && !w.shot_in_flight(0, a.shots_fired)) { phase = def.reloads > 0 ? 3 : 4; phase_t = 0.0; }
        } else if (phase == 3) {
            // reincarcari repetate: un foc (incarcator incomplet) apoi reincarcare
            c.aim = false;
            if (a.reload_stage == 0 && !reload_wanted) {
                if (reload_shots == reloads_done) {
                    if (a.fire_timer <= 0.0 && phase_t > 0.3) { c.fire = true; reload_shots++; phase_t = 0.0; }
                } else {
                    c.reload = true;
                    reload_wanted = true;
                }
            }
            if (reloads_done >= def.reloads) phase = 4;
        }
        w.step(&c);
        for (const auto& e : w.event_log.get_all()) {
            if (e.a != 0) continue;
            if (e.type == EventType::SHOT && phase == 2 && e.shot >= 0 && !counted.count(e.shot)) {
                counted.insert(e.shot);
                shots_counted++;
            } else if (e.type == EventType::TARGET_HIT && counted.count(e.shot) && !hit_shots.count(e.shot)) {
                hit_shots.insert(e.shot);
                tr.hits++;
                if (e.v1 < 0.5) tr.head++;
                off_sum += e.v3;
                off_n++;
            } else if (e.type == EventType::RELOAD_START && phase == 3) {
                reload_start = w.current_time;
            } else if (e.type == EventType::RELOAD_END && phase == 3) {
                if (e.v1 > 0.0 && reload_start >= 0.0) { reload_times.push_back(w.current_time - reload_start); reloads_done++; }
                else if (e.v1 <= 0.0) reload_shots = reloads_done;   // intrerupta: se reia
                reload_start = -1.0;
                reload_wanted = false;
            }
        }
        w.event_log.clear();
    }
    tr.shots = shots_counted;
    tr.accuracy = tr.shots > 0 ? static_cast<double>(tr.hits) / tr.shots : 0.0;
    tr.mre = off_n ? off_sum / off_n : 0.0;
    if (phys_n) {
        tr.hr_mean = hr_sum / phys_n;
        tr.stress_mean = st_sum / phys_n;
        tr.tremor_rms = std::sqrt(trem_sq / phys_n);
    }
    tr.sway_rms = sway_n ? std::sqrt(sway_sq / sway_n) : 0.0;
    tr.fatigue = a.phys.fatigue;
    if (!reload_times.empty()) {
        double s = 0.0;
        for (double r : reload_times) s += r;
        tr.reload_mean = s / reload_times.size();
    }
    return tr;
}

double trial_measure(const ExperimentTrial& t, const std::string& m) {
    if (m == "mre") return t.mre;
    if (m == "sway_rms") return t.sway_rms * 1000.0;     // mrad
    if (m == "tremor_rms") return t.tremor_rms * 1000.0;
    if (m == "reload_mean") return t.reload_mean;
    if (m == "hr_mean") return t.hr_mean;
    return t.accuracy * 100.0;                           // %
}

std::vector<ExperimentTrial> run_experiment_def(const ExperimentDef& def, int64_t run_id, uint64_t base_seed,
                                                const std::string& asset_root, std::atomic<int>* done,
                                                const std::atomic<bool>* cancel) {
    std::vector<ExperimentTrial> out;
    std::string ts = stamp();
    for (int r = 0; r < def.reps; ++r) {
        // numere aleatoare comune: aceeasi samanta pentru replicarea r la toate nivelurile
        uint64_t seed = mix64(base_seed + static_cast<uint64_t>(r) * 7919ull + 17ull);
        for (double lv : def.levels) {
            if (cancel && *cancel) return out;
            ExperimentTrial t = run_trial(def, lv, r, seed, asset_root);
            t.run = run_id;
            t.timestamp = ts;
            out.push_back(t);
            if (done) (*done)++;
        }
    }
    return out;
}

RunSummary summarize_run(const std::vector<ExperimentTrial>& all, int64_t run, const std::string& measure) {
    RunSummary s;
    s.run = run;
    std::map<double, std::vector<double>> by;
    std::vector<double> xs, ys;
    for (const auto& t : all) {
        if (t.run != run) continue;
        if (measure == "reload_mean" && t.reload_mean < 0.0) continue;
        s.experiment = t.experiment;
        s.timestamp = t.timestamp;
        double v = trial_measure(t, measure);
        by[t.level].push_back(v);
        xs.push_back(t.level);
        ys.push_back(v);
        s.trials++;
    }
    for (const auto& kv : by) {
        Summary sm = summarize(kv.second);
        s.levels.push_back({kv.first, sm.n, sm.mean, sm.ci95(), sm.sd});
    }
    Regression rg = regress(xs, ys);
    if (rg.valid) { s.slope = rg.slope; s.r = rg.r; s.p = rg.p; }
    return s;
}

std::vector<int64_t> runs_of(const std::vector<ExperimentTrial>& all, const std::string& experiment) {
    std::vector<int64_t> out;
    for (const auto& t : all)
        if (t.experiment == experiment && std::find(out.begin(), out.end(), t.run) == out.end()) out.push_back(t.run);
    std::sort(out.begin(), out.end());
    return out;
}

// ============================================================================
ExperimentManager::~ExperimentManager() {
    cancel_ = true;
    if (th_.joinable()) th_.join();
}

bool ExperimentManager::load(const std::string& asset_root) {
    root_ = asset_root;
    defs = load_experiment_defs(asset_root + "/config/experiments.ini");
    return !defs.empty();
}

bool ExperimentManager::start(int def_index, int64_t run_id, uint64_t base_seed) {
    if (running_ || def_index < 0 || def_index >= static_cast<int>(defs.size())) return false;
    if (th_.joinable()) th_.join();
    const ExperimentDef def = defs[def_index];
    running_ = true;
    finished_ = false;
    cancel_ = false;
    done_ = 0;
    total_ = static_cast<int>(def.levels.size()) * def.reps;
    active_ = def_index;
    std::string root = root_;
    th_ = std::thread([this, def, run_id, base_seed, root]() {
        std::vector<ExperimentTrial> res = run_experiment_def(def, run_id, base_seed, root, &done_, &cancel_);
        {
            std::lock_guard<std::mutex> lock(mu_);
            results_ = std::move(res);
        }
        finished_ = true;
        running_ = false;
    });
    return true;
}

bool ExperimentManager::fetch(std::vector<ExperimentTrial>& out) {
    if (!finished_) return false;
    std::lock_guard<std::mutex> lock(mu_);
    out = std::move(results_);
    results_.clear();
    finished_ = false;
    active_ = -1;
    if (th_.joinable()) th_.join();
    return !out.empty();
}

}
