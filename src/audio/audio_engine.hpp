#ifndef BODYCAM_AUDIO_ENGINE_HPP
#define BODYCAM_AUDIO_ENGINE_HPP

#include "../sim/map.hpp"
#include "../sim/spatial_audio_engine.hpp"
#include "dsp.hpp"
#include "breath_synth.hpp"
#include <vector>
#include <string>
#include <mutex>
#include <map>
#include <cstdint>

namespace bodycam {

// Sunetele sunt sintetizate procedural la pornire. Oricare poate fi inlocuit cu un
// fisier WAV din assets/audio/<nume>.wav (ex. heartbeat.wav, heartbeat_fast.wav).
enum class Sfx {
    GunRifle, GunPistol, GunSmg, GunShotgun, GunSuppressed, GunDistant, Crack, Ricochet,
    ImpactConcrete, ImpactWood, ImpactMetal, ImpactDirt, ImpactFlesh, ImpactDrywall, ImpactGlass,
    StepConcrete, StepWood, StepMetal, StepDirt, StepAsphalt,
    MagOut, MagIn, BoltRelease, ShellInsert, Pump, DryFire, Casing, ShellCasing, Switch, Cloth, Land,
    Explosion, GrenadePin, GrenadeBounce, DoorOpen, BodyHit, BodyFall, BreathIn, BreathOut, Pain,
    CanHit, CrateHit, BarrelHit, DroneLoop, MotorLoop, Beep, Static, Tinnitus,
    HeartBeat, HeartBeatFast, HeartBeatHeavy, Wind, RoomTone, LampBuzz, RadioClick, Kick, Locked,
    LightSwitch, Breaker, MagDrop, Bandage, GlassBreak, Fumble, StepGrass, StepFabric, Tap, Keyboard,
    TabletOpen, TabletClose, TabletConfirm, TabletError, TabletNotify, Flashbang, SmokeHiss, MeleeHit, Swing,
    SteelHit, TargetUp, GunSniper, BoltOpen, BoltClose, Selector,
    Count
};

const char* sfx_file_name(Sfx s);

// Parametrii auzului (degradare temporara dupa sunete foarte puternice)
struct HearingState {
    float attenuation = 0.0f;   // 0..1: trece-jos + volum mai mic
    float tinnitus = 0.0f;      // 0..1: tiuit
    float width = 1.0f;         // 1 stereo complet .. 0 mono
    float drive = 1.0f;         // distorsiune usoara
};

// Mixer software: voci spatializate (distanta, ocluzie prin pereti cu filtru
// trece-jos, intarziere de propagare, panoramare), reverberatie dependenta de
// marimea incaperii, magistrala "interna" (corpul propriu) nedegradata de auz,
// fluxuri de voce (radio / proximitate) si captura de microfon.
class AudioEngine {
public:
    ~AudioEngine();
    bool init(const std::string& asset_root = "assets");
    void shutdown();
    bool active() const { return device_ != 0; }

    void set_listener(double x, double y, double yaw, const Map* map);
    void update_environment(const Map& map, double x, double y);

    // internal = sunete ale propriului corp (respiratie, inima, haine) - nu sunt filtrate de auz
    void play2d(Sfx s, float gain, float pan = 0.0f, float pitch = 1.0f, float delay = 0.0f, bool internal = false);
    void play3d(Sfx s, double x, double y, float gain, float pitch = 1.0f, float delay = 0.0f);
    int start_loop(Sfx s, bool internal = false);
    void update_loop(int id, double x, double y, float gain, float pitch, bool positional);
    void stop_loop(int id);
    void stop_all();

    void set_hearing(const HearingState& h);
    void set_breath(const BreathVoiceParams& b);
    void set_ambience(float outdoor, float indoor);

    // --- Voce ---
    // pcm: 16 kHz mono. radio = filtru de statie; altfel spatializat la (x, y)
    void push_voice(int talker, const int16_t* pcm, int n, bool radio, double x, double y, float quality);
    bool start_capture();
    void set_capture(bool on) { capture_on_ = on; }
    size_t drain_capture(std::vector<int16_t>& out);
    float capture_level() const { return capture_level_; }

    float master = 0.8f;
    double room_size = 0.0;
    double wet = 0.1;

    void mix(float* out, int frames);
    // inregistrarea iesirii (diagnostic: niveluri, clipping) -> WAV 16 bit stereo
    void start_recording();
    bool save_recording(const std::string& path);
    void on_capture(const int16_t* in, int n);

private:
    struct Voice {
        const std::vector<float>* data = nullptr;
        double pos = 0.0;
        double rate = 1.0;
        float gl = 0.0f, gr = 0.0f;
        float target_gl = 0.0f, target_gr = 0.0f;
        float lp = 1.0f;
        float lp_l = 0.0f, lp_r = 0.0f;
        float lp2_l = 0.0f, lp2_r = 0.0f;     // al doilea pol: 12 dB/octava prin pereti
        float send = 0.0f;
        int delay = 0;
        bool loop = false;
        bool active = false;
        bool internal = false;
        int id = 0;
    };
    struct VoiceStream {
        std::vector<float> buf;     // 48 kHz
        size_t read = 0;
        bool radio = true;
        float gl = 0.7f, gr = 0.7f, lp = 1.0f;
        float quality = 1.0f;
        int idle = 0;
        bool talking = false;
        float hp_state = 0.0f, lp_state = 0.0f, lp2_state = 0.0f;
    };
    void synthesize();
    void load_overrides(const std::string& root);
    void spatialize(Voice& v, double x, double y, float gain);
    int alloc_voice();
    float breath_sample();
    void reset_dsp_state();      // dupa o valoare invalida: goleste reverberatia si filtrele

    uint32_t device_ = 0;
    uint32_t capture_dev_ = 0;
    int sample_rate_ = 48000;
    std::vector<std::vector<float>> bank_;
    std::vector<Voice> voices_;
    std::mutex mu_;
    int next_id_ = 1;
    double lx_ = 0, ly_ = 0, lyaw_ = 0;
    const Map* map_ = nullptr;
    audio::SpatialAudioPipeline spatial_;
    // auz: trece-jos de ordinul 4 + adancitura la 4 kHz (pierderea temporara de auz)
    HearingState hearing_, hearing_cur_;
    BiquadState hear_l1_, hear_l2_, hear_r1_, hear_r2_, hear_n_l_, hear_n_r_;
    float hear_fc_set_ = -1.0f;
    double tin_phase_ = 0.0, tin_time_ = 0.0;
    // respiratia proprie
    BreathSynth breath_synth_;
    // inregistrare
    bool recording_ = false;
    std::vector<int16_t> rec_;
    // ambianta
    float amb_out_ = 0.0f, amb_in_ = 0.0f, amb_out_cur_ = 0.0f, amb_in_cur_ = 0.0f;
    double amb_pos_w_ = 0.0, amb_pos_r_ = 0.0;
    // reverberatie Schroeder
    std::vector<float> comb_[8];
    int comb_pos_[8] = {0};
    std::vector<float> ap_[4];
    int ap_pos_[4] = {0};
    float comb_lp_[8] = {0};
    // voce
    std::map<int, VoiceStream> streams_;
    BiquadState radio_hp_, radio_lp_;
    std::vector<int16_t> capture_q_;
    bool capture_on_ = false;
    float capture_level_ = 0.0f;
    uint32_t radio_rng_ = 99;
};

}
#endif
