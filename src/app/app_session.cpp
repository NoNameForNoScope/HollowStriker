#include "app_session.hpp"
#include "net_session.hpp"
#include "../core/profiler.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace bodycam {

bool AppSession::possessing_pawn() const {
    int l = local_agent();
    const World& w = world();
    return l >= 0 && l < static_cast<int>(w.agents.size()) && w.agents[l].possess != Possession::Body;
}

void AppSession::latch(const InputState& in) {
    pending_.reload |= in.reload;
    pending_.interact |= in.interact;
    pending_.flashlight |= in.flashlight;
    pending_.grenade |= in.grenade;
    pending_.drone |= in.drone;
    pending_.vehicle |= in.vehicle;
    pending_.drone_mode |= in.drone_mode;
    pending_.jump |= in.jump;
    pending_.prone |= in.prone;
    pending_.kick |= in.kick;
    pending_.melee |= in.melee;
    pending_.cycle_throwable |= in.cycle_throwable;
    pending_.tablet_toggle |= in.tablet_toggle;
    pending_.tablet_stow |= in.tablet_stow;
    pending_.gadget_exit |= in.gadget_exit;
    pending_.gadget_recall |= in.gadget_recall;
    pending_.gadget_detonate |= in.gadget_detonate;
    pending_.fire_mode |= in.fire_mode;
    if (in.switch_weapon >= 0) pending_.switch_weapon = in.switch_weapon;
}

const char* input_context_name(InputContext c) {
    switch (c) {
        case InputContext::OnFoot: return "PE JOS";
        case InputContext::Tablet: return "TABLETA";
        case InputContext::Gadget: return "GADGET";
        case InputContext::Incapacitated: return "CAZUT";
    }
    return "?";
}

AgentControl AppSession::build_control(const InputState& in, bool consume) {
    AgentControl c;
    c.look_yaw = in.view_yaw;
    c.look_pitch = in.view_pitch;
    c.moveForward = in.forward;
    c.moveRight = in.right;
    c.sprint = in.sprint;
    c.crouch = in.crouch;
    c.aim = in.aim;
    c.fire = in.fire;
    c.lean = in.lean;
    c.interact_hold = in.interact_hold;
    c.bandage = in.bandage;
    c.grenade_hold = in.grenade_hold;
    // stick-uri pentru drona / vehicul
    c.stick_pitch = in.forward;
    c.stick_roll = in.right;
    c.stick_throttle = (in.throttle_up ? 1.0 : 0.0) - (in.throttle_down ? 1.0 : 0.0);
    double dt = std::max(1e-3, in.frame_dt);
    c.stick_yaw = std::clamp(in.mouse_dx / dt * 0.0022 * in.drone_sens, -1.0, 1.0);
    c.stick_pitch = std::clamp(c.stick_pitch - in.mouse_dy / dt * 0.0012 * in.drone_sens * (in.invert_y ? -1.0 : 1.0), -1.0, 1.0);
    if (consume) {
        c.reload = pending_.reload;
        c.interact = pending_.interact;
        c.toggle_flashlight = pending_.flashlight;
        c.throw_grenade = pending_.grenade;
        c.toggle_drone = pending_.drone;
        c.toggle_vehicle = pending_.vehicle;
        c.toggle_drone_mode = pending_.drone_mode;
        c.jump = pending_.jump;
        c.switch_weapon = pending_.switch_weapon;
        c.toggle_prone = pending_.prone;
        c.kick = pending_.kick;
        c.melee = pending_.melee;
        c.cycle_throwable = pending_.cycle_throwable;
        c.tablet_toggle = pending_.tablet_toggle;
        c.tablet_stow_fast = pending_.tablet_stow;
        c.gadget_exit = pending_.gadget_exit;
        c.gadget_recall = pending_.gadget_recall;
        c.gadget_detonate = pending_.gadget_detonate;
        c.cycle_fire_mode = pending_.fire_mode;
        pending_.clear_one_shots();
    }
    if (possessing_pawn()) { c.crouch = false; c.jump = false; }
    return c;
}

