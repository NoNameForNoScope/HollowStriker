#include "audio_engine.hpp"
#include <SDL.h>
#include <cmath>
#include <algorithm>
#include <filesystem>

namespace bodycam {

namespace {
constexpr double kTwoPi = 6.28318530717958647692;

struct NoiseGen {
    uint32_t s;
    explicit NoiseGen(uint32_t seed) : s(seed ? seed : 1) {}
    float next() {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return static_cast<float>(s & 0xFFFFFF) / 8388607.5f - 1.0f;
    }
};

struct OnePole {
    float a = 1.0f, y = 0.0f;
    OnePole(float fc, float sr) { a = 1.0f - std::exp(-static_cast<float>(kTwoPi) * fc / sr); }
    float lp(float x) { y += a * (x - y); return y; }
    float hp(float x) { return x - lp(x); }
};

void normalize(std::vector<float>& b, float peak) {
    float m = 1e-6f;
    for (float v : b) m = std::max(m, std::abs(v));
    for (float& v : b) v = v / m * peak;
}
void soft_clip(std::vector<float>& b, float drive) { for (float& v : b) v = std::tanh(v * drive); }
std::vector<float> make(float seconds, float sr) { return std::vector<float>(static_cast<size_t>(seconds * sr), 0.0f); }

void add_noise_burst(std::vector<float>& b, float sr, uint32_t seed, float t0, float tau, float amp, float lpf, float hpf) {
    NoiseGen n(seed);
    OnePole lp(lpf, sr), hp(hpf, sr);
    size_t s0 = static_cast<size_t>(t0 * sr);
    for (size_t i = s0; i < b.size(); ++i) {
        float t = (i - s0) / sr;
        float env = std::exp(-t / tau);
        if (env < 1e-4f) break;
        float x = lp.lp(n.next());
        if (hpf > 0.0f) x = hp.hp(x);
        b[i] += x * env * amp;
    }
}

void add_sine_decay(std::vector<float>& b, float sr, float t0, float f0, float f1, float fdecay, float tau, float amp) {
    size_t s0 = static_cast<size_t>(t0 * sr);
    double ph = 0.0;
    for (size_t i = s0; i < b.size(); ++i) {
        float t = (i - s0) / sr;
        float env = std::exp(-t / tau);
        if (env < 1e-4f) break;
        float f = fdecay > 1e-6f ? f1 + (f0 - f1) * std::exp(-t / fdecay) : f1;
        ph += kTwoPi * f / sr;
        b[i] += static_cast<float>(std::sin(ph)) * env * amp;
    }
}

void add_band_noise(std::vector<float>& b, float sr, uint32_t seed, float t0, float dur, float fc, float q, float amp, bool smooth_env) {
    NoiseGen n(seed);
    BiquadState bp;
    bp.bandpass(fc, q, sr);
    size_t s0 = static_cast<size_t>(t0 * sr), s1 = std::min(b.size(), static_cast<size_t>((t0 + dur) * sr));
    for (size_t i = s0; i < s1; ++i) {
        float t = (i - s0) / (dur * sr);
        float env = smooth_env ? std::sin(t * 3.14159f) : std::exp(-t * 5.0f);
        b[i] += bp.run(n.next()) * env * amp;
    }
}

void add_clicks(std::vector<float>& b, float sr, uint32_t seed, float t0, float dur, int count, float amp, float hpf) {
    NoiseGen n(seed);
    for (int k = 0; k < count; ++k) {
        float t = t0 + (n.next() * 0.5f + 0.5f) * dur;
        float a = amp * (0.4f + 0.6f * (n.next() * 0.5f + 0.5f)) * std::exp(-(t - t0) / std::max(0.01f, dur));
        add_noise_burst(b, sr, seed + 17 * k + 3, t, 0.0015f, a, 12000.0f, hpf);
    }
}

std::vector<float> gunshot(float sr, uint32_t seed, float crack_tau, float body_lp, float body_tau, float boom_f0, float boom_f1,
                           float boom_tau, float tail_tau, float len) {
    auto b = make(len, sr);
    add_noise_burst(b, sr, seed, 0.0f, crack_tau, 1.0f, 16000.0f, 200.0f);
    add_noise_burst(b, sr, seed + 1, 0.0f, body_tau, 0.9f, body_lp, 0.0f);
    add_sine_decay(b, sr, 0.0f, boom_f0, boom_f1, 0.03f, boom_tau, 0.85f);
    add_noise_burst(b, sr, seed + 2, 0.01f, tail_tau, 0.22f, 600.0f, 0.0f);
    add_noise_burst(b, sr, seed + 3, 0.05f, 0.003f, 0.12f, 9000.0f, 2500.0f);
    soft_clip(b, 1.6f);
    normalize(b, 0.95f);
    return b;
}

// bucla fara pocnitura: sfarsitul se amesteca peste inceput
void make_loopable(std::vector<float>& b, float sr, float fade) {
    size_t n = static_cast<size_t>(fade * sr);
    if (b.size() < 2 * n) return;
    for (size_t i = 0; i < n; ++i) {
        float t = static_cast<float>(i) / n;
        b[i] = b[i] * t + b[b.size() - n + i] * (1.0f - t);
    }
    b.resize(b.size() - n);
}

std::vector<float> heartbeat(float sr, float gap, float lub_f, float dub_f, float amp_dub, float len, float whoosh) {
    auto b = make(len, sr);
    add_sine_decay(b, sr, 0.0f, lub_f * 1.4f, lub_f, 0.02f, 0.07f, 1.0f);
    add_noise_burst(b, sr, 311, 0.0f, 0.05f, 0.35f, 140.0f, 0.0f);
    add_sine_decay(b, sr, gap, dub_f * 1.3f, dub_f, 0.02f, 0.05f, amp_dub);
    add_noise_burst(b, sr, 312, gap, 0.035f, 0.25f * amp_dub, 180.0f, 0.0f);
    if (whoosh > 0.0f) add_band_noise(b, sr, 313, 0.02f, gap + 0.1f, 220.0f, 0.6f, whoosh, true);
    normalize(b, 0.9f);
    return b;
}
}

const char* sfx_file_name(Sfx s) {
    static const char* names[] = {
        "gun_rifle", "gun_pistol", "gun_smg", "gun_shotgun", "gun_suppressed", "gun_distant", "crack", "ricochet",
        "impact_concrete", "impact_wood", "impact_metal", "impact_dirt", "impact_flesh", "impact_drywall", "impact_glass",
        "step_concrete", "step_wood", "step_metal", "step_dirt", "step_asphalt",
        "mag_out", "mag_in", "bolt_release", "shell_insert", "pump", "dry_fire", "casing", "shell_casing", "switch", "cloth", "land",
        "explosion", "grenade_pin", "grenade_bounce", "door_open", "body_hit", "body_fall", "breath_in", "breath_out", "pain",
        "can_hit", "crate_hit", "barrel_hit", "drone_loop", "motor_loop", "beep", "static", "tinnitus",
        "heartbeat", "heartbeat_fast", "heartbeat_heavy", "wind", "room_tone", "lamp_buzz", "radio_click", "kick", "locked",
        "light_switch", "breaker", "mag_drop", "bandage", "glass_break", "fumble", "step_grass", "step_fabric", "tap", "keyboard",
        "tablet_open", "tablet_close", "tablet_confirm", "tablet_error", "tablet_notify", "flashbang", "smoke_hiss", "melee_hit", "swing",
        "steel_hit", "target_up", "gun_sniper", "bolt_open", "bolt_close", "selector",
    };
    static_assert(sizeof(names) / sizeof(names[0]) == static_cast<size_t>(Sfx::Count), "lista de nume Sfx");
    return names[static_cast<int>(s)];
}

AudioEngine::~AudioEngine() { shutdown(); }

static void sdl_audio_callback(void* userdata, Uint8* stream, int len) {
    static_cast<AudioEngine*>(userdata)->mix(reinterpret_cast<float*>(stream), len / static_cast<int>(sizeof(float) * 2));
}
static void sdl_capture_callback(void* userdata, Uint8* stream, int len) {
    static_cast<AudioEngine*>(userdata)->on_capture(reinterpret_cast<const int16_t*>(stream), len / 2);
}

bool AudioEngine::init(const std::string& asset_root) {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return false;
    SDL_AudioSpec want{}, have{};
    want.freq = 48000;
    want.format = AUDIO_F32SYS;
    want.channels = 2;
    want.samples = 512;
    want.callback = sdl_audio_callback;
    want.userdata = this;
    device_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (device_ == 0) return false;
    sample_rate_ = have.freq;
    synthesize();
    load_overrides(asset_root);
    voices_.assign(112, Voice{});
    const int comb_len[4] = {1557, 1617, 1491, 1422};
    for (int i = 0; i < 8; ++i) {
        comb_[i].assign(static_cast<size_t>((comb_len[i % 4] + (i >= 4 ? 23 : 0)) * sample_rate_ / 44100), 0.0f);
        comb_pos_[i] = 0;
    }
    const int ap_len[2] = {225, 556};
    for (int i = 0; i < 4; ++i) {
        ap_[i].assign(static_cast<size_t>((ap_len[i % 2] + (i >= 2 ? 17 : 0)) * sample_rate_ / 44100), 0.0f);
        ap_pos_[i] = 0;
    }
    float sr = static_cast<float>(sample_rate_);
    breath_synth_.init(sr);
    radio_hp_.highpass(320.0f, 0.7f, sr);
    radio_lp_.lowpass(3100.0f, 0.8f, sr);
    SDL_PauseAudioDevice(device_, 0);
    return true;
}

bool AudioEngine::start_capture() {
    if (capture_dev_ != 0) return true;
    SDL_AudioSpec want{}, have{};
    want.freq = 16000;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 320;
    want.callback = sdl_capture_callback;
    want.userdata = this;
    capture_dev_ = SDL_OpenAudioDevice(nullptr, 1, &want, &have, 0);
    if (capture_dev_ == 0) return false;
    SDL_PauseAudioDevice(capture_dev_, 0);
    return true;
}

void AudioEngine::on_capture(const int16_t* in, int n) {
    std::lock_guard<std::mutex> lock(mu_);
    float peak = 0.0f;
    for (int i = 0; i < n; ++i) peak = std::max(peak, std::abs(in[i] / 32768.0f));
    capture_level_ = capture_level_ * 0.8f + peak * 0.2f;
    if (!capture_on_) return;
    capture_q_.insert(capture_q_.end(), in, in + n);
    if (capture_q_.size() > 16000) capture_q_.erase(capture_q_.begin(), capture_q_.begin() + (capture_q_.size() - 16000));
}

size_t AudioEngine::drain_capture(std::vector<int16_t>& out) {
    std::lock_guard<std::mutex> lock(mu_);
    out.insert(out.end(), capture_q_.begin(), capture_q_.end());
    size_t n = capture_q_.size();
    capture_q_.clear();
    return n;
}

void AudioEngine::reset_dsp_state() {
    for (auto& c : comb_) std::fill(c.begin(), c.end(), 0.0f);
    for (auto& a : ap_) std::fill(a.begin(), a.end(), 0.0f);
    for (float& c : comb_lp_) c = 0.0f;
    hear_l1_.reset(); hear_l2_.reset(); hear_r1_.reset(); hear_r2_.reset(); hear_n_l_.reset(); hear_n_r_.reset();
    radio_hp_.reset(); radio_lp_.reset();
    for (auto& v : voices_) {
        if (!std::isfinite(v.lp_l + v.lp_r + v.lp2_l + v.lp2_r + v.gl + v.gr)) v.active = false;
        v.lp_l = v.lp_r = v.lp2_l = v.lp2_r = 0.0f;
    }
}

void AudioEngine::start_recording() {
    std::lock_guard<std::mutex> lock(mu_);
    rec_.clear();
    rec_.reserve(static_cast<size_t>(sample_rate_) * 2 * 60);
    recording_ = true;
}

bool AudioEngine::save_recording(const std::string& path) {
    std::vector<int16_t> data;
    int sr = 0;
    {
        std::lock_guard<std::mutex> lock(mu_);
        recording_ = false;
        data.swap(rec_);
        sr = sample_rate_;
    }
    FILE* f = nullptr;
#ifdef _MSC_VER
    if (fopen_s(&f, path.c_str(), "wb") != 0) f = nullptr;
#else
    f = std::fopen(path.c_str(), "wb");
#endif
    if (!f) return false;
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    uint32_t bytes = static_cast<uint32_t>(data.size() * 2);
    std::fwrite("RIFF", 1, 4, f); u32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
    u32(16); u16(1); u16(2); u32(static_cast<uint32_t>(sr)); u32(static_cast<uint32_t>(sr) * 4); u16(4); u16(16);
    std::fwrite("data", 1, 4, f); u32(bytes);
    std::fwrite(data.data(), 2, data.size(), f);
    std::fclose(f);
    return true;
}

void AudioEngine::shutdown() {
    if (capture_dev_ != 0) { SDL_CloseAudioDevice(capture_dev_); capture_dev_ = 0; }
    if (device_ != 0) { SDL_CloseAudioDevice(device_); device_ = 0; }
}

void AudioEngine::load_overrides(const std::string& root) {
    // Oricare sunet poate fi inlocuit cu un WAV (convertit la mono float la rata dispozitivului)
    namespace fs = std::filesystem;
    for (int i = 0; i < static_cast<int>(Sfx::Count); ++i) {
        std::string path = root + "/audio/" + sfx_file_name(static_cast<Sfx>(i)) + ".wav";
        std::error_code ec;
        if (!fs::exists(path, ec)) continue;
        SDL_AudioSpec spec;
        Uint8* buf = nullptr;
        Uint32 len = 0;
        if (!SDL_LoadWAV(path.c_str(), &spec, &buf, &len)) continue;
        SDL_AudioCVT cvt;
        if (SDL_BuildAudioCVT(&cvt, spec.format, spec.channels, spec.freq, AUDIO_F32SYS, 1, sample_rate_) < 0) { SDL_FreeWAV(buf); continue; }
        cvt.len = static_cast<int>(len);
        std::vector<Uint8> work(static_cast<size_t>(len) * cvt.len_mult);
        std::copy(buf, buf + len, work.begin());
        cvt.buf = work.data();
        SDL_FreeWAV(buf);
        if (SDL_ConvertAudio(&cvt) != 0) continue;
        const float* f = reinterpret_cast<const float*>(work.data());
        bank_[i].assign(f, f + cvt.len_cvt / sizeof(float));
    }
}

void AudioEngine::synthesize() {
    const float sr = static_cast<float>(sample_rate_);
    bank_.assign(static_cast<size_t>(Sfx::Count), {});
    auto set = [&](Sfx s, std::vector<float> b) {
        for (float& x : b) if (!std::isfinite(x)) x = 0.0f;
        bank_[static_cast<size_t>(s)] = std::move(b);
    };

    set(Sfx::GunRifle, gunshot(sr, 11, 0.004f, 1600.0f, 0.045f, 140.0f, 55.0f, 0.12f, 0.35f, 1.2f));
    set(Sfx::GunPistol, gunshot(sr, 23, 0.003f, 2400.0f, 0.03f, 210.0f, 90.0f, 0.07f, 0.25f, 0.9f));
    set(Sfx::GunSmg, gunshot(sr, 31, 0.0035f, 2000.0f, 0.035f, 180.0f, 75.0f, 0.08f, 0.28f, 0.9f));
    set(Sfx::GunShotgun, gunshot(sr, 41, 0.006f, 900.0f, 0.08f, 120.0f, 40.0f, 0.22f, 0.5f, 1.6f));
    {
        auto b = make(0.35f, sr);
        add_band_noise(b, sr, 51, 0.0f, 0.06f, 1300.0f, 1.2f, 0.9f, false);
        add_noise_burst(b, sr, 52, 0.0f, 0.006f, 0.6f, 10000.0f, 2000.0f);
        add_noise_burst(b, sr, 53, 0.035f, 0.005f, 0.5f, 10000.0f, 2500.0f);
        add_sine_decay(b, sr, 0.035f, 2100.0f, 1900.0f, 0.02f, 0.03f, 0.15f);
        normalize(b, 0.5f);
        set(Sfx::GunSuppressed, std::move(b));
    }
    {
        auto b = make(2.0f, sr);
        add_noise_burst(b, sr, 61, 0.0f, 0.15f, 1.0f, 420.0f, 0.0f);
        add_sine_decay(b, sr, 0.0f, 70.0f, 38.0f, 0.1f, 0.3f, 0.7f);
        add_noise_burst(b, sr, 62, 0.32f, 0.35f, 0.35f, 300.0f, 0.0f);
        add_noise_burst(b, sr, 63, 0.75f, 0.5f, 0.15f, 250.0f, 0.0f);
        normalize(b, 0.9f);
        set(Sfx::GunDistant, std::move(b));
    }
    {
        auto b = make(0.12f, sr);
        size_t n = static_cast<size_t>(0.0015f * sr);
        for (size_t i = 0; i < n; ++i) b[i] = (i < n / 2) ? 1.0f : -1.0f;
        add_noise_burst(b, sr, 71, 0.0f, 0.005f, 0.6f, 14000.0f, 3500.0f);
        normalize(b, 0.9f);
        set(Sfx::Crack, std::move(b));
    }
    {
        auto b = make(0.7f, sr);
        double ph = 0.0;
        for (size_t i = 0; i < b.size(); ++i) {
            float t = i / sr;
            float f = 1300.0f + 2200.0f * std::exp(-t / 0.18f) + 80.0f * std::sin(t * 60.0f);
            ph += kTwoPi * f / sr;
            b[i] = static_cast<float>(std::sin(ph)) * std::exp(-t / 0.22f) * 0.6f;
        }
        add_noise_burst(b, sr, 81, 0.0f, 0.004f, 0.8f, 12000.0f, 1500.0f);
        normalize(b, 0.7f);
        set(Sfx::Ricochet, std::move(b));
    }
    { auto b = make(0.4f, sr); add_noise_burst(b, sr, 91, 0.0f, 0.008f, 1.0f, 9000.0f, 900.0f); add_clicks(b, sr, 92, 0.01f, 0.18f, 14, 0.3f, 1500.0f); normalize(b, 0.8f); set(Sfx::ImpactConcrete, std::move(b)); }
    { auto b = make(0.3f, sr); add_sine_decay(b, sr, 0.0f, 260.0f, 190.0f, 0.02f, 0.045f, 0.9f); add_noise_burst(b, sr, 93, 0.0f, 0.01f, 0.8f, 3000.0f, 200.0f); add_clicks(b, sr, 94, 0.005f, 0.06f, 5, 0.2f, 800.0f); normalize(b, 0.8f); set(Sfx::ImpactWood, std::move(b)); }
    {
        auto b = make(0.8f, sr);
        add_noise_burst(b, sr, 95, 0.0f, 0.003f, 1.0f, 14000.0f, 2000.0f);
        add_sine_decay(b, sr, 0.0f, 2450.0f, 2400.0f, 0.1f, 0.22f, 0.5f);
        add_sine_decay(b, sr, 0.0f, 3660.0f, 3650.0f, 0.1f, 0.16f, 0.35f);
        add_sine_decay(b, sr, 0.0f, 5120.0f, 5100.0f, 0.1f, 0.1f, 0.25f);
        normalize(b, 0.75f);
        set(Sfx::ImpactMetal, std::move(b));
    }
    { auto b = make(0.3f, sr); add_noise_burst(b, sr, 96, 0.0f, 0.03f, 1.0f, 900.0f, 0.0f); add_clicks(b, sr, 97, 0.0f, 0.12f, 10, 0.15f, 1200.0f); normalize(b, 0.7f); set(Sfx::ImpactDirt, std::move(b)); }
    {
        auto b = make(0.25f, sr);
        add_noise_burst(b, sr, 98, 0.0f, 0.025f, 1.0f, 450.0f, 0.0f);
        add_sine_decay(b, sr, 0.0f, 110.0f, 70.0f, 0.02f, 0.05f, 0.8f);
        add_band_noise(b, sr, 99, 0.0f, 0.02f, 1100.0f, 1.0f, 0.4f, false);
        normalize(b, 0.75f);
        set(Sfx::ImpactFlesh, b);
        normalize(b, 0.8f);
        set(Sfx::BodyHit, std::move(b));
    }
    { auto b = make(0.35f, sr); add_band_noise(b, sr, 101, 0.0f, 0.03f, 1500.0f, 0.8f, 1.0f, false); add_clicks(b, sr, 102, 0.01f, 0.2f, 18, 0.2f, 2000.0f); normalize(b, 0.7f); set(Sfx::ImpactDrywall, std::move(b)); }
    { auto b = make(0.6f, sr); add_clicks(b, sr, 103, 0.0f, 0.3f, 30, 0.5f, 4000.0f); add_sine_decay(b, sr, 0.0f, 6200.0f, 6000.0f, 0.1f, 0.12f, 0.2f); normalize(b, 0.7f); set(Sfx::ImpactGlass, std::move(b)); }
    {
        auto b = make(0.2f, sr);
        add_sine_decay(b, sr, 0.0f, 110.0f, 80.0f, 0.01f, 0.02f, 0.6f);
        add_band_noise(b, sr, 111, 0.0f, 0.03f, 1900.0f, 1.0f, 0.8f, false);
        add_band_noise(b, sr, 112, 0.03f, 0.06f, 3500.0f, 0.8f, 0.2f, true);
        normalize(b, 0.6f);
        set(Sfx::StepConcrete, b);
        for (float& v : b) v *= 0.7f;
        set(Sfx::StepAsphalt, std::move(b));
    }
    { auto b = make(0.25f, sr); add_sine_decay(b, sr, 0.0f, 150.0f, 130.0f, 0.02f, 0.06f, 0.7f); add_sine_decay(b, sr, 0.0f, 330.0f, 320.0f, 0.02f, 0.04f, 0.3f); add_noise_burst(b, sr, 113, 0.0f, 0.01f, 0.4f, 2500.0f, 300.0f); normalize(b, 0.6f); set(Sfx::StepWood, std::move(b)); }
    { auto b = make(0.4f, sr); add_sine_decay(b, sr, 0.0f, 710.0f, 700.0f, 0.1f, 0.12f, 0.4f); add_sine_decay(b, sr, 0.0f, 1460.0f, 1450.0f, 0.1f, 0.09f, 0.3f); add_noise_burst(b, sr, 114, 0.0f, 0.008f, 0.6f, 6000.0f, 500.0f); normalize(b, 0.6f); set(Sfx::StepMetal, std::move(b)); }
    { auto b = make(0.25f, sr); add_clicks(b, sr, 115, 0.0f, 0.13f, 40, 0.5f, 1500.0f); add_noise_burst(b, sr, 116, 0.0f, 0.02f, 0.4f, 700.0f, 0.0f); normalize(b, 0.55f); set(Sfx::StepDirt, std::move(b)); }
    { auto b = make(0.25f, sr); add_band_noise(b, sr, 117, 0.0f, 0.16f, 3200.0f, 0.6f, 1.0f, true); add_clicks(b, sr, 118, 0.0f, 0.1f, 12, 0.15f, 3000.0f); normalize(b, 0.4f); set(Sfx::StepGrass, std::move(b)); }
    { auto b = make(0.15f, sr); add_noise_burst(b, sr, 119, 0.0f, 0.03f, 1.0f, 380.0f, 0.0f); add_sine_decay(b, sr, 0.0f, 90.0f, 70.0f, 0.02f, 0.03f, 0.4f); normalize(b, 0.4f); set(Sfx::StepFabric, std::move(b)); }
    { auto b = make(0.25f, sr); add_noise_burst(b, sr, 121, 0.0f, 0.004f, 0.9f, 10000.0f, 2500.0f); add_band_noise(b, sr, 122, 0.01f, 0.08f, 2200.0f, 1.0f, 0.35f, true); normalize(b, 0.5f); set(Sfx::MagOut, std::move(b)); }
    { auto b = make(0.3f, sr); add_band_noise(b, sr, 123, 0.0f, 0.06f, 2000.0f, 1.0f, 0.3f, true); add_noise_burst(b, sr, 124, 0.06f, 0.005f, 1.0f, 9000.0f, 1500.0f); add_sine_decay(b, sr, 0.06f, 1500.0f, 1400.0f, 0.02f, 0.03f, 0.3f); normalize(b, 0.6f); set(Sfx::MagIn, std::move(b)); }
    { auto b = make(0.35f, sr); add_noise_burst(b, sr, 125, 0.0f, 0.007f, 1.0f, 12000.0f, 1500.0f); add_sine_decay(b, sr, 0.0f, 1850.0f, 1800.0f, 0.05f, 0.05f, 0.5f); add_sine_decay(b, sr, 0.0f, 2750.0f, 2700.0f, 0.05f, 0.04f, 0.3f); normalize(b, 0.7f); set(Sfx::BoltRelease, std::move(b)); }
    { auto b = make(0.25f, sr); add_band_noise(b, sr, 126, 0.0f, 0.05f, 900.0f, 1.2f, 0.5f, true); add_noise_burst(b, sr, 127, 0.05f, 0.004f, 0.7f, 8000.0f, 1500.0f); normalize(b, 0.5f); set(Sfx::ShellInsert, std::move(b)); }
    { auto b = make(0.45f, sr); add_noise_burst(b, sr, 128, 0.0f, 0.008f, 1.0f, 10000.0f, 1200.0f); add_band_noise(b, sr, 129, 0.02f, 0.14f, 1400.0f, 1.0f, 0.35f, true); add_noise_burst(b, sr, 130, 0.18f, 0.01f, 1.0f, 10000.0f, 1000.0f); add_sine_decay(b, sr, 0.18f, 1300.0f, 1200.0f, 0.05f, 0.06f, 0.3f); normalize(b, 0.7f); set(Sfx::Pump, std::move(b)); }
    { auto b = make(0.06f, sr); add_noise_burst(b, sr, 131, 0.0f, 0.002f, 1.0f, 12000.0f, 3500.0f); normalize(b, 0.35f); set(Sfx::DryFire, std::move(b)); }
    {
        auto b = make(0.4f, sr);
        const float times[3] = {0.0f, 0.11f, 0.19f};
        for (int k = 0; k < 3; ++k) {
            float a = 1.0f / (1 + k);
            add_sine_decay(b, sr, times[k], 4200.0f + 300.0f * k, 4200.0f, 0.1f, 0.05f, 0.5f * a);
            add_sine_decay(b, sr, times[k], 6100.0f, 6050.0f, 0.1f, 0.035f, 0.3f * a);
        }
        normalize(b, 0.35f);
        set(Sfx::Casing, std::move(b));
    }
    { auto b = make(0.3f, sr); add_sine_decay(b, sr, 0.0f, 1250.0f, 1200.0f, 0.1f, 0.03f, 0.5f); add_sine_decay(b, sr, 0.09f, 1900.0f, 1850.0f, 0.1f, 0.02f, 0.25f); normalize(b, 0.35f); set(Sfx::ShellCasing, std::move(b)); }
    { auto b = make(0.4f, sr); add_band_noise(b, sr, 141, 0.0f, 0.35f, 1300.0f, 0.6f, 0.4f, true); add_noise_burst(b, sr, 142, 0.25f, 0.004f, 0.4f, 9000.0f, 2000.0f); normalize(b, 0.35f); set(Sfx::Switch, std::move(b)); }
    { auto b = make(0.35f, sr); add_band_noise(b, sr, 143, 0.0f, 0.3f, 1100.0f, 0.5f, 0.4f, true); normalize(b, 0.25f); set(Sfx::Cloth, std::move(b)); }
    { auto b = make(0.4f, sr); add_sine_decay(b, sr, 0.0f, 80.0f, 55.0f, 0.02f, 0.07f, 0.8f); add_band_noise(b, sr, 144, 0.0f, 0.2f, 1000.0f, 0.6f, 0.3f, true); add_clicks(b, sr, 145, 0.02f, 0.12f, 6, 0.2f, 2000.0f); normalize(b, 0.6f); set(Sfx::Land, std::move(b)); }
    {
        auto b = make(3.0f, sr);
        add_noise_burst(b, sr, 151, 0.0f, 0.012f, 1.0f, 15000.0f, 100.0f);
        add_sine_decay(b, sr, 0.0f, 65.0f, 28.0f, 0.15f, 0.55f, 1.0f);
        add_noise_burst(b, sr, 152, 0.0f, 0.8f, 0.6f, 220.0f, 0.0f);
        add_clicks(b, sr, 153, 0.1f, 1.2f, 40, 0.15f, 1500.0f);
        soft_clip(b, 2.0f);
        normalize(b, 1.0f);
        set(Sfx::Explosion, std::move(b));
    }
    { auto b = make(0.4f, sr); add_noise_burst(b, sr, 161, 0.0f, 0.003f, 0.6f, 10000.0f, 2000.0f); add_sine_decay(b, sr, 0.0f, 2850.0f, 2800.0f, 0.1f, 0.12f, 0.4f); normalize(b, 0.4f); set(Sfx::GrenadePin, std::move(b)); }
    { auto b = make(0.2f, sr); add_sine_decay(b, sr, 0.0f, 950.0f, 900.0f, 0.05f, 0.03f, 0.6f); add_noise_burst(b, sr, 162, 0.0f, 0.006f, 0.5f, 6000.0f, 500.0f); normalize(b, 0.5f); set(Sfx::GrenadeBounce, std::move(b)); }
    {
        auto b = make(0.9f, sr);
        NoiseGen n(171);
        double ph = 0.0;
        for (size_t i = 0; i < static_cast<size_t>(0.7f * sr); ++i) {
            float t = i / sr;
            float f = 320.0f + 160.0f * std::sin(t * 7.0f) + 40.0f * n.next();
            ph += kTwoPi * f / sr;
            float saw = static_cast<float>(std::fmod(ph, kTwoPi) / kTwoPi) * 2.0f - 1.0f;
            float grain = (n.next() > 0.6f) ? 1.0f : 0.3f;
            b[i] += saw * grain * std::sin(t / 0.7f * 3.14159f) * 0.3f;
        }
        add_noise_burst(b, sr, 172, 0.7f, 0.006f, 0.7f, 8000.0f, 800.0f);
        BiquadState bp;
        bp.bandpass(900.0f, 0.7f, sr);
        for (float& v : b) v = bp.run(v) * 2.0f + v * 0.3f;
        normalize(b, 0.5f);
        set(Sfx::DoorOpen, std::move(b));
    }
    { auto b = make(0.8f, sr); add_sine_decay(b, sr, 0.0f, 70.0f, 50.0f, 0.03f, 0.1f, 0.9f); add_band_noise(b, sr, 181, 0.0f, 0.3f, 900.0f, 0.6f, 0.4f, true); add_clicks(b, sr, 182, 0.05f, 0.4f, 12, 0.25f, 1500.0f); normalize(b, 0.7f); set(Sfx::BodyFall, std::move(b)); }
    {
        auto b = make(0.7f, sr); add_band_noise(b, sr, 191, 0.0f, 0.65f, 950.0f, 0.8f, 1.0f, true); normalize(b, 0.3f); set(Sfx::BreathIn, std::move(b));
        auto c = make(0.9f, sr); add_band_noise(c, sr, 192, 0.0f, 0.85f, 620.0f, 0.7f, 1.0f, true); normalize(c, 0.3f); set(Sfx::BreathOut, std::move(c));
    }
    {
        auto b = make(0.4f, sr);
        double ph = 0.0;
        BiquadState f1, f2;
        f1.bandpass(650.0f, 3.0f, sr);
        f2.bandpass(1100.0f, 4.0f, sr);
        for (size_t i = 0; i < b.size(); ++i) {
            float t = i / sr;
            ph += kTwoPi * (120.0f - 30.0f * t) / sr;
            float saw = static_cast<float>(std::fmod(ph, kTwoPi) / kTwoPi) * 2.0f - 1.0f;
            float env = std::min(1.0f, t / 0.03f) * std::exp(-t / 0.15f);
            b[i] = (f1.run(saw) + f2.run(saw) * 0.6f) * env;
        }
        normalize(b, 0.5f);
        set(Sfx::Pain, std::move(b));
    }
    {
        auto b = make(0.5f, sr);
        const float times[4] = {0.0f, 0.09f, 0.16f, 0.21f};
        for (int k = 0; k < 4; ++k) {
            add_sine_decay(b, sr, times[k], 1850.0f + 90.0f * k, 1800.0f, 0.05f, 0.05f, 0.5f / (1 + k));
            add_sine_decay(b, sr, times[k], 2650.0f, 2600.0f, 0.05f, 0.03f, 0.3f / (1 + k));
        }
        normalize(b, 0.45f);
        set(Sfx::CanHit, std::move(b));
    }
    { auto b = make(0.3f, sr); add_sine_decay(b, sr, 0.0f, 200.0f, 170.0f, 0.03f, 0.06f, 0.8f); add_noise_burst(b, sr, 201, 0.0f, 0.012f, 0.6f, 2500.0f, 150.0f); normalize(b, 0.6f); set(Sfx::CrateHit, std::move(b)); }
    { auto b = make(1.0f, sr); add_sine_decay(b, sr, 0.0f, 260.0f, 250.0f, 0.1f, 0.5f, 0.6f); add_sine_decay(b, sr, 0.0f, 430.0f, 420.0f, 0.1f, 0.35f, 0.4f); add_noise_burst(b, sr, 202, 0.0f, 0.006f, 0.5f, 5000.0f, 300.0f); normalize(b, 0.6f); set(Sfx::BarrelHit, std::move(b)); }
    {
        auto b = make(1.0f, sr);
        NoiseGen n(211);
        OnePole lp(3000.0f, sr);
        for (size_t i = 0; i < b.size(); ++i) {
            double t = i / sr;
            double s = 0.0;
            for (int h = 1; h <= 6; ++h) s += std::sin(kTwoPi * 180.0 * h * t) / h;
            b[i] = static_cast<float>(s * (0.7 + 0.3 * std::sin(kTwoPi * 30.0 * t)) * 0.4) + lp.lp(n.next()) * 0.25f;
        }
        normalize(b, 0.6f);
        set(Sfx::DroneLoop, std::move(b));
    }
    {
        auto b = make(1.0f, sr);
        for (size_t i = 0; i < b.size(); ++i) {
            double t = i / sr;
            b[i] = static_cast<float>(std::sin(kTwoPi * 95.0 * t) * 0.5 + std::sin(kTwoPi * 760.0 * t) * 0.15 + std::sin(kTwoPi * 1520.0 * t) * 0.05);
        }
        normalize(b, 0.5f);
        set(Sfx::MotorLoop, std::move(b));
    }
    {
        auto b = make(0.15f, sr);
        for (size_t i = 0; i < b.size(); ++i) b[i] = static_cast<float>(std::sin(kTwoPi * 1000.0 * i / sr)) * 0.3f;
        set(Sfx::Beep, std::move(b));
        auto s = make(1.0f, sr);
        NoiseGen n(221);
        OnePole lp(6000.0f, sr);
        for (float& v : s) v = lp.lp(n.next()) * 0.4f;
        set(Sfx::Static, std::move(s));
        auto tn = make(4.0f, sr);
        for (size_t i = 0; i < tn.size(); ++i) {
            float t = i / sr;
            tn[i] = static_cast<float>(std::sin(kTwoPi * 3800.0 * t)) * std::exp(-t / 1.6f) * std::min(1.0f, t / 0.05f) * 0.25f;
        }
        set(Sfx::Tinnitus, std::move(tn));
    }
    // --- Corpul propriu: bataile inimii (normal / rapid / greu) ---
    set(Sfx::HeartBeat, heartbeat(sr, 0.24f, 48.0f, 58.0f, 0.7f, 0.6f, 0.0f));
    set(Sfx::HeartBeatFast, heartbeat(sr, 0.16f, 54.0f, 66.0f, 0.85f, 0.45f, 0.0f));
    set(Sfx::HeartBeatHeavy, heartbeat(sr, 0.2f, 44.0f, 52.0f, 0.95f, 0.65f, 0.35f));
    // --- Ambianta (bucle) ---
    {
        auto b = make(6.0f, sr);
        NoiseGen n(231);
        OnePole lp(500.0f, sr), lp2(1800.0f, sr);
        for (size_t i = 0; i < b.size(); ++i) {
            double t = i / sr;
            double gust = 0.55 + 0.45 * std::sin(t * 0.9) * std::sin(t * 0.37 + 1.0);
            float x = n.next();
            b[i] = static_cast<float>((lp.lp(x) * 0.8 + (lp2.lp(x) - lp.y) * 0.15 * gust) * gust);
        }
        make_loopable(b, sr, 0.5f);
        normalize(b, 0.35f);
        set(Sfx::Wind, std::move(b));
    }
    {
        auto b = make(2.0f, sr);
        NoiseGen n(232);
        OnePole lp(300.0f, sr);
        for (size_t i = 0; i < b.size(); ++i) {
            double t = i / sr;
            b[i] = static_cast<float>(std::sin(kTwoPi * 50.0 * t) * 0.3 + std::sin(kTwoPi * 100.0 * t) * 0.12) + lp.lp(n.next()) * 0.5f;
        }
        normalize(b, 0.2f);
        set(Sfx::RoomTone, std::move(b));
    }
    {
        auto b = make(1.0f, sr);
        NoiseGen n(233);
        for (size_t i = 0; i < b.size(); ++i) {
            double t = i / sr;
            double s = 0.0;
            for (int h = 1; h <= 8; ++h) s += std::sin(kTwoPi * 120.0 * h * t) / (h * h);
            b[i] = static_cast<float>(s * 0.5) + (n.next() > 0.995f ? n.next() * 0.3f : 0.0f);
        }
        normalize(b, 0.3f);
        set(Sfx::LampBuzz, std::move(b));
    }
    // --- Interactiuni ---
    { auto b = make(0.25f, sr); add_band_noise(b, sr, 241, 0.0f, 0.18f, 2500.0f, 0.5f, 0.6f, false); add_noise_burst(b, sr, 242, 0.0f, 0.003f, 0.8f, 9000.0f, 2000.0f); normalize(b, 0.5f); set(Sfx::RadioClick, std::move(b)); }
    { auto b = make(0.6f, sr); add_sine_decay(b, sr, 0.0f, 90.0f, 60.0f, 0.03f, 0.12f, 1.0f); add_noise_burst(b, sr, 243, 0.0f, 0.03f, 0.9f, 3500.0f, 100.0f); add_clicks(b, sr, 244, 0.02f, 0.25f, 10, 0.3f, 900.0f); soft_clip(b, 1.5f); normalize(b, 0.9f); set(Sfx::Kick, std::move(b)); }
    { auto b = make(0.35f, sr); add_clicks(b, sr, 245, 0.0f, 0.25f, 9, 0.7f, 2500.0f); add_sine_decay(b, sr, 0.05f, 2300.0f, 2200.0f, 0.1f, 0.04f, 0.2f); normalize(b, 0.5f); set(Sfx::Locked, std::move(b)); }
    { auto b = make(0.1f, sr); add_noise_burst(b, sr, 246, 0.0f, 0.003f, 1.0f, 9000.0f, 1500.0f); add_sine_decay(b, sr, 0.0f, 1800.0f, 1700.0f, 0.02f, 0.01f, 0.3f); normalize(b, 0.5f); set(Sfx::LightSwitch, std::move(b)); }
    {
        auto b = make(1.4f, sr);
        add_sine_decay(b, sr, 0.0f, 120.0f, 80.0f, 0.03f, 0.08f, 1.0f);
        add_noise_burst(b, sr, 247, 0.0f, 0.01f, 0.8f, 8000.0f, 500.0f);
        for (size_t i = 0; i < b.size(); ++i) {
            double t = i / sr;
            b[i] += static_cast<float>(std::sin(kTwoPi * 100.0 * t) * std::exp(-t / 0.5) * 0.3 * (0.7 + 0.3 * std::sin(t * 80.0)));
        }
        normalize(b, 0.7f);
        set(Sfx::Breaker, std::move(b));
    }
    { auto b = make(0.4f, sr); add_noise_burst(b, sr, 248, 0.0f, 0.01f, 0.8f, 4000.0f, 300.0f); add_sine_decay(b, sr, 0.0f, 900.0f, 850.0f, 0.03f, 0.04f, 0.4f); add_clicks(b, sr, 249, 0.03f, 0.2f, 6, 0.3f, 1500.0f); normalize(b, 0.55f); set(Sfx::MagDrop, std::move(b)); }
    {
        auto b = make(0.6f, sr);
        NoiseGen n(250);
        BiquadState bp;
        bp.bandpass(2600.0f, 0.8f, sr);
        for (size_t i = 0; i < b.size(); ++i) {
            double t = i / sr;
            float rough = (n.next() > 0.3f) ? 1.0f : 0.2f;
            b[i] = bp.run(n.next()) * rough * static_cast<float>(std::sin(t / 0.6 * 3.14159));
        }
        normalize(b, 0.4f);
        set(Sfx::Bandage, std::move(b));
    }
    { auto b = make(1.0f, sr); add_noise_burst(b, sr, 251, 0.0f, 0.01f, 1.0f, 15000.0f, 2000.0f); add_clicks(b, sr, 252, 0.0f, 0.7f, 70, 0.6f, 3500.0f); add_sine_decay(b, sr, 0.0f, 5200.0f, 5000.0f, 0.1f, 0.2f, 0.25f); normalize(b, 0.85f); set(Sfx::GlassBreak, std::move(b)); }
    { auto b = make(0.3f, sr); add_clicks(b, sr, 253, 0.0f, 0.2f, 8, 0.6f, 1500.0f); add_sine_decay(b, sr, 0.02f, 1400.0f, 1300.0f, 0.03f, 0.03f, 0.3f); normalize(b, 0.5f); set(Sfx::Fumble, std::move(b)); }
    { auto b = make(0.05f, sr); add_noise_burst(b, sr, 254, 0.0f, 0.002f, 1.0f, 12000.0f, 3000.0f); normalize(b, 0.2f); set(Sfx::Tap, std::move(b)); }
    { auto b = make(1.0f, sr); add_clicks(b, sr, 255, 0.0f, 1.0f, 14, 0.6f, 2500.0f); normalize(b, 0.3f); set(Sfx::Keyboard, std::move(b)); }
    // --- Tableta: carcasa scoasa din buzunar, carligul curelei, ecranul care se aprinde ---
    { auto b = make(0.6f, sr); add_band_noise(b, sr, 256, 0.0f, 0.25f, 1800.0f, 0.7f, 0.5f, true); add_clicks(b, sr, 257, 0.18f, 0.08f, 3, 0.5f, 2500.0f);
      add_sine_decay(b, sr, 0.32f, 1320.0f, 1320.0f, 0.0f, 0.05f, 0.12f); add_sine_decay(b, sr, 0.38f, 1760.0f, 1760.0f, 0.0f, 0.06f, 0.1f); normalize(b, 0.45f); set(Sfx::TabletOpen, std::move(b)); }
    { auto b = make(0.5f, sr); add_sine_decay(b, sr, 0.0f, 1760.0f, 1760.0f, 0.0f, 0.04f, 0.1f); add_sine_decay(b, sr, 0.05f, 1175.0f, 1175.0f, 0.0f, 0.05f, 0.1f);
      add_band_noise(b, sr, 258, 0.12f, 0.25f, 1600.0f, 0.7f, 0.5f, true); add_clicks(b, sr, 259, 0.33f, 0.05f, 2, 0.5f, 2500.0f); normalize(b, 0.45f); set(Sfx::TabletClose, std::move(b)); }
    { auto b = make(0.2f, sr); add_sine_decay(b, sr, 0.0f, 1568.0f, 1568.0f, 0.0f, 0.035f, 0.5f); add_sine_decay(b, sr, 0.06f, 2093.0f, 2093.0f, 0.0f, 0.05f, 0.5f); normalize(b, 0.3f); set(Sfx::TabletConfirm, std::move(b)); }
    { auto b = make(0.3f, sr); add_sine_decay(b, sr, 0.0f, 330.0f, 300.0f, 0.1f, 0.07f, 0.6f); add_sine_decay(b, sr, 0.1f, 330.0f, 280.0f, 0.1f, 0.08f, 0.6f); normalize(b, 0.3f); set(Sfx::TabletError, std::move(b)); }
    { auto b = make(0.45f, sr); add_sine_decay(b, sr, 0.0f, 1046.0f, 1046.0f, 0.0f, 0.05f, 0.5f); add_sine_decay(b, sr, 0.09f, 1318.0f, 1318.0f, 0.0f, 0.05f, 0.5f);
      add_sine_decay(b, sr, 0.18f, 1568.0f, 1568.0f, 0.0f, 0.09f, 0.5f); normalize(b, 0.3f); set(Sfx::TabletNotify, std::move(b)); }
    // --- Grenada asurzitoare: detunatura seaca + fluierat inalt ---
    { auto b = make(2.0f, sr); add_noise_burst(b, sr, 260, 0.0f, 0.05f, 1.0f, 16000.0f, 300.0f); add_sine_decay(b, sr, 0.0f, 70.0f, 40.0f, 0.2f, 0.25f, 0.8f);
      add_band_noise(b, sr, 261, 0.02f, 1.6f, 900.0f, 0.5f, 0.15f, true); normalize(b, 0.98f); set(Sfx::Flashbang, std::move(b)); }
    { auto b = make(2.0f, sr); add_band_noise(b, sr, 262, 0.0f, 2.0f, 4000.0f, 0.4f, 1.0f, true); normalize(b, 0.4f); make_loopable(b, sr, 0.2f); set(Sfx::SmokeHiss, std::move(b)); }
    { auto b = make(0.3f, sr); add_sine_decay(b, sr, 0.0f, 120.0f, 80.0f, 0.05f, 0.06f, 1.0f); add_noise_burst(b, sr, 263, 0.0f, 0.02f, 0.8f, 2500.0f, 100.0f); normalize(b, 0.8f); set(Sfx::MeleeHit, std::move(b)); }
    { auto b = make(0.35f, sr); add_band_noise(b, sr, 264, 0.0f, 0.25f, 700.0f, 0.6f, 1.0f, true); normalize(b, 0.4f); set(Sfx::Swing, std::move(b)); }
    // --- Placa de otel lovita: impact scurt + moduri de vibratie neharmonice ("ding") ---
    { auto b = make(1.2f, sr); add_noise_burst(b, sr, 265, 0.0f, 0.004f, 0.9f, 9000.0f, 600.0f);
      add_sine_decay(b, sr, 0.0f, 1180.0f, 1180.0f, 0.0f, 0.32f, 0.7f); add_sine_decay(b, sr, 0.0f, 2730.0f, 2730.0f, 0.0f, 0.22f, 0.45f);
      add_sine_decay(b, sr, 0.0f, 4410.0f, 4410.0f, 0.0f, 0.12f, 0.3f); add_sine_decay(b, sr, 0.0f, 640.0f, 640.0f, 0.0f, 0.4f, 0.25f);
      normalize(b, 0.85f); set(Sfx::SteelHit, std::move(b)); }
    // --- Mecanismul tintei: clic de arc + bufnitura placii ---
    { auto b = make(0.4f, sr); add_noise_burst(b, sr, 266, 0.0f, 0.01f, 0.6f, 3000.0f, 200.0f);
      add_sine_decay(b, sr, 0.02f, 140.0f, 90.0f, 0.05f, 0.08f, 0.8f); add_noise_burst(b, sr, 267, 0.06f, 0.02f, 0.3f, 1500.0f, 100.0f);
      normalize(b, 0.5f); set(Sfx::TargetUp, std::move(b)); }
    // --- Pusca cu luneta (.308): pocnet mai lung si mai adanc decat carabina, coada lunga
    set(Sfx::GunSniper, gunshot(sr, 271, 0.005f, 1200.0f, 0.06f, 110.0f, 42.0f, 0.18f, 0.6f, 1.8f));
    // --- Inchizator cu repetitie: manerul ridicat (clic) + tras inapoi (frecare metalica) ...
    { auto b = make(0.35f, sr); add_noise_burst(b, sr, 272, 0.0f, 0.004f, 0.8f, 9000.0f, 1500.0f);
      add_sine_decay(b, sr, 0.0f, 2300.0f, 2200.0f, 0.0f, 0.03f, 0.35f);
      add_band_noise(b, sr, 273, 0.05f, 0.16f, 3200.0f, 0.5f, 0.5f, true);
      add_noise_burst(b, sr, 274, 0.21f, 0.005f, 0.6f, 8000.0f, 1200.0f);
      normalize(b, 0.6f); set(Sfx::BoltOpen, std::move(b)); }
    // ... si impins inainte (frecare) + coborat (zavorare: clonc solid)
    { auto b = make(0.35f, sr); add_band_noise(b, sr, 275, 0.0f, 0.14f, 2800.0f, 0.5f, 0.45f, true);
      add_noise_burst(b, sr, 276, 0.15f, 0.006f, 0.9f, 7000.0f, 600.0f);
      add_sine_decay(b, sr, 0.15f, 1600.0f, 1500.0f, 0.0f, 0.04f, 0.4f);
      add_sine_decay(b, sr, 0.15f, 300.0f, 220.0f, 0.0f, 0.03f, 0.3f);
      normalize(b, 0.65f); set(Sfx::BoltClose, std::move(b)); }
    // --- Selectorul modului de foc: clic scurt cu arc (detenta)
    { auto b = make(0.12f, sr); add_noise_burst(b, sr, 277, 0.0f, 0.0025f, 1.0f, 11000.0f, 2500.0f);
      add_sine_decay(b, sr, 0.0f, 3400.0f, 3300.0f, 0.0f, 0.012f, 0.4f);
      normalize(b, 0.45f); set(Sfx::Selector, std::move(b)); }
}

int AudioEngine::alloc_voice() {
    for (size_t i = 0; i < voices_.size(); ++i) if (!voices_[i].active) return static_cast<int>(i);
    int best = -1;
    double bpos = -1.0;
    for (size_t i = 0; i < voices_.size(); ++i)
        if (!voices_[i].loop && voices_[i].pos > bpos) { bpos = voices_[i].pos; best = static_cast<int>(i); }
    return best;
}

void AudioEngine::set_listener(double x, double y, double yaw, const Map* map) {
    std::lock_guard<std::mutex> lock(mu_);
    lx_ = x; ly_ = y; lyaw_ = yaw; map_ = map;
    // campul de propagare (difractie prin usi / deschideri) din pozitia ascultatorului
    if (map) spatial_.build_field(*map, x, y);
}

void AudioEngine::update_environment(const Map& map, double x, double y) {
    int cx = static_cast<int>(std::floor(x)), cy = static_cast<int>(std::floor(y));
    bool outdoor = map.is_outdoor(cx, cy);
    double sum = 0.0;
    int enclosed = 0;
    for (int k = 0; k < 16; ++k) {
        double a = k * kTwoPi / 16.0;
        double dx = std::cos(a), dy = std::sin(a);
        double t = 0.0;
        for (; t < 30.0; t += 0.5)
            if (map.blocks_sight(static_cast<int>(std::floor(x + dx * t)), static_cast<int>(std::floor(y + dy * t)))) break;
        sum += t;
        if (t < 25.0) enclosed++;
    }
    double mean = sum / 16.0;
    double target_size = outdoor ? 0.15 : std::clamp(mean / 12.0, 0.1, 1.0);
    double target_wet = outdoor ? 0.1 : 0.16 + 0.22 * enclosed / 16.0;
    std::lock_guard<std::mutex> lock(mu_);
    room_size += (target_size - room_size) * 0.1;
    wet += (target_wet - wet) * 0.1;
}

void AudioEngine::spatialize(Voice& v, double x, double y, float gain) {
    if (!map_) { v.target_gl = v.target_gr = gain * 0.7f; v.lp = 1.0f; return; }
    audio::AcousticSource src{x, y, gain, 1.0};
    auto ear = spatial_.computeAcousticTransmission(lx_, ly_, lyaw_, src, *map_);
    v.target_gl = static_cast<float>(ear.leftGain);
    v.target_gr = static_cast<float>(ear.rightGain);
    v.lp = static_cast<float>(1.0 - std::exp(-kTwoPi * ear.lowpassHz / sample_rate_));
    v.send = static_cast<float>(ear.reverbSend);
}

void AudioEngine::play2d(Sfx s, float gain, float pan, float pitch, float delay, bool internal) {
    if (!device_) return;
    std::lock_guard<std::mutex> lock(mu_);
    int i = alloc_voice();
    if (i < 0) return;
    Voice& v = voices_[i];
    v = Voice{};
    v.data = &bank_[static_cast<size_t>(s)];
    v.rate = pitch;
    float theta = (std::clamp(pan, -1.0f, 1.0f) + 1.0f) * 0.25f * 3.14159265f;
    v.gl = v.target_gl = gain * std::cos(theta) * 1.414f;
    v.gr = v.target_gr = gain * std::sin(theta) * 1.414f;
    v.lp = 1.0f;
    v.send = internal ? 0.05f : 0.25f;
    v.delay = static_cast<int>(delay * sample_rate_);
    v.internal = internal;
    v.active = true;
    v.id = next_id_++;
}

void AudioEngine::play3d(Sfx s, double x, double y, float gain, float pitch, float delay) {
    if (!device_) return;
    std::lock_guard<std::mutex> lock(mu_);
    int i = alloc_voice();
    if (i < 0) return;
    Voice& v = voices_[i];
    v = Voice{};
    v.data = &bank_[static_cast<size_t>(s)];
    v.rate = pitch;
    spatialize(v, x, y, gain);
    v.gl = v.target_gl; v.gr = v.target_gr;
    double dist = std::hypot(x - lx_, y - ly_);
    v.delay = static_cast<int>((delay + dist / 343.0) * sample_rate_);
    v.active = true;
    v.id = next_id_++;
}

int AudioEngine::start_loop(Sfx s, bool internal) {
    if (!device_) return 0;
    std::lock_guard<std::mutex> lock(mu_);
    int i = alloc_voice();
    if (i < 0) return 0;
    Voice& v = voices_[i];
    v = Voice{};
    v.data = &bank_[static_cast<size_t>(s)];
    v.loop = true;
    v.internal = internal;
    v.active = true;
    v.id = next_id_++;
    return v.id;
}

void AudioEngine::update_loop(int id, double x, double y, float gain, float pitch, bool positional) {
    if (!device_ || id == 0) return;
    std::lock_guard<std::mutex> lock(mu_);
    for (auto& v : voices_) {
        if (!v.active || v.id != id) continue;
        v.rate = pitch;
        if (positional) spatialize(v, x, y, gain);
        else { v.target_gl = v.target_gr = gain; v.lp = 1.0f; v.send = 0.1f; }
    }
}

void AudioEngine::stop_loop(int id) {
    if (!device_ || id == 0) return;
    std::lock_guard<std::mutex> lock(mu_);
    for (auto& v : voices_) if (v.id == id) v.active = false;
}

void AudioEngine::stop_all() {
    if (!device_) return;
    std::lock_guard<std::mutex> lock(mu_);
    for (auto& v : voices_) v.active = false;
    streams_.clear();
}

void AudioEngine::set_hearing(const HearingState& h) {
    std::lock_guard<std::mutex> lock(mu_);
    hearing_ = h;
}

void AudioEngine::set_breath(const BreathVoiceParams& b) {
    std::lock_guard<std::mutex> lock(mu_);
    breath_synth_.set(b);
}

void AudioEngine::set_ambience(float outdoor, float indoor) {
    std::lock_guard<std::mutex> lock(mu_);
    amb_out_ = outdoor;
    amb_in_ = indoor;
}

void AudioEngine::push_voice(int talker, const int16_t* pcm, int n, bool radio, double x, double y, float quality) {
    if (!device_ || n <= 0) return;
    std::lock_guard<std::mutex> lock(mu_);
    VoiceStream& s = streams_[talker];
    s.radio = radio;
    s.quality = quality;
    s.idle = 0;
    if (!radio) {
        Voice tmp;
        spatialize(tmp, x, y, 1.6f);
        s.gl = tmp.target_gl; s.gr = tmp.target_gr; s.lp = tmp.lp;
    } else {
        s.gl = 0.85f; s.gr = 0.6f; s.lp = 1.0f;   // casca radio: usor spre stanga
    }
    // 16 kHz -> rata dispozitivului (interpolare liniara)
    double ratio = sample_rate_ / 16000.0;
    int outn = static_cast<int>(n * ratio);
    for (int i = 0; i < outn; ++i) {
        double src = i / ratio;
        int i0 = static_cast<int>(src);
        int i1 = std::min(n - 1, i0 + 1);
        double t = src - i0;
        s.buf.push_back(static_cast<float>((pcm[i0] * (1.0 - t) + pcm[i1] * t) / 32768.0));
    }
    if (s.buf.size() > static_cast<size_t>(sample_rate_) * 2) {
        size_t drop = s.buf.size() - static_cast<size_t>(sample_rate_);
        s.buf.erase(s.buf.begin(), s.buf.begin() + drop);
        s.read = 0;
    }
}

// Respiratia proprie: sintetizator respiratie cu respiratie (vezi breath_synth.cpp)
float AudioEngine::breath_sample() { return breath_synth_.sample(); }

void AudioEngine::mix(float* out, int frames) {
    std::lock_guard<std::mutex> lock(mu_);
    // --- Filtrul auzului: recalculat o data pe bloc (parametrii se schimba lent) ---
    {
        float att = std::clamp(hearing_cur_.attenuation, 0.0f, 0.95f);
        // 20 kHz (normal) -> ~7 kHz -> ~2.7 kHz -> ~1 kHz (puternic infundat), 24 dB/octava
        float fc = att < 0.01f ? 20000.0f : 20000.0f * std::pow(700.0f / 20000.0f, std::pow(att, 0.85f));
        fc = std::min(fc, sample_rate_ * 0.45f);
        if (std::abs(fc - hear_fc_set_) > hear_fc_set_ * 0.02f) {
            float sr = static_cast<float>(sample_rate_);
            hear_l1_.lowpass(fc, 0.541f, sr); hear_l2_.lowpass(fc, 1.307f, sr);   // Butterworth de ordinul 4
            hear_r1_.lowpass(fc, 0.541f, sr); hear_r2_.lowpass(fc, 1.307f, sr);
            hear_n_l_.peaking(4000.0f, 1.2f, -14.0f * att, sr);                  // "gaura" tipica dupa impuls
            hear_n_r_.peaking(4000.0f, 1.2f, -14.0f * att, sr);
            hear_fc_set_ = fc;
        }
    }
    float fb = static_cast<float>(0.72 + 0.24 * room_size);
    float wetk = static_cast<float>(wet);
    const auto& wind = bank_[static_cast<size_t>(Sfx::Wind)];
    const auto& room = bank_[static_cast<size_t>(Sfx::RoomTone)];
    for (int f = 0; f < frames; ++f) {
        // parametrii auzului se interpoleaza (fara salturi)
        const float ka = 0.0008f;
        hearing_cur_.attenuation += (hearing_.attenuation - hearing_cur_.attenuation) * ka;
        hearing_cur_.tinnitus += (hearing_.tinnitus - hearing_cur_.tinnitus) * ka;
        hearing_cur_.width += (hearing_.width - hearing_cur_.width) * ka;
        hearing_cur_.drive += (hearing_.drive - hearing_cur_.drive) * ka;
        amb_out_cur_ += (amb_out_ - amb_out_cur_) * 0.0002f;
        amb_in_cur_ += (amb_in_ - amb_in_cur_) * 0.0002f;

        float L = 0.0f, R = 0.0f, send = 0.0f, IL = 0.0f, IR = 0.0f;
        for (auto& v : voices_) {
            if (!v.active) continue;
            if (v.delay > 0) { v.delay--; continue; }
            const std::vector<float>& d = *v.data;
            if (d.size() < 2) { v.active = false; continue; }
            size_t i0 = static_cast<size_t>(v.pos);
            if (i0 + 1 >= d.size()) {
                if (v.loop) { v.pos = 0.0; i0 = 0; }
                else { v.active = false; continue; }
            }
            float t = static_cast<float>(v.pos - i0);
            float s = d[i0] + (d[i0 + 1] - d[i0]) * t;
            v.pos += v.rate;
            v.gl += (v.target_gl - v.gl) * 0.002f;
            v.gr += (v.target_gr - v.gr) * 0.002f;
            // doi poli in cascada: prin perete raman doar frecventele joase (infundat, nu doar mai incet)
            v.lp_l += v.lp * (s * v.gl - v.lp_l);
            v.lp_r += v.lp * (s * v.gr - v.lp_r);
            v.lp2_l += v.lp * (v.lp_l - v.lp2_l);
            v.lp2_r += v.lp * (v.lp_r - v.lp2_r);
            float ol = v.lp >= 0.999f ? v.lp_l : v.lp2_l, orr = v.lp >= 0.999f ? v.lp_r : v.lp2_r;
            if (v.internal) { IL += ol; IR += orr; }
            else { L += ol; R += orr; send += (ol + orr) * 0.5f * v.send; }
        }
        // ambianta: vant afara, ton de camera inauntru
        if (wind.size() > 1) {
            amb_pos_w_ += 1.0;
            if (amb_pos_w_ >= wind.size()) amb_pos_w_ = 0.0;
            float wv = wind[static_cast<size_t>(amb_pos_w_)] * amb_out_cur_;
            L += wv; R += wind[(static_cast<size_t>(amb_pos_w_) + wind.size() / 3) % wind.size()] * amb_out_cur_;
        }
        if (room.size() > 1) {
            amb_pos_r_ += 1.0;
            if (amb_pos_r_ >= room.size()) amb_pos_r_ = 0.0;
            float rv = room[static_cast<size_t>(amb_pos_r_)] * amb_in_cur_;
            L += rv; R += rv;
        }
        // voce: proximitate (in lume) si radio (in casca)
        float radio = 0.0f;
        for (auto it = streams_.begin(); it != streams_.end();) {
            VoiceStream& s = it->second;
            size_t avail = s.buf.size() - s.read;
            if (!s.talking && avail > static_cast<size_t>(sample_rate_) * 6 / 100) s.talking = true;
            if (s.talking && avail > 0) {
                float v = s.buf[s.read++];
                if (s.radio) radio += v;
                else { s.lp_state += s.lp * (v - s.lp_state); L += s.lp_state * s.gl; R += s.lp_state * s.gr; }
                s.idle = 0;
            } else if (s.talking) {
                if (++s.idle > sample_rate_ / 8) {
                    s.talking = false;
                    s.buf.clear(); s.read = 0;
                    if (s.radio) {
                        // "squelch": clic la eliberarea emisiei
                        int vi = alloc_voice();
                        if (vi >= 0) {
                            Voice& c = voices_[vi];
                            c = Voice{};
                            c.data = &bank_[static_cast<size_t>(Sfx::RadioClick)];
                            c.gl = c.target_gl = 0.35f; c.gr = c.target_gr = 0.25f;
                            c.internal = true; c.active = true; c.id = next_id_++;
                        }
                    }
                }
            }
            if (s.read > 48000) { s.buf.erase(s.buf.begin(), s.buf.begin() + s.read); s.read = 0; }
            ++it;
        }
        if (radio != 0.0f) {
            radio_rng_ ^= radio_rng_ << 13; radio_rng_ ^= radio_rng_ >> 17; radio_rng_ ^= radio_rng_ << 5;
            float noise = (static_cast<float>(radio_rng_ & 0xFFFF) / 32768.0f - 1.0f) * 0.03f;
            float r = radio_lp_.run(radio_hp_.run(radio * 3.0f + noise));
            r = std::tanh(r * 1.8f) * 0.55f;   // compresie / saturatie de statie
            IL += r * 0.85f; IR += r * 0.6f;
        }
        // reverberatie
        float rev[2] = {0.0f, 0.0f};
        for (int ch = 0; ch < 2; ++ch) {
            float acc = 0.0f;
            for (int k = 0; k < 4; ++k) {
                int ci = ch * 4 + k;
                auto& buf = comb_[ci];
                float y = buf[comb_pos_[ci]];
                comb_lp_[ci] += 0.35f * (y - comb_lp_[ci]);
                buf[comb_pos_[ci]] = send + comb_lp_[ci] * fb;
                comb_pos_[ci] = (comb_pos_[ci] + 1) % static_cast<int>(buf.size());
                acc += y;
            }
            acc *= 0.25f;
            for (int k = 0; k < 2; ++k) {
                int ai = ch * 2 + k;
                auto& buf = ap_[ai];
                float bv = buf[ap_pos_[ai]];
                float y = -acc + bv;
                buf[ap_pos_[ai]] = acc + bv * 0.5f;
                ap_pos_[ai] = (ap_pos_[ai] + 1) % static_cast<int>(buf.size());
                acc = y;
            }
            rev[ch] = acc;
        }
        L += rev[0] * wetk * 2.0f;
        R += rev[1] * wetk * 2.0f;

        // --- Auz degradat dupa sunete puternice: lumea devine infundata si mai slaba
        //     (fara distorsiune); corpul propriu (respiratie, inima) ramane clar ---
        float att = hearing_cur_.attenuation;
        if (hear_fc_set_ < 19000.0f) {
            L = hear_n_l_.run(hear_l2_.run(hear_l1_.run(L)));
            R = hear_n_r_.run(hear_r2_.run(hear_r1_.run(R)));
        }
        float mid = (L + R) * 0.5f, side = (L - R) * 0.5f * (1.0f - 0.25f * att);
        float vol = std::pow(10.0f, -18.0f * att / 20.0f);
        L = (mid + side) * vol;
        R = (mid - side) * vol;

        // --- Magistrala interna: corpul propriu (respiratie, inima), tiuit ---
        float br = breath_sample();
        IL += br; IR += br * 0.9f;
        if (hearing_cur_.tinnitus > 0.002f) {
            // tiuit: un ton aproape pur (~4 kHz) care deriva lent si palpaie usor
            tin_time_ += 1.0 / sample_rate_;
            double tf = 4150.0 + 70.0 * std::sin(tin_time_ * 0.45) + 20.0 * std::sin(tin_time_ * 1.7);
            tin_phase_ += kTwoPi * tf / sample_rate_;
            if (tin_phase_ > kTwoPi) tin_phase_ -= kTwoPi;
            float amp = static_cast<float>(0.055 * std::pow(hearing_cur_.tinnitus, 1.3) * (0.85 + 0.15 * std::sin(tin_time_ * 2.3)));
            float tn = static_cast<float>(std::sin(tin_phase_) + 0.04 * std::sin(2.0 * tin_phase_)) * amp;
            IL += tn; IR += tn * 0.95f;
        }
        if (!std::isfinite(L + R + IL + IR)) { reset_dsp_state(); L = R = IL = IR = 0.0f; }
        out[f * 2] = std::tanh((L + IL) * master);
        out[f * 2 + 1] = std::tanh((R + IR) * master);
        if (recording_) {
            rec_.push_back(static_cast<int16_t>(std::clamp(out[f * 2], -1.0f, 1.0f) * 32767.0f));
            rec_.push_back(static_cast<int16_t>(std::clamp(out[f * 2 + 1], -1.0f, 1.0f) * 32767.0f));
        }
    }
}

}
