#ifndef BODYCAM_TABLET_UI_HPP
#define BODYCAM_TABLET_UI_HPP

// Interfata tabletei tactice. Se randeaza intr-o textura (TabletScreen = "render
// target") afisata apoi pe ecranul fizic al tabletei din maini - nu peste imagine.
#include "viewmodel.hpp"
#include "frame.hpp"
#include "../sim/world.hpp"
#include "../stats/statistics_system.hpp"
#include "../stats/experiments.hpp"
#include "../game/map_catalog.hpp"
#include <string>
#include <vector>

namespace bodycam {

enum class TabletPage { Home = 0, Loadout, Map, Objectives, Match, Gadgets, Settings, Cameras, Stats, Range, Count };
// Sectiunile paginii STATISTICI
enum class StatsTab { Overview = 0, Heart, Stress, Performance, Maps, Weapons, History, Experiments, Stamina, Count };
const char* tablet_page_name(TabletPage p);

struct TabletNotification {
    std::string text;
    double time = 0.0;
    int kind = 0;               // 0 info, 1 atentie, 2 alarma, 3 confirmare
};

// Setarile jucatorului (pagina SETARI), salvate in settings.ini
struct UserSettings {
    double mouse_sens = 1.0;
    double drone_sens = 1.0;
    double tablet_brightness = 1.0;
    double fov = 100.0;
    double weapon_fov = 74.0;   // campul vizual al armei si al mainilor din prim-plan
    double volume = 0.8;
    bool lens = true, noise = true, motion_blur = true, compression = false;
    bool throw_assist = true;
    bool drone_acro_default = false;
    bool invert_y = false;
    bool load(const std::string& path);
    bool save(const std::string& path) const;
};

struct TabletUiState {
    TabletPage page = TabletPage::Home;
    double zoom = 1.0;
    bool heading_up = false;
    bool follow = true;          // harta urmareste operatorul (altfel deplasare manuala)
    double pan_x = 0.0, pan_y = 0.0;
    int marker_kind = 0;         // 0 marcaj, 1 ping, 2 pericol
    int feed = 0;
    double last_tap_time = -10.0;
    // ciorna de echipament (pagina ECHIPAMENT)
    Loadout draft;
    bool draft_init = false;
    int loadout_sel = 0;         // 0 principala, 1 secundara, 2 corp la corp, 3 aruncabile, 4 gadgeturi
    // STATISTICI
    StatsTab stats_tab = StatsTab::Overview;
    int stats_chart = 0;         // graficul ales (PERFORMANTA / HARTI / ARME)
    bool stats_history = false;  // puls / stres: meciul curent sau ultimul meci salvat
    int map_sel = -1;            // harta selectata (HARTI); -1 = toate
    int cmp_a = -1, cmp_b = -1;  // comparatia a doua harti (id)
    bool compare = false;
    int history_scroll = 0;
    int64_t history_sel = 0;     // meciul inspectat (0 = lista)
    int exp_sel = 0;             // experimentul selectat
    int ctx_filter = 0;          // ARME: 0 tot, 1 meciuri, 2 poligon; ISTORIC: 0 meciuri, 1 sesiuni de poligon
    // POLIGON
    int range_mode = 1, range_dist = 1, range_size = 1;
    // MECI: harta pentru meciul urmator
    int next_map = 0;
};

struct TabletClick {
    bool clicked = false;
    bool right = false;
    double u = 0.5, v = 0.5;
    int wheel = 0;
};

// Comenzi produse de atingerile pe ecran (trimise simularii prin AgentControl)
struct TabletCommand {
    bool place_marker = false;
    double mx = 0.0, my = 0.0;
    int kind = 0;
    bool clear_markers = false;
    bool tapped = false;         // feedback audio (atingere)
    bool confirm = false;        // actiune acceptata
    bool error = false;          // actiune refuzata
    bool loadout_apply = false;
    Loadout loadout;
    int gadget = 0;              // 1 drona, 2 RC: preia controlul
    bool gadget_exit = false;
    bool gadget_recall = false;
    bool gadget_detonate = false;
    bool drone_mode = false;
    bool settings_changed = false;
    // poligon
    int range_mode = -1;
    int range_dist = 1, range_size = 1;
    bool range_stop = false;
    // sesiunea: meci nou pe harta aleasa / experiment controlat
    int new_match_map = 0;
    int run_experiment = -1;
    bool cancel_experiment = false;
};

struct TabletInfo {
    double clock_seconds = 0.0;
    const std::vector<TabletNotification>* notifications = nullptr;
    UserSettings* settings = nullptr;
    const HdrFrame* feed = nullptr;        // camera gadgetului pilotat / a coechipierului
    std::string feed_label;
    double signal_loss = 0.0;
    double feed_exposure = 1.0;            // expunerea automata a camerei gadgetului
    double power = 1.0;                    // ecranul porneste (0..1)
    bool loadout_now = false;              // echipamentul se poate schimba acum (la spawn)
    int camera_count = 0;                  // camere de coechipieri disponibile
    // statistici / harti / experimente (doar citire: interfata nu colecteaza date)
    StatisticsSystem* stats = nullptr;
    const MapCatalog* catalog = nullptr;
    const ExperimentManager* experiments = nullptr;
    int map_id = 0;
};

// Pagina STATISTICI (tablet_stats_ui.cpp)
void tablet_stats_page(TabletScreen& scr, TabletUiState& st, const World& w, int local, const TabletClick& click,
                       double time, const TabletInfo& info, TabletCommand& cmd, int top, int bottom);
// Pagina POLIGON (tablet_stats_ui.cpp)
void tablet_range_page(TabletScreen& scr, TabletUiState& st, const World& w, int local, const TabletClick& click,
                       double time, const TabletInfo& info, TabletCommand& cmd, int top, int bottom);

// Randeaza interfata in scr.rgb si proceseaza atingerile.
TabletCommand tablet_ui(TabletScreen& scr, TabletUiState& st, const World& w, int local, const TabletClick& click,
                        double time, const TabletInfo& info);

}
#endif
