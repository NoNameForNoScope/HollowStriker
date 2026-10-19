#include "viewmodel.hpp"
#include "textures.hpp"
#include "math3d.hpp"
#include "mesh.hpp"
#include "weapon_mesh.hpp"
#include "../core/profiler.hpp"
#include "../core/ini.hpp"
#include <sstream>
#include <cmath>
#include <algorithm>

namespace bodycam {

namespace {
constexpr double kPi = 3.14159265358979323846;
double wrap_pi(double a) {
    while (a > kPi) a -= 2.0 * kPi;
    while (a < -kPi) a += 2.0 * kPi;
    return a;
}
inline double ease(double t) { t = std::clamp(t, 0.0, 1.0); return t * t * (3.0 - 2.0 * t); }

struct Raster {
    HdrFrame& f;
    double focal;
    double cx, cy;
    bool project(V3 p, double& sx, double& sy) const {
        if (p.z < 0.03) return false;
        sx = cx + p.x / p.z * focal;
        sy = cy + p.y / p.z * focal;
        return true;
    }
};

void fill_poly(HdrFrame& f, const double* xs, const double* ys, int n, Rgb col, float alpha, bool additive) {
    double miny = 1e9, maxy = -1e9;
    for (int i = 0; i < n; ++i) { miny = std::min(miny, ys[i]); maxy = std::max(maxy, ys[i]); }
    int y0 = std::max(0, static_cast<int>(std::ceil(miny - 0.5)));
    int y1 = std::min(f.h - 1, static_cast<int>(std::floor(maxy - 0.5)));
    for (int y = y0; y <= y1; ++y) {
        double py = y + 0.5;
        double xl = 1e9, xr = -1e9;
        for (int i = 0; i < n; ++i) {
            int j = (i + 1) % n;
            double ya = ys[i], yb = ys[j];
            if ((py < std::min(ya, yb)) || (py > std::max(ya, yb)) || ya == yb) continue;
            double t = (py - ya) / (yb - ya);
            double x = xs[i] + (xs[j] - xs[i]) * t;
            xl = std::min(xl, x);
            xr = std::max(xr, x);
        }
        int x0 = std::max(0, static_cast<int>(std::ceil(xl - 0.5)));
        int x1 = std::min(f.w - 1, static_cast<int>(std::floor(xr - 0.5)));
        for (int x = x0; x <= x1; ++x) {
            float* p = f.px(x, y);
            if (additive) { p[0] += col.r * alpha; p[1] += col.g * alpha; p[2] += col.b * alpha; }
            else if (alpha >= 1.0f) { p[0] = col.r; p[1] = col.g; p[2] = col.b; }
            else {
                p[0] = p[0] * (1 - alpha) + col.r * alpha;
                p[1] = p[1] * (1 - alpha) + col.g * alpha;
                p[2] = p[2] * (1 - alpha) + col.b * alpha;
            }
            f.depth[static_cast<size_t>(y) * f.w + x] = 0.3f; // prim-plan (pentru DoF)
        }
    }
}

// Mana cu degete: incheietura, directia degetelor intinse (fwd), normala palmei
// (spre obiectul apucat). curl = indoirea pe fiecare articulatie (rad); degetele se
// infasoara in jurul unui maner / unei garzi. thumb_side alege partea policelui.
void hand_mesh(Mesh& m, V3 wrist, V3 fwd, V3 palm, double curl, double index_curl, double thumb_curl,
               double thumb_side, Rgb col, V3 thumb_dir = {}, Rgb tip_col = Rgb{-1.0f, 0.0f, 0.0f}) {
    if (tip_col.r < 0.0f) tip_col = col;
    fwd = norm(fwd);
    palm = norm(palm - fwd * dot(palm, fwd));
    V3 side = norm(cross(fwd, palm));
    // palma (dosul mainii usor bombat)
    m.box(wrist + fwd * 0.042 - palm * 0.004, side * 0.041, palm * 0.015, fwd * 0.044, col);
    m.limb(wrist - fwd * 0.03, wrist + fwd * 0.01, 0.026, 0.03, 6, col, side, 1.35);   // incheietura (manusa)
    V3 knuckles = wrist + fwd * 0.086;
    const double seg[3] = {0.04, 0.025, 0.02};
    for (int f = 0; f < 4; ++f) {
        double off = (1.5 - f) * 0.0195 * thumb_side;            // f = 0: aratatorul (langa police)
        double scale = f == 3 ? 0.8 : (f == 0 ? 0.95 : 1.0);
        double c = f == 0 ? index_curl : curl * (1.0 + 0.04 * f);
        V3 p = knuckles + side * off - fwd * (f == 3 ? 0.008 : 0.0);
        V3 d = fwd;
        for (int k = 0; k < 3; ++k) {
            d = norm(rotate_axis(d, side, c * (k == 0 ? 0.8 : 1.0)));
            V3 np = p + d * (seg[k] * scale);
            double r0 = 0.0095 - 0.0012 * k, r1 = r0 - 0.0008;
            uint8_t keep = m.mat;
            if (k >= 1 && (tip_col.r != col.r || tip_col.g != col.g)) m.mat = MatSkin;
            m.limb(p, np, r0, r1, 5, k >= 1 ? tip_col : col, palm, 1.0, k == 2);
            m.mat = keep;
            p = np;
        }
    }
    // policele: pleaca din lateralul palmei si se inchide peste obiect
    V3 tb = wrist + fwd * 0.03 + side * (0.036 * thumb_side) + palm * 0.012;
    V3 td = len(thumb_dir) > 1e-6 ? norm(thumb_dir) : norm(fwd * 0.55 + side * (0.45 * thumb_side) + palm * 0.45);
    V3 p = tb;
    for (int k = 0; k < 2; ++k) {
        td = norm(rotate_axis(td, side * thumb_side, -thumb_curl * 0.6) + palm * (0.25 * thumb_curl));
        V3 np = p + td * (k == 0 ? 0.034 : 0.026);
        m.limb(p, np, 0.0105 - 0.0015 * k, 0.0095 - 0.0015 * k, 5, k == 1 ? tip_col : col, palm, 1.0, k == 1);
        p = np;
    }
}

float frand(uint32_t& s) {
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return (s & 0xFFFFFF) / 16777215.0f;
}

// Amprente si zgarieturi pe ecranul tabletei (u,v in 0..1)
float smudge(double u, double v) {
    const double cx[4] = {0.28, 0.66, 0.45, 0.8};
    const double cy[4] = {0.62, 0.35, 0.82, 0.7};
    float s = 0.0f;
    for (int k = 0; k < 4; ++k) {
        double dx = (u - cx[k]) * 1.5, dy = v - cy[k];
        double r2 = dx * dx + dy * dy;
        if (r2 > 0.012) continue;
        double r = std::sqrt(r2);
        // creste fine de amprenta, deformate de zgomot (nu cercuri perfecte)
        double warp = tex::vnoise(static_cast<float>(u * 40.0), static_cast<float>(v * 40.0), 503 + k) * 0.01;
        s += static_cast<float>((0.5 + 0.5 * std::sin((r + warp) * 520.0 + k)) * std::exp(-r2 / 0.0025) * 0.35);
    }
    s += std::max(0.0f, tex::vnoise(static_cast<float>(u * 9.0), static_cast<float>(v * 6.0), 501) - 0.55f) * 0.8f;
    return std::min(1.0f, s);
}
float scratch(double u, double v) {
    float s = 0.0f;
    if (std::abs(u * 0.8 + v * 0.35 - 0.62) < 0.0012 && u > 0.42 && u < 0.58) s += 0.35f;
    if (std::abs(u * 0.2 - v * 0.9 + 0.55) < 0.0010 && v > 0.64 && v < 0.7) s += 0.25f;
    if (std::abs(u - 0.12 - v * 0.05) < 0.0010 && v > 0.18 && v < 0.24) s += 0.2f;
    return s;
}

// Pozitia piciorului / bratului pentru corpul propriu: spatiul corpului -> camera
V3 body_to_cam(V3 b, double cam_pitch) {
    double phi = -cam_pitch;
    double c = std::cos(phi), s = std::sin(phi);
    return {b.x, b.y * c - b.z * s, b.y * s + b.z * c};
}

}

// ============================================================================
void ViewmodelState::on_shot(const WeaponData& w, double heat) {
    back.impulse(0.9 * w.camera_kick * w.recoil_back);
    rot.impulse(1.0 * w.recoil_rot);
    shoulder.impulse(0.8 * w.recoil_body);
    flash = 1.0;
    flash_rot = frand(rng) * 6.28;
    // gaze si fum la gura tevii (mai mult cand arma e calda)
    int n = static_cast<int>(1 + w.muzzle_smoke * (1.5 + 3.0 * heat));
    for (int i = 0; i < n; ++i) {
        Smoke s;
        s.x = muzzle_x + (frand(rng) - 0.5) * 0.02;
        s.y = muzzle_y + (frand(rng) - 0.5) * 0.02;
        s.z = muzzle_z;
        s.vx = (frand(rng) - 0.5) * 0.4;
        s.vy = -0.15 - 0.2 * frand(rng);
        s.vz = 0.4 + 0.6 * frand(rng);
        s.max_life = s.life = 0.6 + 1.0 * frand(rng) * (0.5 + heat);
        s.size = 0.012 + 0.01 * frand(rng);
        smoke.push_back(s);
    }
    if (smoke.size() > 80) smoke.erase(smoke.begin(), smoke.begin() + (smoke.size() - 80));
}

void ViewmodelState::on_pump() {
    pump.impulse(-1.4);
}

void ViewmodelState::update(double dt, const Agent& a, const WeaponData* w, const CameraPose& pose, bool inspecting) {
    back.step(0.0, 320.0, 30.0, dt);
    rot.step(0.0, 260.0, 24.0, dt);
    shoulder.step(0.0, 90.0, 13.0, dt);   // umarul si trunchiul: raspuns mai lent
    pump.step(0.0, 120.0, 22.0, dt);
    jolt.step(0.0, 260.0, 18.0, dt);
    flash = std::max(0.0, flash - dt / 0.05);
    auto approach = [&](double& v, double target, double rate) { v += (target - v) * std::min(1.0, dt * rate); };
    double fm = std::max(0.4, a.phys.fine_motor);
    approach(sprint_w, a.sprinting ? 1.0 : 0.0, 7.0);
    bool reloading = a.reload_stage != 0 && a.reload_stage != RS_Raise;
    approach(reload_w, reloading ? 1.0 : 0.0, 7.0 * fm);
    approach(inspect_w, inspecting ? 1.0 : 0.0, 6.0);
    approach(examine_w, (examine_held && a.reload_stage == 0 && !inspecting && a.aim < 0.2) ? 1.0 : 0.0, 4.0 * fm);
    approach(bandage_w, a.bandage_progress > 0.0 ? 1.0 : 0.0, 5.0);
    approach(wall_w, a.wall_block, 12.0);
    approach(low_w, (a.possess != Possession::Body || a.stance.state == StanceState::Ragdoll || a.health.downed
                     || a.mantling()) ? 1.0 : 0.0, a.mantling() ? 9.0 : 6.0);
    approach(reach_w, reach_active ? 1.0 : 0.0, 5.0 * fm);
    tablet_w = a.tablet;
    // inertia fata de acceleratia corpului: la pornire arma ramane in urma, la oprire inainteaza
    {
        double ax = (a.vx - prev_vx) / dt, ay = (a.vy - prev_vy) / dt;
        prev_vx = a.vx; prev_vy = a.vy;
        double cf = std::cos(a.facing), sf = std::sin(a.facing);
        double af = ax * cf + ay * sf, al = -ax * sf + ay * cf;
        double heavy = w ? std::sqrt(w->weight / 3.4) : 1.0;
        acc_z.step(std::clamp(-af * 0.0035 * heavy, -0.03, 0.03), 90.0, 11.0, dt);
        acc_x.step(std::clamp(-al * 0.0030 * heavy, -0.025, 0.025), 90.0, 11.0, dt);
    }
    // regrip: dupa sprint, salt/escaladare, lovituri puternice; din cand in cand si in repaus
    if (was_sprinting && !a.sprinting) regrip_kick(0.12);
    was_sprinting = a.sprinting;
    if (a.stance.state == StanceState::Stumbling || a.stance.state == StanceState::Unstable) regrip_kick(0.25);
    if (a.speed < 0.3 && a.aim < 0.1 && a.reload_stage == 0 && a.tablet < 0.05) {
        idle_timer += dt;
        if (idle_timer > 9.0 + 6.0 * frand(rng)) { regrip_kick(0.0); idle_timer = 0.0; }
    } else idle_timer = 0.0;
    if (regrip_t >= 0.0) {
        if (regrip_delay > 0.0) regrip_delay -= dt;
        else {
            regrip_t += dt / (0.55 / fm);
            if (regrip_t >= 1.0) regrip_t = -1.0;
        }
    }
    if (throw_t >= 0.0) {
        throw_t += dt / 0.75;
        if (throw_t >= 1.0) throw_t = -1.0;
    }
    if (melee_t >= 0.0) {
        melee_t += dt / (melee_knife ? 0.42 : 0.5);
        if (melee_t >= 1.0) melee_t = -1.0;
    }
    approach(cook_w, a.nade_cooking ? 1.0 : 0.0, 7.0);
    if (a.nade_cooking) nade_type = static_cast<int>(a.nade_type);
    // tableta: inertie proprie, degetul care se desprinde de pe margine, apropierea la pilotare
    tab_jolt.step(0.0, 150.0, 13.0, dt);
    approach(touch_w, touch_target, 6.0 * fm);
    approach(focus_w, focus_target, 3.5);
    // inertia pozitiei: camera se roteste, arma ramane putin in urma
    double yaw_rate = wrap_pi(pose.yaw - prev_cam_yaw) / dt;
    double pitch_rate = (pose.pitch - prev_cam_pitch) / dt;
    prev_cam_yaw = pose.yaw;
    prev_cam_pitch = pose.pitch;
    double heavy = w ? std::sqrt(w->weight / 3.4) : 1.0;
    lag_x.step(std::clamp(-yaw_rate * 0.006 * heavy, -0.05, 0.05), 140.0, 15.0, dt);
    lag_y.step(std::clamp(pitch_rate * 0.005 * heavy, -0.04, 0.04), 140.0, 15.0, dt);
    // smucituri la evenimentele reincarcarii
    if (a.reload_stage != last_stage) {
        if (last_stage == RS_MagIn || last_stage == RS_Bolt || last_stage == RS_Clear) jolt.impulse(1.2);
        if (a.reload_stage == 0 && last_stage != 0) regrip_kick(0.0);   // mana revine si isi reface priza
        last_stage = a.reload_stage;
    }
    // fum: se ridica, se extinde si ramane in urma la rotirea camerei
    for (auto& s : smoke) {
        s.life -= dt;
        s.vy -= 0.12 * dt;
        s.vx *= std::exp(-dt * 1.5); s.vy *= std::exp(-dt * 0.8); s.vz *= std::exp(-dt * 2.0);
        s.x += s.vx * dt - yaw_rate * dt * s.z;
        s.y += s.vy * dt + pitch_rate * dt * s.z;
        s.z += s.vz * dt;
        s.size += dt * 0.05;
    }
    smoke.erase(std::remove_if(smoke.begin(), smoke.end(), [](const Smoke& s) { return s.life <= 0.0 || s.z < 0.05; }), smoke.end());
    // teava fierbinte fumega continuu
    if (w && a.cur_slot() && a.cur_slot()->heat > w->smoke_heat) {
        double heat = a.cur_slot()->heat;
        if (frand(rng) < dt * 12.0 * (heat - w->smoke_heat) / (1.0 - w->smoke_heat + 1e-3)) {
            Smoke s{muzzle_x, muzzle_y - 0.01, muzzle_z - 0.05, (frand(rng) - 0.5) * 0.05, -0.06, 0.0, 1.8, 1.8, 0.008};
            smoke.push_back(s);
        }
    }
}

namespace {
// Pozitia unei maini: incheietura, directia degetelor, normala palmei, indoirea degetelor
struct HandPose {
    V3 wrist{0, 0, 0.3};
    V3 fwd{0, 0, 1};
    V3 palm{0, 1, 0};
    double curl = 0.9, index = 0.9, thumb = 0.8, thumb_side = 1.0;
    V3 thumb_dir{0, 0, 0};
};
HandPose blend(const HandPose& a, const HandPose& b, double t) {
    if (t <= 0.0) return a;
    if (t >= 1.0) return b;
    HandPose r;
    r.wrist = lerp(a.wrist, b.wrist, t);
    r.fwd = norm(lerp(a.fwd, b.fwd, t));
    r.palm = norm(lerp(a.palm, b.palm, t));
    r.curl = a.curl + (b.curl - a.curl) * t;
    r.index = a.index + (b.index - a.index) * t;
    r.thumb = a.thumb + (b.thumb - a.thumb) * t;
    r.thumb_side = t < 0.5 ? a.thumb_side : b.thumb_side;
    r.thumb_dir = (len(a.thumb_dir) > 0 && len(b.thumb_dir) > 0) ? norm(lerp(a.thumb_dir, b.thumb_dir, t))
                                                                   : (t < 0.5 ? a.thumb_dir : b.thumb_dir);
    return r;
}
// Mana generica (fara obiect apucat): degetele continua directia antebratului
HandPose free_hand(V3 hand_center, V3 shoulder, double curl, bool left) {
    HandPose h;
    h.fwd = norm(hand_center - shoulder + V3{0.0, 0.05, 0.12});
    h.palm = norm(V3{left ? 1.0 : -1.0, 0.35, 0.0});
    h.wrist = hand_center - h.fwd * 0.05;
    h.curl = curl; h.index = curl * 0.9; h.thumb = 0.7;
    h.thumb_side = left ? -1.0 : 1.0;
    return h;
}
}

// ============================================================================
bool ViewmodelPoses::load(const std::string& path) {
    loaded = true;   // fara fisier raman valorile implicite
    IniParser ini;
    if (!ini.load(path)) return false;
    const char* names[5] = {"rifle", "smg", "pistol", "shotgun", "sniper"};
    auto v3 = [&](const char* sec, const char* key, V3 def) {
        std::string s = ini.get(sec, key, "");
        if (s.empty()) return def;
        std::stringstream ss(s);
        V3 v = def;
        ss >> v.x >> v.y >> v.z;
        return v;
    };
    for (int m = 0; m < 5; ++m) {
        ViewmodelPose& q = by_model[m];
        // fiecare tip mosteneste din [rifle], apoi isi suprascrie valorile
        if (m > 0) q = by_model[0];
        {
            const char* sec = names[m];
            q.hip = v3(sec, "hip", q.hip);
            q.hip_rot = v3(sec, "hip_rot", q.hip_rot);
            q.ads_dz = ini.get_d(sec, "ads_dz", q.ads_dz);
            q.ads_fov = ini.get_d(sec, "ads_fov", q.ads_fov);
            q.ads_wrist_drop = ini.get_d(sec, "ads_wrist_drop", q.ads_wrist_drop);
            q.ads_hand_drop = ini.get_d(sec, "ads_hand_drop", q.ads_hand_drop);
            q.shoulder_r = v3(sec, "shoulder_r", q.shoulder_r);
            q.shoulder_l = v3(sec, "shoulder_l", q.shoulder_l);
            q.elbow_r = v3(sec, "elbow_r", q.elbow_r);
            q.elbow_l = v3(sec, "elbow_l", q.elbow_l);
            // fara valoare proprie la ochire: aceeasi directie ca la sold (daca sectiunea o defineste)
            q.elbow_r_ads = v3(sec, "elbow_r_ads", ini.get(sec, "elbow_r", "").empty() ? q.elbow_r_ads : q.elbow_r);
            q.elbow_l_ads = v3(sec, "elbow_l_ads", ini.get(sec, "elbow_l", "").empty() ? q.elbow_l_ads : q.elbow_l);
        }
    }
    return true;
}

// ============================================================================
void draw_viewmodel(HdrFrame& f, const CameraPose& pose, const Agent& a, const WeaponData& w,
                    ViewmodelState& vm, const Rgb& ambient_in, double time, const TabletScreen* tablet,
                    const HdrFrame* scope_view, const CharacterProfile* profile) {
    PROFILE_SCOPE("render.viewmodel");
    if (a.state == FsmState::Dead) return;
    static thread_local Mesh mesh;
    static thread_local std::vector<Rgb> lit;
    static thread_local std::vector<float> zb;
    mesh.clear();
    const ViewmodelPose& VP = vm.poses.by_model[std::clamp(static_cast<int>(w.model), 0, 4)];
    // focala primului plan: campul vizual al armei (mai ingust la ochire), inapoi la campul camerei cand tableta urca
    const double focal_cam = focal_length(f, pose.hfov_deg);
    const double wfov = vm.weapon_fov * (1.0 + (VP.ads_fov - 1.0) * std::clamp(a.aim, 0.0, 1.0));
    double focal_vm = focal_length(f, std::min(pose.hfov_deg, wfov));
    {
        // tableta, reincarcarea, verificarea / inspectarea armei, bandajul: mainile lucreaza jos, in fata
        // pieptului - campul camerei (animatiile acestea sunt asezate pentru el)
        const double tb = tablet ? std::clamp(a.tablet, 0.0, 1.0) : 0.0;
        const double busy = std::max({ease(vm.reload_w), ease(vm.inspect_w), ease(vm.examine_w), ease(vm.bandage_w)});
        focal_vm += (focal_cam - focal_vm) * busy;
        // tableta ridicata: camp de cel mult 100 grade, ca sa ramana aproape pe tot ecranul si la camp larg
        const double focal_tab = focal_length(f, std::min(pose.hfov_deg, 100.0));
        focal_vm += (focal_tab - focal_vm) * ease(tb / 0.6);
    }
    vm.vm_k = focal_vm / focal_cam;
    Raster r{f, focal_vm, f.w * 0.5, f.h * 0.5};
    Rgb ambient = ambient_in * 0.85f + Rgb{0.01f, 0.01f, 0.012f};
    Rgb flash = Rgb{1.0f, 0.7f, 0.35f} * static_cast<float>(vm.flash * 6.0 * w.flash_intensity * (w.has_suppressor ? 0.15 : 1.0));
    if (a.flashlight) ambient += Rgb{0.06f, 0.06f, 0.055f};
    const V3 light_dir = norm(V3{0.25, -0.9, -0.2});
    const WeaponSlot* sl = a.cur_slot();
    const double heat = sl ? sl->heat : 0.0;
    const double arm = a.health.arm_injury();
    const uint32_t seed = a.rplan.seed;
    const Rgb black{0.045f, 0.045f, 0.05f}, dark{0.07f, 0.07f, 0.075f}, poly{0.058f, 0.058f, 0.056f};
    const Rgb metal{0.11f, 0.11f, 0.115f};
    // bratele apartin aceluiasi personaj: culorile si echipamentul vin din profilul jucatorului
    const Rgb glove = profile ? Rgb{profile->glove_color.r, profile->glove_color.g, profile->glove_color.b} : Rgb{0.085f, 0.07f, 0.052f};
    const Rgb sleeve = profile ? Rgb{profile->top.r, profile->top.g, profile->top.b} * 0.4f : Rgb{0.030f, 0.033f, 0.040f};
    const Rgb skin_col = profile ? Rgb{profile->skin.r, profile->skin.g, profile->skin.b} * 0.55f : Rgb{0.3f, 0.21f, 0.16f};
    const bool bare_fingers = profile && profile->gloves == GloveType::Fingerless;
    const bool bare_hands = profile && profile->gloves == GloveType::None;
    const Rgb wood{0.16f, 0.09f, 0.05f};

    // ---------------------------------------------------------------------
    // Corpul propriu: picioare, centura cu port-incarcatoare (inventarul real), tocul
    // ---------------------------------------------------------------------
    if (pose.pitch < -0.25 && a.prone < 0.3 && !a.health.downed) {
        const double cp = pose.pitch;
        const Rgb vest{0.07f, 0.075f, 0.06f}, pants{0.05f, 0.055f, 0.065f}, boot{0.03f, 0.028f, 0.025f};
        M3 B = rot_x(cp);
        auto bbox = [&](V3 c, V3 h, Rgb col) { mesh.box(body_to_cam(c, cp), B.col(0) * h.x, B.col(1) * h.y, B.col(2) * h.z, col); };
        bbox({0.0, 0.48, 0.14}, {0.19, 0.03, 0.06}, Rgb{0.035f, 0.035f, 0.03f});
        int mags = sl ? static_cast<int>(sl->spares.size()) : 0;
        for (int k = 0; k < std::min(mags, 4); ++k) bbox({-0.11 + 0.055 * k, 0.43, 0.2}, {0.022, 0.035, 0.018}, vest);
        bbox({0.15, 0.44, 0.15}, {0.03, 0.05, 0.03}, Rgb{0.03f, 0.03f, 0.032f});
        if (a.inv.tablet && a.tablet < 0.3) bbox({-0.17, 0.5, 0.1}, {0.02, 0.06, 0.085}, Rgb{0.04f, 0.04f, 0.042f});   // port-tableta
        double swing = std::sin(a.bob_phase) * 0.18 * std::min(1.0, a.speed / 2.0);
        double knee_fwd = 0.08 + 0.25 * a.crouch;
        for (int side = -1; side <= 1; side += 2) {
            double s = (side < 0) ? swing : -swing;
            V3 hip{0.1 * side, 0.5 - 0.15 * a.crouch, 0.04};
            V3 knee{0.1 * side, 0.95 - 0.35 * a.crouch, knee_fwd + s};
            V3 foot{0.11 * side, 1.38 - 0.4 * a.crouch, 0.12 + s * 0.6};
            mesh.limb(body_to_cam(hip, cp), body_to_cam(knee, cp), 0.078, 0.064, 7, pants, B.col(0));
            mesh.limb(body_to_cam(knee, cp), body_to_cam(foot, cp), 0.062, 0.05, 7, pants, B.col(0));
            bbox({foot.x, foot.y + 0.04, foot.z + 0.06}, {0.05, 0.04, 0.12}, boot);
        }
    }
    const size_t body_verts = mesh.pos.size();   // corpul propriu: proiectat cu campul camerei (vezi mai jos)

    // ---------------------------------------------------------------------
    // Pozitia armei
    // ---------------------------------------------------------------------
    const bool pistol = w.model == WeaponModel::Pistol;
    const bool shotgun = w.model == WeaponModel::Shotgun;
    const bool smg = w.model == WeaponModel::Smg;
    const bool sniper = w.model == WeaponModel::Sniper;
    V3 P_hip = VP.hip;
    V3 P_ads = pistol ? V3{0.0, 0.0, 0.33} : V3{0.0, 0.0, 0.235};
    if (!pistol) {
        // linia de ochire a opticii montate (sau a catarii) trece prin centrul imaginii
        P_ads.y = -weapon_sight_y(w);
        // luneta: ochiul sta la distanta de ocular (eye relief), arma vine mai aproape
        if (w.has_scope) P_ads.z = 0.07 + w.eye_relief - (w.sockets.optic.z - 0.1);
    }
    if (!w.has_scope) P_ads.z += VP.ads_dz;
    V3 P = P_hip + (P_ads - P_hip) * a.aim;
    double yaw = wrap_pi(a.w_yaw + a.sway_yaw + a.phys.tremor_yaw + a.kick_yaw - pose.yaw);
    double pitch = (a.w_pitch + a.sway_pitch + a.phys.tremor_pitch + a.kick_pitch) - pose.pitch;
    double roll = 0.0;
    yaw += (1.0 - a.aim) * VP.hip_rot.x;
    pitch += (1.0 - a.aim) * VP.hip_rot.y;
    roll += (1.0 - a.aim) * VP.hip_rot.z;
    // tremurul mainilor: deplasarea calculata in lantul corpului (corelata cu orientarea armei)
    P.x += a.phys.hand_x;
    P.y += a.phys.hand_y;
    P.x += vm.lag_x.x + vm.acc_x.x;
    P.y += vm.lag_y.x;
    P.z += vm.acc_z.x;
    P.y += 0.025 * a.prone;
    pitch += 0.04 * a.prone;
    roll += a.lean * 0.15;
    P.y += 0.03 * arm;
    roll += 0.12 * arm;
    P = P + (V3{0.06, 0.27, 0.26} - P) * vm.sprint_w;
    yaw += -0.4 * vm.sprint_w; pitch += -0.55 * vm.sprint_w; roll += -0.45 * vm.sprint_w;
    const ReloadPlan& plan = a.rplan;
    P = P + (V3{0.05, 0.19 + 0.03 * arm, 0.29} - P) * vm.reload_w;
    roll += plan.tilt * vm.reload_w;
    pitch += (-0.2 + 0.1 * plan.mag_angle) * vm.reload_w;
    yaw += (-0.15 + 0.5 * plan.mag_angle) * vm.reload_w;
    if (a.prone > 0.3) {
        double k = vm.reload_w * a.prone;
        P = P + (V3{0.02, 0.16, 0.36} - P) * k;
        roll += 0.8 * k; pitch += 0.22 * k;
    } else if (a.crouch > 0.5) {
        double k = vm.reload_w * a.crouch;
        P.y += 0.07 * k; P.x -= 0.02 * k;
        pitch -= 0.18 * k;
    }
    P = P + (V3{0.04, 0.17, 0.28} - P) * vm.inspect_w;
    roll += 0.65 * vm.inspect_w; yaw += -0.2 * vm.inspect_w;
    const double ex = ease(vm.examine_w);
    P = P + (V3{0.0, 0.15, 0.33} - P) * ex;
    roll += (-1.0 + 0.12 * std::sin(time * 0.7)) * ex;
    yaw += (0.7 + 0.12 * std::sin(time * 0.45)) * ex;
    pitch += 0.12 * ex;
    const double bw = ease(vm.bandage_w);
    P = P + (V3{0.17, 0.38, 0.22} - P) * bw;
    pitch += -0.85 * bw; roll += 0.5 * bw;
    const double wb = ease(vm.wall_w);
    P.z -= 0.16 * wb; P.y += 0.03 * wb; P.x -= 0.02 * wb;
    pitch += 0.75 * wb; roll += 0.25 * wb;
    // tableta: intai arma coboara pe curea (mainile o elibereaza), apoi tableta urca in fata
    const double tabt = tablet ? std::clamp(a.tablet, 0.0, 1.0) : 0.0;
    const double wl = ease(tabt / 0.45);
    const double tup = ease((tabt - 0.35) / 0.65);
    P = P + (V3{0.13, 0.42, 0.2} - P) * wl;
    pitch += -0.95 * wl; roll += 0.55 * wl; yaw += 0.25 * wl;
    const double rw = ease(vm.reach_w);
    P.y += 0.06 * rw; roll += 0.2 * rw;
    const double thr = vm.throw_t >= 0.0 ? std::sin(kPi * vm.throw_t) : 0.0;
    P.y += 0.12 * thr; P.x += 0.03 * thr; roll += 0.35 * thr;
    const double ck = ease(vm.cook_w);
    P.y += 0.07 * ck; roll += 0.2 * ck;
    // corp la corp: lovitura cu patul armei (inainte-stanga) sau cutitul (arma coborata)
    const double ml = vm.melee_t >= 0.0 ? std::sin(kPi * std::min(1.0, vm.melee_t * 1.3)) : 0.0;
    if (!vm.melee_knife) { P.z += 0.11 * ml; P.x -= 0.07 * ml; P.y -= 0.02 * ml; yaw -= 0.55 * ml; roll -= 0.5 * ml; pitch += 0.15 * ml; }
    else { P.y += 0.12 * ml; roll += 0.3 * ml; }
    if (a.switch_timer > 0.0 && a.switch_total > 0.0) {
        double t = 1.0 - a.switch_timer / a.switch_total;
        const double hfrac = 0.46;
        double k = t < hfrac ? ease(t / hfrac) : 1.0 - ease((t - hfrac) / (1.0 - hfrac));
        V3 dest = pistol ? V3{0.2, 0.62, 0.1} : V3{-0.08, 0.55, 0.16};
        P = P + (dest - P) * k;
        pitch += -1.15 * k;
        roll += (pistol ? 0.35 : -0.95) * k;
        yaw += (pistol ? 0.25 : -0.45) * k;
    }
    P.y += 0.38 * vm.low_w;
    const double rg = (vm.regrip_t >= 0.0 && vm.regrip_delay <= 0.0) ? std::sin(kPi * vm.regrip_t) : 0.0;
    P.y += 0.008 * rg; roll += 0.05 * rg;
    P.z -= vm.back.x * 0.05 + vm.shoulder.x * 0.012;
    P.y -= vm.shoulder.x * 0.004;
    pitch += vm.back.x * 0.05 + vm.rot.x * 0.035 + vm.shoulder.x * 0.01;
    roll += vm.shoulder.x * 0.012;
    P.y += vm.jolt.x * 0.008;
    pitch += vm.jolt.x * 0.02;

    const M3 G = rot_z(roll) * rot_x(pitch) * rot_y(yaw);
    auto to_cam = [&](V3 local) { return P + G * local; };
    auto box = [&](V3 c, V3 h, Rgb col, M3 local = identity(), Rgb em = {}) {
        M3 R = G * local;
        mesh.box(to_cam(c), R.col(0) * h.x, R.col(1) * h.y, R.col(2) * h.z, col, em);
    };
    auto cyl = [&](V3 p0, V3 p1, double ra, double rb, int sides, Rgb col, bool caps = true, Rgb em = {}) {
        mesh.limb(to_cam(p0), to_cam(p1), ra, rb, sides, col, G * V3{0, -1, 0}, 1.0, caps, em);
    };
    auto cam_box = [&](V3 c, M3 R, V3 h, Rgb col) { mesh.box(c, R.col(0) * h.x, R.col(1) * h.y, R.col(2) * h.z, col); };

    int stage = a.reload_stage;
    double sp = 0.0;
    if (stage > 0) sp = std::clamp(1.0 - a.reload_timer / std::max(0.02, a.reload_stage_len), 0.0, 1.0);
    const double hl = plan.hand_lag * 0.012;
    const V3 hand_noise{hl * smooth_noise(time * 5.0, seed + 1), hl * smooth_noise(time * 5.7, seed + 2), hl * smooth_noise(time * 4.3, seed + 3)};

    // punctele de prindere vin din datele armei (si din atasamente: manerul vertical muta mana stanga)
    auto L3 = [](const WSocket& k) { return V3{k.x, k.y, k.z}; };
    V3 mag_local = L3(w.sockets.mag);
    V3 grip_local = L3(w.sockets.grip);
    double grip_tilt = w.sockets.grip_tilt;
    const double pump_off = shotgun ? std::clamp(vm.pump.x * 0.06, -0.08, 0.0) : 0.0;
    V3 fore_local = L3(w.sockets.support) + V3{0, 0, pump_off};
    V3 bolt_local = L3(w.sockets.bolt);
    (void)smg;
    bool mag_in_gun = !sl || sl->mag >= 0;
    double mag_drop = 0.0, mag_spin = 0.0;
    bool mag_with_hand = false;
    if (stage == RS_MagOut) {
        double t = std::clamp((sp - 0.35) / 0.65, 0.0, 1.0);
        mag_drop = (plan.drop_mag ? 0.55 : 0.3) * t * t;
        if (plan.drop_mag) mag_spin = t * 1.2;
        mag_in_gun = true;
        mag_with_hand = !plan.drop_mag && sp > 0.35;
    } else if (stage == RS_Fetch) {
        mag_in_gun = false;
        mag_with_hand = sp > 0.5;
    } else if (stage == RS_MagIn) {
        double t = ease(sp);
        mag_drop = plan.fumble && !plan.fumble_done ? 0.2 * (1.0 - std::sin(sp * kPi) * 0.85) : 0.2 * (1.0 - t);
        mag_in_gun = true;
        mag_with_hand = true;
    }
    mag_drop += 0.05 * vm.inspect_w;
    if (sl && !sl->mag_seated && stage == 0) mag_drop += 0.012;

    // --- Mana stanga in timpul reincarcarii (spatiul armei sau al camerei) ---
    const V3 pouch_cam{-0.16, 0.48, 0.18};
    V3 lh;
    bool lh_in_cam = false;
    switch (stage) {
        case RS_Lower: lh = lerp(fore_local, mag_local + V3{-0.01, 0.06, 0.0}, ease(sp) * 0.3); break;
        case RS_MagOut:
            lh = (sp < 0.35) ? lerp(fore_local, mag_local + V3{-0.01, 0.05, 0.0}, ease(sp / 0.35))
                             : mag_local + V3{-0.01, 0.05 + (mag_with_hand ? mag_drop : 0.0), 0.0};
            break;
        case RS_Fetch: {
            V3 start_cam = to_cam(mag_local + V3{-0.01, 0.12, 0.0});
            V3 ctrl = lerp(start_cam, pouch_cam, 0.5) + V3{-plan.hand_arc, 0.0, plan.hand_arc * 0.5};
            double t = sp < 0.5 ? sp / 0.5 : (1.0 - sp) / 0.5;
            lh = bezier(start_cam, ctrl, pouch_cam, ease(t));
            lh_in_cam = true;
            break;
        }
        case RS_MagIn: lh = mag_local + V3{-0.01, 0.05 + mag_drop, 0.0}; break;
        case RS_Bolt: {
            double t = sp < 0.55 ? ease(sp / 0.55) : 1.0 - ease((sp - 0.55) / 0.45) * 0.6;
            lh = lerp(mag_local + V3{-0.01, 0.05, 0.0}, bolt_local + V3{-0.02, 0.0, 0.0}, t);
            break;
        }
        case RS_Raise: lh = lerp(mag_local + V3{-0.01, 0.05, 0.0}, fore_local, ease(sp)); break;
        case RS_Shell: lh = lerp(fore_local, V3{0.0, 0.11, 0.12}, std::abs(std::sin(sp * kPi))); break;
        case RS_Clear:
            if (sp < 0.4) lh = lerp(fore_local, mag_local + V3{0.0, 0.08 - 0.03 * std::sin(sp / 0.4 * kPi), 0.0}, ease(sp / 0.4));
            else lh = lerp(V3{0.0, 0.02, -0.02}, V3{0.0, 0.02, -0.12}, ease((sp - 0.4) / 0.6));
            break;
        default: lh = fore_local; break;
    }
    if (vm.inspect_w > 0.3) lh = lh + (mag_local + V3{-0.01, mag_drop + 0.04, 0} - lh) * vm.inspect_w;
    V3 lh_cam = (lh_in_cam ? lh : to_cam(lh)) + hand_noise;
    bool lh_on_gun = stage == 0 && vm.inspect_w < 0.3;   // mana de sprijin pe garda / pompa

    // ---------------------------------------------------------------------
    // Geometria armei: constructorul comun (date + atasamente), acelasi ca pentru personaje
    // ---------------------------------------------------------------------
    const double glow = std::clamp((heat - w.glow_heat * 0.75) / (1.0 - w.glow_heat * 0.75 + 1e-3), 0.0, 1.0);
    WeaponVisualState wvs;
    wvs.mag_drop = mag_drop;
    wvs.mag_spin = mag_spin;
    if (pistol) {
        wvs.mag_in_gun = mag_in_gun && (stage == RS_MagOut || stage == RS_MagIn);
        wvs.mag_in_hand = mag_with_hand && !wvs.mag_in_gun;
        wvs.slide_back = (sl && !sl->chambered && sl->mag <= 0) ? 0.025 : 0.0;
        lh_on_gun = false;   // pistolul: mana stanga sustine mana dreapta
    } else {
        wvs.mag_in_gun = mag_in_gun && !mag_with_hand;
        wvs.mag_in_hand = mag_with_hand;
    }
    wvs.hand_pos = lh_cam;
    wvs.pump = pump_off;
    double chg = (stage == RS_Clear && sp > 0.4) ? -0.08 * std::sin((sp - 0.4) / 0.6 * kPi) : 0.0;
    if (ex > 0.4) chg = std::min(chg, -0.035 * std::clamp(std::sin(time * 0.9) * 1.4, 0.0, 1.0) * ex);
    wvs.charging = chg;
    // repetitie: inchizatorul se deschide si se inchide in timpul manevrei (dupa fiecare foc)
    if (a.bolt_timer > 0.0 && w.bolt_time > 0.0) {
        double tot = w.bolt_time / std::max(0.4, a.phys.fine_motor);
        double ph = std::clamp(1.0 - a.bolt_timer / tot, 0.0, 1.0);
        wvs.bolt_open = ph < 0.5 ? ease(ph / 0.5) : 1.0 - ease((ph - 0.5) / 0.5);
    }
    if (sniper && stage == RS_Clear) wvs.bolt_open = std::sin(sp * kPi);
    wvs.open_port = !sl || !sl->chambered || (stage == RS_Clear && sp > 0.4) || wvs.bolt_open > 0.3;
    wvs.heat_glow = glow;
    wvs.light_on = a.flashlight;
    wvs.laser_on = true;
    if (sl && !w.fire_modes.empty() && sl->fire_mode >= 0 && sl->fire_mode < static_cast<int>(w.fire_modes.size())) {
        switch (w.fire_modes[sl->fire_mode]) {
            case FireMode::Semi: wvs.selector = kPi * 0.5; break;
            case FireMode::Burst: wvs.selector = kPi * 0.75; break;
            case FireMode::Auto: wvs.selector = kPi; break;
            default: break;
        }
    }
    wvs.see_through = a.aim > 0.3;
    wvs.bipod_deploy = std::clamp(a.prone, 0.0, 1.0);
    wvs.show_stock = a.aim < 0.5 || vm.reload_w > 0.3 || vm.inspect_w > 0.3 || ex > 0.3;
    WeaponMeshOut wo;
    build_weapon_mesh(mesh, w, WeaponFrame{P, G}, wvs, wo);
    mesh.mat = MatDefault;
    V3 lens_c = wo.lens_c;
    double lens_r = wo.lens_r;
    bool optic = wo.optic;
    const bool scope = wo.scope;
    const V3 muzzle_cam = wo.muzzle, port_cam = wo.port;
    const V3 port_local = L3(w.sockets.port);
    vm.scope_lens = wo.lens_c;
    vm.scope_lens_r = scope ? wo.lens_r : 0.0;
    // cartusele vizibile la verificarea incarcatorului
    if (vm.inspect_w > 0.3 && !pistol && !shotgun && sl && sl->mag >= 0) {
        double frac = std::clamp(static_cast<double>(sl->mag) / std::max(1, sl->mag_cap), 0.0, 1.0);
        double h = 0.06 * frac;
        box(mag_local + V3{0.014, mag_drop - 0.06 + (0.12 - h) * 0.5, 0}, {0.002, std::max(0.002, h), 0.01}, {0.55f, 0.42f, 0.18f}, rot_x(-0.2));
    }
    if (ex > 0.4 && std::clamp(std::sin(time * 0.9) * 1.4, 0.0, 1.0) * ex > 0.4)
        box(port_local, {0.004, 0.004, 0.012}, {0.7f, 0.55f, 0.2f});   // alama vizibila in camera cartusului

    // ---------------------------------------------------------------------
    // Mainile: prinderi reale (maner, garda, tableta) + tranzitii
    // ---------------------------------------------------------------------
    // dreapta pe maner: palma pe partea dreapta, degetele in jurul fetei, aratatorul pe tragaci
    HandPose right_gun;
    {
        M3 GR = G * rot_x(grip_tilt);
        V3 gup = GR * V3{0, -1, 0};
        V3 palm = G * V3{-1, 0, 0};
        V3 fwd = norm(cross(palm, gup));
        V3 knuckles = to_cam(grip_local + rot_x(grip_tilt) * V3{0.023, 0.0, 0.004}
                             + V3{0.0, VP.ads_hand_drop, -0.5 * VP.ads_hand_drop} * a.aim);
        right_gun.palm = palm;
        right_gun.fwd = fwd;
        // la ochire incheietura coboara (rotire in jurul degetelor): mana ramane sub linia de ochire
        if (VP.ads_wrist_drop > 0.0 && a.aim > 0.0) {
            V3 wr = knuckles - fwd * 0.086 + G * V3{0.0, VP.ads_wrist_drop * a.aim, 0.0};
            fwd = norm(knuckles - wr);
            palm = norm(palm - fwd * dot(palm, fwd));
            right_gun.palm = palm;
            right_gun.fwd = fwd;
        }
        right_gun.wrist = knuckles - fwd * 0.086 + palm * -0.008 + hand_noise * 0.5;
        right_gun.curl = 1.12;
        bool trigger = a.aim > 0.3 || vm.flash > 0.0 || a.trigger_prev;   // disciplina: degetul pe tragaci doar cand ocheste
        right_gun.index = trigger ? 0.62 : 0.08;
        right_gun.thumb = 0.9;
        right_gun.thumb_side = 1.0;
        right_gun.thumb_dir = G * V3{-0.7, -0.6, 0.25};
        // pat drept (pompa, repetitie): manerul e chiar sub ochi - policele trece pe stanga, nu in sus
        if (shotgun || sniper) right_gun.thumb_dir = G * V3{-0.9, -0.15, 0.45};
    }
    HandPose left_gun;
    if (lh_on_gun && !pistol && w.has_foregrip) {
        // maner vertical: mana il cuprinde din stanga, degetele orizontale in jurul lui, policele deasupra
        const double fy = fore_local.y - 0.012;                          // mijlocul manerului
        V3 palm = G * V3{1, 0, 0};
        V3 fwd = norm(G * V3{0.25, 0.05, 1.0});
        left_gun.palm = palm;
        left_gun.fwd = fwd;
        V3 knuckles = to_cam(V3{fore_local.x - 0.004, fy, fore_local.z + 0.016});
        left_gun.wrist = knuckles - fwd * 0.086 + hand_noise;
        left_gun.curl = 1.25; left_gun.index = 1.2; left_gun.thumb = 0.8;
        left_gun.thumb_side = -1.0;
        left_gun.thumb_dir = G * V3{0.3, -0.2, 0.9};
        if (rg > 0.0 && vm.examine_w < 0.3) {
            double off = std::sin(kPi * std::min(1.0, vm.regrip_t / 0.75));
            left_gun.wrist = left_gun.wrist + G * (V3{-0.03, 0.04, -0.045} * off);
            left_gun.curl *= 1.0 - 0.5 * off;
        }
    } else if (lh_on_gun && !pistol) {
        // stanga sub garda: degetele urca pe partea dreapta, policele pe stanga, spre gura tevii
        V3 fc = to_cam(fore_local + V3{0.01, -0.033, 0.0});
        V3 palm = G * V3{0, -1, 0};
        V3 fwd = G * V3{1, 0, 0.18};
        left_gun.palm = palm;
        left_gun.fwd = norm(fwd);
        V3 knuckles = fc + G * V3{0.014, 0.032, 0.0};
        left_gun.wrist = knuckles - left_gun.fwd * 0.086 + hand_noise;
        left_gun.curl = 0.95; left_gun.index = 0.9; left_gun.thumb = 0.3;
        left_gun.thumb_side = -1.0;
        left_gun.thumb_dir = G * V3{-0.45, -0.45, 0.8};
        if (rg > 0.0 && vm.examine_w < 0.3) {
            double off = std::sin(kPi * std::min(1.0, vm.regrip_t / 0.75));
            left_gun.wrist = left_gun.wrist + G * (V3{-0.03, 0.04, -0.045} * off + V3{0.0, 0.0, 0.012} * std::sin(kPi * vm.regrip_t));
            left_gun.curl *= 1.0 - 0.5 * off;
        }
    } else if (pistol && stage == 0) {
        // pistolul: mana stanga infasoara mana dreapta (prinderea cu doua maini)
        left_gun.palm = G * V3{1, 0, 0};
        left_gun.fwd = norm(G * V3{0.2, 0.35, 1.0});
        left_gun.wrist = to_cam(grip_local + V3{-0.035, 0.035, -0.04}) + hand_noise;
        left_gun.curl = 1.15; left_gun.index = 1.1; left_gun.thumb = 0.4; left_gun.thumb_side = -1.0;
        left_gun.thumb_dir = G * V3{0.2, -0.5, 0.85};
    } else {
        V3 shoulder_l{-0.20, 0.17, -0.06};
        left_gun = free_hand(lh_cam, shoulder_l, mag_with_hand ? 1.05 : 0.8, true);
    }
    // inspectarea: mana stanga trage manerul de armare
    if (ex > 0.4 && stage == 0) {
        double pc = std::clamp(std::sin(time * 0.9) * 1.4, 0.0, 1.0) * ex;
        V3 handle = pistol ? V3{0.0, 0.03, 0.04} : V3{0.0, 0.02, -0.03};
        V3 shoulder_l{-0.20, 0.17, -0.06};
        HandPose h = free_hand(to_cam(handle + V3{-0.015, -0.01, -0.035 * pc}), shoulder_l, 1.1, true);
        left_gun = blend(left_gun, h, std::min(1.0, (ex - 0.4) / 0.4));
    }
    // bandaj: ambele maini infasoara rola alba
    HandPose right = right_gun, left = left_gun;
    if (bw > 0.05) {
        double t = time * 7.0;
        V3 c{-0.02, 0.3, 0.3};
        V3 shoulder_l{-0.20, 0.17, -0.06}, shoulder_r{0.20, 0.17, -0.06};
        HandPose l = free_hand(c + V3{-0.035 + 0.03 * std::cos(t), 0.02 * std::sin(t), 0.0}, shoulder_l, 0.9, true);
        HandPose rr = free_hand(c + V3{0.04 - 0.03 * std::cos(t + 1.8), 0.02 * std::sin(t + 1.8), -0.01}, shoulder_r, 0.9, false);
        left = blend(left, l, bw);
        right = blend(right, rr, bw);
        if (bw > 0.5) mesh.box(l.wrist + l.fwd * 0.08, V3{0.022, 0, 0}, V3{0, 0.022, 0}, V3{0, 0, 0.03}, {0.75f, 0.74f, 0.7f});
    }
    // grenada gatita / aruncata: mana stanga
    if (vm.cook_w > 0.02 || vm.throw_t >= 0.0) {
        V3 shoulder_l{-0.20, 0.17, -0.06};
        V3 hold{-0.13, 0.19, 0.24};
        V3 h = lerp(lh_cam, hold, ease(vm.cook_w));
        if (vm.throw_t >= 0.0) {
            double t = vm.throw_t;
            V3 back_pos{-0.26, -0.12, 0.12}, fwd_pos{-0.06, -0.06, 0.62};   // bratul trece pe langa umar, nu prin camera
            if (t < 0.35) h = lerp(hold, back_pos, ease(t / 0.35));
            else if (t < 0.6) h = lerp(back_pos, fwd_pos, ease((t - 0.35) / 0.25));
            else h = lerp(fwd_pos, lh_cam, ease((t - 0.6) / 0.4));
        }
        double k = vm.throw_t >= 0.0 ? 1.0 : ease(vm.cook_w);
        HandPose g = free_hand(h, shoulder_l, 1.15, true);
        left = blend(left, g, k);
        bool in_hand = vm.throw_t < 0.0 || vm.throw_t < 0.55;
        if (in_hand && k > 0.3) {
            V3 gc = g.wrist + g.fwd * 0.07 + g.palm * 0.03;
            Rgb gcol = vm.nade_type == 1 ? Rgb{0.12f, 0.12f, 0.13f} : (vm.nade_type == 2 ? Rgb{0.18f, 0.2f, 0.16f} : Rgb{0.1f, 0.12f, 0.07f});
            if (vm.nade_type == 0) mesh.ellipsoid(gc, V3{0.028, 0, 0}, V3{0, 0.034, 0}, V3{0, 0, 0.028}, 4, 8, gcol);
            else mesh.limb(gc - V3{0, 0.045, 0}, gc + V3{0, 0.045, 0}, 0.024, 0.024, 10, gcol);
        }
    }
    // corp la corp cu cutitul: mana stanga taie in diagonala
    if (vm.melee_t >= 0.0 && vm.melee_knife) {
        V3 shoulder_l{-0.20, 0.17, -0.06};
        double t = std::clamp(vm.melee_t, 0.0, 1.0);
        V3 h = bezier(V3{-0.2, 0.05, 0.2}, V3{-0.05, -0.02, 0.5}, V3{0.12, 0.2, 0.32}, ease(t));
        HandPose kn = free_hand(h, shoulder_l, 1.2, true);
        left = blend(left, kn, std::sin(kPi * t) * 1.5 > 1.0 ? 1.0 : std::sin(kPi * t) * 1.5);
        V3 blade0 = kn.wrist + kn.fwd * 0.07, blade1 = blade0 + norm(kn.fwd + kn.palm * -0.6) * 0.17;
        mesh.limb(blade0 - kn.fwd * 0.03, blade0, 0.012, 0.012, 6, black);
        mesh.box((blade0 + blade1) * 0.5, norm(cross(blade1 - blade0, kn.palm)) * 0.012, kn.palm * 0.002, norm(blade1 - blade0) * 0.085, metal);
    }
    // interactiuni: mana stanga se intinde spre obiect
    if (rw > 0.01) {
        double rx = vm.reach_x - pose.x, ry = vm.reach_y - pose.y, rz = vm.reach_z - pose.z;
        double cyw = std::cos(pose.yaw), syw = std::sin(pose.yaw);
        V3 rel{-rx * syw + ry * cyw, -rz, rx * cyw + ry * syw};
        V3 tgt = body_to_cam(rel, pose.pitch);
        V3 sh{-0.2, 0.17, -0.06};
        V3 d = tgt - sh;
        if (len(d) > 0.6) tgt = sh + norm(d) * 0.6;
        if (tgt.z < 0.12) tgt.z = 0.12;
        HandPose rch = free_hand(tgt, sh, 0.3, true);
        left = blend(left, rch, rw);
    }

    // ---------------------------------------------------------------------
    // Tableta: obiect fizic tinut de maini (tintele mainilor sunt pe marginile ei)
    // ---------------------------------------------------------------------
    const bool tablet_visible = tup > 0.001 && tablet;
    const V3 tab_h{0.135, 0.09, 0.0075};
    V3 tab_c{0, 0, 0};
    M3 tab_R = identity();
    if (tablet) {
        const double fw = ease(vm.focus_w);
        V3 hold = lerp(V3{0.0, 0.012, 0.188}, V3{0.0, 0.006, 0.17}, fw);
        if (a.prone > 0.3) hold = lerp(hold, V3{0.0, 0.045, 0.19}, a.prone);
        else if (a.crouch > 0.5) hold.z -= 0.006 * a.crouch;
        const V3 pocket{-0.15, 0.52, 0.12};
        tab_c = bezier(pocket, (pocket + hold) * 0.5 + V3{-0.08, 0.0, 0.05}, hold, tup);
        // lantul corp -> umeri -> brate -> maini -> tableta: respiratie, tremur, stres, oboseala, rani, pasi
        // aceleasi maini care tin arma: deplasarea din lantul corpului (nu zgomot separat)
        const double br = a.phys.breath_signal * 0.006;
        const double moving = std::min(1.0, a.speed / 1.5);
        tab_c.y += br + 0.8 * a.phys.hand_y + 0.02 * (1.0 - a.stamina) * tup + 0.03 * arm
                 + 0.004 * std::sin(a.bob_phase * 2.0) * moving + vm.tab_jolt.x * 0.025 + vm.lag_y.x * 0.7;
        tab_c.x += 0.8 * a.phys.hand_x + vm.lag_x.x * 0.7 + vm.acc_x.x * 0.8
                 + 0.003 * std::sin(a.bob_phase) * moving + 0.25 * a.phys.torso_yaw;
        tab_c.z += vm.acc_z.x * 0.8;
        tab_c.z = std::min(tab_c.z, std::max(0.13, vm.front_clear - 0.05));
        tab_R = rot_x(-0.12 + 0.75 * (1.0 - tup) + 0.05 * fw + 0.08 * a.prone + br * 1.5 + vm.tab_jolt.x * 0.06)
              * rot_y(0.06 * (1.0 - fw) + a.phys.hand_x * 3.0)
              * rot_z(-0.04 + 0.35 * (1.0 - tup) + 0.06 * arm + a.lean * 0.1);
    }
    HandPose left_tab, right_tab;
    if (tablet) {
        auto L = [&](V3 v) { return tab_c + tab_R * v; };
        // stanga: in spatele marginii stangi, policele pe rama din fata
        // degetele stau intinse pe spatele tabletei (nu o strapung), policele pe rama din fata
        left_tab.wrist = L({-0.215, 0.04, 0.026});
        left_tab.fwd = tab_R * V3{1, 0.1, 0};
        left_tab.palm = tab_R * V3{0, 0, -1};
        left_tab.curl = -0.04; left_tab.index = -0.03; left_tab.thumb = 0.25; left_tab.thumb_side = -1.0;
        left_tab.thumb_dir = tab_R * V3{0.5, -0.15, -0.85};
        // dreapta: tine marginea dreapta ...
        HandPose edge;
        edge.wrist = L({0.215, 0.045, 0.026});
        edge.fwd = tab_R * V3{-1, 0.1, 0};
        edge.palm = tab_R * V3{0, 0, -1};
        edge.curl = -0.04; edge.index = -0.03; edge.thumb = 0.25; edge.thumb_side = 1.0;
        edge.thumb_dir = tab_R * V3{-0.5, -0.15, -0.85};
        // ... sau atinge ecranul cu aratatorul la pozitia cursorului
        V3 su = tab_R * V3{tab_h.x * 2.0, 0, 0}, sv = tab_R * V3{0, tab_h.y * 2.0, 0};
        V3 corner = L({-tab_h.x, -tab_h.y, -tab_h.z - 0.001});
        V3 touch = corner + su * tablet->cursor_u + sv * tablet->cursor_v;
        V3 nrm = tab_R * V3{0, 0, -1};
        V3 tip = touch + nrm * (0.004 + (1.0 - tablet->tap) * 0.025);
        HandPose tp;
        tp.fwd = norm(tip - (tip + V3{0.05, 0.08, -0.04}));
        tp.palm = norm(V3{0.0, 1.0, 0.0} - tp.fwd * dot(V3{0.0, 1.0, 0.0}, tp.fwd));
        V3 side = norm(cross(tp.fwd, tp.palm));
        tp.wrist = tip - tp.fwd * 0.163 - side * (1.5 * 0.0195);
        tp.curl = 1.35; tp.index = 0.04; tp.thumb = 0.9; tp.thumb_side = 1.0;
        right_tab = blend(edge, tp, ease(vm.touch_w) * tup);
    }
    // tranzitii: stanga merge la tableta (in port-tableta, apoi o ridica), dreapta lasa arma dupa ea
    if (tablet && tabt > 0.0) {
        double lb = ease(tabt / 0.4);
        double rb = ease((tabt - 0.3) / 0.55);
        left = blend(left, left_tab, lb);
        right = blend(right, right_tab, rb);
    }

    // --- Brate (IK cu doua segmente) pana la incheieturi + mainile cu degete ---
    // umerii: pozitia de tragere (umarul stang inainte) cand mainile tin arma, relaxati la tableta / bandaj
    const double stance = (1.0 - std::max(tabt, bw)) * (1.0 - vm.sprint_w * 0.5);
    const V3 shoulder_r = lerp(V3{0.20, 0.17, -0.06}, VP.shoulder_r, stance);
    const V3 shoulder_l = lerp(V3{-0.20, 0.17, -0.06}, VP.shoulder_l, stance);
    const V3 hint_r = lerp(VP.elbow_r, VP.elbow_r_ads, a.aim), hint_l = lerp(VP.elbow_l, VP.elbow_l_ads, a.aim);
    V3 er = solve_elbow(shoulder_r, right.wrist, 0.30, 0.26, lerp(V3{0.6, 1.0, -0.3}, hint_r, stance));
    V3 el = solve_elbow(shoulder_l, left.wrist, 0.30, 0.26, lerp(V3{-0.6, 1.0, -0.3}, hint_l, stance));
    mesh.mat = MatFabric;
    mesh.limb(right.wrist - right.fwd * 0.02, er, 0.027, 0.033, 7, sleeve);
    mesh.limb(left.wrist - left.fwd * 0.02, el, 0.027, 0.033, 7, sleeve);
    mesh.limb(er, shoulder_r, 0.036, 0.042, 7, sleeve);
    mesh.limb(el, shoulder_l, 0.036, 0.042, 7, sleeve);
    // rana la brat: bandaj alb pe antebrat (zona bratului sub 75%)
    for (int sd = 0; sd < 2; ++sd) {
        BodyZone z = sd == 0 ? BodyZone::RightArm : BodyZone::LeftArm;
        if (a.health.frac(z) > 0.75) continue;
        const HandPose& hp = sd == 0 ? right : left;
        V3 elbow = sd == 0 ? er : el;
        V3 c = lerp(hp.wrist, elbow, 0.45);
        V3 d = norm(elbow - hp.wrist);
        mesh.limb(c - d * 0.03, c + d * 0.03, 0.031, 0.034, 7, Rgb{0.62f, 0.6f, 0.56f});
        if (a.health.frac(z) < 0.45) mesh.limb(c - d * 0.012, c + d * 0.012, 0.0345, 0.0345, 7, Rgb{0.35f, 0.04f, 0.03f});
    }
    // ceasul pe incheietura stanga
    if (profile && profile->watch) {
        V3 d = norm(el - left.wrist);
        V3 wc = left.wrist + d * 0.035;
        mesh.mat = MatRubber;
        mesh.limb(wc - d * 0.011, wc + d * 0.011, 0.03, 0.03, 8, Rgb{0.02f, 0.02f, 0.022f});
        mesh.mat = MatGlass;
        V3 face = norm(cross(d, left.palm)) * -1.0;
        mesh.box(wc - left.palm * 0.031, norm(cross(d, left.palm)) * 0.012, left.palm * 0.003, d * 0.012, Rgb{0.03f, 0.035f, 0.03f},
                 Rgb{0.0f, 0.03f, 0.02f});
        (void)face;
    }
    mesh.mat = bare_hands ? MatSkin : MatLeather;
    Rgb hand_col = bare_hands ? skin_col : glove;
    Rgb tip_col = (bare_fingers || bare_hands) ? skin_col : glove;
    hand_mesh(mesh, right.wrist, right.fwd, right.palm, right.curl, right.index, right.thumb, right.thumb_side, hand_col, right.thumb_dir, tip_col);
    hand_mesh(mesh, left.wrist, left.fwd, left.palm, left.curl, left.index, left.thumb, left.thumb_side, hand_col, left.thumb_dir, tip_col);
    mesh.mat = MatDefault;

    // --- Tableta: carcasa cauciucata, colturi, butoane, camera, curea pe spate ---
    if (tablet_visible) {
        auto tbox = [&](V3 c, V3 h, Rgb col, Rgb em = {}) {
            mesh.box(tab_c + tab_R * c, tab_R.col(0) * h.x, tab_R.col(1) * h.y, tab_R.col(2) * h.z, col, em);
        };
        const Rgb rubber{0.03f, 0.03f, 0.033f}, bezel{0.012f, 0.012f, 0.014f};
        tbox({0, 0, 0.002}, {tab_h.x, tab_h.y, tab_h.z}, rubber);
        tbox({0, 0, -tab_h.z + 0.0012}, {tab_h.x - 0.004, tab_h.y - 0.004, 0.0012}, bezel);
        for (int cx = -1; cx <= 1; cx += 2)
            for (int cy = -1; cy <= 1; cy += 2)
                tbox({cx * (tab_h.x - 0.008), cy * (tab_h.y - 0.008), 0.002}, {0.012, 0.012, tab_h.z + 0.002}, Rgb{0.05f, 0.048f, 0.04f});
        tbox({0.06, -tab_h.y - 0.0015, 0.002}, {0.012, 0.0015, 0.003}, Rgb{0.08f, 0.08f, 0.08f});    // pornire
        tbox({0.09, -tab_h.y - 0.0015, 0.002}, {0.006, 0.0015, 0.003}, Rgb{0.08f, 0.08f, 0.08f});    // volum
        tbox({0.0, -tab_h.y + 0.006, -tab_h.z - 0.0005}, {0.003, 0.003, 0.0005}, Rgb{0.02f, 0.02f, 0.03f}, Rgb{0.0f, 0.0f, 0.02f});
        tbox({0.0, 0.0, tab_h.z + 0.004}, {0.09, 0.012, 0.003}, Rgb{0.04f, 0.045f, 0.04f});          // curea pe spate
        tbox({-tab_h.x + 0.025, tab_h.y - 0.01, -tab_h.z - 0.0006}, {0.004, 0.0015, 0.0005}, Rgb{}, tablet->power > 0.5 ? Rgb{0.1f, 1.2f, 0.2f} : Rgb{});  // LED
    }

    // pozitiile pentru tuburi si fum (folosite de GameView)
    V3 port = port_cam, muz = muzzle_cam;
    vm.port_x = port.x; vm.port_y = port.y; vm.port_z = port.z;
    vm.muzzle_x = muz.x; vm.muzzle_y = muz.y; vm.muzzle_z = muz.z;

    // picioarele si centura apartin lumii: aceeasi pozitie pe ecran ca la camera (podeaua se potriveste)
    if (focal_vm != focal_cam)
        for (size_t i = 0; i < body_verts; ++i) { mesh.pos[i].x *= focal_cam / focal_vm; mesh.pos[i].y *= focal_cam / focal_vm; }
    if (vm.debug_orbit != 0.0) {
        // depanare: tot primul plan rotit in jurul armei (vedere din lateral / de sus)
        const V3 piv{0.05, 0.12, 0.32};
        const M3 Ro = rot_y(vm.debug_orbit);
        for (auto& q : mesh.pos) q = piv + Ro * (q - piv) + V3{0.0, 0.0, 0.32};
        for (auto& n : mesh.nrm) n = Ro * n;
    }

    // ---------------------------------------------------------------------
    // Iluminare pe varf + randare cu z-buffer (fara sortarea pieselor)
    // ---------------------------------------------------------------------
    lit.resize(mesh.pos.size());
    for (size_t i = 0; i < mesh.pos.size(); ++i) {
        const V3& n = mesh.nrm[i];
        const MaterialParams& mp = material_params(i < mesh.material.size() ? mesh.material[i] : 0);
        V3 v = norm(-mesh.pos[i]);
        double lam = std::max(0.0, dot(n, light_dir));
        double rim = std::pow(1.0 - std::abs(dot(n, v)), 3.0) * mp.rim;
        V3 hv = norm(light_dir + v);
        double spec = std::pow(std::max(0.0, dot(n, hv)), mp.shininess) * mp.spec * 1.6;
        Rgb L = ambient * static_cast<float>(0.32 + 0.68 * lam + rim) + flash * static_cast<float>(0.4 + 0.6 * std::max(0.0, -n.z));
        // metalele reflecta in culoarea lor, restul alb
        const Rgb& al = mesh.albedo[i];
        float mx = std::max(0.02f, std::max(al.r, std::max(al.g, al.b)));
        Rgb sc = Rgb{1.0f, 1.0f, 1.0f} * (1.0f - mp.metallic) + Rgb{al.r / mx, al.g / mx, al.b / mx} * mp.metallic;
        lit[i] = al * L + (ambient + flash * 0.3f) * sc * static_cast<float>(spec) + mesh.emissive[i];
    }
    zb.resize(static_cast<size_t>(f.w) * f.h);
    {
        const int NZ = static_cast<int>(zb.size());
        float* zd = zb.data();
#pragma omp parallel for
        for (int i = 0; i < NZ; ++i) zd[i] = 1e9f;
    }
    Projector PC = Projector::for_camera(pose, f);
    PC.focal = focal_vm;
    draw_mesh(f, zb.data(), nullptr, PC, mesh, lit, true, -1.0f);

    // --- Ecranul tabletei: textura UI pe suprafata fizica (perspectiva, degetele il acopera) ---
    if (tablet_visible) {
        V3 c00 = tab_c + tab_R * V3{-tab_h.x + 0.01, -tab_h.y + 0.01, -tab_h.z - 0.0005};
        V3 U = tab_R * V3{(tab_h.x - 0.01) * 2.0, 0, 0}, Vv = tab_R * V3{0, (tab_h.y - 0.01) * 2.0, 0};
        V3 nrm = tab_R * V3{0, 0, -1};
        double xs[4], ys[4];
        V3 cs[4] = {c00, c00 + U, c00 + U + Vv, c00 + Vv};
        bool ok = true;
        for (int k = 0; k < 4; ++k) ok &= r.project(cs[k], xs[k], ys[k]);
        if (ok) {
            int x0 = std::max(0, static_cast<int>(std::min({xs[0], xs[1], xs[2], xs[3]})));
            int x1 = std::min(f.w - 1, static_cast<int>(std::max({xs[0], xs[1], xs[2], xs[3]})) + 1);
            int y0 = std::max(0, static_cast<int>(std::min({ys[0], ys[1], ys[2], ys[3]})));
            int y1 = std::min(f.h - 1, static_cast<int>(std::max({ys[0], ys[1], ys[2], ys[3]})) + 1);
            double uu = dot(U, U), vv = dot(Vv, Vv);
            float amb_l = std::max(0.02f, ambient.luminance());
            double fres_base = std::pow(1.0 - std::abs(dot(nrm, norm(tab_c))), 2.0);
            float B = tablet->brightness * tablet->power;
            static std::vector<float> glass_tex;            // amprente (canal 0) + zgarieturi (canal 1)
            const int GW = 256, GH = 170;
            if (glass_tex.empty()) {
                glass_tex.resize(static_cast<size_t>(GW) * GH * 2);
                for (int gy = 0; gy < GH; ++gy)
                    for (int gx = 0; gx < GW; ++gx) {
                        double gu = (gx + 0.5) / GW, gv = (gy + 0.5) / GH;
                        glass_tex[(static_cast<size_t>(gy) * GW + gx) * 2] = smudge(gu, gv);
                        glass_tex[(static_cast<size_t>(gy) * GW + gx) * 2 + 1] = scratch(gu, gv);
                    }
            }
            const float flash_l = flash.luminance();
            const std::vector<float>& GT = glass_tex;
            // marirea (pixeli de ecran pe texel): esantionare "sharp bilinear" -> margini nete,
            // dar latimi egale ale literelor (fara scara neregulata a esantionarii nearest)
            const float magx = static_cast<float>(std::max(1.0, std::hypot(xs[1] - xs[0], ys[1] - ys[0]) / tablet->w));
            const float magy = static_cast<float>(std::max(1.0, std::hypot(xs[3] - xs[0], ys[3] - ys[0]) / tablet->h));
            const int TW = tablet->w, TH = tablet->h;
            const float* TR = tablet->rgb.data();
            float* ZB = zb.data();          // zb este thread_local: firele de lucru au nevoie de pointerul firului principal
#pragma omp parallel for schedule(dynamic, 4)
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x) {
                    V3 dir{(x + 0.5 - r.cx) / r.focal, (y + 0.5 - r.cy) / r.focal, 1.0};
                    double den = dot(dir, nrm);
                    if (std::abs(den) < 1e-6) continue;
                    double t = dot(c00, nrm) / den;
                    if (t <= 0.0) continue;
                    V3 X = dir * t;
                    size_t idx = static_cast<size_t>(y) * f.w + x;
                    if (X.z >= ZB[idx]) continue;          // degetul / mana in fata ecranului
                    double u = dot(X - c00, U) / uu, v = dot(X - c00, Vv) / vv;
                    if (u < 0.0 || u > 1.0 || v < 0.0 || v > 1.0) continue;
                    float fx = static_cast<float>(u * TW) - 0.5f, fy = static_cast<float>(v * TH) - 0.5f;
                    int ix = static_cast<int>(std::floor(fx)), iy = static_cast<int>(std::floor(fy));
                    float ax = std::clamp((fx - ix - 0.5f) * magx + 0.5f, 0.0f, 1.0f);
                    float ay = std::clamp((fy - iy - 0.5f) * magy + 0.5f, 0.0f, 1.0f);
                    int ix0 = std::clamp(ix, 0, TW - 1), ix1 = std::clamp(ix + 1, 0, TW - 1);
                    int iy0 = std::clamp(iy, 0, TH - 1), iy1 = std::clamp(iy + 1, 0, TH - 1);
                    const float* t00 = &TR[(static_cast<size_t>(iy0) * TW + ix0) * 3];
                    const float* t10 = &TR[(static_cast<size_t>(iy0) * TW + ix1) * 3];
                    const float* t01 = &TR[(static_cast<size_t>(iy1) * TW + ix0) * 3];
                    const float* t11 = &TR[(static_cast<size_t>(iy1) * TW + ix1) * 3];
                    float src[3];
                    for (int c = 0; c < 3; ++c) {
                        float top = t00[c] + (t10[c] - t00[c]) * ax;
                        float bot = t01[c] + (t11[c] - t01[c]) * ax;
                        src[c] = top + (bot - top) * ay;
                    }
                    const float* gt = &GT[(static_cast<size_t>(std::min(GH - 1, static_cast<int>(v * GH))) * GW
                                                  + std::min(GW - 1, static_cast<int>(u * GW))) * 2];
                    float sm = gt[0], scr = gt[1];
                    float diag = static_cast<float>(u * 0.7 + v * 0.3 - 0.45);
                    float spec = std::exp(-diag * diag / 0.003f) * 0.07f;
                    // sticla reflecta mediul: in plin soare ecranul se "spala"
                    float refl = amb_l * static_cast<float>(0.008 + 0.03 * sm + 0.03 * scr + spec * 0.3 + 0.04 * fres_base)
                               + flash_l * 0.05f;
                    float* p = f.px(x, y);
                    p[0] = src[0] * B * (1.0f - 0.15f * sm) + refl;
                    p[1] = src[1] * B * (1.0f - 0.15f * sm) + refl;
                    p[2] = src[2] * B * (1.0f - 0.15f * sm) + refl * 1.05f;
                    ZB[idx] = static_cast<float>(X.z);
                }
        }
    }
    // --- Luneta: imaginea marita (randata pe directia reala a armei) apare in ocular ---
    if (scope && scope_view && scope_view->w > 0 && a.aim > 0.3 && lens_c.z > 0.03) {
        double scx, scy;
        if (r.project(lens_c, scx, scy)) {
            const double R = lens_r / lens_c.z * r.focal;
            const float align = static_cast<float>(std::clamp((a.aim - 0.6) / 0.35, 0.0, 1.0));   // ochiul aliniat cu ocularul
            const int x0 = std::max(0, static_cast<int>(scx - R)), x1 = std::min(f.w - 1, static_cast<int>(scx + R));
            const int y0 = std::max(0, static_cast<int>(scy - R)), y1 = std::min(f.h - 1, static_cast<int>(scy + R));
            const float lz = static_cast<float>(lens_c.z);
            float* ZB = zb.data();
            const HdrFrame& sv = *scope_view;
#pragma omp parallel for
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x) {
                    double dx = (x + 0.5 - scx) / R, dy = (y + 0.5 - scy) / R;
                    double rr = std::sqrt(dx * dx + dy * dy);
                    if (rr > 1.0) continue;
                    size_t idx = static_cast<size_t>(y) * f.w + x;
                    if (ZB[idx] < lz - 0.004f) continue;          // degetele / mana in fata ocularului
                    int sx = std::clamp(static_cast<int>((0.5 + 0.5 * dx) * sv.w), 0, sv.w - 1);
                    int sy = std::clamp(static_cast<int>((0.5 + 0.5 * dy) * sv.h), 0, sv.h - 1);
                    const float* q = sv.px(sx, sy);
                    // umbra pupilei de iesire la margine; ochiul nealiniat vede negru
                    float vig = static_cast<float>(std::clamp((1.0 - rr) / 0.18, 0.0, 1.0)) * align;
                    float c[3] = {q[0] * vig, q[1] * vig, q[2] * vig};
                    // reticulul duplex: linii groase spre margine, fine in centru, punct central
                    double ax = std::abs(dx), ay = std::abs(dy);
                    bool thick = rr > 0.42 && (ax < 0.022 || ay < 0.022);
                    bool thin = rr <= 0.42 && (ax < 0.0055 || ay < 0.0055);
                    if (thick || thin) c[0] = c[1] = c[2] = 0.0f;
                    if (rr < 0.012) { c[0] = 4.0f * align; c[1] = 0.1f; c[2] = 0.05f; }
                    float* p = f.px(x, y);
                    p[0] = c[0]; p[1] = c[1]; p[2] = c[2];
                    ZB[idx] = lz;
                }
        }
    }
    // adancimea reala a primului plan (pentru profunzimea de camp)
    {
        const int NZ = static_cast<int>(zb.size());
        float* zd = zb.data();
#pragma omp parallel for
        for (int i = 0; i < NZ; ++i) if (zd[i] < 1e8f) f.depth[static_cast<size_t>(i)] = zd[i];
    }

    // --- Sticla opticii: nuanta usoara + reflexie pe margine ---
    if (optic && lens_c.z > 0.05 && a.aim > 0.5) {
        V3 ln = norm(G * V3{0, 0, 1});
        V3 ax = norm(cross(ln, V3{0, 1, 0})), ay = norm(cross(ln, ax));
        double xs[12], ys[12];
        bool ok = true;
        for (int k = 0; k < 12; ++k) {
            double ang = k * 2.0 * kPi / 12.0;
            ok &= r.project(lens_c + ax * (std::cos(ang) * lens_r) + ay * (std::sin(ang) * lens_r), xs[k], ys[k]);
        }
        if (ok) fill_poly(f, xs, ys, 12, Rgb{0.05f, 0.08f, 0.1f} * (ambient.luminance() * 2.0f + 0.02f), 0.16f, false);
    }

    // --- Punctul rosu / reticulul holografic: directia reala a tevii ---
    if ((w.has_reddot || w.has_holo) && a.aim > 0.55 && !pistol && tabt < 0.1) {
        V3 d = G * V3{0, 0, 1};
        double sx, sy;
        Raster rw = r;
        rw.focal = focal_cam;   // punctul e la infinit pe directia tevii: acolo unde merge glontul in lume
        if (rw.project(d * 50.0, sx, sy)) {
            float k0 = static_cast<float>((a.aim - 0.55) / 0.45);
            for (int yy = -1; yy <= 1; ++yy)
                for (int xx = -1; xx <= 1; ++xx) {
                    int px = static_cast<int>(sx) + xx, py = static_cast<int>(sy) + yy;
                    if (px < 0 || py < 0 || px >= f.w || py >= f.h) continue;
                    float k = static_cast<float>(std::exp(-(xx * xx + yy * yy) / 0.6)) * k0;
                    float* q = f.px(px, py);
                    q[0] += 9.0f * k; q[1] += 0.4f * k; q[2] += 0.3f * k;
                }
            if (w.has_holo) {
                double rad = 0.019 * r.focal;
                for (int k = 0; k < 48; ++k) {
                    double ang = k * 2.0 * kPi / 48.0;
                    int px = static_cast<int>(sx + std::cos(ang) * rad), py = static_cast<int>(sy + std::sin(ang) * rad);
                    if (px < 0 || py < 0 || px >= f.w || py >= f.h) continue;
                    float* q = f.px(px, py);
                    q[0] += 4.0f * k0; q[1] += 0.2f * k0; q[2] += 0.15f * k0;
                }
            }
        }
    }

    // --- Flama de la gura tevii ---
    if (vm.flash > 0.0) {
        double sx, sy;
        V3 m = muzzle_cam;
        if (r.project(m, sx, sy)) {
            double scale = r.focal / std::max(0.1, m.z);
            double rad = (w.has_suppressor ? 0.03 : 0.11) * scale * w.flash_intensity;
            float inten = static_cast<float>(vm.flash * (w.has_suppressor ? 6.0 : 45.0));
            int x0 = std::max(0, static_cast<int>(sx - rad * 1.6)), x1 = std::min(f.w - 1, static_cast<int>(sx + rad * 1.6));
            int y0 = std::max(0, static_cast<int>(sy - rad * 1.6)), y1 = std::min(f.h - 1, static_cast<int>(sy + rad * 1.6));
            const double frot = vm.flash_rot;
#pragma omp parallel for
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x) {
                    double dx = (x - sx) / rad, dy = (y - sy) / rad;
                    double d2 = dx * dx + dy * dy;
                    if (d2 > 5.8) continue;                          // exp(-0.8 * d2) * 0.8 < 0.01
                    double ang = std::atan2(dy, dx) + frot;
                    double cs = std::abs(std::cos(ang * 2.5));
                    double c2 = cs * cs, c4 = c2 * c2;
                    double star = c4 * c4 * std::exp(-d2 * 0.8);
                    double k = std::exp(-d2 * 3.0) + star * 0.8;
                    if (k < 0.01) continue;
                    float* q = f.px(x, y);
                    q[0] += inten * static_cast<float>(k);
                    q[1] += inten * 0.62f * static_cast<float>(k);
                    q[2] += inten * 0.25f * static_cast<float>(k);
                }
        }
    }

    // --- Fum de la gura tevii / teava fierbinte ---
    for (const auto& s : vm.smoke) {
        double sx, sy;
        if (!r.project({s.x, s.y, s.z}, sx, sy)) continue;
        double rad = s.size / s.z * r.focal;
        float alpha = static_cast<float>(0.18 * std::min(1.0, s.life / s.max_life * 2.0));
        int x0 = std::max(0, static_cast<int>(sx - rad)), x1 = std::min(f.w - 1, static_cast<int>(sx + rad));
        int y0 = std::max(0, static_cast<int>(sy - rad)), y1 = std::min(f.h - 1, static_cast<int>(sy + rad));
        Rgb col = ambient * 0.9f + flash * 0.3f;
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                double dx = (x - sx) / rad, dy = (y - sy) / rad;
                double d2 = dx * dx + dy * dy;
                if (d2 > 1.0) continue;
                float k = alpha * static_cast<float>(1.0 - d2);
                float* q = f.px(x, y);
                q[0] = q[0] * (1 - k) + col.r * k;
                q[1] = q[1] * (1 - k) + col.g * k;
                q[2] = q[2] * (1 - k) + col.b * k;
            }
    }
}

}
