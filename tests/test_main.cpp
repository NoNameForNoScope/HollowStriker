#include "../src/core/rng.hpp"
#include "../src/core/hash.hpp"
#include "../src/sim/map.hpp"
#include "../src/sim/los.hpp"
#include "../src/sim/world.hpp"
#include "../src/sim/player_sim.hpp"
#include "../src/sim/ballistics.hpp"
#include "../src/sim/lightmap.hpp"
#include "../src/sim/physiology.hpp"
#include "../src/sim/reload.hpp"
#include "../src/sim/spatial_audio_engine.hpp"
#include "../src/render/tablet_ui.hpp"
#include "../src/render/character.hpp"
#include "../src/audio/breath_synth.hpp"
#include "../src/game/scenario.hpp"
#include "../src/experiment/runner.hpp"
#include "../src/game/map_catalog.hpp"
#include "../src/stats/statistics_system.hpp"
#include "../src/stats/analytics.hpp"
#include "../src/stats/experiments.hpp"
#include "../src/stats/data_storage.hpp"
#include "../src/sim/weapon_stats.hpp"
#include "../src/core/asset_manifest.hpp"
#include "../src/render/obj_model.hpp"
#include "../src/render/weapon_mesh.hpp"
#include "../src/render/viewmodel.hpp"
#include "../src/game/campaign.hpp"
#include <filesystem>
#include <set>
#include <iostream>
#include <cstdio>
#include <fstream>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

using namespace bodycam;

static int g_failed = 0;
static int g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failed; std::cerr << "  FAIL " << __FILE__ << ":" << __LINE__ << "  " #cond "\n"; } } while (0)

static Map open_room(int w, int h) {
    std::vector<std::string> rows;
    for (int y = 0; y < h; ++y) {
        std::string r;
        for (int x = 0; x < w; ++x) r += (x == 0 || y == 0 || x == w - 1 || y == h - 1) ? '1' : '.';
        rows.push_back(r);
    }
    Map m;
    m.from_rows(rows);
    return m;
}

static World make_world_with_player(const Map& m, double x, double y, bool interactive = false) {
    World w(42);
    w.map = m;
    w.rebuild_lighting();
    w.physiology_enabled = interactive;
    w.allow_downed = interactive;
    w.weapons.push_back(make_default_rifle());
    w.weapons.push_back(make_default_pistol());
    w.weapons[0].projectile = false;    // testele de baza folosesc trasare instantanee
    w.weapons[0].malfunction_base = 0.0;
    w.weapons[0].malfunction_heat = 0.0;
    Agent a;
    a.id = 0;
    a.x = x; a.y = y;
    a.is_human = true;
    a.perception_enabled = false;
    w.give_loadout(a, {0, 1});
    w.agents.push_back(a);
    w.setup_interactables();
    return w;
}

static Agent make_bot(int id, double x, double y) {
    Agent b;
    b.id = id;
    b.faction = Faction::Red;
    b.x = x; b.y = y;
    b.weapon_enabled = false;
    b.perception_enabled = false;
    return b;
}

static void run_steps(World& w, const AgentControl& c, int n) { for (int i = 0; i < n; ++i) w.step(&c); }

static bool has_event(const World& w, EventType t) {
    for (const auto& e : w.event_log.get_all()) if (e.type == t) return true;
    return false;
}

// ----------------------------------------------------------------------------
static void test_rng_los() {
    CHECK(u01(1, 2, 3, 4, 5) == u01(1, 2, 3, 4, 5));
    CHECK(u01(1, 2, 3, 4, 5) != u01(1, 2, 3, 4, 6));
    Map m = open_room(10, 10);
    CHECK(check_los(m, 1.5, 1.5, 8.5, 8.5));
    m.grid[5 * 10 + 5] = '1';
    CHECK(!check_los(m, 1.5, 5.5, 8.5, 5.5));
    int walls = 0;
    check_los(m, 1.5, 5.5, 8.5, 5.5, &walls);
    CHECK(walls == 1);
    // fereastra: vederea trece, miscarea nu
    m.grid[5 * 10 + 5] = '6';
    CHECK(check_los(m, 1.5, 5.5, 8.5, 5.5));
    CHECK(m.is_wall(5, 5));
}

static void test_map_markers_doors_rooms() {
    Map m;
    m.from_rows({"1111111", "1S.E.L1", "11X1111", "1.p.z.1", "1111111"});
    CHECK(m.markers_of('S').size() == 1);
    CHECK(m.markers_of('z').size() == 1);
    CHECK(m.cell(1, 1) == '.');
    CHECK(m.is_door(2, 2) && m.is_locked(2, 2));
    CHECK(m.room_at(1, 1) != m.room_at(1, 3));   // usa separa incaperile
    m.door_locked[2 * 7 + 2] = 0;
    m.set_door(2, 2, true, 1);
    bool crossed = false;
    for (int i = 0; i < 60; ++i) crossed |= m.update_doors(1.0 / 60.0);
    CHECK(crossed && !m.is_wall(2, 2));
}

static void test_movement_inertia_stamina_load() {
    World w = make_world_with_player(open_room(40, 10), 2.5, 5.0);
    AgentControl c;
    c.moveForward = 1.0;
    w.step(&c);
    double v1 = std::hypot(w.agents[0].vx, w.agents[0].vy);
    CHECK(v1 > 0.0 && v1 < w.params.v_walk * 0.5);
    run_steps(w, c, 60);
    double vwalk = std::hypot(w.agents[0].vx, w.agents[0].vy);
    CHECK(vwalk > w.params.v_walk * 0.85 && vwalk <= w.params.v_walk + 0.05);   // echipamentul incetineste putin
    c.sprint = true;
    run_steps(w, c, 120);
    CHECK(w.agents[0].stamina < 0.8);
    AgentControl stop;
    w.step(&stop);
    CHECK(std::hypot(w.agents[0].vx, w.agents[0].vy) > 0.5);   // inertie
    // greutatea: echipament mai greu => acceleratie mai lenta
    World a = make_world_with_player(open_room(40, 10), 2.5, 5.0);
    World b = make_world_with_player(open_room(40, 10), 2.5, 5.0);
    b.agents[0].inv.base_gear_kg = 40.0;
    AgentControl go;
    go.moveForward = 1.0;
    run_steps(a, go, 10);
    run_steps(b, go, 10);
    CHECK(a.load_kg(a.agents[0]) < b.load_kg(b.agents[0]));
    CHECK(std::hypot(b.agents[0].vx, b.agents[0].vy) < std::hypot(a.agents[0].vx, a.agents[0].vy));
}

static void test_prone_lean() {
    World w = make_world_with_player(open_room(20, 10), 5.5, 5.0);
    double eye0 = w.eye_z(w.agents[0]);
    AgentControl c;
    c.toggle_prone = true;
    w.step(&c);
    c.toggle_prone = false;
    run_steps(w, c, 90);
    CHECK(w.agents[0].prone > 0.9);
    CHECK(w.eye_z(w.agents[0]) < eye0 * 0.4);
    // aplecarea muta capul lateral (fizic), nu doar camera
    World l = make_world_with_player(open_room(20, 10), 5.5, 5.0);
    AgentControl lean;
    lean.lean = 1.0;
    run_steps(l, lean, 60);
    double hx, hy;
    agent_head_offset(l.agents[0], hx, hy);
    double free_lean = std::hypot(hx, hy);
    CHECK(free_lean > 0.25);
    // langa perete aplecarea este limitata
    World wall = make_world_with_player(open_room(20, 10), 5.5, 8.6);
    run_steps(wall, lean, 60);   // privire spre +x: dreapta = +y = peretele
    agent_head_offset(wall.agents[0], hx, hy);
    CHECK(std::hypot(hx, hy) < free_lean * 0.75);
}

static void test_physiology() {
    PhysiologyConfig cfg;
    cfg.load(find_asset_root() + "/config/physiology.ini");
    Physiology p;
    PhysiologyInputs in;
    for (int i = 0; i < 600; ++i) update_physiology(p, in, cfg, 1.0 / 60.0, 1);
    CHECK(p.heart_rate < 80.0);
    CHECK(p.level == StressLevel::Calm && p.state == PhysState::Calm);
    // explozie: stresul NU sare instant (tinta da, valoarea curenta urca treptat), pulsul urca, apoi revenire lenta
    physiology_event(p, PhysEvent::Explosion, 1.0, cfg);
    update_physiology(p, in, cfg, 1.0 / 60.0, 1);
    CHECK(p.stress_target > 0.25 && p.stress < 0.05);
    double hr_1 = p.heart_rate;
    for (int i = 0; i < 300; ++i) update_physiology(p, in, cfg, 1.0 / 60.0, 1);
    CHECK(p.heart_rate > hr_1 + 8.0);
    CHECK(p.stress > 0.2 && p.adrenaline > 0.1);
    CHECK(p.level >= StressLevel::Alert);
    CHECK(p.fine_motor < 1.0);
    double s1 = p.stress;
    for (int i = 0; i < 60 * 20; ++i) update_physiology(p, in, cfg, 1.0 / 60.0, 1);
    CHECK(p.stress < s1 && p.stress > 0.0);          // revenire graduala, nu reset
    // foc sustinut: caldura de foc + respiratie
    Physiology f;
    for (int i = 0; i < 300; ++i) update_physiology(f, in, cfg, 1.0 / 60.0, 2);
    double br0 = f.breath_rate;
    for (int i = 0; i < 120; ++i) {
        if (i % 5 == 0) physiology_on_shot(f, cfg);
        update_physiology(f, in, cfg, 1.0 / 60.0, 2);
    }
    CHECK(f.fire_heat > 0.5);
    for (int i = 0; i < 240; ++i) update_physiology(f, in, cfg, 1.0 / 60.0, 2);
    CHECK(f.breath_rate > br0);
    // respiratia nu este perfect periodica
    Physiology r;
    std::vector<double> peaks;
    double prev = 0, prev2 = 0;
    for (int i = 0; i < 60 * 30; ++i) {
        update_physiology(r, in, cfg, 1.0 / 60.0, 3);
        if (prev > prev2 && prev > r.breath_signal) peaks.push_back(i / 60.0);
        prev2 = prev; prev = r.breath_signal;
    }
    CHECK(peaks.size() >= 4);
    if (peaks.size() >= 4) {
        double d1 = peaks[1] - peaks[0], d2 = peaks[3] - peaks[2];
        CHECK(std::abs(d1 - d2) > 1e-3);
    }
    // curbele din fisier
    Curve c;
    CHECK(c.parse("0:0, 0.5:1, 1:0.5"));
    CHECK(std::abs(c(0.25) - 0.5) < 1e-9 && std::abs(c(2.0) - 0.5) < 1e-9 && std::abs(c(-1.0)) < 1e-9);
}

// ---------------------------------------------------------------------------
// Cele 8 scenarii ale fiziologiei (stres, adrenalina, descarcare, revenire, a doua lupta)
// ---------------------------------------------------------------------------
namespace {
struct PhysRun {
    PhysiologyConfig cfg;
    Physiology p;
    PhysiologyInputs in;
    int frame = 0;
    double max_dtrem = 0.0;          // cea mai mare schimbare a abaterii armei intr-un cadru
    double max_trem = 0.0, max_cam = 0.0;
    double cam_sq = 0.0, trem_sq = 0.0;
    int n = 0;
    PhysRun() { cfg.load(find_asset_root() + "/config/physiology.ini"); in.weapon_weight = 3.4; }
    double t() const { return frame / 60.0; }
    // pas cu evenimente: fn(frame) poate trimite evenimente
    template <class F> void run(double secs, F fn) {
        int steps = static_cast<int>(std::lround(secs * 60.0));
        for (int i = 0; i < steps; ++i) {
            fn(frame);
            double py = p.tremor_yaw, pp = p.tremor_pitch;
            update_physiology(p, in, cfg, 1.0 / 60.0, 7);
            max_dtrem = std::max(max_dtrem, std::hypot(p.tremor_yaw - py, p.tremor_pitch - pp));
            max_trem = std::max(max_trem, p.tremor_amp);
            max_cam = std::max(max_cam, std::hypot(p.cam_yaw, p.cam_pitch));
            cam_sq += p.cam_yaw * p.cam_yaw + p.cam_pitch * p.cam_pitch;
            trem_sq += p.tremor_yaw * p.tremor_yaw + p.tremor_pitch * p.tremor_pitch;
            n++;
            frame++;
        }
    }
    void idle(double secs) { run(secs, [](int) {}); }
    // lupta: foc inamic aproape, gloante pe langa, focurile proprii in rafale, inamici care apar
    void combat(double secs, bool with_damage) {
        in.in_combat = true;
        in.speed = 1.2;
        int start = frame;
        run(secs, [&](int f) {
            int k = f - start;
            if (k % 90 == 0) physiology_event(p, PhysEvent::NearbyGunfire, 1.0, cfg);
            if (k % 360 == 120) physiology_event(p, PhysEvent::NearMiss, 1.0, cfg);
            if (k % 180 < 30 && k % 10 == 0) physiology_on_shot(p, cfg);
            if (k % 1200 == 600) physiology_event(p, PhysEvent::EnemyAppears, 1.0, cfg);
            if (with_damage && k == 60 * 60) physiology_event(p, PhysEvent::MinorDamage, 1.0, cfg);
        });
        in.in_combat = false;
        in.speed = 0.0;
    }
};
}

