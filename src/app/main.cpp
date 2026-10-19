#define SDL_MAIN_HANDLED
#include <SDL.h>
#include "game_view.hpp"
#include "app_session.hpp"
#include "../game/scenario.hpp"
#include "../game/map_catalog.hpp"
#include "../stats/statistics_system.hpp"
#include "../stats/experiments.hpp"
#include "../core/profiler.hpp"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <memory>
#include <algorithm>

using namespace bodycam;

namespace {

struct Options {
    int win_w = 1600, win_h = 900;
    int res_w = 960, res_h = 540;
    bool fullscreen = false;
    bool audio = true;
    uint64_t seed = 20261005;
    std::string mode = "elimination";
    bool mode_set = false;
    std::string map;            // --map urban | industrial | residential | training | tactical | compound
    std::string data_dir;       // --data: unde se salveaza statisticile (implicit <proiect>/data)
    bool data_rw = false;       // --data-rw: captura poate scrie (doar intr-un --data separat, pentru verificari)
    std::string weapon;
    std::string attachments;    // --attachments a,b,c: atasamentele armei principale (captura)
    int teammates = 0;
    // retea
    std::string host_port;      // --host 7777
    std::string connect_addr;   // --connect 127.0.0.1:7777
    int sim_lag_ms = 0;
    double sim_loss = 0.0;
    // captura fara fereastra (verificare vizuala / documentatie)
    std::string capture;
    int frames = 120;
    bool has_pos = false;
    double px = 0, py = 0, pyaw = 0, ppitch = 0;
    std::string hold;
    bool freeze_bots = false;
    double lineup = 0.0;   // captura: botii aliniati in fata camerei (distanta, m)
    bool lineup_side = false;   // ... intorsi lateral, cu arma la ochire
    bool phys_debug = false;    // panoul fiziologiei (F11)
    double vm_orbit = 0.0;      // captura: primul plan rotit (grade), vedere laterala de depanare
    double weapon_fov = -1.0;   // captura: campul vizual al armei (grade)
    bool procedural = false;    // captura: armele fara modele externe (comparatie)
    int reload_at = -1;         // captura: reincarcarea porneste la acest cadru
    double force_stress = -1.0; // captura: stresul fixat la acest nivel (0..1) pe tot parcursul
    double gadgets = 0.0;       // captura: masina RC + drona plasate in fata camerei (distanta, m)
    double gadget_hp = 1.0;     // ... cu structura data (fractiune; 0 = distruse)
    double gadget_yaw = 0.0;    // ... orientarea fata de camera (grade)
    bool clean = false;
    bool drone = false;
    bool tablet = false;
    bool inspect = false;
    bool debug = false;
    int smoke = 0;
    std::string record_audio;   // --record-audio out.wav: inregistreaza mixajul final              // test automat cu fereastra ascunsa (N cadre)
};

void parse_size(const std::string& s, int& w, int& h) {
    size_t x = s.find('x');
    if (x == std::string::npos) return;
    w = std::stoi(s.substr(0, x));
    h = std::stoi(s.substr(x + 1));
}

Options parse(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : ""; };
        if (a == "--fullscreen") o.fullscreen = true;
        else if (a == "--no-audio") o.audio = false;
        else if (a == "--seed") o.seed = std::stoull(next());
        else if (a == "--mode") { o.mode = next(); o.mode_set = true; }
        else if (a == "--map") o.map = next();
        else if (a == "--data") o.data_dir = next();
        else if (a == "--data-rw") o.data_rw = true;
        else if (a == "--weapon") o.weapon = next();
        else if (a == "--attachments") o.attachments = next();
        else if (a == "--teammates") o.teammates = std::stoi(next());
        else if (a == "--res") parse_size(next(), o.res_w, o.res_h);
        else if (a == "--window") parse_size(next(), o.win_w, o.win_h);
        else if (a == "--host") o.host_port = next();
        else if (a == "--connect") o.connect_addr = next();
        else if (a == "--simlag") o.sim_lag_ms = std::stoi(next());
        else if (a == "--simloss") o.sim_loss = std::stod(next());
        else if (a == "--capture") o.capture = next();
        else if (a == "--frames") o.frames = std::stoi(next());
        else if (a == "--pos") { o.has_pos = true; o.px = std::stod(next()); o.py = std::stod(next()); }
        else if (a == "--yaw") o.pyaw = std::stod(next()) * 3.14159265358979 / 180.0;
        else if (a == "--pitch") o.ppitch = std::stod(next()) * 3.14159265358979 / 180.0;
        else if (a == "--hold") o.hold = next();
        else if (a == "--freeze-bots") o.freeze_bots = true;
        else if (a == "--lineup") o.lineup = std::stod(next());
        else if (a == "--lineup-side") o.lineup_side = true;
        else if (a == "--gadgets") o.gadgets = std::stod(next());
        else if (a == "--physdebug") o.phys_debug = true;
        else if (a == "--procedural") o.procedural = true;
        else if (a == "--vm-orbit") o.vm_orbit = std::stod(next()) * 3.14159265358979 / 180.0;
        else if (a == "--wfov") o.weapon_fov = std::stod(next());
        else if (a == "--reload-at") o.reload_at = std::stoi(next());
        else if (a == "--stress") o.force_stress = std::stod(next());
        else if (a == "--gadget-hp") o.gadget_hp = std::stod(next());
        else if (a == "--gadget-yaw") o.gadget_yaw = std::stod(next());
        else if (a == "--clean") o.clean = true;
        else if (a == "--drone") o.drone = true;
        else if (a == "--tablet") o.tablet = true;
        else if (a == "--inspect") o.inspect = true;
        else if (a == "--debug") o.debug = true;
        else if (a == "--smoke") o.smoke = std::stoi(next());
        else if (a == "--record-audio") o.record_audio = next();
    }
    return o;
}