TabletCommand AppSession::merge_tablet(AgentControl& c, GameView& view) {
    TabletCommand t = view.take_tablet_command();
    if (t.place_marker) { c.place_marker = true; c.marker_x = t.mx; c.marker_y = t.my; c.marker_kind = t.kind; }
    if (t.clear_markers) c.clear_markers = true;
    if (t.tapped) c.tablet_touch = true;
    if (t.loadout_apply) { c.loadout_apply = true; c.loadout = t.loadout; }
    if (t.gadget == 1) c.toggle_drone = true;
    if (t.gadget == 2) c.toggle_vehicle = true;
    c.gadget_exit |= t.gadget_exit;
    c.gadget_recall |= t.gadget_recall;
    c.gadget_detonate |= t.gadget_detonate;
    c.toggle_drone_mode |= t.drone_mode;
    c.range_mode = t.range_mode;
    c.range_dist = t.range_dist;
    c.range_size = t.range_size;
    c.range_stop = t.range_stop;
    return t;
}

// ---------------------------------------------------------------------------
// Joc local (un jucator + boti): meciuri pe hartile din catalog, statistici, experimente
// ---------------------------------------------------------------------------
class LocalSession : public AppSession {
public:
    explicit LocalSession(const SessionConfig& cfg) : cfg_(cfg), world_(cfg.round.seed) {
        const std::string root = cfg_.round.asset_root;
        catalog_.load(root);
        stats_.init(cfg_.data_dir.empty() ? root + "/../data" : cfg_.data_dir, root + "/config/stats.ini");
        stats_.storage.read_only = cfg_.stats_read_only;
        exp_.load(root);
        const MapInfo* m = !cfg_.map_key.empty() ? catalog_.by_key(cfg_.map_key) : catalog_.by_file(cfg_.round.map_file);
        if (m) use_map(*m);
        start_match();
    }

    void update(const InputState& in, GameView& view, double frame_dt) override {
        PROFILE_SCOPE("sim.frame");
        latch(in);
        if (first_) { view.reset_for_world(world_, 0); first_ = false; }
        acc_ += frame_dt;
        int steps = 0;
        while (acc_ >= world_.dt && steps < 8) {
            AgentControl c = build_control(in, steps == 0);
            if (steps == 0) {
                TabletCommand t = merge_tablet(c, view);
                session_commands(t, view);
            }
            {
                PROFILE_SCOPE("sim.step");
                world_.step(&c);
            }
            std::vector<Event> evs = world_.event_log.get_all();
            world_.event_log.clear();
            view.on_step(world_, evs, 0, world_.dt);
            stats_.on_step(world_, evs, 0);
            acc_ -= world_.dt;
            steps++;
            // echipamentul ales pe tableta ramane pentru runda urmatoare (acelasi inventar real)
            if (!world_.agents.empty() && world_.agents[0].pending_loadout.valid) cfg_.round.player_loadout = world_.agents[0].pending_loadout;
            if (switch_map_) {
                // meci nou pe alta harta: meciul curent (daca a avut runde) ramane ca abandonat
                switch_map_ = false;
                stats_.shutdown(world_, 0);
                if (const MapInfo* m = catalog_.by_id(pending_map_)) use_map(*m);
                pending_map_ = 0;
                match_seq_++;
                start_match();
                after_reset(view);
                if (const MapInfo* m = catalog_.by_id(cfg_.round.map_id)) view.notify(world_, 0, "HARTA: " + m->name, 3);
                break;
            }
            if (world_.game.restart_requested || restart_) {
                restart_ = false;
                GameState prev = world_.game;
                bool over = prev.score_blue >= stats_.cfg.rounds_to_win || prev.score_red >= stats_.cfg.rounds_to_win
                         || prev.round >= stats_.cfg.max_rounds;
                if (over && cfg_.round.mode != GameMode::Training) {
                    std::string res = prev.score_blue > prev.score_red ? "W" : prev.score_blue < prev.score_red ? "L" : "D";
                    stats_.end_match(world_, 0, res);
                    char buf[96];
                    std::snprintf(buf, sizeof(buf), "MECI %s %d:%d - SALVAT IN STATISTICI",
                                  res == "W" ? "CASTIGAT" : res == "L" ? "PIERDUT" : "EGAL", prev.score_blue, prev.score_red);
                    if (pending_map_ > 0) { if (const MapInfo* m = catalog_.by_id(pending_map_)) use_map(*m); pending_map_ = 0; }
                    match_seq_++;
                    start_match();
                    after_reset(view);
                    view.notify(world_, 0, buf, res == "W" ? 3 : 1);
                } else {
                    world_ = World(cfg_.round.seed + static_cast<uint64_t>(prev.round) * 7919 + match_seq_ * 104729);
                    setup_round(world_, cfg_.round, &prev);
                    stats_.begin_round(world_, 0);
                    after_reset(view);
                }
                break;
            }
        }
        if (acc_ > world_.dt * 4) acc_ = 0.0;
        // experimentele ruleaza in fundal; rezultatele se salveaza cand sunt gata
        std::vector<ExperimentTrial> tr;
        if (exp_.fetch(tr)) {
            stats_.add_trials(tr);
            view.notify(world_, 0, "EXPERIMENT TERMINAT: " + std::to_string(tr.size()) + " PROBE SALVATE", 3);
        }
    }