static void test_physiology_scenarios() {
    // TEST 1: in picioare, zona sigura -> stres / adrenalina mici, arma stabila
    {
        PhysRun r;
        r.idle(60.0);
        std::printf("  [T1 repaus] stres %.3f adrenalina %.3f puls %.0f tremur %.2f mrad stabilitate %.2f stare %s\n",
                    r.p.stress, r.p.adrenaline, r.p.heart_rate, r.p.tremor_amp * 1000.0, r.p.weapon_stability, phys_state_name(r.p.state));
        CHECK(r.p.stress < 0.05 && r.p.adrenaline < 0.03);
        CHECK(r.p.weapon_stability > 0.85 && r.p.state == PhysState::Calm);
    }
    // TEST 2: un singur foc aproape -> crestere mica, raspuns mic de adrenalina, fara tremur violent
    {
        PhysRun r;
        r.idle(10.0);
        double trem0 = r.p.tremor_amp;
        physiology_event(r.p, PhysEvent::NearbyGunfire, 1.0, r.cfg);
        r.idle(1.0 / 60.0);
        CHECK(r.p.stress < 0.01);                             // nu instant
        double smax = 0, amax = 0, tmax = 0;
        r.run(12.0, [&](int) { smax = std::max(smax, r.p.stress); amax = std::max(amax, r.p.adrenaline); tmax = std::max(tmax, r.p.tremor_amp); });
        std::printf("  [T2 un foc] stres max %.3f adrenalina max %.3f tremur %.2f -> %.2f mrad\n", smax, amax, trem0 * 1000.0, tmax * 1000.0);
        CHECK(smax > 0.02 && smax < 0.15);
        CHECK(amax > 0.005 && amax < 0.15);
        CHECK(tmax < trem0 + 0.0004);
    }
    // TEST 3: mai multe focuri aproape -> stres, adrenalina, puls, respiratie cresc treptat
    {
        PhysRun r;
        r.idle(10.0);
        double hr0 = r.p.heart_rate, br0 = r.p.breath_rate;
        std::vector<double> st;
        int start = r.frame;
        r.run(8.0, [&](int f) {
            int k = f - start;
            if (k % 30 == 0) physiology_event(r.p, PhysEvent::NearbyGunfire, 1.0, r.cfg);
            if (k % 60 == 0) st.push_back(r.p.stress);
        });
        std::printf("  [T3 mai multe focuri] stres %.2f %.2f %.2f %.2f -> %.2f, adrenalina %.2f, puls %.0f -> %.0f, respiratie %.1f -> %.1f\n",
                    st[1], st[2], st[4], st[6], r.p.stress, r.p.adrenaline, hr0, r.p.heart_rate, br0, r.p.breath_rate);
        CHECK(st[1] < st[2] && st[2] < st[4] && st[4] < st[6] && st[6] < r.p.stress);
        CHECK(r.p.adrenaline > 0.1 && r.p.heart_rate > hr0 + 15.0 && r.p.breath_rate > br0 + 2.0);
        CHECK(r.max_trem < 0.0030);
    }
    // TEST 4: rana -> varf de stres si adrenalina, puls, instabilitate subtila
    {
        PhysRun r;
        r.idle(10.0);
        double s0 = r.p.stress, hr0 = r.p.heart_rate, tr0 = r.p.tremor_amp;
        physiology_event(r.p, PhysEvent::MinorDamage, 1.0, r.cfg);
        r.in.arm_injury = 0.2;
        r.in.hp_frac = 0.75;
        r.idle(5.0);
        std::printf("  [T4 rana] stres %.2f -> %.2f adrenalina %.2f puls %.0f -> %.0f tremur %.2f -> %.2f mrad\n",
                    s0, r.p.stress, r.p.adrenaline, hr0, r.p.heart_rate, tr0 * 1000.0, r.p.tremor_amp * 1000.0);
        CHECK(r.p.stress > s0 + 0.08 && r.p.adrenaline > 0.12 && r.p.heart_rate > hr0 + 10.0);
        CHECK(r.p.tremor_amp > tr0 && r.p.tremor_amp < 0.0025);
    }
    // TEST 5..7: lupta sustinuta (3 minute) -> sfarsit brusc -> perioada sigura
    PhysRun r;
    r.in.stamina = 0.75;
    r.idle(20.0);
    double fresh_hr = r.p.heart_rate;
    double s30 = 0, s120 = 0, a30 = 0, ws30 = 0, fat30 = 0;
    r.combat(30.0, false);
    s30 = r.p.stress; a30 = r.p.adrenaline; ws30 = r.p.weapon_stability; fat30 = r.p.fatigue;
    r.combat(90.0, true);
    s120 = r.p.stress;
    r.combat(60.0, false);
    const double s_end = r.p.stress, a_end = r.p.adrenaline, hr_end = r.p.heart_rate, ws_end = r.p.weapon_stability;
    std::printf("  [T5 lupta 3 min] stres %.2f -> %.2f -> %.2f  adrenalina %.2f -> %.2f  oboseala %.3f -> %.3f  stabilitate %.2f -> %.2f  puls %.0f  tremur max %.2f mrad  stare %s\n",
                s30, s120, s_end, a30, a_end, fat30, r.p.fatigue, ws30, ws_end, hr_end, r.max_trem * 1000.0, phys_state_name(r.p.state));
    CHECK(s30 < s120 && a30 < a_end + 0.05);
    CHECK(r.p.fatigue > fat30);
    CHECK(ws_end < ws30);
    CHECK(r.max_trem < 0.0045 && r.p.fine_motor >= r.cfg.fine_motor_min);   // jucabil
    // TEST 6: lupta se termina brusc -> stresul nu dispare, adrenalina ramane, urmeaza descarcarea
    r.in.stamina = 0.75;
    r.idle(1.0);
    CHECK(r.p.stress > 0.85 * s_end && r.p.adrenaline > 0.8 * a_end);
    bool dump_seen = false;
    double t_dump = -1.0, dump_max = 0.0, br_dump = 0.0, fat_eff_dump = 0.0, fat_dump = 0.0, ws_dump = 1.0, ws_min_after = 1.0;
    int start = r.frame;
    r.run(90.0, [&](int f) {
        if (!dump_seen && r.p.state == PhysState::AdrenalineDump) {
            dump_seen = true;
            t_dump = (f - start) / 60.0;
        }
        ws_min_after = std::min(ws_min_after, r.p.weapon_stability);
        if (r.p.dump > dump_max) {
            // la varful descarcarii: respiratie inca grea, oboseala "iese la iveala", arma doar putin mai instabila
            dump_max = r.p.dump;
            br_dump = r.p.breath_rate; fat_eff_dump = r.p.fatigue_eff; fat_dump = r.p.fatigue; ws_dump = r.p.weapon_stability;
        }
    });
    std::printf("  [T6 sfarsitul luptei] descarcare dupa %.0f s (nivel %.2f, durata %.0f s), varf %.2f: respiratie %.1f  oboseala resimtita %.2f (reala %.2f)  stabilitate %.2f (in lupta %.2f)\n",
                t_dump + 1.0, r.p.dump_level, r.p.dump_duration, dump_max, br_dump, fat_eff_dump, fat_dump, ws_dump, ws_end);
    CHECK(dump_seen && t_dump < 30.0);
    CHECK(dump_max > 0.3 && br_dump > r.cfg.br_rest + 4.0 && fat_eff_dump > fat_dump + 0.05);
    CHECK(ws_min_after > ws_end - 0.05);              // descarcarea nu e tremur violent: arma nu devine mai instabila decat in lupta
    // TEST 7: perioada sigura -> adrenalina scade treptat, pulsul si respiratia revin, arma redevine stabila
    std::vector<double> adr, hr;
    start = r.frame;
    r.run(240.0, [&](int f) {
        if ((f - start) % (60 * 30) == 0) { adr.push_back(r.p.adrenaline); hr.push_back(r.p.heart_rate); }
    });
    std::printf("  [T7 revenire] adrenalina:");
    for (double v : adr) std::printf(" %.2f", v);
    std::printf("  puls:");
    for (double v : hr) std::printf(" %.0f", v);
    std::printf("  -> stres %.2f puls %.0f respiratie %.1f stabilitate %.2f stare %s\n",
                r.p.stress, r.p.heart_rate, r.p.breath_rate, r.p.weapon_stability, phys_state_name(r.p.state));
    bool falling = true;
    for (size_t i = 1; i < adr.size(); ++i) if (adr[i] > adr[i - 1] + 1e-6) falling = false;
    CHECK(falling && adr.size() >= 6 && adr.front() > adr.back() + 0.2);
    CHECK(hr[1] > hr[3] && hr[3] > hr.back());
    CHECK(r.p.heart_rate < fresh_hr + 15.0 && r.p.stress < 0.2 && r.p.breath_rate < r.cfg.br_rest + 6.0);
    CHECK(r.p.weapon_stability > 0.8);
    // TEST 8: a doua lupta imediat dupa prima -> porneste dintr-o stare deja ridicata si urca mai repede
    PhysRun fresh, again;
    fresh.in.stamina = again.in.stamina = 0.75;
    fresh.idle(20.0);
    again.idle(20.0);
    again.combat(90.0, false);
    again.idle(12.0);                                 // pauza scurta
    double start_s = again.p.stress, start_hr = again.p.heart_rate, start_a = again.p.adrenaline;
    fresh.combat(20.0, false);
    again.combat(20.0, false);
    std::printf("  [T8 a doua lupta] start: stres %.2f puls %.0f adrenalina %.2f | dupa 20 s: proaspat stres %.2f puls %.0f, a doua oara stres %.2f puls %.0f\n",
                start_s, start_hr, start_a, fresh.p.stress, fresh.p.heart_rate, again.p.stress, again.p.heart_rate);
    CHECK(start_s > 0.2 && start_hr > fresh_hr + 15.0);
    CHECK(again.p.stress > fresh.p.stress + 0.1 && again.p.heart_rate > fresh.p.heart_rate);
}

// La stres maxim jocul ramane jucabil: tremur marginit, camera protejata, miscare corelata, fara salturi
static void test_physiology_playable_and_correlated() {
    PhysRun r;
    r.in.stamina = 0.2;
    r.in.in_combat = true;
    r.run(90.0, [&](int f) {
        if (f % 30 == 0) { physiology_event(r.p, PhysEvent::NearMiss, 1.0, r.cfg); physiology_force_stress(r.p, 1.0, r.cfg); }
    });
    double cam_rms = std::sqrt(r.cam_sq / r.n), trem_rms = std::sqrt(r.trem_sq / r.n);
    std::printf("  [stres maxim] stres %.2f adrenalina %.2f puls %.0f tremur max %.2f mrad (arma) camera max %.3f mrad, rms camera / arma %.3f, salt maxim pe cadru %.3f mrad, motricitate %.2f\n",
                r.p.stress, r.p.adrenaline, r.p.heart_rate, r.max_trem * 1000.0, r.max_cam * 1000.0, cam_rms / std::max(1e-9, trem_rms),
                r.max_dtrem * 1000.0, r.p.fine_motor);
    CHECK(r.p.stress > 0.95 && r.p.adrenaline > 0.7);
    CHECK(r.max_trem < 0.0045);                       // arma ramane controlabila (< 0.26 grade)
    CHECK(cam_rms < 0.25 * trem_rms);                 // camera << maini / arma
    CHECK(r.max_cam < 0.0006);
    CHECK(r.max_dtrem < 0.0004);                      // fara oscilatii violente intre cadre
    CHECK(r.p.fine_motor >= r.cfg.fine_motor_min - 1e-9);
    // miscare corelata: arma urmeaza trunchiul (aceeasi sursa), nu zgomot independent
    Physiology q;
    PhysiologyInputs in;
    in.stamina = 0.1;                                 // oboseala: banda lenta (trunchiul) domina
    double sxy = 0, sxx = 0, syy = 0;
    for (int i = 0; i < 60 * 40; ++i) {
        update_physiology(q, in, r.cfg, 1.0 / 60.0, 9);
        if (i < 600) continue;
        sxy += q.torso_yaw * q.tremor_yaw; sxx += q.torso_yaw * q.torso_yaw; syy += q.tremor_yaw * q.tremor_yaw;
    }
    double corr = sxy / std::sqrt(std::max(1e-30, sxx * syy));
    std::printf("  [lant] corelatia trunchi -> arma %.2f\n", corr);
    CHECK(corr > 0.6);
}

static void test_magazines_and_reload_styles() {
    World w = make_world_with_player(open_room(20, 10), 2.5, 5.0, true);
    WeaponSlot& sl = w.agents[0].slots[0];
    sl.mag = 5;
    size_t spares0 = sl.spares.size();
    AgentControl c;
    c.reload = true;
    w.step(&c);
    c.reload = false;
    CHECK(w.agents[0].reload_stage != 0);
    CHECK(w.agents[0].rplan.style == ReloadStyle::Tactical);   // calm, incarcator partial
    double t = 0.0;
    while (w.agents[0].reload_stage != 0 && t < 6.0) { w.step(&c); t += w.dt; }
    CHECK(w.agents[0].reload_stage == 0);
    CHECK(sl.mag == 30 && sl.chambered);
    CHECK(sl.spares.size() == spares0);       // incarcatorul partial (5) a fost pastrat
    bool kept5 = false;
    for (const auto& m : sl.spares) if (m.rounds == 5) kept5 = true;
    CHECK(kept5);

    // Reincarcare pe gol in lupta: incarcatorul gol cade, inchizatorul trebuie eliberat
    sl.mag = 0;
    sl.chambered = false;
    w.agents[0].phys.time_since_threat = 0.0;
    c.reload = true;
    w.step(&c);
    c.reload = false;
    CHECK(w.agents[0].rplan.style == ReloadStyle::Emergency || w.agents[0].rplan.style == ReloadStyle::Fast);
    CHECK(w.agents[0].rplan.drop_mag);
    t = 0.0;
    bool bolt = false;
    while (w.agents[0].reload_stage != 0 && t < 6.0) {
        w.step(&c);
        t += w.dt;
        if (w.agents[0].reload_stage == RS_Bolt) bolt = true;
    }
    CHECK(bolt);
    CHECK(sl.chambered && sl.mag == 29);

    // Variatie controlata: doua planuri nu sunt identice, dar raman in limite
    ReloadContext ctx;
    ReloadPlan p1 = make_reload_plan(w.weapons[0], ctx, 7, 1, 1);
    ReloadPlan p2 = make_reload_plan(w.weapons[0], ctx, 7, 1, 2);
    CHECK(p1.dur[RS_MagIn] != p2.dur[RS_MagIn] || p1.tilt != p2.tilt);
    CHECK(p1.tilt > 0.1 && p1.tilt < 0.9);
    ctx.stress = 0.97;                       // supraincarcare: panica
    CHECK(make_reload_plan(w.weapons[0], ctx, 7, 1, 3).style == ReloadStyle::Panic);
    ctx.stress = 0.88; ctx.time_since_threat = 1.0;   // lupta grea obisnuita: rapid, nu panica
    CHECK(make_reload_plan(w.weapons[0], ctx, 7, 1, 3).style == ReloadStyle::Fast);
    ctx.time_since_threat = 99.0;
    ctx.stress = 0.0;
    ctx.arm_injury = 0.6;
    ReloadPlan inj = make_reload_plan(w.weapons[0], ctx, 7, 1, 4);
    CHECK(inj.style == ReloadStyle::Injured);
    CHECK(inj.dur[RS_MagIn] > p1.dur[RS_MagIn] * 1.3);
}

static void test_reload_interrupt_resume() {
    World w = make_world_with_player(open_room(30, 10), 2.5, 5.0, true);
    WeaponSlot& sl = w.agents[0].slots[0];
    sl.mag = 10;
    AgentControl c;
    c.reload = true;
    w.step(&c);
    c.reload = false;
    int guard = 0;
    while (w.agents[0].reload_stage != RS_Fetch && guard++ < 400) w.step(&c);
    CHECK(w.agents[0].reload_stage == RS_Fetch);
    CHECK(sl.mag == -1);                    // incarcatorul e deja scos
    AgentControl sprint;
    sprint.moveForward = 1.0;
    sprint.sprint = true;
    run_steps(w, sprint, 60);
    CHECK(w.agents[0].reload_stage == 0);   // intrerupt
    CHECK(sl.mag == -1 && sl.chambered);    // starea logica pastrata: fara incarcator, un cartus pe teava
    CHECK(w.agents[0].ammo == 1);
    // reluare: incepe direct cu luarea incarcatorului (fara scoatere)
    AgentControl r;
    r.reload = true;
    w.step(&r);
    r.reload = false;
    bool saw_magout = false;
    guard = 0;
    while (w.agents[0].reload_stage != 0 && guard++ < 600) {
        if (w.agents[0].reload_stage == RS_MagOut) saw_magout = true;
        w.step(&r);
    }
    CHECK(!saw_magout);
    CHECK(sl.mag == 30);
}

static void test_fire_heat_recoil_malfunction() {
    World w = make_world_with_player(open_room(30, 10), 2.5, 5.0);
    AgentControl c;
    c.fire = true;
    int shots = 0;
    for (int i = 0; i < 60; ++i) {
        w.step(&c);
        for (const auto& e : w.event_log.get_all()) if (e.type == EventType::SHOT) shots++;
        w.event_log.clear();
    }
    CHECK(shots >= 11 && shots <= 13);
    CHECK(w.agents[0].kick_pitch > 0.0);
    CHECK(w.agents[0].slots[0].heat > 0.03);
    CHECK(w.agents[0].ammo == 31 - shots);   // 30 + 1 pe teava
    double heat = w.agents[0].slots[0].heat;
    AgentControl idle;
    run_steps(w, idle, 120);
    CHECK(w.agents[0].kick_pitch < 0.01);
    CHECK(w.agents[0].slots[0].heat < heat);
    // blocaj fortat: tragaciul nu mai trage, R il inlatura (tap-rack)
    w.agents[0].slots[0].malfunction = 1;
    int ammo0 = w.agents[0].ammo;
    AgentControl f;
    f.fire = true;
    run_steps(w, f, 10);
    CHECK(w.agents[0].ammo == ammo0);
    AgentControl rr;
    rr.reload = true;
    w.step(&rr);
    CHECK(w.agents[0].rplan.style == ReloadStyle::Clear);
    run_steps(w, idle, 120);
    CHECK(w.agents[0].slots[0].malfunction == 0);
}

static void test_ballistics() {
    Map m;
    m.from_rows({"1111111111", "1........1", "1...2....1", "1...1....1", "1...6....1", "1111111111"});
    BulletRay r{1.5, 2.5, 1.5, 0.0, 0.0, 1700.0, 100.0, 0};
    std::vector<TargetBody> targets = {{5, 6.5, 2.5, 0.0, 1.8, 0.3, true, 3.14159}};
    auto imps = trace_bullet(m, r, targets);
    bool hit_wood = false, hit_target = false;
    for (const auto& i : imps) {
        if (i.surface == SurfaceType::Wood && !i.exit) hit_wood = true;
        if (i.target_id == 5) { hit_target = true; CHECK(i.energy < 1700.0 * 0.6); CHECK(i.zone == HitZone::Torso); }
    }
    CHECK(hit_wood && hit_target);
    BulletRay r2{1.5, 3.5, 1.5, 0.0, 0.0, 1700.0, 100.0, 0};
    auto imps2 = trace_bullet(m, r2, {{5, 6.5, 3.5, 0.0, 1.8, 0.3, true, 0.0}});
    CHECK(!imps2.empty() && imps2[0].surface == SurfaceType::Concrete && !imps2[0].penetrated);
    // sticla: trece cu pierdere mica de energie
    BulletRay r3{1.5, 4.5, 1.5, 0.0, 0.0, 1700.0, 100.0, 0};
    auto imps3 = trace_bullet(m, r3, {{9, 6.5, 4.5, 0.0, 1.8, 0.3, true, 0.0}});
    bool glass = false, through = false;
    for (const auto& i : imps3) { if (i.surface == SurfaceType::Glass) glass = true; if (i.target_id == 9) through = true; }
    CHECK(glass && through);
    // stanga / dreapta
    BulletRay r4{1.5, 1.5, 1.2, 0.0, 0.0, 1700.0, 100.0, 0};
    auto imps4 = trace_bullet(m, r4, {{7, 5.5, 1.75, 0.0, 1.8, 0.3, true, 3.14159}});
    CHECK(!imps4.empty() && (imps4[0].zone == HitZone::LeftArm || imps4[0].zone == HitZone::RightArm));
}

static void test_projectiles() {
    World w = make_world_with_player(open_room(120, 10), 2.5, 5.0);
    w.weapons[0].projectile = true;
    w.weapons[1] = make_default_pistol();
    w.agents.push_back(make_bot(1, 100.5, 5.0));
    AgentControl c;
    c.aim = true;
    run_steps(w, c, 30);
    c.fire = true;
    w.step(&c);
    c.fire = false;
    CHECK(!w.projectiles.empty());           // glontul zboara (nu e instantaneu)
    bool hit = false;
    int ticks = 0;
    for (int i = 0; i < 30 && !hit; ++i) {
        w.step(&c);
        ticks++;
        for (const auto& e : w.event_log.get_all()) if (e.type == EventType::HIT) hit = true;
    }
    CHECK(hit);
    CHECK(ticks >= 6);                       // ~98 m la ~860 m/s: cateva tick-uri de zbor
}

static void test_localized_damage_downed_revive_bandage() {
    World w = make_world_with_player(open_room(30, 10), 2.5, 5.0, true);
    Agent mate;
    mate.id = 1;
    mate.faction = Faction::Blue;
    mate.x = 3.5; mate.y = 5.0;
    mate.is_human = true;
    mate.health.init(w.health_cfg);
    w.agents.push_back(mate);
    w.agents.push_back(make_bot(2, 20.5, 5.0));
    w.agents[0].health.init(w.health_cfg);
    // bratul ranit: tremur, motricitate scazuta
    w.agents[0].health.zone[3] = 5.0;
    AgentControl idle;
    run_steps(w, idle, 120);
    CHECK(w.agents[0].health.arm_injury() > 0.8);
    CHECK(w.agents[0].phys.fine_motor < 0.8);
    // piciorul ranit: viteza maxima redusa, fara sprint
    w.agents[0].health.zone[5] = 0.0;
    AgentControl run;
    run.moveForward = 1.0;
    run.sprint = true;
    run_steps(w, run, 90);
    CHECK(!w.agents[0].sprinting);
    CHECK(w.agents[0].speed < w.params.v_walk * 0.7);
    // incapacitare (nu moarte instanta) -> resuscitare de catre coechipier
    w.agents[1].hp = 0.005;
    w.agents[1].health.bleeding = 1.0;
    w.step(&idle);
    CHECK(w.agents[1].health.downed);
    CHECK(w.agents[1].alive());
    bool revive_it = false;
    for (const auto& it : w.interactables) if (it.kind == InteractKind::Revive && it.target_agent == 1) revive_it = true;
    CHECK(revive_it);
    AgentControl rev;
    rev.look_yaw = 0.0;
    rev.interact = true;
    rev.interact_hold = true;
    w.agents[0].x = w.agents[1].x - 0.9;
    w.agents[0].y = w.agents[1].y;
    w.agents[0].vx = w.agents[0].vy = 0.0;   // salvatorul sta pe loc (miscarea intrerupe actiunea)
    w.step(&rev);
    rev.interact = false;
    run_steps(w, rev, 60);
    run_steps(w, rev, 60 * 6);
    CHECK(!w.agents[1].health.downed);
    CHECK(has_event(w, EventType::REVIVE));
    // bandaj: opreste sangerarea
    w.agents[0].health.bleeding = 1.5;
    AgentControl band;
    band.bandage = true;
    int b0 = w.agents[0].inv.bandages;
    run_steps(w, band, 60 * 5);
    CHECK(w.agents[0].health.bleeding == 0.0);
    CHECK(w.agents[0].inv.bandages == b0 - 1);
}