bool write_bmp(const std::string& path, const uint32_t* px, int w, int h) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    int row = (w * 3 + 3) & ~3;
    uint32_t size = 54 + row * h;
    unsigned char hdr[54] = {'B', 'M'};
    auto put32 = [&](int off, uint32_t v) { for (int k = 0; k < 4; ++k) hdr[off + k] = static_cast<unsigned char>(v >> (8 * k)); };
    put32(2, size); put32(10, 54); put32(14, 40); put32(18, static_cast<uint32_t>(w)); put32(22, static_cast<uint32_t>(h));
    hdr[26] = 1; hdr[28] = 24; put32(34, row * h);
    f.write(reinterpret_cast<char*>(hdr), 54);
    std::vector<unsigned char> line(row, 0);
    for (int y = h - 1; y >= 0; --y) {
        for (int x = 0; x < w; ++x) {
            uint32_t p = px[y * w + x];
            line[x * 3 + 0] = p & 0xFF;
            line[x * 3 + 1] = (p >> 8) & 0xFF;
            line[x * 3 + 2] = (p >> 16) & 0xFF;
        }
        f.write(reinterpret_cast<char*>(line.data()), row);
    }
    return true;
}

RoundConfig make_config(const Options& o) {
    RoundConfig cfg;
    cfg.asset_root = find_asset_root();
    cfg.mode = (o.mode == "objective") ? GameMode::Objective : GameMode::Elimination;
    cfg.blue_bots = o.teammates;
    cfg.seed = o.seed;
    {
        MapCatalog cat;
        if (cat.load(cfg.asset_root, false)) {
            const MapInfo* m = !o.map.empty() ? cat.by_key(o.map) : cat.by_file(cfg.map_file);
            if (m) apply_map_info(cfg, *m, o.mode_set);
            else if (!o.map.empty()) std::fprintf(stderr, "Harta necunoscuta: %s\n", o.map.c_str());
        }
    }
    if (!o.weapon.empty()) {
        // arma aleasa devine principala
        std::string f = "weapons/" + o.weapon + ".ini";
        auto& v = cfg.weapon_files;
        v.erase(std::remove(v.begin(), v.end(), f), v.end());
        v.insert(v.begin(), f);
    }
    return cfg;
}

