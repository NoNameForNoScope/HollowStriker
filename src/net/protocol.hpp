#ifndef BODYCAM_NET_PROTOCOL_HPP
#define BODYCAM_NET_PROTOCOL_HPP

#include "../sim/world.hpp"
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <string>
#include <vector>

namespace bodycam {
namespace net {

constexpr uint32_t kMagic = 0xBC4D1A02;
constexpr int kTickRate = 60;
constexpr int kSnapshotEvery = 2;       // 30 Hz
constexpr int kInterpDelayTicks = 6;    // 100 ms de interpolare
constexpr int kInputRedundancy = 4;     // fiecare pachet repeta ultimele intrari

enum class Msg : uint8_t { Hello = 1, Welcome, Input, Snapshot, Voice, Round, Bye };

class Writer {
public:
    std::vector<uint8_t> buf;
    void u8(uint8_t v) { buf.push_back(v); }
    void i8(int8_t v) { buf.push_back(static_cast<uint8_t>(v)); }
    void u16(uint16_t v) { buf.push_back(v & 255); buf.push_back(v >> 8); }
    void i16(int16_t v) { u16(static_cast<uint16_t>(v)); }
    void u32(uint32_t v) { for (int i = 0; i < 4; ++i) buf.push_back((v >> (8 * i)) & 255); }
    void u64(uint64_t v) { for (int i = 0; i < 8; ++i) buf.push_back((v >> (8 * i)) & 255); }
    void f32(float v) { uint32_t u; static_assert(sizeof(u) == sizeof(v), ""); std::memcpy(&u, &v, 4); u32(u); }
    void f64(double v) { uint64_t u; std::memcpy(&u, &v, 8); u64(u); }
    void str(const std::string& s) { u16(static_cast<uint16_t>(s.size())); buf.insert(buf.end(), s.begin(), s.end()); }
    // valori cuantizate
    void unorm8(double v) { u8(static_cast<uint8_t>(std::clamp(v, 0.0, 1.0) * 255.0 + 0.5)); }
    void snorm8(double v) { i8(static_cast<int8_t>(std::clamp(v, -1.0, 1.0) * 127.0)); }
    void pos16(double v) { u16(static_cast<uint16_t>(std::clamp(v, 0.0, 255.99) * 256.0)); }
    void angle16(double v) { i16(static_cast<int16_t>(std::clamp(v, -3.2, 3.2) * 10000.0)); }
};

class Reader {
public:
    Reader(const uint8_t* p, size_t n) : p_(p), n_(n) {}
    bool ok() const { return ok_; }
    size_t remaining() const { return n_ - pos_; }
    uint8_t u8() { if (pos_ + 1 > n_) { ok_ = false; return 0; } return p_[pos_++]; }
    int8_t i8() { return static_cast<int8_t>(u8()); }
    uint16_t u16() { uint16_t a = u8(); uint16_t b = u8(); return static_cast<uint16_t>(a | (b << 8)); }
    int16_t i16() { return static_cast<int16_t>(u16()); }
    uint32_t u32() { uint32_t v = 0; for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(u8()) << (8 * i); return v; }
    uint64_t u64() { uint64_t v = 0; for (int i = 0; i < 8; ++i) v |= static_cast<uint64_t>(u8()) << (8 * i); return v; }
    float f32() { uint32_t u = u32(); float v; std::memcpy(&v, &u, 4); return v; }
    double f64() { uint64_t u = u64(); double v; std::memcpy(&v, &u, 8); return v; }
    std::string str() {
        uint16_t n = u16();
        if (pos_ + n > n_) { ok_ = false; return {}; }
        std::string s(reinterpret_cast<const char*>(p_ + pos_), n);
        pos_ += n;
        return s;
    }
    double unorm8() { return u8() / 255.0; }
    double snorm8() { return i8() / 127.0; }
    double pos16() { return u16() / 256.0; }
    double angle16() { return i16() / 10000.0; }
private:
    const uint8_t* p_;
    size_t n_;
    size_t pos_ = 0;
    bool ok_ = true;
};

struct InputCmd {
    uint32_t seq = 0;
    AgentControl c;
};

struct WelcomeInfo {
    int agent_id = -1;
    uint64_t seed = 0;
    int mode = 0;
    int round = 1;
    double round_time_limit = 300.0;
    std::string map_file;
    std::vector<std::string> weapon_files;
    uint32_t tick = 0;
};

struct EventRecord {
    uint32_t id;
    Event e;
};

// Starea unui agent, asa cum o vad ceilalti (cuantizata)
struct AgentNetState {
    int id = 0;
    Faction faction = Faction::Blue;
    bool is_human = false;
    FsmState state = FsmState::Move;
    double x = 0, y = 0, z = 0, facing = 0, pitch = 0;
    double vx = 0, vy = 0, speed = 0, bob_phase = 0;
    double crouch = 0, prone = 0, lean = 0, aim = 0, tablet = 0;
    double hp = 100;
    bool downed = false, flashlight = false, sprinting = false;
    Possession possess = Possession::Body;
    int drone_id = -1, vehicle_id = -1;
    StanceState stance = StanceState::Stable;
    double tilt = 0, death_timer = 0, death_dir = 0;
    int weapon = 0;
    int reload_stage = 0;
    double fire_age = 9.0;
};

// Instantaneu decodat (clientul pastreaza cateva pentru interpolare)
struct Snapshot {
    uint32_t tick = 0;
    uint32_t last_input_seq = 0;
    double echo_time = 0.0;
    std::vector<AgentNetState> agents;
    std::vector<uint8_t> raw_world;   // starea lumii (aplicata direct)
    std::vector<uint8_t> raw_local;   // starea extinsa a jucatorului local
    std::vector<EventRecord> events;
};

void write_control(Writer& w, const AgentControl& c);
AgentControl read_control(Reader& r);
void write_event(Writer& w, const Event& e);
Event read_event(Reader& r);

// Instantanee: serverul scrie, clientul citeste si aplica
void write_snapshot(Writer& w, const World& world, int client_agent, uint32_t last_input_seq, double echo_time,
                    const std::vector<EventRecord>& events);
bool read_snapshot(Reader& r, Snapshot& s);
void apply_world_state(World& world, const std::vector<uint8_t>& raw);
void apply_local_state(Agent& a, const std::vector<uint8_t>& raw, bool movement_too);
void apply_agent_state(Agent& a, const AgentNetState& s);
AgentNetState lerp_agent(const AgentNetState& a, const AgentNetState& b, double t);

// Voce: mu-law 8 biti la 16 kHz
uint8_t mulaw_encode(int16_t s);
int16_t mulaw_decode(uint8_t u);

}
}
#endif
