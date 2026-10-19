#ifndef BODYCAM_GAME_VIEW_HPP
#define BODYCAM_GAME_VIEW_HPP

#include "../sim/world.hpp"
#include "../render/frame.hpp"
#include "../render/body_camera.hpp"
#include "../render/viewmodel.hpp"
#include "../render/fx.hpp"
#include "../render/postfx.hpp"
#include "../render/raycaster.hpp"
#include "../render/tablet_ui.hpp"
#include "../audio/audio_engine.hpp"
#include <string>
#include <vector>

namespace bodycam {

// Starea auzului jucatorului local (UAudioStateComponent): expunerea la sunete
// puternice se acumuleaza si se recupereaza treptat.
struct HearingModel {
    double load = 0.0;          // expunere acumulata
    double tinnitus = 0.0;
    void expose(double amount) { load = std::min(2.0, load + amount); if (amount > 0.25) tinnitus = std::max(tinnitus, amount); }
    void update(double dt) {
        double tau = load > 0.6 ? 11.0 : 6.0;     // recuperare mai lenta dupa expunere mare
        load *= std::exp(-dt / tau);
        tinnitus *= std::exp(-dt / 7.0);
    }
};

// Prezentarea pentru un jucator local: consuma evenimentele simularii si
// produce imaginea (camera, efecte, arma, tableta, post-procesare) si sunetul.
// Nu modifica simularea - aceeasi clasa serveste jocul local, host-ul si clientul.
class GameView {
public:
    GameView(int internal_w, int internal_h);

    void reset_for_world(const World& w, int local_agent);
    void on_step(const World& w, const std::vector<Event>& events, int local_agent, double dt);
    void render(const World& w, int local_agent, double frame_dt, uint32_t* pixels);

    // Reculul permanent pe care clientul il adauga privirii (consumat de aplicatie)
    double take_view_punch_pitch() { double v = punch_pitch_; punch_pitch_ = 0; return v; }
    double take_view_punch_yaw() { double v = punch_yaw_; punch_yaw_ = 0; return v; }
    // Interactiunea cu tableta (mouse cand tableta e ridicata)
    void tablet_cursor(double du, double dv);
    void tablet_click(bool right);
    void tablet_wheel(int dir);
    void tablet_page(int index);                    // tastele 1..7 cand tableta e in maini
    bool tablet_active(const World& w, int local) const;
    // Notificari contextuale afisate pe ecranul tabletei (cu sunet din tableta)
    void notify(const World& w, int local, const std::string& text, int kind);
    TabletCommand take_tablet_command() { TabletCommand c = pending_cmd_; pending_cmd_ = TabletCommand{}; return c; }
    // Clientul de retea: focurile proprii sunt prezise local
    bool predicted_local_fire = false;
    void local_fire_effects(const World& w, const Agent& me, const WeaponData& wd, double eye_z, bool suppressed);

    BodyCamera camera;
    ViewmodelState viewmodel;
    SceneFx fx;
    PostFx post;
    HdrFrame frame;
    HdrFrame feed_frame;
    HdrFrame scope_frame;            // imaginea prin luneta (camp ingust, pe directia armei)
    AudioEngine* audio = nullptr;
    HearingModel hearing;
    TabletScreen tablet_screen;
    TabletUiState tablet_ui_state;
    TabletInfo tablet_info;
    UserSettings settings;
    std::string settings_path = "settings.ini";
    std::vector<TabletNotification> notifications;
    // statistici / harti / experimente ale sesiunii (doar citire pentru tableta)
    StatisticsSystem* stats = nullptr;
    const MapCatalog* catalog = nullptr;
    const ExperimentManager* experiments = nullptr;
    int map_id = 0;

    bool tablet_held = false;
    bool inspect_held = false;       // T: verificarea incarcatorului
    bool examine_held = false;       // I: inspectarea completa a armei
    bool show_debug = false;
    bool show_phys_debug = false;     // F11: panoul fiziologiei (valori curente / tinte)
    bool clean_camera = false;     // F8: camera virtuala perfecta (comparatie A/B)
    std::vector<std::string> debug_lines;
    std::string unit_id = "BCM-MINI X81A2";
    std::string prompt;            // indiciu discret de interactiune
    double signal_loss = 0.0;
    uint64_t frame_index = 0;
    CameraPose last_pose;

private:
    struct Delayed { double t; Sfx sfx; double x, y; float gain; bool positional; };
    std::vector<Delayed> delayed_;
    std::vector<AgentFx> agent_fx_;
    std::vector<AgentFx> agent_fx_v_;
    std::vector<Ragdoll> ragdolls_;       // corpurile cazute (prezentare)
    std::vector<uint8_t> last_downed_;
    std::vector<int> last_stage_;
    double punch_pitch_ = 0.0, punch_yaw_ = 0.0;
    double death_fade_ = 0.0;
    double white_flash_ = 0.0;
    double af_ = 0.2, af_v_ = 0.0;        // autofocus (dioptrii) cu "cautare" usoara
    double glitch_ = 0.0;                 // comutarea sursei video
    bool last_body_mode_ = true;
    double reach_timer_ = 0.0;            // mana stanga atinge un obiect (usa, intrerupator)
    double reach_tx_ = 0.0, reach_ty_ = 0.0, reach_tz_ = 1.0;
    double time_ = 0.0;
    uint32_t last_beats_ = 0;
    int drone_loop_ = 0, motor_loop_ = 0, static_loop_ = 0, lamp_loop_ = 0;
    uint32_t rng_ = 99;
    int last_local_ = -1;
    Possession last_possess_ = Possession::Body;
    TabletClick pending_click_;
    TabletCommand pending_cmd_;
    int smoke_loop_ = 0;
    double feed_exposure_ = 1.0;
    double low_bat_note_ = 0.0;
    int last_spares_ = -1;
    int last_capture_q_ = -1;

    float rnd();
    void sfx3d(Sfx s, double x, double y, float gain, float pitch = 1.0f);
    void sfx2d(Sfx s, float gain, float pan = 0.0f, float pitch = 1.0f, float delay = 0.0f, bool internal = false);
    CameraPose current_pose(const World& w, int local_agent) const;
    void spawn_casing_world(const World& w, const Agent& shooter, bool local, const WeaponData* wd);
    void update_audio_state(const World& w, const Agent* me, double dt);
};

}
#endif