// ---------------------------------------------------------------------------
// Captura fara fereastra: simuleaza N cadre cu intrari scriptate si salveaza imaginea
// ---------------------------------------------------------------------------
int run_capture(const Options& o) {
    RoundConfig cfg = make_config(o);
    World world(o.seed);
    setup_round(world, cfg, nullptr);
    if (o.freeze_bots)
        for (auto& a : world.agents) if (!a.is_human) { a.weapon_enabled = false; a.perception_enabled = false; a.policy = Policy::Hold; }
    if (o.procedural) {
        for (auto& wd : world.weapons) { wd.visual.model.clear(); wd.visual.model_parts.clear(); }
        for (auto& wd : world.weapon_raw) { wd.visual.model.clear(); wd.visual.model_parts.clear(); }
    }
    Agent& me = world.agents[0];
    if (o.has_pos) { me.x = o.px; me.y = o.py; }
    me.facing = o.pyaw;
    me.pitch = o.ppitch;
    if (o.lineup > 0.0) {
        // verificare vizuala: toti ceilalti intr-un rand in fata camerei, cu fata spre ea
        int k = 0, n = 0;
        for (auto& a : world.agents) if (&a != &me) ++n;
        const double fx = std::cos(me.facing), fy = std::sin(me.facing);
        for (auto& a : world.agents) {
            if (&a == &me) continue;
            double off = (k++ - (n - 1) * 0.5) * 0.95;
            a.x = me.x + fx * o.lineup - fy * off;
            a.y = me.y + fy * o.lineup + fx * off;
            a.facing = me.facing + (o.lineup_side ? 3.14159265358979 * 0.5 : 3.14159265358979);
            if (o.lineup_side && !a.is_human) a.state = FsmState::Engage;
        }
    }
    if (o.gadgets > 0.0) {
        // verificare vizuala a modelelor 3D: masina (stanga) si drona (dreapta), fara pilot
        const double fx = std::cos(me.facing), fy = std::sin(me.facing);
        const double gy = me.facing + 3.14159265358979 + o.gadget_yaw * 3.14159265358979 / 180.0;
        Vehicle v;
        v.cfg = world.gadget_cfg.vehicle;
        v.id = static_cast<int>(world.vehicles.size());
        v.x = me.x + fx * o.gadgets - fy * 0.35; v.y = me.y + fy * o.gadgets + fx * 0.35;
        v.yaw = gy; v.steer = 0.35; v.charge = true;
        v.hp = v.cfg.hp * o.gadget_hp; v.alive = o.gadget_hp > 0.0;
        world.vehicles.push_back(v);
        Drone d;
        d.cfg = world.gadget_cfg.drone;
        d.id = static_cast<int>(world.drones.size());
        d.x = me.x + fx * o.gadgets + fy * 0.35; d.y = me.y + fy * o.gadgets - fx * 0.35; d.z = o.gadget_hp > 0.0 ? 1.45 : 0.06;
        d.yaw = gy;
        d.hp = d.cfg.hp * o.gadget_hp; d.alive = o.gadget_hp > 0.0;
        world.drones.push_back(d);
    }

    GameView view(o.res_w, o.res_h);
    view.clean_camera = o.clean;
    view.inspect_held = o.inspect;
    view.settings.load(view.settings_path);   // aceleasi setari ca jocul (camp vizual etc.)
    view.settings_path.clear();               // captura nu scrie niciodata setarile jucatorului
    if (o.weapon_fov > 0.0) view.settings.weapon_fov = o.weapon_fov;
    view.show_phys_debug = o.phys_debug;
    view.show_debug = o.debug;
    view.reset_for_world(world, 0);
    view.viewmodel.debug_orbit = o.vm_orbit;
    MapCatalog catalog;
    catalog.load(cfg.asset_root);
    StatisticsSystem stats;
    stats.init(o.data_dir.empty() ? cfg.asset_root + "/../data" : o.data_dir, cfg.asset_root + "/config/stats.ini");
    stats.storage.read_only = !(o.data_rw && !o.data_dir.empty());   // captura nu scrie in statisticile jucatorului
    ExperimentManager experiments;
    experiments.load(cfg.asset_root);
    {
        const MapInfo* mi = catalog.by_id(cfg.map_id);
        std::string nm = mi ? mi->name : cfg.map_file;
        if (cfg.mode == GameMode::Training) stats.begin_session(cfg.map_id, nm, true);
        else stats.begin_match(cfg.map_id, nm, cfg.mode);
        stats.begin_round(world, 0);
    }
    view.stats = &stats;
    view.catalog = &catalog;
    view.experiments = &experiments;
    view.map_id = cfg.map_id;
    std::vector<uint32_t> pixels(static_cast<size_t>(o.res_w) * o.res_h);
    std::vector<std::string> tokens;
    {
        std::stringstream ss(o.hold);
        std::string t;
        while (std::getline(ss, t, ',')) tokens.push_back(t);
    }
    auto held = [&](const char* k) { return std::find(tokens.begin(), tokens.end(), std::string(k)) != tokens.end(); };
    if (held("lowammo") && !me.slots.empty()) me.slots[0].mag = 4;
    if (held("flash")) { me.inv.flashbangs = std::max(1, me.inv.flashbangs); me.inv.throwable = GrenadeType::Flash; }
    if (held("smoke")) { me.inv.smokes = std::max(1, me.inv.smokes); me.inv.throwable = GrenadeType::Smoke; }
    if (held("knife")) me.inv.melee = MeleeKind::Knife;
    if (held("stress")) { physiology_stimulus(me.phys, 1.0, world.phys_cfg); me.phys.exertion = 0.9; }
    for (int pg = 0; pg < static_cast<int>(TabletPage::Count); ++pg)
        if (held(("page" + std::to_string(pg)).c_str())) view.tablet_ui_state.page = static_cast<TabletPage>(pg);
    for (int tb = 0; tb < static_cast<int>(StatsTab::Count); ++tb)
        if (held(("tab" + std::to_string(tb)).c_str())) view.tablet_ui_state.stats_tab = static_cast<StatsTab>(tb);
    for (int ch = 0; ch < 8; ++ch)
        if (held(("chart" + std::to_string(ch)).c_str())) view.tablet_ui_state.stats_chart = ch;
    if (held("compare")) view.tablet_ui_state.compare = true;
    int range_start = -1;
    for (int rm = 1; rm < static_cast<int>(RangeMode::Count); ++rm)
        if (held(("range" + std::to_string(rm)).c_str())) range_start = rm;
    double yaw = o.pyaw, pitch = o.ppitch;
    for (int f = 0; f < o.frames; ++f) {
        AgentControl c;
        c.look_yaw = yaw; c.look_pitch = pitch;
        c.moveForward = held("w") ? 1.0 : (held("back") ? -1.0 : 0.0);
        c.moveRight = held("strafe") ? 1.0 : 0.0;
        c.sprint = held("sprint");
        c.crouch = held("crouch");
        c.aim = held("aim");
        c.fire = held("fire") || (held("burst") && f < 40) || (held("tap") && f % 24 == 0);
        c.toggle_flashlight = (f == 0 && held("flashlight"));
        c.reload = (f == o.frames / 3 && held("reload")) || f == o.reload_at;
        if (held("tap") && me.cur_slot() && me.cur_slot()->mag == 0 && me.reload_stage == 0) c.reload = true;   // incarcator gol
        c.grenade_hold = held("grenade") && f < o.frames - 8;   // cuiul scos, aruncare la final
        c.melee = held("melee") && f == 20;
        c.toggle_drone = (f == 0 && o.drone);
        c.toggle_vehicle = (f == 0 && held("rc"));
        c.stick_pitch = held("w") ? 0.6 : 0.0;
        c.stick_throttle = held("up") ? 0.5 : 0.0;
        c.switch_weapon = held("pistol") && f == 0 ? 1 : -1;
        c.tablet_toggle = (f == 0 && o.tablet) || (held("close") && f == o.frames - 30) || (held("tabletlate") && f == o.frames - 100);
        if (held("tabletlate") && f >= o.frames - 100) { c.fire = false; c.aim = false; }
        c.lean = held("leanr") ? 1.0 : (held("leanl") ? -1.0 : 0.0);
        c.toggle_prone = f == 0 && held("prone");
        c.interact = f == 1 && held("use");
        c.interact_hold = held("use");
        c.jump = f == 1 && held("jump");
        c.bandage = held("bandage");
        if (f == 1 && range_start > 0) { c.range_mode = range_start; c.range_dist = 1; c.range_size = 1; }
        if (f == 0 && !o.attachments.empty()) {
            // echipamentul real (acelasi drum ca pagina ECHIPAMENT a tabletei)
            Loadout L = world.current_loadout(me);
            L.primary_att.clear();
            std::stringstream as(o.attachments);
            std::string t;
            while (std::getline(as, t, ',')) if (!t.empty()) L.primary_att.push_back(t);
            if (!o.weapon.empty()) L.primary = o.weapon;
            L.valid = true;
            world.apply_loadout(me, L);   // captura: echipamentul se aplica direct (nu doar langa start)
        }
        view.examine_held = held("examine");
        if (held("turn")) yaw += 0.03;
        if (held("touch")) view.tablet_cursor(0.004 * std::sin(f * 0.07), 0.003 * std::cos(f * 0.05));
        if (f == o.frames / 2 && o.tablet) view.tablet_click(false);
        if (f > 0) {
            TabletCommand t = view.take_tablet_command();
            if (t.tapped) c.tablet_touch = true;
            if (t.gadget == 1) c.toggle_drone = true;
            if (t.gadget == 2) c.toggle_vehicle = true;
        }
        if (o.force_stress >= 0.0) physiology_force_stress(me.phys, o.force_stress, world.phys_cfg);
        world.step(&c);
        std::vector<Event> evs = world.event_log.get_all();
        world.event_log.clear();
        view.on_step(world, evs, 0, world.dt);
        stats.on_step(world, evs, 0);
        yaw -= view.take_view_punch_yaw() * 0.0;
        pitch += view.take_view_punch_pitch();
        if ((held("burst") && f > 40) || held("tap")) pitch += (o.ppitch - pitch) * 0.1;   // revenirea privirii dupa rafala
        if (f >= o.frames - 3 || f % 4 == 0) view.render(world, 0, world.dt, pixels.data());
    }
    view.debug_lines = {"EV " + std::to_string(view.post.ev)};
    view.render(world, 0, world.dt, pixels.data());
    if (!write_bmp(o.capture, pixels.data(), o.res_w, o.res_h)) { std::fprintf(stderr, "Nu pot scrie %s\n", o.capture.c_str()); return 1; }
    std::printf("Captura salvata: %s (EV %.2f, lum %.3f)\n", o.capture.c_str(), view.post.ev, view.post.metered_luminance());
    std::printf("  arma: perete %.2f  ochire %.2f  regrip %.2f  examinare %.2f  coborata %.2f  pitch %.3f/%.3f\n",
                me.wall_block, me.aim, view.viewmodel.regrip_t, view.viewmodel.examine_w, view.viewmodel.low_w,
                me.w_pitch, view.last_pose.pitch);
    std::printf("  tableta: %s %.2f  pilotare %d  timp lume %.2fs  pasi %llu\n", tablet_state_name(me.tab.state), me.tablet,
                static_cast<int>(me.possess), world.current_time, static_cast<unsigned long long>(world.step_count));
    for (const auto& e : Profiler::get().entries()) std::printf("  %-16s %.2f ms\n", e.name.c_str(), e.avg_ms);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    Options o = parse(argc, argv);
    if (!o.capture.empty()) return run_capture(o);

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    Uint32 wflags = (o.smoke ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN) | SDL_WINDOW_RESIZABLE | (o.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    std::string title = "BODYCAM-MINI";
    if (!o.host_port.empty()) title += "  [HOST :" + o.host_port + "]";
    if (!o.connect_addr.empty()) title += "  [CLIENT " + o.connect_addr + "]";
    SDL_Window* window = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, o.win_w, o.win_h, wflags);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_RenderSetLogicalSize(renderer, o.res_w, o.res_h);
    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, o.res_w, o.res_h);
    if (!o.smoke) SDL_SetRelativeMouseMode(SDL_TRUE);

    AudioEngine audio;
    bool audio_ok = o.audio && audio.init(find_asset_root());
    if (audio_ok && !o.record_audio.empty()) audio.start_recording();

    SessionConfig sc;
    sc.round = make_config(o);
    sc.host_port = o.host_port.empty() ? 0 : std::stoi(o.host_port);
    sc.connect_addr = o.connect_addr;
    sc.sim_lag_ms = o.sim_lag_ms;
    sc.sim_loss = o.sim_loss;
    sc.map_key = o.map;
    sc.data_dir = o.data_dir;
    sc.mode_forced = o.mode_set;
    sc.stats_read_only = o.smoke > 0;
    std::unique_ptr<AppSession> session = make_session(sc);
    if (!session) {
        std::fprintf(stderr, "Nu am putut porni sesiunea (retea?)\n");
        return 1;
    }

    GameView view(o.res_w, o.res_h);
    view.audio = audio_ok ? &audio : nullptr;
    view.show_debug = o.debug;
    view.settings.load(view.settings_path);
    view.stats = session->statistics();
    view.catalog = session->catalog();
    view.experiments = session->experiments();

    std::vector<uint32_t> pixels(static_cast<size_t>(o.res_w) * o.res_h);
    InputState input;
    bool running = true;
    bool mouse_captured = o.smoke == 0;
    int smoke_frames = 0;
    Uint64 last = SDL_GetPerformanceCounter();
    double fps = 60.0;
    const double sens = 0.0022;

    while (running) {
        Uint64 now = SDL_GetPerformanceCounter();
        double frame_dt = static_cast<double>(now - last) / SDL_GetPerformanceFrequency();
        last = now;
        if (frame_dt > 0.25) frame_dt = 0.25;
        fps = fps * 0.95 + (1.0 / std::max(1e-4, frame_dt)) * 0.05;

        double mdx = 0.0, mdy = 0.0;
        // Contextul intrarilor, din starea reala a corpului (nu un meniu care opreste jocul)
        InputContext ctx = InputContext::OnFoot;
        {
            const World& cw = session->world();
            int cl = session->local_agent();
            if (cl >= 0 && cl < static_cast<int>(cw.agents.size())) {
                const Agent& ca = cw.agents[cl];
                if (!ca.active()) ctx = InputContext::Incapacitated;
                else if (ca.possess != Possession::Body) ctx = InputContext::Gadget;
                else if (view.tablet_active(cw, cl)) ctx = InputContext::Tablet;
            }
        }
        const bool tablet_mode = ctx == InputContext::Tablet;
        const bool gadget_mode = ctx == InputContext::Gadget;
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            else if (e.type == SDL_MOUSEMOTION && mouse_captured) { mdx += e.motion.xrel; mdy += e.motion.yrel; }
            else if (e.type == SDL_MOUSEBUTTONDOWN && !mouse_captured) { mouse_captured = true; SDL_SetRelativeMouseMode(SDL_TRUE); }
            else if (e.type == SDL_MOUSEBUTTONDOWN && (tablet_mode || gadget_mode)) {
                if (e.button.button == SDL_BUTTON_LEFT) view.tablet_click(false);
                else if (e.button.button == SDL_BUTTON_RIGHT) {
                    if (gadget_mode) input.gadget_exit = true;     // elibereaza gadgetul, tableta ramane in maini
                    else input.tablet_stow = true;                 // intrare de lupta: tableta jos, arma sus
                }
            }
            else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_MIDDLE) input.melee = true;
            else if (e.type == SDL_MOUSEWHEEL && tablet_mode) view.tablet_wheel(e.wheel.y > 0 ? 1 : -1);
            else if (e.type == SDL_KEYDOWN && !e.key.repeat) {
                switch (e.key.keysym.sym) {
                    case SDLK_ESCAPE:
                        if (mouse_captured) { mouse_captured = false; SDL_SetRelativeMouseMode(SDL_FALSE); }
                        else running = false;
                        break;
                    case SDLK_r: if (gadget_mode) input.gadget_recall = true; else input.reload = true; break;
                    case SDLK_f: input.interact = true; break;
                    case SDLK_l: input.flashlight = true; break;
                    case SDLK_z: input.prone = true; break;
                    case SDLK_k: input.kick = true; break;
                    case SDLK_TAB: input.tablet_toggle = true; break;
                    case SDLK_g: if (gadget_mode) input.gadget_detonate = true; break;   // pe jos: G tinut (cuiul scos)
                    case SDLK_v: input.drone = true; break;
                    case SDLK_b: input.vehicle = true; break;
                    case SDLK_x: input.drone_mode = true; break;
                    case SDLK_n: input.fire_mode = true; break;   // selectorul de foc
                    case SDLK_SPACE: input.jump = true; break;
                    case SDLK_1: if (tablet_mode) view.tablet_page(0); else input.switch_weapon = 0; break;
                    case SDLK_2: if (tablet_mode) view.tablet_page(1); else input.switch_weapon = 1; break;
                    case SDLK_3: if (tablet_mode) view.tablet_page(2); else input.switch_weapon = 2; break;
                    case SDLK_4: if (tablet_mode) view.tablet_page(3); else input.switch_weapon = 3; break;
                    case SDLK_5: if (tablet_mode) view.tablet_page(4); else input.cycle_throwable = true; break;
                    case SDLK_6: if (tablet_mode) view.tablet_page(5); break;
                    case SDLK_7: if (tablet_mode) view.tablet_page(6); break;
                    case SDLK_8: if (tablet_mode) view.tablet_page(7); break;
                    case SDLK_F1: view.show_debug = !view.show_debug; break;
                    case SDLK_F2: view.post.settings.lens = !view.post.settings.lens; break;
                    case SDLK_F3: view.post.settings.vignette = !view.post.settings.vignette; break;
                    case SDLK_F4: view.post.settings.noise = !view.post.settings.noise; break;
                    case SDLK_F5: view.post.settings.motion_blur = !view.post.settings.motion_blur; break;
                    case SDLK_F6: view.post.settings.auto_exposure = !view.post.settings.auto_exposure; break;
                    case SDLK_F7: view.post.settings.compression = !view.post.settings.compression; break;
                    case SDLK_F8: view.clean_camera = !view.clean_camera; break;
                    case SDLK_F9: session->request_restart(); break;
                    case SDLK_F10: view.camera.enabled_inertia = !view.camera.enabled_inertia; break;
                    case SDLK_F11: view.show_phys_debug = !view.show_phys_debug; break;
                    case SDLK_m: if (audio_ok) audio.master = audio.master > 0.0f ? 0.0f : 0.8f; break;
                    default: break;
                }
            }
        }
        if (o.smoke) {
            // intrari scriptate: mers, privire, foc, reincarcare, drona
            ++smoke_frames;
            mdx = (smoke_frames % 120 < 60) ? 3.0 : -3.0;
            if (smoke_frames == 30) input.flashlight = true;
            if (smoke_frames == 100) input.fire_mode = true;   // selectorul de foc
            if (smoke_frames == 200) input.reload = true;
            if (smoke_frames == 300) input.drone = true;
            if (smoke_frames == 420) input.drone = true;
            if (smoke_frames == 500) input.tablet_toggle = true;
            if (smoke_frames == 540) view.tablet_click(false);
            if (smoke_frames == 590) input.tablet_toggle = true;
            if (smoke_frames == 520) input.prone = true;
            if (smoke_frames == 560) input.prone = true;
            if (smoke_frames >= o.smoke) running = false;
        }
        const Uint8* ks = SDL_GetKeyboardState(nullptr);
        Uint32 mb = SDL_GetMouseState(nullptr, nullptr);
        input.forward = (ks[SDL_SCANCODE_W] ? 1.0 : 0.0) - (ks[SDL_SCANCODE_S] ? 1.0 : 0.0);
        input.right = (ks[SDL_SCANCODE_D] ? 1.0 : 0.0) - (ks[SDL_SCANCODE_A] ? 1.0 : 0.0);
        input.sprint = ks[SDL_SCANCODE_LSHIFT] != 0;
        input.crouch = ks[SDL_SCANCODE_C] || ks[SDL_SCANCODE_LCTRL];
        input.throttle_up = ks[SDL_SCANCODE_SPACE] != 0;
        input.throttle_down = ks[SDL_SCANCODE_LCTRL] || ks[SDL_SCANCODE_C];
        // pe jos: butoanele mouse-ului sunt arma; cu tableta / gadgetul sunt atingeri pe ecran
        input.fire = ctx == InputContext::OnFoot && mouse_captured && (mb & SDL_BUTTON(SDL_BUTTON_LEFT));
        if (o.smoke) {
            input.forward = (smoke_frames % 200 < 100) ? 1.0 : 0.0;
            input.fire = ctx == InputContext::OnFoot && smoke_frames % 90 < 10;
        }
        input.aim = ctx == InputContext::OnFoot && mouse_captured && (mb & SDL_BUTTON(SDL_BUTTON_RIGHT));
        input.grenade_hold = ctx == InputContext::OnFoot && (ks[SDL_SCANCODE_G] != 0 || (o.smoke && smoke_frames > 440 && smoke_frames < 470));
        input.drone_sens = view.settings.drone_sens;
        input.invert_y = view.settings.invert_y;
        input.interact_hold = ks[SDL_SCANCODE_F] != 0;
        input.bandage = ks[SDL_SCANCODE_H] != 0;
        input.lean = (ks[SDL_SCANCODE_E] ? 1.0 : 0.0) - (ks[SDL_SCANCODE_Q] ? 1.0 : 0.0);
        view.inspect_held = ks[SDL_SCANCODE_T] != 0;
        view.examine_held = ks[SDL_SCANCODE_I] != 0;

        // Privirea (autoritatea clientului) sau stick-urile pawn-ului
        const double ms = sens * view.settings.mouse_sens;
        const double inv = view.settings.invert_y ? -1.0 : 1.0;
        bool cursor_in_gadget = gadget_mode && ks[SDL_SCANCODE_LALT] != 0;   // Alt: degetul pe butoanele de pe ecran
        input.mouse_dx = cursor_in_gadget ? 0.0 : mdx;
        input.mouse_dy = cursor_in_gadget ? 0.0 : mdy;
        if (tablet_mode || cursor_in_gadget) {
            // mouse-ul misca degetul pe ecranul tabletei; privirea ramane (aproape) fixa
            view.tablet_cursor(mdx * 0.0016 * view.settings.mouse_sens, mdy * 0.0024 * view.settings.mouse_sens);
            if (tablet_mode) input.view_yaw += mdx * ms * 0.08;
        } else if (ctx == InputContext::OnFoot || ctx == InputContext::Incapacitated) {
            input.view_yaw += mdx * ms;
            input.view_pitch = std::clamp(input.view_pitch - mdy * ms * inv, -1.2, 1.2);
        }
        input.frame_dt = frame_dt;

        session->update(input, view, frame_dt);
        input.clear_one_shots();
        view.map_id = session->map_id();

        // Reculul permanent ramane in privire
        input.view_pitch = std::clamp(input.view_pitch + view.take_view_punch_pitch(), -1.2, 1.2);
        input.view_yaw += view.take_view_punch_yaw();
        if (session->view_reset_requested()) { input.view_yaw = session->spawn_yaw(); input.view_pitch = 0.0; }

        const World& w = session->world();
        int local = session->local_agent();
        if (view.show_debug) {
            view.debug_lines.clear();
            char buf[128];
            std::snprintf(buf, sizeof(buf), "FPS %.0f  EV %+.2f  LUM %.3f", fps, view.post.ev, view.post.metered_luminance());
            view.debug_lines.push_back(buf);
            if (local >= 0 && local < static_cast<int>(w.agents.size())) {
                const Agent& a = w.agents[local];
                std::snprintf(buf, sizeof(buf), "CONTEXT %s  TABLETA %s %.2f  T %.1fS", input_context_name(ctx),
                              tablet_state_name(a.tab.state), a.tablet, w.current_time);
                view.debug_lines.push_back(buf);
                std::snprintf(buf, sizeof(buf), "HP %.0f  STAM %.2f  STRES %.2f/%.2f %s  ADR %.2f  PULS %.0f  RESP %.0f  %s", a.hp, a.stamina,
                              a.phys.stress, a.phys.stress_target, stress_level_name(a.phys.level), a.phys.adrenaline,
                              a.phys.heart_rate, a.phys.breath_rate, phys_state_name(a.phys.state));
                view.debug_lines.push_back(buf);
                std::snprintf(buf, sizeof(buf), "AMMO %d  RLD %d %s  CALD %.2f  TREMUR %.4f  MOTOR %.2f  KG %.1f", a.ammo,
                              a.reload_stage, reload_style_name(a.rplan.style), a.cur_slot() ? a.cur_slot()->heat : 0.0,
                              a.phys.tremor_amp, a.phys.fine_motor, w.load_kg(a));
                view.debug_lines.push_back(buf);
                std::snprintf(buf, sizeof(buf), "AUZ %.2f  TIUIT %.2f  SANGE %.2f  POST %d", view.hearing.load, view.hearing.tinnitus,
                              a.health.bleeding, static_cast<int>(a.stance.state));
                view.debug_lines.push_back(buf);
                std::snprintf(buf, sizeof(buf), "POS %.1f %.1f  V %.2f  AIM %.2f  SWAY %.4f", a.x, a.y, a.speed, a.aim, a.sway_yaw);
                view.debug_lines.push_back(buf);
            }
            for (const auto& l : session->debug_lines()) view.debug_lines.push_back(l);
            for (const auto& pe : Profiler::get().entries()) {
                std::snprintf(buf, sizeof(buf), "%-15s %5.2f MS (MAX %5.2f)", pe.name.c_str(), pe.avg_ms, pe.max_ms);
                view.debug_lines.push_back(buf);
            }
            view.debug_lines.push_back("F2 LENTILA F3 VIGNETA F4 ZGOMOT F5 BLUR F6 EXPUNERE F7 COMPRESIE");
            view.debug_lines.push_back("F8 CAMERA CURATA  F9 RESTART  F10 INERTIE  F11 FIZIOLOGIE  M SUNET");
        }
        view.render(w, local, frame_dt, pixels.data());

        SDL_UpdateTexture(texture, nullptr, pixels.data(), o.res_w * 4);
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);
    }

    if (o.smoke) {
        const World& w = session->world();
        std::printf("Smoke test OK: %d cadre, FPS mediu %.0f, audio %s, agenti %zu, EV %.2f\n", smoke_frames, fps,
                    audio_ok ? "activ" : "inactiv", w.agents.size(), view.post.ev);
        for (const auto& pe : Profiler::get().entries())
            std::printf("  %-16s %.2f ms (max %.2f)\n", pe.name.c_str(), pe.avg_ms, pe.max_ms);
    }
    session->shutdown();   // meciul neterminat / focurile din poligon se salveaza
    session.reset();
    if (audio_ok && !o.record_audio.empty()) {
        if (audio.save_recording(o.record_audio)) std::printf("Audio inregistrat: %s\n", o.record_audio.c_str());
    }
    audio.shutdown();
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