static void test_stance_classes() {
    PhysicalStance s;
    s.applyImpact(0.1, 1, 0, 1);
    CHECK(s.state == StanceState::Unstable);
    PhysicalStance s2;
    s2.applyImpact(0.6, 1, 0, 1);
    CHECK(s2.state == StanceState::Stumbling);
    PhysicalStance s3;
    s3.applyImpact(1.5, 1, 0, -1);
    CHECK(s3.state == StanceState::Ragdoll);
    for (int i = 0; i < 600; ++i) s3.update(1.0 / 60.0);
    CHECK(s3.state == StanceState::Stable);
}

static void test_lightmap_power() {
    Map m;
    m.from_rows({"1111111111", "1;;;;1...1", "1;;;;....1", "1;;;;1.L.1", "1111111111"});
    LightMap lm;
    lm.build(m);
    float out = lm.cell(2, 2).luminance();
    float door = lm.cell(6, 2).luminance();
    float lamp = lm.cell(7, 3).luminance();
    CHECK(out > door);
    CHECK(lamp > 0.5f);
    m.power = false;
    lm.build(m);
    Rgb em = lm.cell(7, 3);
    CHECK(em.luminance() < lamp * 0.3f);
    CHECK(em.r > em.g * 3.0f);                    // lumina de avarie e rosie
}

static void test_interactions() {
    Map m;
    m.from_rows({"1111111111", "1......z.1", "1111X11111", "1..L.....1", "1111111111"});
    World w = make_world_with_player(m, 4.5, 1.5, true);
    w.emit_fx_events = true;
    AgentControl c;
    c.look_yaw = 1.5708;
    c.interact = true;
    w.step(&c);
    c.interact = false;
    run_steps(w, c, 5);
    CHECK(has_event(w, EventType::DOOR_LOCKED));
    CHECK(w.map.is_wall(4, 2));
    // spargerea usii (poate dura cateva lovituri)
    AgentControl k;
    k.look_yaw = 1.5708;
    for (int i = 0; i < 12 && w.map.is_locked(4, 2); ++i) {
        k.kick = true;
        w.step(&k);
        k.kick = false;
        run_steps(w, k, 60);
        w.agents[0].stamina = 1.0;
    }
    CHECK(!w.map.is_locked(4, 2));
    run_steps(w, k, 60);
    CHECK(!w.map.is_wall(4, 2));
    // intrerupatorul stinge lampile incaperii lui (alta incapere ramane neschimbata)
    World s = make_world_with_player(m, 6.5, 1.5, true);
    AgentControl sw;
    sw.look_yaw = 0.0;
    sw.interact = true;
    s.step(&sw);
    CHECK(has_event(s, EventType::LIGHT_SWITCH));
    // marcaje tactice
    AgentControl mk;
    mk.place_marker = true;
    mk.marker_x = 5.0; mk.marker_y = 3.5;
    s.step(&mk);
    CHECK(s.markers.size() == 1);
    // tableta: atingerea hartii produce un marcaj
    TabletScreen scr;
    TabletUiState st;
    st.page = TabletPage::Map;
    TabletClick click;
    click.clicked = true;
    click.u = 0.5; click.v = 0.5;
    TabletCommand cmd = tablet_ui(scr, st, s, 0, click, 0.0, TabletInfo{});
    CHECK(cmd.place_marker);
    CHECK(cmd.mx > 0.0 && cmd.mx < m.width && cmd.my > 0.0 && cmd.my < m.height);
}

static void test_spatial_audio() {
    Map m = open_room(30, 10);
    audio::SpatialAudioPipeline p;
    audio::AcousticSource src{10.5, 5.0, 1.0, 1.0};
    auto clear = p.computeAcousticTransmission(2.5, 5.0, 0.0, src, m);
    CHECK(clear.walls == 0);
    CHECK(std::abs(clear.leftGain - clear.rightGain) < 1e-6);
    m.grid[5 * 30 + 6] = '1';
    auto occ = p.computeAcousticTransmission(2.5, 5.0, 0.0, src, m);
    CHECK(occ.walls == 1);
    CHECK(occ.leftGain < clear.leftGain * 0.5);
    CHECK(occ.lowpassHz < clear.lowpassHz);
    audio::AcousticSource right{2.5, 8.0, 1.0, 1.0};
    auto r = p.computeAcousticTransmission(2.5, 5.0, 0.0, right, m);
    CHECK(r.rightGain > r.leftGain);

    // materiale: prin lemn se aude mai mult si mai clar decat prin beton
    m.grid[5 * 30 + 6] = '2';
    auto wood = p.computeAcousticTransmission(2.5, 5.0, 0.0, src, m);
    CHECK(wood.leftGain > occ.leftGain * 1.5);
    CHECK(wood.lowpassHz > occ.lowpassHz);

    // difractie: doua camere despartite de beton, usa deschisa mai jos -> sunetul vine
    // ocolind prin usa (mai putin infundat decat prin zid, din directia usii)
    Map h;
    h.from_rows({"111111111111",
                 "1....1.....1",
                 "1....1.....1",
                 "1....1.....1",
                 "1....D.....1",
                 "111111111111"});
    audio::AcousticSource other{7.5, 1.5, 1.0, 1.0};
    h.set_door(5, 4, false, 1);
    p.build_field(h, 2.5, 1.5);
    auto closed = p.computeAcousticTransmission(2.5, 1.5, 0.0, other, h);
    h.set_door(5, 4, true, 1);
    h.update_doors(10.0);
    p.build_field(h, 2.5, 1.5);
    auto open = p.computeAcousticTransmission(2.5, 1.5, 0.0, other, h);
    std::printf("  [ocluzie] beton %.0f Hz  lemn %.0f Hz  usa inchisa %.0f Hz  difractie %.0f Hz (castig %.3f -> %.3f)\n",
                occ.lowpassHz, wood.lowpassHz, closed.lowpassHz, open.lowpassHz, closed.leftGain + closed.rightGain,
                open.leftGain + open.rightGain);
    CHECK(closed.lowpassHz < 1100.0);                // zid de beton + usa inchisa: doar basul
    CHECK(open.diffracted);
    CHECK(open.lowpassHz > closed.lowpassHz * 2.5);   // prin usa: mult mai clar
    CHECK(open.leftGain + open.rightGain > closed.leftGain + closed.rightGain);
    CHECK(open.rightGain > open.leftGain);            // usa e in dreapta-jos (fata = +x, y in jos)
    // zid gros (doua straturi de beton): chiar si prin usa inchisa se aude mai bine decat prin zid
    Map t;
    t.from_rows({"1111111111111",
                 "1....11.....1",
                 "1....11.....1",
                 "1....11.....1",
                 "1....DD.....1",
                 "1111111111111"});
    t.set_door(5, 4, false, 1);
    t.set_door(6, 4, false, 1);
    audio::AcousticSource far2{8.5, 1.5, 1.0, 1.0};
    p.build_field(t, 2.5, 1.5);
    auto thick = p.computeAcousticTransmission(2.5, 1.5, 0.0, far2, t);
    std::printf("  [ocluzie] zid gros + usi inchise: %.0f Hz, difractat %d, castig %.4f\n", thick.lowpassHz,
                thick.diffracted ? 1 : 0, thick.leftGain + thick.rightGain);
    CHECK(thick.walls == 2);
    CHECK(thick.diffracted);                          // vine dinspre usi
    CHECK(thick.lowpassHz <= 1100.0 && thick.lowpassHz > 450.0);
}

static void test_grenade_props_pawns() {
    Map m;
    m.from_rows({"1111111111", "1........1", "1111D11111", "1........1", "1111111111"});
    World w = make_world_with_player(m, 4.5, 1.5);
    w.emit_fx_events = true;
    w.map.set_door(4, 2, true, 1);
    run_steps(w, AgentControl{}, 60);
    w.agents.push_back(make_bot(1, 4.5, 3.5));
    AgentControl c;
    c.look_yaw = 1.5708;
    c.look_pitch = -0.6;
    c.throw_grenade = true;
    w.step(&c);
    c.throw_grenade = false;
    bool boom = false;
    for (int i = 0; i < 240; ++i) { w.step(&c); boom |= has_event(w, EventType::EXPLOSION); }
    CHECK(boom);
    CHECK(w.agents[1].hp < 100.0);
    Prop p;
    init_prop(p, PropType::Crate);
    p.x = 7.5; p.y = 3.5;
    push_prop(p, 30.0, 0.0, 0.0);
    for (int i = 0; i < 300; ++i) step_prop(p, w.map, 9.81, 1.0 / 60.0);
    CHECK(p.sleeping);
    Drone d;
    d.x = 5.0; d.y = 1.5; d.z = 1.2;
    AgentControl hover;
    for (int i = 0; i < 120; ++i) step_drone(d, &hover, w.map, 9.81, 1.0 / 60.0);
    CHECK(std::abs(d.z - 1.2) < 0.25);
    Vehicle v;
    v.x = 2.5; v.y = 3.5;
    AgentControl drive;
    drive.stick_pitch = 1.0;
    drive.stick_roll = 0.5;
    for (int i = 0; i < 30; ++i) step_vehicle(v, &drive, w.map, 1.0 / 60.0);
    CHECK(v.speed > 1.0 && v.yaw > 0.05);
}

static void test_determinism_and_bots() {
    auto run = [](uint64_t seed) {
        World w(seed);
        RoundConfig cfg;
        cfg.asset_root = find_asset_root();
        cfg.human_players = 0;
        cfg.blue_bots = 2;
        cfg.seed = seed;
        setup_round(w, cfg, nullptr);
        for (int i = 0; i < 60 * 40; ++i) w.step();
        uint64_t h = 0;
        for (const auto& a : w.agents) {
            h ^= fnv1a(&a.hp, sizeof(a.hp)) + 31 * h;
            h ^= fnv1a(&a.x, sizeof(a.x));
            h ^= fnv1a(&a.phys.stress, sizeof(a.phys.stress));
        }
        return std::make_pair(h, w.event_log.size());
    };
    auto a = run(7), b = run(7), c = run(8);
    CHECK(a == b);
    CHECK(a.first != c.first);
    CHECK(a.second > 10);
}

static void test_runner_scenarios() {
    ExpConfig cfg;
    cfg.reps = 3;
    cfg.out_dir = "test_results";
    cfg.write_events = true;
    cfg.scenario_file = find_asset_root() + "/scenarios/e1_range.ini";
    run_experiment(cfg);
    std::ifstream in("test_results/replications.csv");
    std::string line;
    int lines = 0;
    while (std::getline(in, line)) lines++;
    CHECK(lines == 4);
}

static void test_mantle_wallblock_cover() {
    // Zid scund in mijlocul camerei: escaladare cu Space
    Map m;
    m.from_rows({"111111111", "1.......1", "1...h...1", "1.......1", "111111111"});
    World w = make_world_with_player(m, 2.5, 2.5);
    CHECK(w.map.is_wall(4, 2) && !w.map.blocks_sight(4, 2) && w.map.is_low(4, 2));
    AgentControl c;
    c.look_yaw = 0.0;
    run_steps(w, c, 20);
    c.moveForward = 1.0;
    run_steps(w, c, 40);                      // mergem pana la zid: ramanem blocati inainte de el
    CHECK(w.agents[0].x < 4.0);
    c.jump = true;
    w.step(&c);
    c.jump = false;
    CHECK(w.agents[0].mantling());
    double max_z = 0.0;
    for (int i = 0; i < 90; ++i) { w.step(&c); max_z = std::max(max_z, w.agents[0].z); }
    CHECK(!w.agents[0].mantling());
    CHECK(w.agents[0].x > 5.0);               // a trecut dincolo de zid
    CHECK(max_z > 0.8);                       // corpul a urcat peste coama
    CHECK(w.agents[0].z == 0.0 && w.agents[0].grounded);

    // Glont peste zid (de la inaltimea ochilor) trece; unul jos se opreste in zid
    BulletRay hi{2.5, 2.5, 1.5, 0.0, 0.0, 1500.0, 20.0, 0};
    auto imps = trace_bullet(w.map, hi, {});
    bool stopped_at_low = false;
    for (const auto& im : imps) if (im.cell_x == 4 && im.cell_y == 2) stopped_at_low = true;
    CHECK(!stopped_at_low);
    BulletRay lo{2.5, 2.5, 0.5, 0.0, 0.0, 1500.0, 20.0, 0};
    imps = trace_bullet(w.map, lo, {});
    CHECK(!imps.empty() && imps.front().cell_x == 4 && imps.front().surface == SurfaceType::Concrete);

    // Ghemuit in spatele zidului: nu se vede de departe; in picioare se vede
    Agent t = make_bot(1, 5.4, 2.5);
    t.crouch = 1.0;
    CHECK(w.hidden_by_low_cover(1.5, 2.5, 1.62, t));
    t.crouch = 0.0;
    CHECK(!w.hidden_by_low_cover(1.5, 2.5, 1.62, t));

    // Teava lipita de perete: arma blocata, nu se poate trage
    World b = make_world_with_player(open_room(8, 6), 6.55, 3.0);
    AgentControl f;
    f.look_yaw = 0.0;
    run_steps(b, f, 30);
    CHECK(b.agents[0].wall_block > 0.75);
    int ammo0 = b.agents[0].ammo;
    f.fire = true;
    run_steps(b, f, 20);
    CHECK(b.agents[0].ammo == ammo0);
    f.look_yaw = 3.14159;                     // intoarcere spre camera libera
    f.fire = false;
    run_steps(b, f, 40);
    CHECK(b.agents[0].wall_block < 0.1);
}

static void test_unseated_magazine() {
    World w = make_world_with_player(open_room(10, 6), 3.0, 3.0);
    AgentControl c;
    run_steps(w, c, 10);
    WeaponSlot& sl = w.agents[0].slots[0];
    sl.mag_seated = false;                    // ca dupa o reincarcare sub panica
    int mag0 = sl.mag;
    c.fire = true;
    w.step(&c);                               // cartusul de pe teava pleaca...
    c.fire = false;
    run_steps(w, c, 10);
    CHECK(sl.malfunction == 2 && !sl.chambered && sl.mag == mag0);   // ...dar urmatorul nu urca
    c.fire = true;
    run_steps(w, c, 5);
    CHECK(sl.mag == mag0);                    // doar "click"
    c.fire = false;
    c.reload = true;
    w.step(&c);
    c.reload = false;
    CHECK(w.agents[0].rplan.style == ReloadStyle::Clear);
    run_steps(w, c, 120);
    CHECK(sl.malfunction == 0 && sl.mag_seated && sl.chambered && sl.mag == mag0 - 1);
    // sub stres foarte mare incarcatorul ramane uneori nefixat (rar: reincarcarea ramane fiabila)
    WeaponData wd = make_default_rifle();
    ReloadContext ctx;
    ctx.stress = 0.95; ctx.has_mag = true; ctx.mag_rounds = 0; ctx.chambered = false;
    int fails = 0;
    for (int i = 0; i < 400; ++i) fails += make_reload_plan(wd, ctx, 5, 1, static_cast<uint64_t>(i)).seat_fail ? 1 : 0;
    CHECK(fails > 15 && fails < 70);
    ctx.stress = 0.0; ctx.time_since_threat = 99.0;
    int calm_fails = 0;
    for (int i = 0; i < 400; ++i) calm_fails += make_reload_plan(wd, ctx, 5, 1, static_cast<uint64_t>(i)).seat_fail ? 1 : 0;
    CHECK(calm_fails < 20);
}

static void test_controlled_breathing() {
    PhysiologyConfig cfg;
    PhysiologyInputs in;
    // respiratia tinuta: semnalul aproape constant, CO2 creste, apoi cedeaza (gasp)
    Physiology p;
    for (int i = 0; i < 120; ++i) update_physiology(p, in, cfg, 1.0 / 60.0, 3);
    in.hold_breath = true;
    std::vector<double> sig;
    bool gasp = false, locked_after_gasp = false;
    double t_gasp = 0.0;
    for (int i = 0; i < 60 * 15; ++i) {
        update_physiology(p, in, cfg, 1.0 / 60.0, 3);
        if (i > 30 && i < 150) sig.push_back(p.breath_signal);
        if (p.gasp && !gasp) { gasp = true; t_gasp = i / 60.0; locked_after_gasp = p.hold_locked; }
    }
    double mn = 1e9, mx = -1e9;
    for (double s : sig) { mn = std::min(mn, s); mx = std::max(mx, s); }
    CHECK(mx - mn < 0.05);                    // plamanii raman plini (aproape nemiscat)
    CHECK(gasp);
    CHECK(t_gasp > 3.0 && t_gasp < 10.0);
    CHECK(locked_after_gasp);                 // dupa ce ai cedat nu o mai poti tine imediat
    // respiratia tactica: stresul scade mai repede, respiratia e mai lenta
    Physiology a, b;
    physiology_stimulus(a, 0.8, cfg);
    physiology_stimulus(b, 0.8, cfg);
    PhysiologyInputs calm, tac;
    tac.tactical_breathing = true;
    for (int i = 0; i < 60 * 12; ++i) {
        update_physiology(a, calm, cfg, 1.0 / 60.0, 1);
        update_physiology(b, tac, cfg, 1.0 / 60.0, 1);
    }
    CHECK(b.stress < a.stress - 0.03);
    CHECK(b.breath_rate < a.breath_rate);
    // un inamic vizibil foarte aproape apasa continuu
    Physiology c, d;
    PhysiologyInputs near_enemy;
    near_enemy.enemy_proximity = 0.9;
    for (int i = 0; i < 60 * 8; ++i) {
        update_physiology(c, calm, cfg, 1.0 / 60.0, 2);
        update_physiology(d, near_enemy, cfg, 1.0 / 60.0, 2);
    }
    CHECK(d.stress > c.stress + 0.05);
}