    const World& world() const override { return world_; }
    int local_agent() const override { return 0; }
    void request_restart() override { restart_ = true; }
    StatisticsSystem* statistics() override { return &stats_; }
    const MapCatalog* catalog() const override { return &catalog_; }
    const ExperimentManager* experiments() const override { return &exp_; }
    int map_id() const override { return cfg_.round.map_id; }
    void shutdown() override {
        exp_.cancel();
        stats_.shutdown(world_, 0);
    }

private:
    SessionConfig cfg_;
    World world_;
    MapCatalog catalog_;
    StatisticsSystem stats_;
    ExperimentManager exp_;
    double acc_ = 0.0;
    bool first_ = true;
    bool restart_ = false;
    bool switch_map_ = false;
    int pending_map_ = 0;
    uint64_t match_seq_ = 0;

    void use_map(const MapInfo& m) {
        cfg_.round.map_file = m.file;
        cfg_.round.map_id = m.id;
        cfg_.round.range_config = m.range_config;
        if (!cfg_.mode_forced || m.training()) cfg_.round.mode = m.mode;
        else if (cfg_.round.mode == GameMode::Training) cfg_.round.mode = GameMode::Elimination;
    }
    // lume noua: meci nou (scor 0:0) sau sesiune de poligon
    void start_match() {
        world_ = World(cfg_.round.seed + match_seq_ * 104729);
        setup_round(world_, cfg_.round, nullptr);
        const MapInfo* m = catalog_.by_id(cfg_.round.map_id);
        std::string name = m ? m->name : cfg_.round.map_file;
        if (cfg_.round.mode == GameMode::Training) stats_.begin_session(cfg_.round.map_id, name, true);
        else stats_.begin_match(cfg_.round.map_id, name, cfg_.round.mode);
        stats_.begin_round(world_, 0);
    }
    void after_reset(GameView& view) {
        view.reset_for_world(world_, 0);
        spawn_yaw_ = world_.agents.empty() ? 0.0 : world_.agents[0].facing;
        view_reset_ = true;
    }
    void session_commands(const TabletCommand& t, GameView& view) {
        if (t.new_match_map > 0 && catalog_.by_id(t.new_match_map)) {
            // pe poligon / inainte de prima runda incheiata schimbarea e imediata; altfel la meciul urmator
            bool immediate = cfg_.round.mode == GameMode::Training || !stats_.in_match() || stats_.live_rounds().empty()
                          || catalog_.by_id(t.new_match_map)->training();
            pending_map_ = t.new_match_map;
            if (immediate) switch_map_ = true;
            else view.notify(world_, 0, "HARTA SE SCHIMBA LA MECIUL URMATOR", 1);
        }
        if (t.cancel_experiment) exp_.cancel();
        if (t.run_experiment >= 0 && !exp_.running()) {
            int64_t run = 1;
            for (const auto& tr : stats_.trials) run = std::max<int64_t>(run, tr.run + 1);
            if (exp_.start(t.run_experiment, run, cfg_.round.seed + static_cast<uint64_t>(run) * 31))
                view.notify(world_, 0, "EXPERIMENT PORNIT: " + exp_.defs[t.run_experiment].name, 0);
        }
    }
};

std::unique_ptr<AppSession> make_session(const SessionConfig& cfg) {
    if (cfg.host_port > 0 || !cfg.connect_addr.empty()) return make_network_session(cfg);
    return std::make_unique<LocalSession>(cfg);
}

}
