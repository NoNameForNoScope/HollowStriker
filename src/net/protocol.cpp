#include "protocol.hpp"
#include <cmath>

namespace bodycam {
namespace net {

namespace {
constexpr double kPi = 3.14159265358979323846;
double wrap_pi(double a) {
    while (a > kPi) a -= 2.0 * kPi;
    while (a < -kPi) a += 2.0 * kPi;
    return a;
}
}

// ---------------------------------------------------------------------------
// Intrari (comenzile jucatorului)
// ---------------------------------------------------------------------------
void write_control(Writer& w, const AgentControl& c) {
    w.f32(static_cast<float>(c.look_yaw));
    w.f32(static_cast<float>(c.look_pitch));
    w.snorm8(c.moveForward);
    w.snorm8(c.moveRight);
    w.snorm8(c.lean);
    uint32_t f = 0;
    const bool bits[] = {c.sprint, c.crouch, c.toggle_prone, c.jump, c.aim, c.fire, c.reload, c.interact, c.interact_hold,
                         c.kick, c.bandage, c.tablet_toggle, c.throw_grenade, c.toggle_flashlight, c.inspect, c.place_marker,
                         c.clear_markers, c.toggle_drone, c.toggle_vehicle, c.toggle_drone_mode};
    for (int i = 0; i < 20; ++i) if (bits[i]) f |= 1u << i;
    w.u32(f);
    w.i8(static_cast<int8_t>(c.switch_weapon));
    if (c.place_marker) { w.f32(static_cast<float>(c.marker_x)); w.f32(static_cast<float>(c.marker_y)); w.u8(static_cast<uint8_t>(c.marker_kind)); }
    w.snorm8(c.stick_throttle);
    w.snorm8(c.stick_yaw);
    w.snorm8(c.stick_pitch);
    w.snorm8(c.stick_roll);
    w.u32(c.view_tick);
}

AgentControl read_control(Reader& r) {
    AgentControl c;
    c.look_yaw = r.f32();
    c.look_pitch = r.f32();
    c.moveForward = r.snorm8();
    c.moveRight = r.snorm8();
    c.lean = r.snorm8();
    uint32_t f = r.u32();
    bool* bits[] = {&c.sprint, &c.crouch, &c.toggle_prone, &c.jump, &c.aim, &c.fire, &c.reload, &c.interact, &c.interact_hold,
                    &c.kick, &c.bandage, &c.tablet, &c.throw_grenade, &c.toggle_flashlight, &c.inspect, &c.place_marker,
                    &c.clear_markers, &c.toggle_drone, &c.toggle_vehicle, &c.toggle_drone_mode};
    for (int i = 0; i < 20; ++i) *bits[i] = (f >> i) & 1u;
    c.switch_weapon = r.i8();
    if (c.place_marker) { c.marker_x = r.f32(); c.marker_y = r.f32(); c.marker_kind = r.u8(); }
    c.stick_throttle = r.snorm8();
    c.stick_yaw = r.snorm8();
    c.stick_pitch = r.snorm8();
    c.stick_roll = r.snorm8();
    c.view_tick = r.u32();
    return c;
}

// ---------------------------------------------------------------------------
// Evenimente (replicare fiabila: numerotate, retrimise pana la confirmare)
// ---------------------------------------------------------------------------
void write_event(Writer& w, const Event& e) {
    w.u8(static_cast<uint8_t>(e.type));
    w.i16(static_cast<int16_t>(e.a));
    w.i16(static_cast<int16_t>(e.b));
    w.f32(static_cast<float>(e.x));
    w.f32(static_cast<float>(e.y));
    w.f32(static_cast<float>(e.z));
    w.f32(static_cast<float>(e.v1));
    w.f32(static_cast<float>(e.v2));
    w.f32(static_cast<float>(e.v3));
    w.snorm8(e.nx);
    w.snorm8(e.ny);
}

Event read_event(Reader& r) {
    Event e;
    e.type = static_cast<EventType>(r.u8());
    e.a = r.i16();
    e.b = r.i16();
    e.x = r.f32(); e.y = r.f32(); e.z = r.f32();
    e.v1 = r.f32(); e.v2 = r.f32(); e.v3 = r.f32();
    e.nx = r.snorm8(); e.ny = r.snorm8();
    return e;
}

// ---------------------------------------------------------------------------
// Instantanee
// ---------------------------------------------------------------------------
namespace {
void write_agent(Writer& w, const World& world, const Agent& a) {
    uint16_t flags = 0;
    if (a.faction == Faction::Red) flags |= 1;
    if (a.is_human) flags |= 2;
    if (a.health.downed) flags |= 4;
    if (a.flashlight) flags |= 8;
    if (a.sprinting) flags |= 16;
    flags |= static_cast<uint16_t>(static_cast<int>(a.possess) & 3) << 5;
    w.u16(flags);
    w.u8(static_cast<uint8_t>(a.state));
    w.pos16(a.x);
    w.pos16(a.y);
    w.u8(static_cast<uint8_t>(std::clamp(a.z * 100.0, 0.0, 255.0)));
    w.angle16(wrap_pi(a.facing));
    w.angle16(a.pitch);
    w.i16(static_cast<int16_t>(std::clamp(a.vx * 1000.0, -32000.0, 32000.0)));
    w.i16(static_cast<int16_t>(std::clamp(a.vy * 1000.0, -32000.0, 32000.0)));
    w.u8(static_cast<uint8_t>(std::clamp(a.speed * 25.0, 0.0, 255.0)));
    w.unorm8(std::fmod(a.bob_phase, kPi) / kPi);
    w.unorm8(a.crouch);
    w.unorm8(a.prone);
    w.snorm8(a.lean);
    w.unorm8(a.aim);
    w.unorm8(a.tablet);
    w.u8(static_cast<uint8_t>(std::clamp(a.hp, 0.0, 255.0)));
    w.i8(static_cast<int8_t>(a.drone_id));
    w.i8(static_cast<int8_t>(a.vehicle_id));
    w.u8(static_cast<uint8_t>(a.stance.state));
    w.snorm8(a.stance.tilt / 0.6);
    w.u8(static_cast<uint8_t>(std::clamp(a.death_timer * 50.0, 0.0, 255.0)));
    w.angle16(wrap_pi(a.death_dir));
    w.u8(static_cast<uint8_t>(a.slots.empty() ? 0 : a.slots[a.slot].weapon));
    w.u8(static_cast<uint8_t>(a.reload_stage));
    w.u8(static_cast<uint8_t>(std::clamp((world.current_time - a.last_fire_time) * 50.0, 0.0, 255.0)));
}

AgentNetState read_agent(Reader& r, int id) {
    AgentNetState s;
    s.id = id;
    uint16_t flags = r.u16();
    s.faction = (flags & 1) ? Faction::Red : Faction::Blue;
    s.is_human = flags & 2;
    s.downed = flags & 4;
    s.flashlight = flags & 8;
    s.sprinting = flags & 16;
    s.possess = static_cast<Possession>((flags >> 5) & 3);
    s.state = static_cast<FsmState>(r.u8());
    s.x = r.pos16();
    s.y = r.pos16();
    s.z = r.u8() / 100.0;
    s.facing = r.angle16();
    s.pitch = r.angle16();
    s.vx = r.i16() / 1000.0;
    s.vy = r.i16() / 1000.0;
    s.speed = r.u8() / 25.0;
    s.bob_phase = r.unorm8() * kPi;
    s.crouch = r.unorm8();
    s.prone = r.unorm8();
    s.lean = r.snorm8();
    s.aim = r.unorm8();
    s.tablet = r.unorm8();
    s.hp = r.u8();
    s.drone_id = r.i8();
    s.vehicle_id = r.i8();
    s.stance = static_cast<StanceState>(r.u8());
    s.tilt = r.snorm8() * 0.6;
    s.death_timer = r.u8() / 50.0;
    s.death_dir = r.angle16();
    s.weapon = r.u8();
    s.reload_stage = r.u8();
    s.fire_age = r.u8() / 50.0;
    return s;
}

void write_world(Writer& w, const World& world, Faction viewer) {
    const GameState& g = world.game;
    w.u8(static_cast<uint8_t>(g.mode));
    w.u8(static_cast<uint8_t>(g.phase));
    w.u16(static_cast<uint16_t>(g.round));
    w.f32(static_cast<float>(g.round_time));
    w.f32(static_cast<float>(g.round_time_limit));
    w.f32(static_cast<float>(g.phase_timer));
    w.u8(static_cast<uint8_t>(g.score_blue));
    w.u8(static_cast<uint8_t>(g.score_red));
    w.i8(static_cast<int8_t>(g.winner));
    w.u8(g.has_objective ? 1 : 0);
    w.f32(static_cast<float>(g.obj_x));
    w.f32(static_cast<float>(g.obj_y));
    w.f32(static_cast<float>(g.capture_progress));
    w.u8(g.objective_reached ? 1 : 0);
    w.f32(static_cast<float>(world.current_time));
    w.f32(static_cast<float>(world.intel_until));
    // harta: usi, ferestre sparte, lampi, curent
    const Map& m = world.map;
    for (size_t i = 0; i < m.grid.size(); ++i) {
        if (m.grid[i] != 'D') continue;
        w.unorm8(m.door_open[i]);
        w.unorm8(m.door_target[i]);
        w.i8(m.door_swing[i]);
        w.u8(m.door_locked[i]);
    }
    std::vector<uint16_t> broken;
    for (size_t i = 0; i < m.grid.size(); ++i) if (m.grid[i] == '7') broken.push_back(static_cast<uint16_t>(i));
    w.u16(static_cast<uint16_t>(broken.size()));
    for (auto b : broken) w.u16(b);
    w.u8(m.power ? 1 : 0);
    uint8_t bits = 0;
    int nb = 0;
    for (size_t i = 0; i < m.grid.size(); ++i) {
        if (m.grid[i] != 'L') continue;
        if (m.lamp_on[i]) bits |= static_cast<uint8_t>(1u << nb);
        if (++nb == 8) { w.u8(bits); bits = 0; nb = 0; }
    }
    if (nb) w.u8(bits);
    // drone, vehicule, obiecte, grenade
    w.u8(static_cast<uint8_t>(world.drones.size()));
    for (const auto& d : world.drones) {
        w.i8(static_cast<int8_t>(d.owner));
        w.u8(static_cast<uint8_t>((d.alive ? 1 : 0) | (d.acro ? 2 : 0)));
        w.f32(static_cast<float>(d.x)); w.f32(static_cast<float>(d.y)); w.f32(static_cast<float>(d.z));
        w.angle16(d.yaw); w.angle16(wrap_pi(d.pitch)); w.angle16(wrap_pi(d.roll));
        w.unorm8(d.battery); w.unorm8(d.throttle);
        w.u8(static_cast<uint8_t>(std::clamp(d.last_impact * 20.0, 0.0, 255.0)));
    }
    w.u8(static_cast<uint8_t>(world.vehicles.size()));
    for (const auto& v : world.vehicles) {
        w.i8(static_cast<int8_t>(v.owner));
        w.u8(v.alive ? 1 : 0);
        w.f32(static_cast<float>(v.x)); w.f32(static_cast<float>(v.y));
        w.angle16(v.yaw); w.f32(static_cast<float>(v.speed));
        w.snorm8(v.pitch * 4.0); w.snorm8(v.roll * 4.0);
        w.unorm8(v.battery);
    }
    w.u16(static_cast<uint16_t>(world.props.size()));
    for (const auto& p : world.props) {
        w.pos16(p.x); w.pos16(p.y);
        w.u8(static_cast<uint8_t>(std::clamp(p.z * 100.0, 0.0, 255.0)));
        w.angle16(wrap_pi(p.yaw));
    }
    uint8_t active = 0;
    for (const auto& gr : world.grenades) if (gr.active) active++;
    w.u8(active);
    for (const auto& gr : world.grenades) {
        if (!gr.active) continue;
        w.f32(static_cast<float>(gr.x)); w.f32(static_cast<float>(gr.y)); w.f32(static_cast<float>(gr.z));
    }
    // marcaje: doar ale echipei celui care primeste
    uint8_t nm = 0;
    for (const auto& mk : world.markers) if (mk.faction == viewer) nm++;
    w.u8(nm);
    for (const auto& mk : world.markers) {
        if (mk.faction != viewer) continue;
        w.u16(static_cast<uint16_t>(mk.id));
        w.f32(static_cast<float>(mk.x)); w.f32(static_cast<float>(mk.y));
        w.u8(static_cast<uint8_t>(mk.kind));
        w.i8(static_cast<int8_t>(mk.owner));
        w.f32(static_cast<float>(mk.time));
    }
    // interactiuni (inclusiv obiectele aparute dinamic)
    w.u16(static_cast<uint16_t>(world.interactables.size()));
    for (const auto& it : world.interactables) {
        w.u8(static_cast<uint8_t>(it.kind));
        w.u8(static_cast<uint8_t>((it.enabled ? 1 : 0) | (it.state ? 2 : 0)));
        w.unorm8(it.progress);
        w.f32(static_cast<float>(it.x)); w.f32(static_cast<float>(it.y));
        w.u8(static_cast<uint8_t>(it.pickup));
        w.i8(static_cast<int8_t>(it.weapon));
        w.u8(static_cast<uint8_t>(std::clamp(it.rounds, 0, 255)));
        w.i8(static_cast<int8_t>(it.target_agent));
        w.i8(static_cast<int8_t>(it.room));
        w.i8(static_cast<int8_t>(it.cx));
        w.i8(static_cast<int8_t>(it.cy));
    }
}

void write_local(Writer& w, const Agent& a) {
    // miscare (precizie completa: baza reconcilierii)
    w.f64(a.x); w.f64(a.y); w.f32(static_cast<float>(a.vx)); w.f32(static_cast<float>(a.vy));
    w.f32(static_cast<float>(a.z)); w.f32(static_cast<float>(a.vz)); w.u8(a.grounded ? 1 : 0);
    w.f32(static_cast<float>(a.crouch)); w.f32(static_cast<float>(a.prone)); w.u8(a.prone_target ? 1 : 0);
    w.f32(static_cast<float>(a.lean)); w.f32(static_cast<float>(a.stamina)); w.u8(a.exhausted ? 1 : 0);
    w.f32(static_cast<float>(a.step_dist)); w.u32(static_cast<uint32_t>(a.steps));
    w.u8(static_cast<uint8_t>(a.stance.state)); w.f32(static_cast<float>(a.stance.stumbleTimer));
    w.f32(static_cast<float>(a.stance.pushX)); w.f32(static_cast<float>(a.stance.pushY));
    w.f32(static_cast<float>(a.stance.tilt)); w.f32(static_cast<float>(a.stance.balance));
    // arme si incarcatoare
    w.u8(static_cast<uint8_t>(a.slot));
    w.u8(static_cast<uint8_t>(a.slots.size()));
    for (const auto& s : a.slots) {
        w.u8(static_cast<uint8_t>(s.weapon));
        w.i16(static_cast<int16_t>(s.mag));
        w.u8(static_cast<uint8_t>(s.mag_cap));
        w.unorm8(s.mag_condition);
        w.u8(static_cast<uint8_t>((s.chambered ? 1 : 0) | (s.malfunction ? 2 : 0)));
        w.unorm8(s.heat);
        w.u8(static_cast<uint8_t>(s.spares.size()));
        for (const auto& m : s.spares) { w.u16(static_cast<uint16_t>(m.rounds)); w.u16(static_cast<uint16_t>(m.capacity)); w.unorm8(m.condition); }
    }
    w.f32(static_cast<float>(a.switch_timer)); w.f32(static_cast<float>(a.switch_total)); w.i8(static_cast<int8_t>(a.switch_to));
    w.f32(static_cast<float>(a.fire_timer)); w.f32(static_cast<float>(a.recoil)); w.u32(static_cast<uint32_t>(a.shots_fired));
    w.u8(static_cast<uint8_t>(a.reload_stage)); w.f32(static_cast<float>(a.reload_timer)); w.f32(static_cast<float>(a.reload_stage_len));
    w.f32(static_cast<float>(a.reload_total)); w.u8(a.reload_empty ? 1 : 0); w.u32(static_cast<uint32_t>(a.reload_count));
    const ReloadPlan& p = a.rplan;
    w.u8(static_cast<uint8_t>(p.style)); w.f32(static_cast<float>(p.tilt)); w.f32(static_cast<float>(p.mag_angle));
    w.f32(static_cast<float>(p.hand_arc)); w.f32(static_cast<float>(p.hand_lag)); w.f32(static_cast<float>(p.camera_bob));
    w.u8(static_cast<uint8_t>((p.drop_mag ? 1 : 0) | (p.fumble ? 2 : 0) | (p.fumble_done ? 4 : 0) | (p.retain_mag ? 8 : 0)));
    w.u32(p.seed);
    // inventar, sanatate, fiziologie, interactiuni
    w.u8(static_cast<uint8_t>(a.inv.grenades)); w.u8(static_cast<uint8_t>(a.inv.bandages));
    w.unorm8(a.inv.flashlight_battery); w.u8(a.inv.tablet ? 1 : 0);
    for (int z = 0; z < kZoneCount; ++z) w.f32(static_cast<float>(a.health.zone[z]));
    w.f32(static_cast<float>(a.health.bleeding)); w.f32(static_cast<float>(a.health.bleedout));
    w.f32(static_cast<float>(a.hp));
    const Physiology& ph = a.phys;
    w.f32(static_cast<float>(ph.acute)); w.f32(static_cast<float>(ph.chronic)); w.f32(static_cast<float>(ph.adrenaline));
    w.f32(static_cast<float>(ph.heart_rate)); w.f32(static_cast<float>(ph.exertion)); w.f32(static_cast<float>(ph.fire_heat));
    w.f32(static_cast<float>(ph.time_since_threat)); w.f32(static_cast<float>(ph.time_since_shot));
    w.i16(static_cast<int16_t>(a.interact_id)); w.unorm8(a.interact_progress); w.unorm8(a.bandage_progress);
    w.f32(static_cast<float>(a.kick_cooldown));
}
}

void write_snapshot(Writer& w, const World& world, int client_agent, uint32_t last_input_seq, double echo_time,
                    const std::vector<EventRecord>& events) {
    w.u32(kMagic);
    w.u8(static_cast<uint8_t>(Msg::Snapshot));
    w.u32(world.tick());
    w.u32(last_input_seq);
    w.f64(echo_time);
    w.u8(static_cast<uint8_t>(world.agents.size()));
    for (const auto& a : world.agents) write_agent(w, world, a);
    Faction viewer = (client_agent >= 0 && client_agent < static_cast<int>(world.agents.size())) ? world.agents[client_agent].faction : Faction::Blue;
    Writer ws;
    write_world(ws, world, viewer);
    w.u32(static_cast<uint32_t>(ws.buf.size()));
    w.buf.insert(w.buf.end(), ws.buf.begin(), ws.buf.end());
    Writer wl;
    if (client_agent >= 0 && client_agent < static_cast<int>(world.agents.size())) write_local(wl, world.agents[client_agent]);
    w.u32(static_cast<uint32_t>(wl.buf.size()));
    w.buf.insert(w.buf.end(), wl.buf.begin(), wl.buf.end());
    w.u8(static_cast<uint8_t>(std::min<size_t>(events.size(), 255)));
    for (size_t i = 0; i < events.size() && i < 255; ++i) {
        w.u32(events[i].id);
        write_event(w, events[i].e);
    }
}

bool read_snapshot(Reader& r, Snapshot& s) {
    s.tick = r.u32();
    s.last_input_seq = r.u32();
    s.echo_time = r.f64();
    int na = r.u8();
    s.agents.clear();
    for (int i = 0; i < na; ++i) s.agents.push_back(read_agent(r, i));
    uint32_t nw = r.u32();
    if (!r.ok() || nw > r.remaining()) return false;
    s.raw_world.resize(nw);
    for (uint32_t i = 0; i < nw; ++i) s.raw_world[i] = r.u8();
    uint32_t nl = r.u32();
    if (!r.ok() || nl > r.remaining()) return false;
    s.raw_local.resize(nl);
    for (uint32_t i = 0; i < nl; ++i) s.raw_local[i] = r.u8();
    int ne = r.u8();
    s.events.clear();
    for (int i = 0; i < ne && r.ok(); ++i) {
        EventRecord rec;
        rec.id = r.u32();
        rec.e = read_event(r);
        s.events.push_back(rec);
    }
    return r.ok();
}

void apply_world_state(World& world, const std::vector<uint8_t>& raw) {
    Reader r(raw.data(), raw.size());
    GameState& g = world.game;
    g.mode = static_cast<GameMode>(r.u8());
    g.phase = static_cast<MatchPhase>(r.u8());
    g.round = r.u16();
    g.round_time = r.f32();
    g.round_time_limit = r.f32();
    g.phase_timer = r.f32();
    g.score_blue = r.u8();
    g.score_red = r.u8();
    g.winner = r.i8();
    g.has_objective = r.u8() != 0;
    g.obj_x = r.f32();
    g.obj_y = r.f32();
    g.capture_progress = r.f32();
    g.objective_reached = r.u8() != 0;
    world.current_time = r.f32();
    world.intel_until = r.f32();
    Map& m = world.map;
    bool doors_changed = false;
    for (size_t i = 0; i < m.grid.size(); ++i) {
        if (m.grid[i] != 'D') continue;
        float open = static_cast<float>(r.unorm8());
        bool was = m.door_open[i] < 0.6f;
        m.door_open[i] = open;
        m.door_target[i] = static_cast<float>(r.unorm8());
        m.door_swing[i] = r.i8();
        m.door_locked[i] = r.u8();
        if (was != (open < 0.6f)) doors_changed = true;
    }
    int nb = r.u16();
    bool windows_changed = false;
    for (int i = 0; i < nb; ++i) {
        uint16_t idx = r.u16();
        if (idx < m.grid.size() && m.grid[idx] == '6') { m.grid[idx] = '7'; windows_changed = true; }
    }
    bool power = r.u8() != 0;
    bool lamps_changed = power != m.power;
    m.power = power;
    int bit = 8;
    uint8_t bits = 0;
    for (size_t i = 0; i < m.grid.size(); ++i) {
        if (m.grid[i] != 'L') continue;
        if (bit == 8) { bits = r.u8(); bit = 0; }
        uint8_t on = (bits >> bit) & 1u;
        if (m.lamp_on[i] != on) lamps_changed = true;
        m.lamp_on[i] = on;
        bit++;
    }
    if (doors_changed || lamps_changed || windows_changed) { m.revision++; world.rebuild_lighting(); }
    int nd = r.u8();
    world.drones.resize(nd);
    for (int i = 0; i < nd; ++i) {
        Drone& d = world.drones[i];
        d.id = i;
        d.owner = r.i8();
        uint8_t f = r.u8();
        d.alive = f & 1; d.acro = (f & 2) != 0;
        d.x = r.f32(); d.y = r.f32(); d.z = r.f32();
        d.yaw = r.angle16(); d.pitch = r.angle16(); d.roll = r.angle16();
        d.battery = r.unorm8(); d.throttle = r.unorm8();
        d.last_impact = r.u8() / 20.0;
    }
    int nv = r.u8();
    world.vehicles.resize(nv);
    for (int i = 0; i < nv; ++i) {
        Vehicle& v = world.vehicles[i];
        v.id = i;
        v.owner = r.i8();
        v.alive = r.u8() != 0;
        v.x = r.f32(); v.y = r.f32();
        v.yaw = r.angle16(); v.speed = r.f32();
        v.pitch = r.snorm8() / 4.0; v.roll = r.snorm8() / 4.0;
        v.battery = r.unorm8();
    }
    int np = r.u16();
    if (static_cast<int>(world.props.size()) != np) world.props.resize(np);
    for (int i = 0; i < np; ++i) {
        Prop& p = world.props[i];
        p.x = r.pos16(); p.y = r.pos16();
        p.z = r.u8() / 100.0;
        p.yaw = r.angle16();
    }
    int ng = r.u8();
    world.grenades.clear();
    for (int i = 0; i < ng; ++i) {
        Grenade g2;
        g2.id = i;
        g2.x = r.f32(); g2.y = r.f32(); g2.z = r.f32();
        g2.active = true;
        world.grenades.push_back(g2);
    }
    int nm = r.u8();
    world.markers.clear();
    for (int i = 0; i < nm; ++i) {
        TacMarker mk;
        mk.id = r.u16();
        mk.x = r.f32(); mk.y = r.f32();
        mk.kind = r.u8();
        mk.owner = r.i8();
        mk.time = r.f32();
        mk.faction = (mk.owner >= 0 && mk.owner < static_cast<int>(world.agents.size())) ? world.agents[mk.owner].faction : Faction::Blue;
        world.markers.push_back(mk);
    }
    int ni = r.u16();
    world.interactables.resize(ni);
    for (int i = 0; i < ni; ++i) {
        Interactable& it = world.interactables[i];
        it.id = i;
        it.kind = static_cast<InteractKind>(r.u8());
        uint8_t f = r.u8();
        it.enabled = f & 1;
        it.state = (f & 2) ? 1 : 0;
        it.progress = r.unorm8();
        it.x = r.f32(); it.y = r.f32();
        it.pickup = static_cast<PickupKind>(r.u8());
        it.weapon = r.i8();
        it.rounds = r.u8();
        it.target_agent = r.i8();
        it.room = r.i8();
        it.cx = r.i8();
        it.cy = r.i8();
        it.z = (it.kind == InteractKind::LightSwitch) ? 1.25 : 1.0;
    }
}

void apply_local_state(Agent& a, const std::vector<uint8_t>& raw, bool movement_too) {
    if (raw.empty()) return;
    Reader r(raw.data(), raw.size());
    double x = r.f64(), y = r.f64(), vx = r.f32(), vy = r.f32();
    double z = r.f32(), vz = r.f32();
    bool grounded = r.u8() != 0;
    double crouch = r.f32(), prone = r.f32();
    bool prone_target = r.u8() != 0;
    double lean = r.f32(), stamina = r.f32();
    bool exhausted = r.u8() != 0;
    double step_dist = r.f32();
    int steps = static_cast<int>(r.u32());
    StanceState ss = static_cast<StanceState>(r.u8());
    double st = r.f32(), px = r.f32(), py = r.f32(), tilt = r.f32(), bal = r.f32();
    if (movement_too) {
        a.x = x; a.y = y; a.vx = vx; a.vy = vy; a.z = z; a.vz = vz; a.grounded = grounded;
        a.crouch = crouch; a.prone = prone; a.prone_target = prone_target; a.lean = lean;
        a.stamina = stamina; a.exhausted = exhausted; a.step_dist = step_dist; a.steps = steps;
        a.stance.state = ss; a.stance.stumbleTimer = st; a.stance.pushX = px; a.stance.pushY = py;
        a.stance.tilt = tilt; a.stance.balance = bal;
    }
    a.slot = r.u8();
    int ns = r.u8();
    a.slots.resize(ns);
    for (int i = 0; i < ns; ++i) {
        WeaponSlot& s = a.slots[i];
        s.weapon = r.u8();
        s.mag = r.i16();
        s.mag_cap = r.u8();
        s.mag_condition = r.unorm8();
        uint8_t f = r.u8();
        s.chambered = f & 1;
        s.malfunction = (f & 2) ? 1 : 0;
        s.heat = r.unorm8();
        int nsp = r.u8();
        s.spares.resize(nsp);
        for (auto& m : s.spares) { m.rounds = r.u16(); m.capacity = r.u16(); m.condition = r.unorm8(); }
    }
    a.switch_timer = r.f32(); a.switch_total = r.f32(); a.switch_to = r.i8();
    a.fire_timer = r.f32(); a.recoil = r.f32(); a.shots_fired = static_cast<int>(r.u32());
    a.reload_stage = r.u8(); a.reload_timer = r.f32(); a.reload_stage_len = r.f32();
    a.reload_total = r.f32(); a.reload_empty = r.u8() != 0; a.reload_count = static_cast<int>(r.u32());
    ReloadPlan& p = a.rplan;
    p.style = static_cast<ReloadStyle>(r.u8()); p.tilt = r.f32(); p.mag_angle = r.f32();
    p.hand_arc = r.f32(); p.hand_lag = r.f32(); p.camera_bob = r.f32();
    uint8_t pf = r.u8();
    p.drop_mag = pf & 1; p.fumble = (pf & 2) != 0; p.fumble_done = (pf & 4) != 0; p.retain_mag = (pf & 8) != 0;
    p.seed = r.u32();
    a.inv.grenades = r.u8(); a.inv.bandages = r.u8();
    a.inv.flashlight_battery = r.unorm8(); a.inv.tablet = r.u8() != 0;
    for (int zi = 0; zi < kZoneCount; ++zi) a.health.zone[zi] = r.f32();
    a.health.bleeding = r.f32(); a.health.bleedout = r.f32();
    a.hp = r.f32();
    Physiology& ph = a.phys;
    // valorile autoritare se amesteca lin (evita salturi in efectele locale)
    double acute = r.f32(), chronic = r.f32(), adren = r.f32(), hr = r.f32(), ex = r.f32(), fh = r.f32();
    ph.acute += (acute - ph.acute) * 0.3;
    ph.chronic = chronic;
    ph.adrenaline += (adren - ph.adrenaline) * 0.3;
    ph.heart_rate += (hr - ph.heart_rate) * 0.2;
    ph.exertion += (ex - ph.exertion) * 0.3;
    ph.fire_heat += (fh - ph.fire_heat) * 0.3;
    ph.time_since_threat = r.f32();
    ph.time_since_shot = r.f32();
    a.interact_id = r.i16();
    a.interact_progress = r.unorm8();
    a.bandage_progress = r.unorm8();
    a.kick_cooldown = r.f32();
    if (!a.slots.empty()) a.ammo = a.slots[std::clamp(a.slot, 0, ns - 1)].total();
}

void apply_agent_state(Agent& a, const AgentNetState& s) {
    a.id = s.id;
    a.faction = s.faction;
    a.is_human = s.is_human;
    a.state = s.state;
    a.x = s.x; a.y = s.y; a.z = s.z;
    a.facing = s.facing; a.pitch = s.pitch;
    a.vx = s.vx; a.vy = s.vy; a.speed = s.speed; a.bob_phase = s.bob_phase;
    a.crouch = s.crouch; a.prone = s.prone; a.lean = s.lean; a.aim = s.aim; a.tablet = s.tablet;
    a.hp = s.hp;
    a.health.downed = s.downed;
    a.flashlight = s.flashlight;
    a.sprinting = s.sprinting;
    a.possess = s.possess;
    a.drone_id = s.drone_id;
    a.vehicle_id = s.vehicle_id;
    a.stance.state = s.stance;
    a.stance.tilt = s.tilt;
    a.death_timer = s.death_timer;
    a.death_dir = s.death_dir;
    if (a.slots.empty()) a.slots.push_back(WeaponSlot{});
    a.slot = std::clamp(a.slot, 0, static_cast<int>(a.slots.size()) - 1);
    a.slots[a.slot].weapon = s.weapon;
    a.reload_stage = s.reload_stage;
    a.w_yaw = a.facing;
    a.w_pitch = a.pitch;
}

AgentNetState lerp_agent(const AgentNetState& a, const AgentNetState& b, double t) {
    AgentNetState o = b;
    auto L = [&](double x, double y) { return x + (y - x) * t; };
    o.x = L(a.x, b.x); o.y = L(a.y, b.y); o.z = L(a.z, b.z);
    o.facing = a.facing + wrap_pi(b.facing - a.facing) * t;
    o.pitch = L(a.pitch, b.pitch);
    o.crouch = L(a.crouch, b.crouch); o.prone = L(a.prone, b.prone); o.lean = L(a.lean, b.lean);
    o.aim = L(a.aim, b.aim); o.tablet = L(a.tablet, b.tablet);
    o.speed = L(a.speed, b.speed);
    double db = b.bob_phase - a.bob_phase;
    if (db < -kPi * 0.5) db += kPi;    // faza pasului se reia la fiecare pas
    o.bob_phase = std::fmod(a.bob_phase + db * t + kPi, kPi);
    o.tilt = L(a.tilt, b.tilt);
    o.death_timer = L(a.death_timer, b.death_timer);
    return o;
}

// mu-law (G.711)
uint8_t mulaw_encode(int16_t sample) {
    const int BIAS = 0x84, CLIP = 32635;
    int s = sample;
    int sign = (s >> 8) & 0x80;
    if (sign) s = -s;
    if (s > CLIP) s = CLIP;
    s += BIAS;
    int exponent = 7;
    for (int mask = 0x4000; (s & mask) == 0 && exponent > 0; mask >>= 1) exponent--;
    int mantissa = (s >> (exponent + 3)) & 0x0F;
    return static_cast<uint8_t>(~(sign | (exponent << 4) | mantissa));
}

int16_t mulaw_decode(uint8_t u) {
    u = static_cast<uint8_t>(~u);
    int sign = u & 0x80, exponent = (u >> 4) & 7, mantissa = u & 0x0F;
    int s = ((mantissa << 3) + 0x84) << exponent;
    s -= 0x84;
    return static_cast<int16_t>(sign ? -s : s);
}

}
}