// ----------------------------------------------------------------------------
// Tableta tactica: obiect fizic, masina de stari, lumea nu se opreste
// ----------------------------------------------------------------------------
static World tablet_world(bool with_bot = true) {
    World w = make_world_with_player(open_room(14, 10), 3.5, 5.0, true);
    w.emit_fx_events = true;
    if (with_bot) {
        Agent b = make_bot(1, 10.5, 5.0);
        b.policy = Policy::Patrol;
        b.patrol_points = {{10.5, 2.0}, {10.5, 8.0}};
        b.perception_enabled = false;
        b.health.init(w.health_cfg);
        w.agents.push_back(b);
    }
    return w;
}

static int steps_until(World& w, AgentControl c, const std::function<bool()>& cond, int max_steps) {
    for (int i = 0; i < max_steps; ++i) {
        if (cond()) return i;
        w.step(&c);
        c.tablet_toggle = false; c.toggle_drone = false; c.toggle_vehicle = false; c.tablet_stow_fast = false;
        c.gadget_recall = false; c.gadget_detonate = false; c.gadget_exit = false; c.jump = false; c.switch_weapon = -1;
    }
    return -1;
}

static void test_tablet_state_machine_no_pause() {
    // TEST 1 + 12/13: deschidere fizica, tranzitii, lumea continua (botul patruleaza)
    World w = tablet_world();
    Agent& me = w.agents[0];
    AgentControl c;
    run_steps(w, c, 5);
    double bot_y0 = w.agents[1].y;
    uint64_t steps0 = w.step_count;
    c.tablet_toggle = true;
    w.step(&c);
    c.tablet_toggle = false;
    CHECK(me.tab.state == TabletState::Equipping);
    CHECK(me.tablet >= 0.0 && me.tablet < 0.2);                      // nu apare instant
    int n = steps_until(w, c, [&] { return me.tab.state == TabletState::Open; }, 200);
    CHECK(n > 20);                                                    // tranzitie (aprox. 0.75 s)
    CHECK(me.tablet == 1.0);
    run_steps(w, c, 120);
    CHECK(w.step_count > steps0 + 140);                               // simularea nu e pusa pe pauza
    CHECK(std::abs(w.agents[1].y - bot_y0) > 0.3);                    // inamicul s-a miscat
    // TEST 2: miscarea cu tableta in maini (mai lent, tableta ramane)
    double x0 = me.x;
    c.moveForward = 1.0;
    run_steps(w, c, 60);
    CHECK(me.x > x0 + 0.5);
    CHECK(me.speed < w.params.v_walk * 0.75);
    CHECK(me.tab.usable());
    c.moveForward = 0.0;
    // TEST 3: privirea in jur
    c.look_yaw = 1.0;
    run_steps(w, c, 5);
    CHECK(std::abs(me.facing - 1.0) < 1e-9 && me.tab.usable());
    c.look_yaw = 0.0;
    // nu se trage si nu se ocheste cu tableta in maini
    int ammo0 = me.ammo;
    c.fire = true; c.aim = true;
    run_steps(w, c, 20);
    CHECK(me.ammo == ammo0 && me.aim < 0.05);
    c.fire = false; c.aim = false;
    // atingerea ecranului: starea Interacting, apoi revine la Open
    c.tablet_touch = true;
    w.step(&c);
    c.tablet_touch = false;
    CHECK(me.tab.state == TabletState::Interacting);
    run_steps(w, c, 60);
    CHECK(me.tab.state == TabletState::Open);
    // TEST 7: inchiderea si focul imediat (arma revine abia dupa ce tableta coboara)
    c.tablet_toggle = true;
    c.fire = true;
    w.step(&c);
    c.tablet_toggle = false;
    CHECK(me.tab.state == TabletState::Unequipping);
    int first_shot = -1;
    for (int i = 0; i < 120 && first_shot < 0; ++i) { w.step(&c); if (me.ammo < ammo0) first_shot = i; }
    CHECK(first_shot > 10);                                           // nu instant
    CHECK(first_shot < 70);                                           // dar imediat ce mainile sunt libere
    c.fire = false;
    run_steps(w, c, 20);
    CHECK(me.tab.state == TabletState::Hidden);
}

static void test_tablet_interruptions() {
    // TEST 6: lovit cu tableta deschisa -> dauna normala, stres, tableta coborata brusc
    World w = tablet_world(false);
    Agent& me = w.agents[0];
    me.health.init(w.health_cfg);
    AgentControl c;
    c.tablet_toggle = true;
    w.step(&c);
    c.tablet_toggle = false;
    steps_until(w, c, [&] { return me.tab.state == TabletState::Open; }, 200);
    double hp0 = me.hp, st0 = me.phys.stress;
    Agent shooter = make_bot(1, 8.0, 5.0);
    shooter.weapon_enabled = true;
    w.agents.push_back(shooter);
    Agent& me2 = w.agents[0];
    // lovitura directa (aceeasi cale ca orice glont)
    BulletRay ray{8.0, 5.0, 1.3, 3.14159265, 0.0, 1500.0, 20.0, 1};
    auto imps = trace_bullet(w.map, ray, w.collect_targets(1, 0));
    bool hit_me = false;
    for (const auto& im : imps) if (im.target_id == 0) hit_me = true;
    CHECK(hit_me);
    me2.invulnerable = false;
    w.agents[1].weapon_enabled = false;
    // folosim o grenada asurzitoare foarte aproape: dauna mica + impact + intrerupere
    Grenade g;
    g.id = 0; g.owner = 1; g.x = me2.x + 0.6; g.y = me2.y; g.z = 1.0; g.fuse = 0.0; g.type = static_cast<int>(GrenadeType::Flash);
    w.grenades.push_back(g);
    run_steps(w, c, 2);
    CHECK(w.agents[0].tab.state == TabletState::Unequipping || w.agents[0].tab.state == TabletState::Hidden);
    CHECK(w.agents[0].tab.reason == TabletReason::Hit);
    CHECK(w.agents[0].hp < hp0);                                      // nu e invulnerabil
    CHECK(w.agents[0].phys.stress > st0);
    CHECK(w.agents[0].blind > 0.5);
    steps_until(w, c, [&] { return w.agents[0].tab.state == TabletState::Hidden; }, 120);
    CHECK(w.agents[0].tablet == 0.0);

    // sprint, salt, schimbarea armei: intreruperi cu tranzitie (nu reset)
    World s = tablet_world(false);
    Agent& m2 = s.agents[0];
    AgentControl o;
    o.tablet_toggle = true;
    s.step(&o);
    o.tablet_toggle = false;
    steps_until(s, o, [&] { return m2.tab.usable(); }, 200);
    o.sprint = true; o.moveForward = 1.0;
    s.step(&o);
    CHECK(m2.tab.state == TabletState::Unequipping && m2.tab.reason == TabletReason::Sprint);
    steps_until(s, o, [&] { return m2.tab.state == TabletState::Hidden; }, 60);
    run_steps(s, o, 10);
    CHECK(m2.sprinting);                                              // sprintul porneste dupa ce tableta e la loc
    o.sprint = false; o.moveForward = 0.0;
    run_steps(s, o, 30);
    o.tablet_toggle = true;
    s.step(&o);
    o.tablet_toggle = false;
    steps_until(s, o, [&] { return m2.tab.usable(); }, 200);
    o.switch_weapon = 1;
    s.step(&o);
    o.switch_weapon = -1;
    CHECK(m2.tab.reason == TabletReason::WeaponSwitch && m2.pending_switch == 1);
    steps_until(s, o, [&] { return m2.slot == 1; }, 200);
    CHECK(m2.slot == 1 && m2.tab.state == TabletState::Hidden);       // schimbarea s-a facut dupa ce tableta a fost pusa la loc
    // incapacitat: tableta scapata imediat; nu se poate scoate cazut
    o.tablet_toggle = true;
    s.step(&o);
    o.tablet_toggle = false;
    run_steps(s, o, 20);
    m2.hp = 1.0;
    m2.health.zone[1] = 1.0;
    Grenade fr;
    fr.id = 0; fr.owner = -1; fr.x = m2.x + 0.5; fr.y = m2.y; fr.z = 0.3; fr.fuse = 0.0;
    s.grenades.push_back(fr);
    run_steps(s, o, 2);
    CHECK(m2.health.downed || !m2.alive());
    CHECK(m2.tab.state == TabletState::Hidden && m2.tablet == 0.0);
    o.tablet_toggle = true;
    s.step(&o);
    CHECK(m2.tab.state == TabletState::Hidden);
}

static void test_tablet_gadgets() {
    // TEST 8: drona prin tableta - V cu tableta in buzunar o scoate si preia controlul
    World w = tablet_world(false);
    Agent& me = w.agents[0];
    AgentControl c;
    c.toggle_drone = true;
    w.step(&c);
    c.toggle_drone = false;
    CHECK(me.tab.state == TabletState::Equipping && me.possess == Possession::Body);
    steps_until(w, c, [&] { return me.possess == Possession::Drone; }, 200);
    CHECK(me.possess == Possession::Drone && me.tab.state == TabletState::Interacting);
    double bx = me.x;
    AgentControl fly;
    fly.stick_pitch = 0.7;
    fly.moveForward = 1.0;                                            // mainile pe tableta: corpul nu se misca
    run_steps(w, fly, 60);
    CHECK(std::abs(me.x - bx) < 0.05);
    CHECK(std::hypot(w.drones[me.drone_id].x - bx, w.drones[me.drone_id].y - me.y) > 1.0);
    // rechemare: pilot automat pana la operator, apoi recuperata in rucsac
    AgentControl rc;
    rc.gadget_recall = true;
    w.step(&rc);
    rc.gadget_recall = false;
    CHECK(me.possess == Possession::Body && w.drones[me.drone_id].recall);
    steps_until(w, rc, [&] { return w.drones[me.drone_id].stowed; }, 900);
    CHECK(w.drones[me.drone_id].stowed);
    // TEST 9: masina RC cu incarcatura: detonare
    World r = tablet_world(false);
    Agent& m2 = r.agents[0];
    m2.inv.rc_charge = true;
    AgentControl v;
    v.toggle_vehicle = true;
    r.step(&v);
    v.toggle_vehicle = false;
    steps_until(r, v, [&] { return m2.possess == Possession::Vehicle; }, 200);
    CHECK(m2.possess == Possession::Vehicle);
    CHECK(r.vehicles[m2.vehicle_id].charge);
    AgentControl drive;
    drive.stick_pitch = 1.0;
    run_steps(r, drive, 150);                                        // departe de operator (raza exploziei)
    CHECK(std::hypot(r.vehicles[m2.vehicle_id].x - m2.x, r.vehicles[m2.vehicle_id].y - m2.y) > r.params.grenade_radius);
    AgentControl det;
    det.gadget_detonate = true;
    r.step(&det);
    CHECK(!r.vehicles[m2.vehicle_id].alive);
    CHECK(has_event(r, EventType::EXPLOSION));
    CHECK(m2.possess == Possession::Body && m2.tab.usable());
}

static void test_loadout_throwables_melee() {
    World w = make_world_with_player(open_room(14, 10), 3.5, 5.0, true);
    w.emit_fx_events = true;
    // baza de date: arme de baza + atasamente din fisier
    w.attachment_db = load_attachment_db(find_asset_root() + "/config/attachments.ini");
    WeaponData raw;
    raw.load(find_asset_root() + "/weapons/mk18.ini", nullptr, false);
    WeaponData pr;
    pr.load(find_asset_root() + "/weapons/g17.ini", nullptr, false);
    w.weapon_raw = {raw, pr};
    w.weapons.clear();
    for (auto r2 : w.weapon_raw) { r2.apply_attachments(&w.attachment_db); w.weapons.push_back(r2); }
    w.map.markers.push_back({'S', 3, 5});                           // zona de start
    Agent& me = w.agents[0];
    Loadout l;
    l.primary = "mk18";
    l.primary_att = {"holo", "suppressor", "angledgrip", "laser", "reddot"};   // doua optici: doar prima ramane
    l.frags = 0; l.flashbangs = 2; l.smokes = 3;                    // peste limita: se taie
    l.melee = MeleeKind::Knife;
    l.drone = false;
    AgentControl c;
    c.loadout_apply = true;
    c.loadout = l;
    w.step(&c);
    const WeaponData* pw = w.weapon_of(me);
    CHECK(pw && pw->has_holo && !pw->has_reddot && pw->has_suppressor && pw->has_laser && pw->has_angledgrip);
    CHECK(me.inv.flashbangs + me.inv.smokes + me.inv.grenades == 3);
    CHECK(me.inv.melee == MeleeKind::Knife && !me.inv.drone);
    CHECK(me.pending_loadout.valid);
    // in afara zonei de start: ramane pentru runda urmatoare
    World f = make_world_with_player(open_room(14, 10), 9.5, 5.0, true);
    f.weapon_raw = w.weapon_raw; f.weapons = w.weapons; f.attachment_db = w.attachment_db;
    f.give_loadout(f.agents[0], {0, 1});
    AgentControl c2;
    c2.loadout_apply = true;
    c2.loadout = l;
    f.step(&c2);
    CHECK(!f.weapon_of(f.agents[0])->has_holo && f.agents[0].pending_loadout.valid);

    // grenada asurzitoare: botul care priveste spre ea e orbit si nu mai trage
    World g = make_world_with_player(open_room(16, 10), 3.5, 5.0, true);
    g.emit_fx_events = true;
    Agent b = make_bot(1, 10.0, 5.0);
    b.facing = 3.14159;
    b.weapon_enabled = true;
    b.perception_enabled = true;
    g.agents.push_back(b);
    Agent& gm = g.agents[0];
    gm.inv.flashbangs = 1;
    gm.inv.throwable = GrenadeType::Flash;
    AgentControl t;
    t.look_yaw = 0.0;
    t.grenade_hold = true;
    run_steps(g, t, 20);
    CHECK(gm.nade_cooking && gm.inv.flashbangs == 0);
    t.grenade_hold = false;
    run_steps(g, t, 150);
    CHECK(g.agents[1].blind > 0.0 || has_event(g, EventType::EXPLOSION));
    // fumul blocheaza linia de vedere
    SmokeCloud sc;
    sc.x = 7.0; sc.y = 5.0; sc.age = 5.0; sc.radius = 3.0;
    g.smokes.push_back(sc);
    CHECK(g.smoke_blocks(3.5, 5.0, 10.0, 5.0));
    CHECK(!g.smoke_blocks(3.5, 1.5, 10.0, 1.5));
    // grenada de fragmentare tinuta prea mult explodeaza in mana
    World h = make_world_with_player(open_room(10, 8), 4.5, 4.0, true);
    h.emit_fx_events = true;
    h.agents[0].health.init(h.health_cfg);
    AgentControl hold;
    hold.grenade_hold = true;
    bool boom = false;
    for (int i = 0; i < 240 && !boom; ++i) { h.step(&hold); boom = has_event(h, EventType::EXPLOSION); }
    CHECK(boom);
    CHECK(h.agents[0].hp < 100.0);
    // corp la corp: cutitul raneste tinta din fata
    World m = make_world_with_player(open_room(10, 8), 4.0, 4.0, true);
    m.emit_fx_events = true;
    m.agents[0].inv.melee = MeleeKind::Knife;
    Agent tgt = make_bot(1, 4.9, 4.0);
    tgt.health.init(m.health_cfg);
    m.agents.push_back(tgt);
    AgentControl k;
    k.look_yaw = 0.0;
    k.melee = true;
    m.step(&k);
    CHECK(has_event(m, EventType::MELEE));
    CHECK(m.agents[1].hp < 100.0);
}

static void test_tablet_seen_by_others() {
    // TEST 10 (fara retea): un coechipier cu tableta in maini e desenat tinand-o
    World w = tablet_world(false);
    Agent mate = make_bot(1, 6.0, 5.0);
    mate.faction = Faction::Blue;
    mate.tablet = 1.0;
    w.agents.push_back(mate);
    Skeleton sk = pose_skeleton(w, w.agents[1], nullptr);
    CHECK(sk.tablet > 0.9);
    CHECK(std::hypot(sk.j[J_LHa].x - sk.tab_c.x, sk.j[J_LHa].y - sk.tab_c.y) < 0.2);   // mainile pe marginile tabletei
    CHECK(sk.gun_dir.z < -0.5);                                                         // arma atarna pe curea
}


// Respiratia sintetizata: respiratii distincte (liniste intre ele), mai puternica la efort,
// spectru de "rasuflare" (nu sasait), fiecare respiratie putin diferita.
static void test_breath_synth() {
    const float sr = 48000.0f;
    auto run = [&](double rate_bpm, double depth, float intensity, float exhaustion, double secs,
                   std::vector<float>& outv, std::vector<float>& flowv) {
        BreathSynth b;
        b.init(sr);
        Physiology ph;
        BreathVoiceParams p;
        p.enabled = true; p.intensity = intensity; p.exhaustion = exhaustion; p.gain = 0.06f + 0.22f * intensity;
        int n = static_cast<int>(secs * sr);
        outv.assign(n, 0.0f);
        flowv.assign(n, 0.0f);
        double phase = 0.0, prev = 0.0;
        for (int i = 0; i < n; ++i) {
            if (i % 800 == 0) {   // 60 Hz, ca simularea
                phase += rate_bpm / 60.0 / 60.0;
                double sig = breath_wave(phase, intensity < 0.5f ? 0.25 : 0.04) * depth;
                p.flow = static_cast<float>((sig - prev) * 60.0);
                prev = sig;
                b.set(p);
            }
            outv[i] = b.sample();
            flowv[i] = p.flow;
        }
        return b.breaths();
    };
    std::vector<float> rest, heavy, fr, fh;
    int nb_rest = run(13.0, 0.25, 0.12f, 0.0f, 20.0, rest, fr);
    int nb_heavy = run(40.0, 0.9, 0.9f, 0.7f, 20.0, heavy, fh);
    CHECK(nb_rest >= 3 && nb_rest <= 6);          // ~13/min * 20 s = 4 respiratii
    CHECK(nb_heavy >= 11 && nb_heavy <= 16);      // ~40/min * 20 s = 13
    auto rms = [](const std::vector<float>& v, size_t a, size_t b) {
        double s = 0; for (size_t i = a; i < b; ++i) s += v[i] * v[i]; return std::sqrt(s / std::max<size_t>(1, b - a));
    };
    double r_rest = rms(rest, 48000, rest.size()), r_heavy = rms(heavy, 48000, heavy.size());
    std::printf("  [respiratie] RMS repaus %.4f  efort %.4f  raport %.1f\n", r_rest, r_heavy, r_heavy / std::max(1e-9, r_rest));
    CHECK(r_heavy > r_rest * 4.0);                 // efortul se aude clar, repausul abia
    CHECK(r_heavy < 0.35);                         // fara saturare
    // liniste in pauze: ferestrele cu debit mic sunt mult mai incete decat varfurile
    double loud = 0, quiet = 0; int nl = 0, nq = 0;
    for (size_t i = 48000; i + 2400 < heavy.size(); i += 2400) {
        double f = std::abs(fh[i + 1200]);
        double r = rms(heavy, i, i + 2400);
        if (f > 2.5) { loud += r; nl++; } else if (f < 0.4) { quiet += r; nq++; }
    }
    std::printf("  [respiratie] ferestre varf %d pauza %d\n", nl, nq);
    if (nl && nq) {
        std::printf("  [respiratie] varf %.4f  pauza %.4f\n", loud / nl, quiet / nq);
        CHECK(quiet / nq < loud / nl * 0.3);
    }
    // centrul spectral (treceri prin zero): "rasuflare" intre ~600 Hz si ~3 kHz, nu sasait
    int zc = 0;
    for (size_t i = 48001; i < heavy.size(); ++i) if ((heavy[i - 1] < 0) != (heavy[i] < 0)) zc++;
    double centroid = zc / 2.0 / ((heavy.size() - 48000) / sr);
    std::printf("  [respiratie] frecventa medie %.0f Hz\n", centroid);
    CHECK(centroid > 500.0 && centroid < 3200.0);
}

// ============================================================================
// Simulare si modelare: harti, poligon, statistici, analiza, experimente
// ============================================================================
static std::string temp_dir(const char* name) {
    std::filesystem::path p = std::filesystem::temp_directory_path() / name;
    std::error_code ec;
    std::filesystem::remove_all(p, ec);
    return p.string();
}

static void test_map_catalog() {
    const std::string root = find_asset_root();
    MapCatalog cat;
    CHECK(cat.load(root));
    CHECK(cat.maps.size() >= 5);
    std::set<int> ids;
    for (const auto& m : cat.maps) {
        CHECK(ids.insert(m.id).second);              // MapId unic
        CHECK(m.walkable > 0 && m.spawn_count >= 1);
        if (!m.training()) CHECK(m.enemy_count >= 3 && m.eng_mean > 0.0);
    }
    const MapInfo* ind = cat.by_key("industrial");
    const MapInfo* res = cat.by_key("residential");
    const MapInfo* tac = cat.by_key("tactical");
    const MapInfo* tr = cat.by_key("training");
    CHECK(ind && res && tac && tr && cat.by_key("urban"));
    if (ind && res) {
        std::printf("  [harti] distanta posibila medie: industrial %.1f m, rezidential %.1f m, tactic %.1f m\n", ind->eng_mean,
                    res->eng_mean, tac ? tac->eng_mean : 0.0);
        CHECK(ind->eng_mean > res->eng_mean);        // hale / curti: linii de vedere mai lungi decat in case
    }
    if (tac) {
        CHECK(tac->objective_count >= 4);
        for (const auto& m : cat.maps) if (m.id != tac->id) CHECK(tac->walkable > m.walkable);
    }
    if (tr) CHECK(tr->training() && !tr->range_config.empty());
    // fiecare harta porneste o runda: jucator, inamicii din harta, poligon doar pe antrenament
    for (const auto& m : cat.maps) {
        RoundConfig cfg;
        cfg.asset_root = root;
        apply_map_info(cfg, m);
        World w(5);
        setup_round(w, cfg, nullptr);
        CHECK(!w.agents.empty() && w.agents[0].is_human);
        int red = 0;
        for (const auto& a : w.agents) red += a.faction == Faction::Red;
        if (m.training()) { CHECK(w.range.enabled && w.range.targets.size() == 24 && red == 0); CHECK(!w.game.auto_round_end); }
        else { CHECK(!w.range.enabled && red == m.enemy_count); }
        run_steps(w, AgentControl{}, 20);
        CHECK(w.agents[0].alive());
    }
}

static World range_world(uint64_t seed) {
    const std::string root = find_asset_root();
    MapCatalog cat;
    cat.load(root, false);
    RoundConfig cfg;
    cfg.asset_root = root;
    apply_map_info(cfg, *cat.by_key("training"));
    World w(seed);
    setup_round(w, cfg, nullptr);
    w.emit_fx_events = false;
    return w;
}

static void test_range_target_zones() {
    World w = range_world(9);
    Agent& a = w.agents[0];
    int id = w.range.show_only(1, 0, 1);              // culoarul 1, 10 m, marimea M
    CHECK(id >= 0);
    if (id < 0) return;
    const RangeTarget& t = w.range.targets[id];
    a.x = w.range.cfg.firing_line_x - 1.0;
    a.y = t.y;
    a.facing = 0.0;
    auto shoot_at = [&](double z, double dyaw) {
        AgentControl c;
        c.aim = true;
        int zone = -1;
        for (int i = 0; i < 110; ++i) {
            double eye = w.eye_z(a) - 0.1;
            c.look_yaw = std::atan2(t.y - a.y, t.x - a.x) + dyaw;
            c.look_pitch = std::atan2(z - eye, t.x - a.x);
            c.fire = i == 100;
            w.step(&c);
            for (const auto& e : w.event_log.get_all()) if (e.type == EventType::TARGET_HIT) zone = e.v1 < 0.5 ? 0 : 1;
            w.event_log.clear();
        }
        return zone;
    };
    const RangeConfig& rc = w.range.cfg;
    int zh = shoot_at(rc.head_z, 0.0);
    int zb = shoot_at(0.5 * (rc.body_z0 + rc.body_z1), 0.0);
    int zm = shoot_at(1.2, 0.15);                      // 1.5 m lateral la 10 m: ratat
    std::printf("  [poligon] cap -> %d, corp -> %d, lateral -> %d, urme %zu\n", zh, zb, zm, t.marks.size());
    CHECK(zh == 0);
    CHECK(zb == 1);
    CHECK(zm == -1);
    CHECK(t.marks.size() == 2);
}

static void test_range_reaction_session() {
    World w = range_world(11);
    w.range.cfg.reaction_trials = 3;
    w.range.cfg.reaction_delay_min = 0.6;
    w.range.cfg.reaction_delay_max = 1.0;
    w.range.cfg.countdown = 0.5;
    Agent& a = w.agents[0];
    a.x = w.range.cfg.firing_line_x - 1.0;
    a.y = w.range.cfg.lane_y[2];
    AgentControl start;
    start.range_mode = static_cast<int>(RangeMode::Reaction);
    start.range_dist = 1;                              // 25 m
    w.step(&start);
    CHECK(w.range.session_active());
    const double delay = 0.30;                         // "timpul de reactie" al pilotului automat
    for (int i = 0; i < 60 * 40 && w.range.result_seq == 0; ++i) {
        AgentControl c;
        c.aim = true;
        c.look_yaw = a.facing;
        for (const auto& t : w.range.targets) {
            if (!t.want_up || !t.hittable()) continue;
            double eye = w.eye_z(a) - 0.1;
            c.look_yaw = std::atan2(t.y - a.y, t.x - a.x);
            c.look_pitch = std::atan2(1.16 - eye, std::hypot(t.x - a.x, t.y - a.y));
            c.fire = t.appear_time >= 0.0 && w.current_time - t.appear_time >= delay;
        }
        w.step(&c);
        w.event_log.clear();
    }
    CHECK(w.range.result_seq == 1);
    const RangeResult& r = w.range.result;
    std::printf("  [reactie] %zu probe, lovite %d/%d, reactii:", r.reactions.size(), r.hits, r.shots);
    for (double x : r.reactions) std::printf(" %.3f", x);
    std::printf("\n");
    CHECK(r.reactions.size() == 3);
    for (double x : r.reactions) CHECK(x >= delay - 0.02 && x < delay + 0.1);
    CHECK(r.hits >= 1);
}

static void test_statistics_match_pipeline() {
    const std::string dir = temp_dir("bodycam_test_stats");
    Map m = open_room(40, 12);
    World w = make_world_with_player(m, 5.5, 6.0, true);
    w.weapons[0].projectile = true;                    // focurile se decid abia cand glontul nu mai zboara
    w.agents.push_back(make_bot(1, 26.5, 6.0));
    w.game.auto_round_end = true;
    StatisticsSystem S;
    S.init(dir, find_asset_root() + "/config/stats.ini");
    S.begin_match(1, "TEST", GameMode::Elimination);
    S.begin_round(w, 0);
    bool ended = false;
    int hit_events = 0;
    for (int i = 0; i < 60 * 30 && !ended; ++i) {
        const Agent& a = w.agents[0];
        const Agent& b = w.agents[1];
        AgentControl c;
        c.aim = true;
        double eye = w.eye_z(a) - 0.1;
        c.look_yaw = std::atan2(b.y - a.y, b.x - a.x) + (i % 120 < 60 ? 0.0 : 0.03);   // si cateva focuri ratate
        c.look_pitch = std::atan2(1.2 - eye, std::hypot(b.x - a.x, b.y - a.y));
        c.fire = i > 60 && i % 20 == 0 && b.alive();
        w.step(&c);
        std::vector<Event> evs = w.event_log.get_all();
        w.event_log.clear();
        for (const auto& e : evs) {
            if (e.type == EventType::HIT && e.a == 0) hit_events++;
            if (e.type == EventType::ROUND_END) ended = true;
        }
        S.on_step(w, evs, 0);
    }
    for (int i = 0; i < 30; ++i) { w.step(nullptr); std::vector<Event> evs = w.event_log.get_all(); w.event_log.clear(); S.on_step(w, evs, 0); }
    CHECK(ended);
    S.end_match(w, 0, "W");
    CHECK(S.matches.size() == 1);
    if (S.matches.empty()) return;
    const MatchRecord r = S.matches.back();
    std::printf("  [statistici] focuri %d, lovite %d (HIT %d), ucideri %d, puls %.0f/%.0f/%.0f, esantioane %zu\n", r.shots, r.hits,
                hit_events, r.kills, r.hr_min, r.hr_avg, r.hr_max, S.samples_of(r.id).size());
    CHECK(r.kills == 1 && r.deaths == 0);
    CHECK(r.rounds == 1 && r.rounds_won == 1 && r.result == "W");
    CHECK(r.hits >= 1 && r.shots > r.hits);            // au existat si focuri ratate
    CHECK(r.hits == hit_events);                       // fiecare HIT al jucatorului apartine unui foc
    int recs = 0, rec_hits = 0, kill_shots = 0;
    for (const auto& s : S.shots) if (s.match == r.id) { recs++; rec_hits += s.hit; kill_shots += s.kill; }
    CHECK(recs == r.shots && rec_hits == r.hits && kill_shots == 1);
    CHECK(r.hr_min > 40.0 && r.hr_min <= r.hr_avg && r.hr_avg <= r.hr_max);
    CHECK(S.samples_of(r.id).size() >= static_cast<size_t>(r.duration / S.cfg.sample_interval) - 2);
    CHECK(r.engagements >= 1 && r.eng_dist_avg > 15.0 && r.eng_dist_avg < 25.0);
    CHECK(r.score > 0);
    // persistenta: o instanta noua citeste aceleasi date din fisiere
    StatisticsSystem S2;
    S2.init(dir, find_asset_root() + "/config/stats.ini");
    CHECK(S2.matches.size() == 1 && S2.matches[0].kills == 1 && S2.matches[0].shots == r.shots && S2.matches[0].result == "W");
    CHECK(S2.shots.size() == static_cast<size_t>(r.shots));
    CHECK(S2.samples_of(r.id).size() == S.samples_of(r.id).size());
    CHECK(S2.rounds.size() == 1 && S2.engagements.size() == static_cast<size_t>(r.engagements));
    CHECK(S2.next_match_id() == r.id + 1);
    OverallStats o = overall(S2.matches);
    CHECK(o.matches == 1 && o.wins == 1 && std::abs(o.win_rate - 1.0) < 1e-9);
}

static void test_analytics_functions() {
    Regression r = regress({1, 2, 3, 4, 5}, {2, 4, 6, 8, 10});
    CHECK(r.valid && std::abs(r.r - 1.0) < 1e-9 && std::abs(r.slope - 2.0) < 1e-9 && r.p < 1e-6);
    Regression r0 = regress({1, 2, 3, 4, 5, 6, 7, 8}, {3, 1, 4, 1, 5, 2, 2, 3});
    CHECK(r0.valid && r0.p > 0.05);
    CHECK(std::abs(student_p_two_sided(2.228, 10) - 0.05) < 0.002);
    Proportion pw = wilson(5, 10);
    CHECK(std::abs(pw.lo - 0.2366) < 0.002 && std::abs(pw.hi - 0.7634) < 0.002);
    std::vector<MatchRecord> ms(6);
    const char* res[6] = {"W", "L", "W", "W", "D", "A"};
    const int map[6] = {1, 1, 2, 2, 2, 1};
    for (int i = 0; i < 6; ++i) { ms[i].id = i + 1; ms[i].result = res[i]; ms[i].map_id = map[i]; ms[i].kills = i; ms[i].deaths = 1; ms[i].shots = 10; ms[i].hits = 4; }
    auto pm = per_map(ms);
    CHECK(pm.size() == 2);
    for (const auto& x : pm) {
        if (x.map_id == 1) CHECK(x.matches == 2 && std::abs(x.win_rate - 0.5) < 1e-9);
        if (x.map_id == 2) CHECK(x.matches == 3 && std::abs(x.win_rate - 2.0 / 3.0) < 1e-9 && std::abs(x.accuracy - 0.4) < 1e-9);
    }
    OverallStats o = overall(ms);
    CHECK(o.matches == 5 && o.abandoned == 1 && o.wins == 3 && o.losses == 1 && o.draws == 1);
    CHECK(o.best_win_streak == 2 && o.worst_loss_streak == 1 && o.streak == 0);
    CHECK(std::abs(o.kills_w - (0 + 2 + 3) / 3.0) < 1e-9 && std::abs(o.kills_l - 1.0) < 1e-9);
    auto bins = bin_by({0.1, 0.1, 0.9, 0.9}, {1, 1, 0, 1}, {0.0, 0.5, 1.0}, true);
    CHECK(bins.size() == 2 && bins[0].prop.k == 2 && bins[1].prop.n == 2 && bins[1].prop.k == 1);
    std::vector<PhysioSample> smp;
    for (int i = 0; i <= 60; ++i) { PhysioSample s; s.t = i; s.hr = i <= 10 ? 100 + 5 * i : 150 - (i - 10); smp.push_back(s); }
    HrRecovery hrr = hr_recovery(smp, 30.0, 105.0);
    CHECK(hrr.peaks == 1 && std::abs(hrr.drop_avg - 30.0) < 1e-9);
    Paired pc = paired_compare({80, 70, 90, 60}, {60, 55, 75, 50});
    CHECK(pc.n == 4 && pc.mean_diff < 0.0 && pc.p < 0.05);
}

static void test_storage_csv_migration() {
    DataStorage st;
    st.dir = temp_dir("bodycam_test_csv");
    CHECK(st.ensure_dir());
    { std::ofstream f(st.path("matches.csv")); f << "id,map_id,result\n5,2,W\n"; }   // fisier vechi, cu mai putine coloane
    std::vector<MatchRecord> v(1);
    v[0].id = 6; v[0].map_id = 3; v[0].result = "L"; v[0].kills = 4; v[0].map_name = "A,B";
    CHECK(st.append("matches.csv", v));
    auto all = st.load<MatchRecord>("matches.csv");
    CHECK(all.size() == 2);
    if (all.size() == 2) {
        CHECK(all[0].id == 5 && all[0].map_id == 2 && all[0].result == "W" && all[0].kills == 0);
        CHECK(all[1].id == 6 && all[1].kills == 4 && all[1].map_name == "A B");
    }
    std::ifstream in(st.path("matches.csv"));
    std::string hdr;
    std::getline(in, hdr);
    CHECK(hdr == csvio::header<MatchRecord>());
    st.read_only = true;
    CHECK(st.append("matches.csv", v));
    CHECK(st.load<MatchRecord>("matches.csv").size() == 2);   // nimic scris in modul doar-citire
}

static void test_fatigue_and_experiments() {
    PhysiologyConfig cfg;
    Physiology p;
    PhysiologyInputs in;
    in.sprinting = true; in.speed = 6.0; in.stamina = 0.5;
    for (int i = 0; i < 60 * 60; ++i) update_physiology(p, in, cfg, 1.0 / 60.0, 0);
    double f1 = p.fatigue;
    in.sprinting = false; in.speed = 0.0; in.stamina = 1.0;
    for (int i = 0; i < 60 * 120; ++i) update_physiology(p, in, cfg, 1.0 / 60.0, 0);
    std::printf("  [oboseala] dupa 60 s sprint %.3f, dupa 120 s repaus %.3f\n", f1, p.fatigue);
    CHECK(f1 > 0.12);
    CHECK(p.fatigue < f1 * 0.8);
    Physiology a, b;
    b.fatigue = 0.9;
    PhysiologyInputs rest;
    update_physiology(a, rest, cfg, 1.0 / 60.0, 0);
    update_physiology(b, rest, cfg, 1.0 / 60.0, 0);
    CHECK(b.fine_motor < a.fine_motor);
    // experimentele controlate: acelasi seed (CRN), doar factorul difera
    const std::string root = find_asset_root();
    auto defs = load_experiment_defs(root + "/config/experiments.ini");
    CHECK(defs.size() >= 6);
    const ExperimentDef* fat = nullptr;
    const ExperimentDef* hr = nullptr;
    for (const auto& d : defs) { if (d.key == "oboseala_reincarcare") fat = &d; if (d.key == "puls_stabilitate") hr = &d; }
    CHECK(fat && hr);
    if (fat) {
        ExperimentTrial t0 = run_trial(*fat, 0.0, 0, 77, root), t1 = run_trial(*fat, 1.0, 0, 77, root);
        std::printf("  [experiment] reincarcare: oboseala 0 -> %.2f s, oboseala 1 -> %.2f s\n", t0.reload_mean, t1.reload_mean);
        CHECK(t0.reload_mean > 0.5 && t1.reload_mean > t0.reload_mean * 1.1);
    }
    if (hr) {
        double s0 = 0.0, s1 = 0.0;
        for (int r = 0; r < 2; ++r) { s0 += run_trial(*hr, 70.0, r, 100 + r, root).sway_rms; s1 += run_trial(*hr, 175.0, r, 100 + r, root).sway_rms; }
        std::printf("  [experiment] instabilitate: puls 70 -> %.2f mrad, puls 175 -> %.2f mrad\n", s0 * 500.0, s1 * 500.0);
        CHECK(s1 > s0 * 1.1);
    }
}

// pilot automat pentru sesiunile de poligon: ochire spre tinta ridicata cea mai apropiata, foc rar
static AgentControl range_pilot(const World& w, const Agent& a, int i) {
    AgentControl c;
    c.aim = true;
    c.look_yaw = 0.0;
    double best = 1e9;
    for (const auto& t : w.range.targets) {
        if (!t.want_up || !t.hittable()) continue;
        double d = std::hypot(t.x - a.x, t.y - a.y);
        if (d >= best) continue;
        best = d;
        double eye = w.eye_z(a) - 0.1;
        c.look_yaw = std::atan2(t.y - a.y, t.x - a.x);
        c.look_pitch = std::atan2(1.16 * t.size - eye, d);
    }
    c.fire = best < 1e8 && i % 20 == 0;
    const WeaponSlot* sl = a.cur_slot();
    if (sl && sl->mag == 0 && a.reload_stage == 0) c.reload = true;
    return c;
}

static void test_range_stress_and_speed() {
    // STRES: tragere in repaus -> sprint real (puls + stamina) -> tragere dupa sprint
    {
        World w = range_world(13);
        w.range.cfg.countdown = 0.5;
        w.range.cfg.stress_shots = 6;
        Agent& a = w.agents[0];
        const double sx = w.range.cfg.firing_line_x - 1.0, sy = w.range.cfg.lane_y[1];
        a.x = sx; a.y = sy;
        AgentControl start;
        start.range_mode = static_cast<int>(RangeMode::Stress);
        start.range_dist = 1;
        w.step(&start);
        double sprint_t = 0.0;
        for (int i = 0; i < 60 * 180 && w.range.result_seq == 0; ++i) {
            AgentControl c;
            if (w.range.phase == RangePhase::Sprint) {
                // alearga pe culoar si inapoi (directia se schimba la 4 s)
                sprint_t += w.dt;
                c.sprint = true;
                c.moveForward = 1.0;
                c.look_yaw = std::fmod(sprint_t, 8.0) < 4.0 ? 0.0 : 3.14159265;
            } else if (w.range.phase == RangePhase::PostShoot || w.range.phase == RangePhase::RestShoot) {
                c = range_pilot(w, a, i);
            }
            w.step(&c);
            w.event_log.clear();
        }
        std::printf("  [stres] faza %s, puls %.0f, stamina %.2f, exhausted %d\n", range_phase_name(w.range.phase),
                    a.phys.heart_rate, a.stamina, a.exhausted ? 1 : 0);
        CHECK(w.range.result_seq == 1);
        const RangeResult& r = w.range.result;
        std::printf("  [stres] repaus %d/%d puls %.0f | dupa sprint (%.1f s) %d/%d puls %.0f\n", r.hits_rest, r.shots_rest, r.hr_rest,
                    sprint_t, r.hits_post, r.shots_post, r.hr_post);
        CHECK(r.shots_rest == 6 && r.shots_post == 6);
        CHECK(r.hr_post > r.hr_rest + 30.0);           // pulsul masurat dupa sprint, din modelul fiziologic
        CHECK(sprint_t > 3.0);
    }
    // VITEZA: sase tinte ridicate simultan, cronometrul se opreste la ultima doborata
    {
        World w = range_world(17);
        w.range.cfg.countdown = 0.5;
        Agent& a = w.agents[0];
        a.x = w.range.cfg.firing_line_x - 1.0;
        a.y = w.range.cfg.lane_y[2];
        AgentControl start;
        start.range_mode = static_cast<int>(RangeMode::Speed);
        w.step(&start);
        for (int i = 0; i < 60 * 120 && w.range.result_seq == 0; ++i) {
            AgentControl c = range_pilot(w, a, i);
            w.step(&c);
            w.event_log.clear();
        }
        CHECK(w.range.result_seq == 1);
        const RangeResult& r = w.range.result;
        std::printf("  [viteza] %zu tinte in %.2f s, %d focuri, %d lovite\n", r.splits.size(), r.total_time, r.shots, r.hits);
        CHECK(static_cast<int>(r.splits.size()) == w.range.cfg.speed_targets);
        CHECK(r.total_time > 0.0 && r.hits >= w.range.cfg.speed_targets && r.shots >= r.hits);
        for (size_t k = 1; k < r.splits.size(); ++k) CHECK(r.splits[k] >= r.splits[k - 1]);
    }
}

// ============================================================================
// Arme: atasamente pe locasuri, calculul centralizat, profiluri de recul, moduri de foc
// ============================================================================
static void test_weapon_attachments_data() {
    const std::string root = find_asset_root();
    auto db = load_attachment_db(root + "/config/attachments.ini");
    CHECK(db.size() >= 16);
    WeaponData raw;
    CHECK(raw.load(root + "/weapons/mk18.ini", nullptr, false));
    auto variant = [&](std::vector<std::string> atts) { WeaponData v = raw; v.attachments = atts; v.apply_attachments(&db); return v; };
    WeaponData bare = variant({});
    WeaponData sup = variant({"suppressor"});
    CHECK(sup.has_suppressor && sup.sound_radius < bare.sound_radius * 0.5 && sup.length > bare.length);
    WeaponData two = variant({"suppressor", "compensator"});        // acelasi locas: doar primul
    CHECK(two.mounted.size() == 1 && two.has_suppressor && !two.has_compensator);
    WeaponData drum = variant({"drum"});
    CHECK(drum.has_drum && drum.mag_size == bare.mag_size + 30 && drum.ads_time > bare.ads_time && drum.movement_penalty < 1.0);
    WeaponData scope = variant({"scope4x"});
    CHECK(scope.has_scope && std::abs(scope.magnification - 4.0) < 1e-9 && scope.sight_height > 0.03 && scope.ads_time > bare.ads_time);
    WeaponData lb = variant({"longbarrel"});
    CHECK(lb.has_long_barrel && lb.muzzle_velocity > bare.muzzle_velocity && lb.spread_ads < bare.spread_ads);
    CHECK(lb.sockets.muzzle.z > bare.sockets.muzzle.z);              // teava lunga muta gura tevii
    WeaponData vg = variant({"foregrip"}), ag = variant({"angledgrip"});
    CHECK(vg.recoil_pitch < bare.recoil_pitch && vg.sockets.support.y > bare.sockets.support.y);   // mana stanga coboara pe maner
    CHECK(ag.sockets.support.z < bare.sockets.support.z);             // manerul inclinat: mana mai in spate
    WeaponData light_laser = variant({"light", "laser"});              // locasuri diferite: ambele
    CHECK(light_laser.has_light && light_laser.has_laser && light_laser.mounted.size() == 2);
    WeaponData pistol;
    CHECK(pistol.load(root + "/weapons/g17.ini", nullptr, false));
    pistol.attachments = {"drum", "suppressor"};
    pistol.apply_attachments(&db);
    CHECK(!pistol.has_drum && pistol.has_suppressor);                  // toba nu e compatibila cu pistolul
    WeaponData sniper;
    CHECK(sniper.load(root + "/weapons/m24.ini", &db, true));
    CHECK(sniper.model == WeaponModel::Sniper && sniper.has_scope && sniper.has_mode(FireMode::Bolt) && sniper.bolt_time > 0.5);
}

static void test_weapon_stats_model() {
    HandlingConfig h;
    h.load(find_asset_root() + "/config/handling.ini");
    WeaponData w = make_default_rifle();
    WeaponSlot sl;
    Agent a;
    a.stamina = 1.0;
    auto spread = [&](Agent x) { return compute_spread(w, x, sl, h).total; };
    double stand = spread(a);
    Agent c = a; c.crouch = 1.0;
    Agent p = a; p.prone = 1.0;
    Agent m = a; m.speed = 3.0;
    Agent ads = a; ads.aim = 1.0;
    Agent st = a; st.phys.stress = 1.0;
    Agent ft = a; ft.phys.fatigue = 1.0; ft.stamina = 0.2;
    Agent arm = a; arm.health.zone[static_cast<int>(BodyZone::RightArm)] = 15.0;
    std::printf("  [dispersie] picioare %.4f ghemuit %.4f culcat %.4f miscare %.4f ochire %.4f stres %.4f oboseala %.4f brat %.4f\n",
                stand, spread(c), spread(p), spread(m), spread(ads), spread(st), spread(ft), spread(arm));
    CHECK(spread(p) < spread(c) && spread(c) < stand);
    CHECK(spread(m) > stand && spread(ads) < stand * 0.5);
    CHECK(spread(st) > stand && spread(ft) > stand && spread(arm) > stand);
    // recul: seria de foc creste, primul foc separat
    RecoilBreakdown r0 = compute_recoil(w, a, sl, 0, h), r5 = compute_recoil(w, a, sl, 5, h);
    CHECK(r5.total > r0.total);
    RecoilBreakdown rp = compute_recoil(w, p, sl, 0, h);
    CHECK(rp.total < r0.total);
    // revenirea: obosit / ranit mai lenta
    CHECK(compute_recovery(w, ft, h) < compute_recovery(w, a, h));
    CHECK(compute_recovery(w, arm, h) < compute_recovery(w, a, h));
}

// arma din lume dupa id, cu atasamentele din fisier
static World armed_world(const std::string& weapon_id) {
    World w = range_world(21);
    Agent& a = w.agents[0];
    int idx = w.weapon_index(weapon_id);
    if (idx >= 0) w.give_loadout(a, {idx});
    a.x = w.range.cfg.firing_line_x - 1.0;
    a.y = w.range.cfg.lane_y[1];
    a.facing = 0.0;
    return w;
}

static void test_weapon_recoil_profiles() {
    // acelasi tragator, cinci focuri rare: varful reculului si cat de repede revine arma
    struct R { const char* id; double peak = 0.0, after = 0.0; };
    R rs[3] = {{"g17"}, {"mk18"}, {"m24"}};
    for (auto& r : rs) {
        World w = armed_world(r.id);
        Agent& a = w.agents[0];
        AgentControl c;
        c.aim = true;
        for (int i = 0; i < 60; ++i) w.step(&c);
        c.fire = true;
        w.step(&c);
        c.fire = false;
        double peak = 0.0;
        for (int i = 0; i < 6; ++i) { w.step(&c); peak = std::max(peak, a.kick_pitch); }
        for (int i = 0; i < 18; ++i) w.step(&c);      // 0.3 s dupa foc
        r.peak = peak;
        r.after = a.kick_pitch / std::max(1e-9, peak);
        w.event_log.clear();
    }
    std::printf("  [recul] varf: pistol %.4f  carabina %.4f  luneta %.4f | ramas dupa 0.3 s: %.2f  %.2f  %.2f\n",
                rs[0].peak, rs[1].peak, rs[2].peak, rs[0].after, rs[1].after, rs[2].after);
    CHECK(rs[2].peak > rs[1].peak && rs[2].peak > rs[0].peak);       // pusca cu luneta: lovitura cea mai mare
    CHECK(rs[0].after < rs[1].after && rs[1].after < rs[2].after);    // pistolul revine cel mai repede, luneta cel mai lent
}

static void test_weapon_fire_modes() {
    auto count_shots = [](World& w, const std::vector<int>& fire_frames, int frames) {
        int shots = 0;
        AgentControl c;
        c.aim = true;
        for (int i = 0; i < frames; ++i) {
            c.fire = std::find(fire_frames.begin(), fire_frames.end(), i) != fire_frames.end() || (fire_frames.size() == 1 && fire_frames[0] < 0);
            w.step(&c);
            for (const auto& e : w.event_log.get_all()) if (e.type == EventType::SHOT && e.a == 0) shots++;
            w.event_log.clear();
        }
        return shots;
    };
    // automat: tinut 1 s -> multe focuri; semi: tinut 1 s -> un foc
    {
        World w = armed_world("mk18");
        std::vector<int> hold;
        for (int i = 10; i < 70; ++i) hold.push_back(i);
        int automatic = count_shots(w, hold, 80);
        World w2 = armed_world("mk18");
        AgentControl sel;
        sel.cycle_fire_mode = true;
        w2.step(&sel);                                   // AUTOMAT -> SEMI
        CHECK(w2.agents[0].cur_slot()->fire_mode == 1);
        int semi = count_shots(w2, hold, 80);
        std::printf("  [moduri] automat tinut 1 s: %d focuri, semi tinut 1 s: %d\n", automatic, semi);
        CHECK(automatic >= 9 && semi == 1);
    }
    // rafala: o apasare scurta -> exact burst_count focuri
    {
        World w = armed_world("mp5");
        Agent& a = w.agents[0];
        WeaponData& wd = w.weapons[a.cur_slot()->weapon];
        int bi = -1;
        for (size_t i = 0; i < wd.fire_modes.size(); ++i) if (wd.fire_modes[i] == FireMode::Burst) bi = static_cast<int>(i);
        if (bi < 0) { wd.fire_modes.push_back(FireMode::Burst); bi = static_cast<int>(wd.fire_modes.size()) - 1; }
        a.cur_slot()->fire_mode = bi;
        int burst = count_shots(w, {10, 11}, 60);
        std::printf("  [moduri] rafala (o apasare): %d focuri\n", burst);
        CHECK(burst == wd.burst_count);
    }
    // repetitie: al doilea foc doar dupa manevrarea inchizatorului
    {
        World w = armed_world("m24");
        Agent& a = w.agents[0];
        const WeaponData& wd = w.weapons[a.cur_slot()->weapon];
        std::vector<int> presses;
        for (int i = 10; i < 200; i += 8) presses.push_back(i);   // apasari rapide, de doua ori pe sfert de secunda
        int shots = count_shots(w, presses, 200);
        double window = (200 - 10) / 60.0;
        std::printf("  [moduri] repetitie: %d focuri in %.1f s (inchizator %.2f s)\n", shots, window, wd.bolt_time);
        CHECK(shots >= 2 && shots <= static_cast<int>(window / wd.bolt_time) + 1);
        CHECK(a.cur_slot()->chambered || a.bolt_timer > 0.0);
    }
}

static void test_bots_share_weapon_system() {
    struct Out { int shots = 0, hits = 0, reload_end = 0, bolts = 0, projectiles = 0; int mag_before = 0, mag_min = 999; std::string weapon; };
    auto run = [](const char* profile, int seconds) {
        Out o;
        World w = range_world(31);
        CHECK(w.bots_ballistic && !w.profiles.empty());
        Agent& me = w.agents[0];
        me.x = 20.5; me.y = 13.5;
        me.invulnerable = true;                          // tinta: nu moare, conteaza doar loviturile
        Agent b;
        b.id = static_cast<int>(w.agents.size());
        b.faction = Faction::Red;
        b.x = 35.5; b.y = 13.5;
        b.facing = 3.14159265;
        b.state = FsmState::Engage;
        b.policy = Policy::Hold;
        b.psi = 1.0;
        b.health.init(w.health_cfg);
        int pi = find_profile(w.profiles, profile);
        CHECK(pi >= 0);
        w.equip_from_profile(b, pi);
        b.acquired_targets.push_back(0);
        b.target_id = 0;
        w.agents.push_back(b);
        const WeaponData* wd = w.weapon_of(w.agents.back());
        CHECK(wd != nullptr);
        if (!wd) return o;
        o.weapon = wd->name;
        o.mag_before = w.agents.back().cur_slot()->mag;
        AgentControl idle;
        idle.look_yaw = 0.0;
        for (int i = 0; i < 60 * seconds; ++i) {
            w.step(&idle);
            Agent& bot = w.agents.back();
            if (bot.cur_slot()) o.mag_min = std::min(o.mag_min, bot.cur_slot()->mag);
            for (const auto& e : w.event_log.get_all()) {
                if (e.a != bot.id) continue;
                if (e.type == EventType::SHOT) { o.shots++; CHECK(e.weapon >= 0 && e.shot >= 0); }
                if (e.type == EventType::HIT) o.hits++;
                if (e.type == EventType::RELOAD_END) o.reload_end++;
                if (e.type == EventType::BOLT_CYCLE && e.v1 > 1.5) o.bolts++;
            }
            for (const auto& pr : w.projectiles) if (pr.owner == bot.id) o.projectiles++;
            w.event_log.clear();
        }
        return o;
    };
    Out r = run("bot_01", 14);
    std::printf("  [boti] %s: %d focuri, %d lovituri (%.0f%%), incarcator %d -> min %d, %d reincarcari, proiectile %d\n",
                r.weapon.c_str(), r.shots, r.hits, r.shots ? 100.0 * r.hits / r.shots : 0.0, r.mag_before, r.mag_min, r.reload_end, r.projectiles);
    CHECK(r.weapon == "MK18");
    CHECK(r.shots >= 20 && r.projectiles > 0);           // gloante reale (proiectile), nu model statistic
    CHECK(r.reload_end >= 1 && r.mag_min <= 1);          // a golit incarcatorul si a reincarcat din rezerva
    CHECK(r.hits >= 3 && r.hits < r.shots);               // loveste, dar nu infailibil
    Out s = run("bot_04", 8);
    std::printf("  [boti] %s: %d focuri, %d manevre de inchizator, %d lovituri\n", s.weapon.c_str(), s.shots, s.bolts, s.hits);
    CHECK(s.weapon == "M24" && s.shots >= 2 && s.bolts >= s.shots - 1);
}

static void test_gadget_config_physics() {
    GadgetConfig gc;
    CHECK(gc.load(find_asset_root() + "/config/gadgets.ini"));
    CHECK(gc.vehicle.cell_count == 2.0 && gc.drone.hp == 30.0 && gc.vehicle.cam_w == 256 && gc.drone.name == "DRONA FPV");
    // camera deschisa 40 x 12 m
    std::vector<std::string> rows;
    rows.push_back(std::string(40, '1'));
    for (int r = 0; r < 10; ++r) rows.push_back("1" + std::string(38, '.') + "1");
    rows.push_back(std::string(40, '1'));
    Map m;
    m.from_rows(rows);
    const double dt = 1.0 / 60.0, kTwoPi = 6.283185307179586;
    auto make_v = [&](const VehicleConfig& c, double x, double y) { Vehicle v; v.cfg = c; v.hp = c.hp; v.x = x; v.y = y; return v; };
    AgentControl gas, brake, none, turn;
    gas.stick_pitch = 1.0;
    brake.stick_pitch = -1.0;
    turn.stick_pitch = 0.6; turn.stick_roll = 1.0;

    // accelerare: botul se ridica; viteza maxima din config; rotile se rotesc cu distanta / raza
    Vehicle v = make_v(gc.vehicle, 2.5, 5.5);
    double max_pitch = 0.0;
    for (int i = 0; i < 20; ++i) { step_vehicle(v, &gas, m, dt); max_pitch = std::max(max_pitch, v.pitch); }
    CHECK(max_pitch > 0.01);
    for (int i = 0; i < 120; ++i) step_vehicle(v, &gas, m, dt);
    CHECK(std::abs(v.speed - gc.vehicle.max_speed) < 0.05);
    double ph0 = v.wheel_phase, od0 = v.odometer;
    for (int i = 0; i < 30; ++i) step_vehicle(v, &gas, m, dt);
    double expect = std::fmod(ph0 + (v.odometer - od0) / gc.vehicle.wheel_radius, kTwoPi);
    double dphi = std::abs(std::remainder(v.wheel_phase - expect, kTwoPi));
    CHECK(dphi < 0.02);
    // franare: botul se lasa
    double min_pitch = 0.0;
    for (int i = 0; i < 20; ++i) { step_vehicle(v, &brake, m, dt); min_pitch = std::min(min_pitch, v.pitch); }
    CHECK(min_pitch < -0.01);
    // viteza maxima e data de fisier, nu de cod
    VehicleConfig slow = gc.vehicle;
    slow.max_speed = 3.0;
    Vehicle s2 = make_v(slow, 2.5, 3.5);
    for (int i = 0; i < 180; ++i) step_vehicle(s2, &gas, m, dt);
    CHECK(s2.speed <= 3.0 + 1e-9 && s2.speed > 2.9);
    // viraj la stanga (giratie pozitiva): caroseria se inclina spre exterior (ruliu negativ)
    Vehicle t = make_v(gc.vehicle, 20.0, 6.0);
    double min_roll = 0.0, yaw0 = t.yaw;
    for (int i = 0; i < 40; ++i) { step_vehicle(t, &turn, m, dt); min_roll = std::min(min_roll, t.roll); }
    CHECK(t.yaw > yaw0 + 0.3 && min_roll < -0.01);
    // bateria: consum la ralanti = idle_drain din consumul maxim (60 s din 5 min -> 1%)
    Vehicle b = make_v(gc.vehicle, 5.5, 8.5);
    for (int i = 0; i < 3600; ++i) step_vehicle(b, &none, m, dt);
    CHECK(std::abs((1.0 - b.battery) - 60.0 / (gc.vehicle.battery_minutes * 60.0) * gc.vehicle.idle_drain) < 1e-3);
    CHECK(std::abs(b.voltage() - gc.vehicle.cell_count * (3.5 + 0.7 * b.battery)) < 1e-9);
    // impact in perete peste prag: structura scade (dar nu e distrusa), botul se infige
    Vehicle c = make_v(gc.vehicle, 28.5, 10.5);
    double hit_pitch = 0.0;
    bool hit = false;
    for (int i = 0; i < 180; ++i) {
        PawnStepResult r = step_vehicle(c, &gas, m, dt);
        if (r.collided) hit = true;
        if (hit) hit_pitch = std::min(hit_pitch, c.pitch);
        if (hit && i > 0 && !r.collided && c.speed < 0.0) break;
    }
    std::printf("  [rc] impact: structura %.0f / %.0f, tangaj minim %.3f rad\n", c.hp, gc.vehicle.hp, hit_pitch);
    CHECK(hit && c.hp < gc.vehicle.hp && c.alive && c.health() < 1.0 && hit_pitch < -0.02);

    // drona: elicele se rotesc la plutire, tensiunea 4S in domeniu; inclinarea maxima din config
    Drone d;
    d.cfg = gc.drone; d.hp = d.cfg.hp;
    d.x = 5.0; d.y = 6.0; d.z = 1.2;
    AgentControl hover;
    for (int i = 0; i < 120; ++i) step_drone(d, &hover, m, 9.81, dt);
    CHECK(d.prop_speed > gc.drone.prop_idle && std::abs(d.z - 1.2) < 0.25);
    CHECK(d.voltage() > 14.0 && d.voltage() <= 16.8 + 1e-9);
    auto fwd_speed = [&](double tilt) {
        Drone q;
        q.cfg = gc.drone; q.cfg.max_tilt = tilt;
        q.x = 3.0; q.y = 6.0; q.z = 1.2;
        AgentControl f;
        f.stick_pitch = 1.0;
        for (int i = 0; i < 60; ++i) step_drone(q, &f, m, 9.81, dt);
        return std::hypot(q.vx, q.vy);
    };
    double v_low = fwd_speed(0.25), v_high = fwd_speed(0.5);
    std::printf("  [drona] viteza dupa 1 s: inclinare 0.25 -> %.2f m/s, 0.5 -> %.2f m/s\n", v_low, v_high);
    CHECK(v_low < v_high * 0.8);
    // autonomia: dublarea minutelor de baterie injumatateste consumul
    auto drain = [&](double minutes) {
        Drone q;
        q.cfg = gc.drone; q.cfg.battery_minutes = minutes;
        q.x = 5.0; q.y = 6.0; q.z = 1.2;
        for (int i = 0; i < 600; ++i) step_drone(q, &hover, m, 9.81, dt);
        return 1.0 - q.battery;
    };
    double d3 = drain(3.0), d6 = drain(6.0);
    CHECK(d6 > 0.0 && std::abs(d3 / d6 - 2.0) < 0.05);
}

static void test_character_hands_on_weapon_sockets() {
    World w = range_world(31);
    auto pose = [&](const char* prof, double height, double& hand_dist, double& socket_dist, V3& lhand_local) {
        Agent b;
        b.id = static_cast<int>(w.agents.size());
        b.faction = Faction::Red;
        b.x = 10.5; b.y = 10.5;
        b.state = FsmState::Engage;
        b.health.init(w.health_cfg);
        int pi = find_profile(w.profiles, prof);
        CHECK(pi >= 0);
        if (pi < 0) return;
        const double h0 = w.profiles[pi].height;
        w.profiles[pi].height = height;
        w.equip_from_profile(b, pi);
        Skeleton s = pose_skeleton(w, b, nullptr);
        w.profiles[pi].height = h0;
        const WeaponData* wd = w.weapon_of(b);
        CHECK(wd != nullptr && s.weapon == wd);
        if (!wd) return;
        V3 g{wd->sockets.grip.x, wd->sockets.grip.y, wd->sockets.grip.z};
        V3 sp{wd->sockets.support.x, wd->sockets.support.y, wd->sockets.support.z};
        hand_dist = len(s.j[J_LHa] - s.j[J_RHa]);
        socket_dist = len(sp - g);
        // mana stanga in sistemul armei (dreapta / sus / inainte), relativ la mana dreapta
        V3 dv = s.j[J_LHa] - s.j[J_RHa];
        V3 up = s.gun_up, fw = s.gun_dir, rt = norm(cross(-up, fw));
        lhand_local = V3{dot(dv, rt), dot(dv, -up), dot(dv, fw)};
    };
    double hd = 0, sd = 0;
    V3 a_rd, a_vg, a_ag, tmp;
    pose("bot_01", 1.0, hd, sd, a_rd);                 // MK18 cu red dot: mana pe garda
    CHECK(std::abs(hd - sd) < 1e-6);
    pose("bot_01", 1.08, hd, sd, tmp);                 // personaj mai inalt: mainile raman pe arma
    CHECK(std::abs(hd - sd) < 1e-6);
    pose("operator", 1.0, hd, sd, a_vg);               // maner vertical
    CHECK(std::abs(hd - sd) < 1e-6);
    pose("operator_2", 0.95, hd, sd, a_ag);            // maner inclinat, personaj mai scund
    CHECK(std::abs(hd - sd) < 1e-6);
    pose("bot_04", 1.0, hd, sd, tmp);                  // M24: alta geometrie
    CHECK(std::abs(hd - sd) < 1e-6);
    std::printf("  [personaje] mana stanga fata de dreapta (y jos, z inainte): garda %.3f/%.3f, maner vertical %.3f/%.3f, inclinat %.3f/%.3f\n",
                a_rd.y, a_rd.z, a_vg.y, a_vg.z, a_ag.y, a_ag.z);
    CHECK(a_vg.y > a_rd.y + 0.03);                     // manerul vertical coboara mana stanga sub teava
    CHECK(std::abs(a_ag.z - a_rd.z) > 0.02);           // manerul inclinat o muta de-a lungul armei
}

static void test_external_assets_license_and_obj() {
    namespace fs = std::filesystem;
    // folder temporar (nu atinge proiectul / data): un cub OBJ + MTL scris de test
    fs::path dir = fs::temp_directory_path() / "bodycam_test_external";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir / "Weapons" / "kit", ec);
    {
        std::ofstream mtl(dir / "Weapons" / "cube.mtl");
        mtl << "newmtl metal_body\nKd 0.2 0.2 0.2\nKs 0.8 0.8 0.8\nNs 300\n";
        std::ofstream obj(dir / "Weapons" / "cube.obj");
        obj << "mtllib cube.mtl\nusemtl metal_body\n";
        obj << "v -1 -1 -1\nv 1 -1 -1\nv 1 1 -1\nv -1 1 -1\nv -1 -1 1\nv 1 -1 1\nv 1 1 1\nv -1 1 1\n";
        obj << "f 1 2 3 4\nf 5 8 7 6\nf 1 5 6 2\nf 2 6 7 3\nf 3 7 8 4\nf 5 1 4 8\n";
    }
    fs::copy_file(dir / "Weapons" / "cube.obj", dir / "Weapons" / "kit" / "a.obj", ec);
    fs::copy_file(dir / "Weapons" / "cube.obj", dir / "Weapons" / "nc.obj", ec);
    fs::copy_file(dir / "Weapons" / "cube.obj", dir / "Weapons" / "noauthor.obj", ec);
    fs::copy_file(dir / "Weapons" / "cube.obj", dir / "Weapons" / "pending.obj", ec);
    fs::copy_file(dir / "Weapons" / "cube.obj", dir / "Weapons" / "unlisted.obj", ec);
    {
        std::ofstream man(dir / "manifest.ini");
        man << "[cube]\nfile = Weapons/cube.obj\nauthor = test\nsource = https://example.org/cube\nlicense = CC0\napproved = yes\n";
        man << "[kit]\nfile = Weapons/kit/\nauthor = Autor\nsource = https://example.org/kit\nlicense = CC-BY-4.0\napproved = yes\n";
        man << "[nc]\nfile = Weapons/nc.obj\nauthor = A\nsource = https://example.org/nc\nlicense = CC-BY-NC-4.0\napproved = yes\n";
        man << "[noauthor]\nfile = Weapons/noauthor.obj\nsource = https://example.org/x\nlicense = CC-BY-4.0\napproved = yes\n";
        man << "[pending]\nfile = Weapons/pending.obj\nauthor = A\nsource = https://example.org/p\nlicense = CC0\napproved = no\n";
    }
    auto P = [&](const char* rel) { return (dir / rel).string(); };
    CHECK(verify_external_file(P("Weapons/cube.obj")).empty());
    CHECK(verify_external_file(P("Weapons/kit/a.obj")).empty());          // intrare pe folder, CC-BY cu autor
    CHECK(!verify_external_file(P("Weapons/nc.obj")).empty());            // necomercial: refuzat
    CHECK(!verify_external_file(P("Weapons/noauthor.obj")).empty());      // CC-BY fara autor: refuzat
    CHECK(!verify_external_file(P("Weapons/pending.obj")).empty());       // neaprobat de utilizator: refuzat
    CHECK(!verify_external_file(P("Weapons/unlisted.obj")).empty());      // lipsa din manifest: refuzat
    std::string why;
    bool attr = false;
    CHECK(!AssetManifest::license_allowed("ripped", attr, why));
    CHECK(!AssetManifest::license_allowed("", attr, why));
    CHECK(AssetManifest::license_allowed("MIT", attr, why) && attr);
    std::printf("  [resurse] refuz NC: \"%s\"; neaprobat: \"%s\"\n", verify_external_file(P("Weapons/nc.obj")).c_str(),
                verify_external_file(P("Weapons/pending.obj")).c_str());

    // incarcarea OBJ: 6 fete patrate -> 12 triunghiuri, materialul MTL "metal" -> otel
    ObjModel om;
    CHECK(load_obj(P("Weapons/cube.obj"), om));
    CHECK(om.triangles == 12 && om.pos.size() == 36);
    CHECK(!om.mat.empty() && om.mat[0] == MatSteel);
    CHECK(std::abs(om.bmax.x - 1.0) < 1e-9 && std::abs(om.bmin.z + 1.0) < 1e-9);
    CHECK(external_model(P("Weapons/cube.obj")) != nullptr);
    CHECK(external_model(P("Weapons/nc.obj")) == nullptr);

    // arma cu model extern: plasa contine modelul + atasamentele procedurale pe punctele de prindere
    WeaponData base;
    CHECK(base.load(find_asset_root() + "/weapons/mk18.ini"));
    CHECK(!base.visual.model.empty() && external_model(base.visual.model) != nullptr);   // MK18 foloseste modelul Quaternius (CC0)
    base.visual.model.clear();                                                           // referinta: modelul procedural
    base.visual.model_parts.clear();
    WeaponData ext = base;
    ext.visual.model = P("Weapons/cube.obj");
    ext.visual.model_scale = 0.02;
    Mesh m0, m1;
    WeaponMeshOut o0, o1;
    WeaponVisualState vs;
    build_weapon_mesh(m0, base, WeaponFrame{}, vs, o0);
    build_weapon_mesh(m1, ext, WeaponFrame{}, vs, o1);
    CHECK(m1.pos.size() >= 36 && m1.pos.size() < m0.pos.size());      // corpul procedural inlocuit de model
    CHECK(len(o1.muzzle - o0.muzzle) < 0.25);                           // gura tevii tot din punctele de prindere
    WeaponData refused = base;
    refused.visual.model = P("Weapons/nc.obj");
    Mesh m2;
    WeaponMeshOut o2;
    build_weapon_mesh(m2, refused, WeaponFrame{}, vs, o2);
    CHECK(m2.pos.size() == m0.pos.size());                              // refuzat -> modelul procedural
    // piesele modelului extern: incarcatorul coboara la reincarcare si lipseste cand e scos; patul dispare la ochire
    {
        WeaponData q;
        CHECK(q.load(find_asset_root() + "/weapons/mk18.ini"));
        Mesh a0, a1, a2, a3;
        WeaponMeshOut o_;
        WeaponVisualState in_gun, dropped, out_, ads;
        dropped.mag_drop = 0.10;
        out_.mag_in_gun = false;
        ads.show_stock = false;
        build_weapon_mesh(a0, q, WeaponFrame{}, in_gun, o_);
        build_weapon_mesh(a1, q, WeaponFrame{}, dropped, o_);
        build_weapon_mesh(a2, q, WeaponFrame{}, out_, o_);
        build_weapon_mesh(a3, q, WeaponFrame{}, ads, o_);
        double y0 = -1e9, y1 = -1e9;
        for (const V3& v : a0.pos) y0 = std::max(y0, v.y);
        for (const V3& v : a1.pos) y1 = std::max(y1, v.y);
        std::printf("  [model extern] MK18: %zu varfuri; incarcatorul scos: %zu; la ochire (fara pat): %zu; coborare max %.3f m\n",
                    a0.pos.size(), a2.pos.size(), a3.pos.size(), y1 - y0);
        CHECK(a0.pos.size() == a1.pos.size() && std::abs((y1 - y0) - 0.10) < 0.02);   // aceeasi piesa, mutata in jos
        CHECK(a2.pos.size() < a0.pos.size() && a3.pos.size() < a0.pos.size());
    }
    fs::remove_all(dir, ec);
}

static void test_bots_ammo_sidearm_and_controls() {
    // botul cu arma principala goala si fara rezerve trece pe pistol; fara munitie deloc nu mai trage
    World w = range_world(33);
    Agent& me = w.agents[0];
    me.x = 20.5; me.y = 13.5;
    me.invulnerable = true;
    Agent b;
    b.id = static_cast<int>(w.agents.size());
    b.faction = Faction::Red;
    b.x = 30.5; b.y = 13.5;
    b.facing = 3.14159265;
    b.state = FsmState::Engage;
    b.policy = Policy::Hold;
    b.psi = 1.0;
    b.health.init(w.health_cfg);
    int pi = find_profile(w.profiles, "bot_01");
    CHECK(pi >= 0);
    w.equip_from_profile(b, pi);
    CHECK(b.slots.size() >= 2);
    b.slots[0].mag = 3;
    for (auto& m : b.slots[0].spares) m.rounds = 0;          // fara rezerve pentru pusca
    b.acquired_targets.push_back(0);
    b.target_id = 0;
    w.agents.push_back(b);
    AgentControl idle;
    bool switched = false;
    int pistol_shots = 0, rifle_reloads = 0;
    for (int i = 0; i < 60 * 12; ++i) {
        w.step(&idle);
        const Agent& bot = w.agents.back();
        for (const auto& e : w.event_log.get_all()) {
            if (e.a != bot.id) continue;
            if (e.type == EventType::WEAPON_SWITCH) switched = true;
            if (e.type == EventType::SHOT && switched) pistol_shots++;
            if (e.type == EventType::RELOAD_END && !switched) rifle_reloads++;
        }
        w.event_log.clear();
    }
    const Agent& bot = w.agents.back();
    const WeaponData* cur = w.weapon_of(bot);
    std::printf("  [boti] pusca goala -> %s: %s, %d focuri cu pistolul, reincarcari magice ale pustii %d\n",
                cur ? cur->name.c_str() : "?", switched ? "a schimbat arma" : "NU a schimbat", pistol_shots, rifle_reloads);
    CHECK(switched && cur && cur->model == WeaponModel::Pistol && pistol_shots > 0);
    CHECK(rifle_reloads == 0);                                 // nicio reumplere din nimic
    // fara nicio munitie: nu mai trage
    {
        Agent& z = w.agents.back();
        for (auto& sl : z.slots) { sl.mag = 0; sl.chambered = false; for (auto& m : sl.spares) m.rounds = 0; }
        int shots = 0;
        for (int i = 0; i < 60 * 5; ++i) {
            w.step(&idle);
            for (const auto& e : w.event_log.get_all()) if (e.a == z.id && e.type == EventType::SHOT) shots++;
            w.event_log.clear();
        }
        CHECK(shots == 0);
    }
    // reincarcarea culcat e mai lenta (arma pe o parte), ghemuit aproape la fel
    WeaponData wd = make_default_rifle();
    ReloadContext stand, prone;
    prone.prone = 1.0;
    ReloadPlan ps = make_reload_plan(wd, stand, 3, 1, 1), pp = make_reload_plan(wd, prone, 3, 1, 1);
    double ts = 0, tp = 0;
    for (int i = 1; i < RS_Count; ++i) if (i != RS_Shell && i != RS_Clear) { ts += ps.dur[i]; tp += pp.dur[i]; }
    CHECK(tp > ts * 1.12);
    // sensibilitatea comenzilor gadgeturilor vine din date
    Map m;
    m.from_rows({"111111111111", "1..........1", "1..........1", "1..........1", "1..........1", "111111111111"});
    AgentControl turn;
    turn.stick_pitch = 0.6; turn.stick_roll = 0.4;
    auto yaw_after = [&](double sens) {
        Vehicle v;
        v.cfg.steer_sensitivity = sens;
        v.x = 5.5; v.y = 3.0;
        for (int i = 0; i < 30; ++i) step_vehicle(v, &turn, m, 1.0 / 60.0);
        return v.yaw;
    };
    CHECK(yaw_after(0.5) < yaw_after(1.0) * 0.7);
    VehicleConfig vc;
    vc.steer_expo = 1.0;
    Vehicle e1, e0;
    e1.cfg = vc;
    e1.x = e0.x = 5.5; e1.y = e0.y = 3.0;
    for (int i = 0; i < 30; ++i) { step_vehicle(e1, &turn, m, 1.0 / 60.0); step_vehicle(e0, &turn, m, 1.0 / 60.0); }
    CHECK(std::abs(e1.steer) < std::abs(e0.steer));          // expo: comenzile mici sunt mai fine
}

static void test_weapon_view_and_accessory_models() {
    const std::string root = find_asset_root();
    // pozitiile armei in prim-plan vin din config, pe tip de arma (cu mostenire din [rifle])
    ViewmodelPoses vp;
    CHECK(vp.load(root + "/config/viewmodel.ini"));
    const ViewmodelPose& rifle = vp.by_model[static_cast<int>(WeaponModel::Rifle)];
    const ViewmodelPose& pistol = vp.by_model[static_cast<int>(WeaponModel::Pistol)];
    const ViewmodelPose& shotgun = vp.by_model[static_cast<int>(WeaponModel::Shotgun)];
    const ViewmodelPose& smg = vp.by_model[static_cast<int>(WeaponModel::Smg)];
    CHECK(pistol.ads_dz > 0.15);                                   // pistolul la brate intinse
    CHECK(len(smg.hip - rifle.hip) < 1e-9 && len(shotgun.shoulder_l - rifle.shoulder_l) < 1e-9);   // mostenire
    CHECK(shotgun.ads_wrist_drop > rifle.ads_wrist_drop && shotgun.ads_hand_drop > 0.0);
    CHECK(rifle.shoulder_l.z > 0.0 && rifle.ads_fov < 1.0);         // umarul stang inainte, arma marita la ochire

    // atasamentele cu model extern: licenta verificata, modelul inlocuieste forma procedurala
    auto db = load_attachment_db(root + "/config/attachments.ini");
    int with_model = 0;
    for (const auto& a : db) {
        if (a.model.empty()) continue;
        ++with_model;
        CHECK(external_model(a.model) != nullptr);
    }
    CHECK(with_model >= 5);                                         // luneta, amortizor, maner, lanterna, bipied
    WeaponData raw;
    CHECK(raw.load(root + "/weapons/mk18.ini", nullptr, false));
    auto variant = [&](std::vector<std::string> atts, bool models) {
        auto d = db;
        if (!models) for (auto& a : d) { a.model.clear(); a.model_pistol.clear(); }
        WeaponData v = raw; v.attachments = atts; v.apply_attachments(&d); return v;
    };
    WeaponVisualState vs;
    WeaponData proc = variant({"suppressor", "scope4x"}, false), ext = variant({"suppressor", "scope4x"}, true);
    Mesh mp, me;
    WeaponMeshOut op, oe;
    build_weapon_mesh(mp, proc, WeaponFrame{}, vs, op);
    build_weapon_mesh(me, ext, WeaponFrame{}, vs, oe);
    CHECK(me.pos.size() > mp.pos.size());                           // modelele (sute de triunghiuri) in locul formelor simple
    const double sup_len = oe.muzzle.z - ext.sockets.muzzle.z;
    CHECK(sup_len > 0.14 && sup_len < 0.2);                         // amortizorul lungeste arma ca in date (0.17 m)
    CHECK(oe.scope && oe.lens_r > 0.008 && std::abs(oe.lens_c.z - (ext.sockets.optic.z - 0.1)) < 0.01);   // ocularul la locul lui
    CHECK(std::abs(oe.lens_c.y - weapon_sight_y(ext)) < 0.004);     // axa lunetei pe linia de ochire
    std::printf("  [accesorii] %d modele externe; amortizor %.3f m; luneta: ocular z=%.3f r=%.3f; plasa %zu -> %zu varfuri\n",
                with_model, sup_len, oe.lens_c.z, oe.lens_r, mp.pos.size(), me.pos.size());
    // red dot: linia de ochire la inaltimea opticii (montaj realist), maner vertical sub garda reala
    WeaponData rd = variant({"reddot", "foregrip"}, true);
    CHECK(std::abs(weapon_sight_y(rd) - (rd.sockets.optic.y - 0.038)) < 1e-9);
    CHECK(std::abs(rd.sockets.support.y - (rd.visual.rail_y + rd.visual.rail_radius + 0.066)) < 1e-9);
    // la ochire sticla nu e opaca (se vede prin optica)
    Mesh ma, mb;
    WeaponMeshOut oa, ob;
    WeaponVisualState aim = vs;
    aim.see_through = true;
    build_weapon_mesh(ma, rd, WeaponFrame{}, vs, oa);
    build_weapon_mesh(mb, rd, WeaponFrame{}, aim, ob);
    CHECK(mb.pos.size() < ma.pos.size());

    // bipiedul: ajuta doar culcat (dispersie, recul), strans / desfacut in plasa
    HandlingConfig h;
    h.load(root + "/config/handling.ini");
    WeaponData sn;
    CHECK(sn.load(root + "/weapons/m24.ini", nullptr, false));
    WeaponData bare = sn, bp = sn;
    bare.attachments = {"scope4x"}; bare.apply_attachments(&db);
    bp.attachments = {"scope4x", "bipod"}; bp.apply_attachments(&db);
    CHECK(bp.has_bipod && !bare.has_bipod);
    WeaponSlot sl;
    Agent stand, prone;
    stand.stamina = prone.stamina = 1.0;
    prone.prone = 1.0;
    CHECK(std::abs(compute_spread(bp, stand, sl, h).total - compute_spread(bare, stand, sl, h).total) < 1e-12);
    CHECK(compute_spread(bp, prone, sl, h).total < compute_spread(bare, prone, sl, h).total * 0.8);
    CHECK(compute_recoil(bp, prone, sl, 3, h).total < compute_recoil(bare, prone, sl, 3, h).total * 0.7);
    Mesh f0, f1;
    WeaponMeshOut q0, q1;
    WeaponVisualState dep = vs;
    dep.bipod_deploy = 1.0;
    build_weapon_mesh(f0, bp, WeaponFrame{}, vs, q0);
    build_weapon_mesh(f1, bp, WeaponFrame{}, dep, q1);
    double low0 = -1e9, low1 = -1e9;
    for (const V3& v : f0.pos) low0 = std::max(low0, v.y);
    for (const V3& v : f1.pos) low1 = std::max(low1, v.y);
    CHECK(low1 > low0 + 0.1);                                       // desfacut: picioarele coboara spre sol
}

static void test_campaign_and_bot_stamina() {
    namespace fs = std::filesystem;
    // campania: meciuri simulate cu operatorul AI, salvate ca meciurile jucate (director temporar, nu data/)
    fs::path dir = fs::temp_directory_path() / "bodycam_test_campaign";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    CampaignConfig cc;
    cc.asset_root = find_asset_root();
    cc.data_dir = dir.string();
    cc.maps = {"urban"};
    cc.matches_per_map = 1;
    CampaignSummary sum = run_campaign(cc, nullptr);
    CHECK(sum.matches == 1 && sum.rounds >= 1 && sum.sim_seconds > 30.0);
    StatisticsSystem S;
    S.init(dir.string(), cc.asset_root + "/config/stats.ini");
    CHECK(S.matches.size() == 1);
    if (!S.matches.empty()) {
        const MatchRecord& m = S.matches[0];
        CHECK(m.map_name == "URBAN" && m.counted() && m.rounds >= 1);
        CHECK(m.hr_max > m.hr_min + 20.0);                   // pulsul a reactionat la lupta
        CHECK(!S.samples_of(m.id).empty());
        std::printf("  [campanie] URBAN: %s, %d runde, puls %.0f-%.0f, stres max %.2f, stamina consumata %.0f%%, %d focuri\n",
                    m.result.c_str(), m.rounds, m.hr_min, m.hr_max, m.stress_max, m.stamina_used * 100.0, m.shots);
    }
    CampaignConfig none = cc;
    none.data_dir.clear();
    CHECK(run_campaign(none, nullptr).matches == 0);          // fara director: nu scrie nicaieri
    fs::remove_all(dir, ec);

    // coechipierii AI alearga spre zgomot / obiectiv doar fara inamic in vizor si obosesc ca jucatorul
    World w = range_world(41);
    Agent b;
    b.id = static_cast<int>(w.agents.size());
    b.faction = Faction::Blue;
    b.x = 2.5; b.y = 13.5;
    b.state = FsmState::Investigate;
    b.last_known_x = 30.5; b.last_known_y = 13.5;
    b.health.init(w.health_cfg);
    b.stamina = 1.0;
    w.agents.push_back(b);
    AgentControl idle;
    double vmax = 0.0, smin = 1.0;
    for (int i = 0; i < 60 * 4; ++i) {
        w.step(&idle);
        vmax = std::max(vmax, w.agents.back().speed);
        smin = std::min(smin, w.agents.back().stamina);
    }
    std::printf("  [boti] alergare: viteza maxima %.1f m/s, stamina minima %.2f\n", vmax, smin);
    CHECK(vmax > w.params.v_walk * 1.2 && smin < 0.8);
}

int main() {
    struct T { const char* name; std::function<void()> fn; };
    std::vector<T> tests = {
        {"rng + LOS + ferestre", test_rng_los},
        {"harta: markeri, usi incuiate, incaperi", test_map_markers_doors_rooms},
        {"miscare: inertie, stamina, greutate", test_movement_inertia_stamina_load},
        {"culcat + aplecare (fizica)", test_prone_lean},
        {"fiziologie: stres, puls, respiratie, tremur", test_physiology},
        {"fiziologie: cele 8 scenarii (stres, adrenalina, descarcare, revenire)", test_physiology_scenarios},
        {"fiziologie: jucabil la stres maxim, camera protejata, miscare corelata", test_physiology_playable_and_correlated},
        {"incarcatoare fizice + stiluri de reincarcare", test_magazines_and_reload_styles},
        {"reincarcare intrerupta si reluata", test_reload_interrupt_resume},
        {"cadenta, caldura, recul, blocaj", test_fire_heat_recoil_malfunction},
        {"balistica: penetrare, sticla, zone L/R", test_ballistics},
        {"proiectile cu timp de zbor", test_projectiles},
        {"daune localizate, cadere, resuscitare, bandaj", test_localized_damage_downed_revive_bandage},
        {"postura: tresarire/impleticire/cadere", test_stance_classes},
        {"iluminare + curent / avarie", test_lightmap_power},
        {"interactiuni: usi, lovitura, intrerupator, marcaje, tableta", test_interactions},
        {"audio spatial: ocluzie/pan", test_spatial_audio},
        {"grenade, obiecte, drona, vehicul", test_grenade_props_pawns},
        {"determinism + AI pe harta compound", test_determinism_and_bots},
        {"runner experimente (--scenario)", test_runner_scenarios},
        {"escaladare, teava in perete, acoperire joasa", test_mantle_wallblock_cover},
        {"respiratie tinuta / tactica, proximitate", test_controlled_breathing},
        {"incarcator nefixat -> tap-rack", test_unseated_magazine},
        {"tableta: stari, miscare, privire, inchidere + foc, fara pauza", test_tablet_state_machine_no_pause},
        {"tableta: lovit, sprint, schimbare arma, cazut", test_tablet_interruptions},
        {"tableta: drona + rechemare, RC + detonare", test_tablet_gadgets},
        {"echipament, aruncabile, fum, corp la corp", test_loadout_throwables_melee},
        {"tableta vazuta de ceilalti (model 3D)", test_tablet_seen_by_others},
        {"respiratie sintetizata (respiratii distincte)", test_breath_synth},
        {"catalogul hartilor (MapId, masuratori, runde)", test_map_catalog},
        {"poligon: zone cap / corp / ratat", test_range_target_zones},
        {"poligon: sesiune de reactie", test_range_reaction_session},
        {"poligon: proba de stres (sprint real) si viteza", test_range_stress_and_speed},
        {"statistici: meci -> inregistrari -> CSV -> citire", test_statistics_match_pipeline},
        {"analiza: regresie, Wilson, harti, serii, revenire puls", test_analytics_functions},
        {"stocare CSV: coloane noi, doar-citire", test_storage_csv_migration},
        {"oboseala + experimente controlate (CRN)", test_fatigue_and_experiments},
        {"arme: atasamente pe locasuri (date, compatibilitate, efecte)", test_weapon_attachments_data},
        {"arme: calculul centralizat al dispersiei / reculului", test_weapon_stats_model},
        {"arme: profiluri de recul (pistol / carabina / luneta)", test_weapon_recoil_profiles},
        {"arme: moduri de foc (automat, semi, rafala, repetitie)", test_weapon_fire_modes},
        {"boti: acelasi sistem de arme (gloante, incarcatoare, inchizator)", test_bots_share_weapon_system},
        {"gadgeturi: date din fisier, suspensie, roti, baterie, avarii", test_gadget_config_physics},
        {"personaje: mainile pe punctele de prindere ale armei reale", test_character_hands_on_weapon_sockets},
        {"resurse externe: licenta verificata, OBJ/MTL, arma cu model extern", test_external_assets_license_and_obj},
        {"boti: arma secundara, fara munitie magica; reincarcare culcat; comenzile gadgeturilor", test_bots_ammo_sidearm_and_controls},
        {"arma in prim-plan: pozitii din config, atasamente cu modele externe, bipied", test_weapon_view_and_accessory_models},
        {"campanie pe harti (operator AI) salvata in statistici; alergarea si stamina botilor", test_campaign_and_bot_stamina},
    };
    for (auto& t : tests) {
        int before = g_failed;
        t.fn();
        std::cout << (g_failed == before ? "[ OK ] " : "[FAIL] ") << t.name << "\n";
    }
    std::cout << "\n" << g_checks << " verificari, " << g_failed << " esuate.\n";
    return g_failed == 0 ? 0 : 1;
}
