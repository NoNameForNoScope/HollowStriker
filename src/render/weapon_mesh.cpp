#include "weapon_mesh.hpp"
#include "obj_model.hpp"
#include <algorithm>
#include <cmath>

namespace bodycam {

namespace {
constexpr double kPi = 3.14159265358979323846;
const Rgb kBlack{0.045f, 0.045f, 0.05f}, kDark{0.07f, 0.07f, 0.075f}, kPoly{0.058f, 0.058f, 0.056f};
const Rgb kMetal{0.11f, 0.11f, 0.115f}, kWood{0.16f, 0.09f, 0.05f}, kTan{0.24f, 0.2f, 0.14f};
}

double weapon_sight_y(const WeaponData& w) {
    // y in jos: linia de ochire e deasupra sinei cu inaltimea opticii (sau a catarii)
    double h = w.has_optic() ? w.sight_height : (w.visual.iron_height > 0.0 ? w.visual.iron_height : 0.012);
    return w.sockets.optic.y - h;
}

void build_weapon_mesh(Mesh& m, const WeaponData& w, const WeaponFrame& f, const WeaponVisualState& st, WeaponMeshOut& out) {
    const bool lo = st.lod > 0;
    const int sd8 = lo ? 6 : 8, sd12 = lo ? 8 : 12;
    const Rgb hot = Rgb{1.0f, 0.22f, 0.04f} * static_cast<float>(st.heat_glow * st.heat_glow * 3.0);
    auto P = [&](V3 l) { return f.to(l); };
    auto box = [&](V3 c, V3 h, Rgb col, M3 local = identity(), Rgb em = {}) {
        M3 R = f.R * local;
        m.box(P(c), R.col(0) * h.x, R.col(1) * h.y, R.col(2) * h.z, col, em);
    };
    auto cyl = [&](V3 p0, V3 p1, double ra, double rb, int sides, Rgb col, bool caps = true, Rgb em = {}) {
        m.limb(P(p0), P(p1), ra, rb, sides, col, f.R * V3{0, -1, 0}, 1.0, caps, em);
    };
    // inel (coroana circulara) perpendicular pe teava: capetele tuburilor se vad si din interior
    auto ring = [&](V3 c, double r_in, double r_out, double depth, int n, Rgb col) {
        const double rm = 0.5 * (r_in + r_out);
        for (int k = 0; k < n; ++k) {
            const double ang = 2.0 * kPi * k / n;
            box(c + V3{std::cos(ang) * rm, std::sin(ang) * rm, 0.0}, {0.5 * (r_out - r_in), rm * kPi / n * 1.08, 0.5 * depth}, col, rot_z(ang));
        }
    };
    const WeaponSockets& S = w.sockets;
    const WeaponVisual& V = w.visual;
    const std::string& rcv = V.receiver;
    // Modelul extern al unui atasament montat (ExternalAssets, licenta verificata), pus cu punctul
    // lui de prindere in `at` si rotit cu Rl (axele modelului: +x spre gura tevii, +y in sus).
    // false = nu exista / refuzat -> se deseneaza forma procedurala.
    struct AccInfo { double front = 0.0, back = 0.0, end_r = 0.0; };
    auto acc_model = [&](AttachmentVisual v, V3 at, const M3& Rl, AccInfo* info = nullptr, V3* front_pt = nullptr) -> bool {
        const WeaponAttachment* a = nullptr;
        for (const auto& x : w.mounted) if (x.visual == v) { a = &x; break; }
        if (!a) return false;
        const bool pv = rcv == "pistol" && !a->model_pistol.empty();
        const std::string& path = pv ? a->model_pistol : a->model;
        if (path.empty()) return false;
        const ObjModel* om = external_model(path);
        if (!om || om->pos.empty()) return false;
        const V3 A{a->model_anchor[0], a->model_anchor[1], a->model_anchor[2]};
        const double sc = (pv && a->model_pistol_scale > 0.0) ? a->model_pistol_scale : a->model_scale;
        auto conv = [&](V3 q) { return Rl * V3{q.z, -q.y, q.x}; };   // model -> arma
        const uint8_t keep = m.mat;
        for (size_t t = 0; t + 2 < om->pos.size(); t += 3) {
            uint32_t ids[3];
            for (int c = 0; c < 3; ++c) {
                const size_t i = t + c;
                V3 q = at + conv((om->pos[i] - A) * sc);
                m.mat = om->mat[i];
                ids[c] = m.vertex(f.to(q), norm(f.R * conv(om->nrm[i])), om->col[i]);
            }
            m.tri(ids[0], ids[1], ids[2]);
        }
        m.mat = keep;
        if (info) {
            info->front = (om->bmax.x - A.x) * sc;
            info->back = (om->bmin.x - A.x) * sc;
            // raza capatului dinspre ochi (ocularul lunetei)
            double r = 0.0;
            const double lim = om->bmin.x + 0.03 * (om->bmax.x - om->bmin.x);
            for (const V3& q : om->pos)
                if (q.x <= lim) r = std::max(r, std::sqrt((q.y - A.y) * (q.y - A.y) + (q.z - A.z) * (q.z - A.z)));
            info->end_r = r * sc;
        }
        if (front_pt) *front_pt = at + conv(V3{om->bmax.x - A.x, 0.5 * (om->bmin.y + om->bmax.y) - A.y, 0.0} * sc);
        return true;
    };
    out = WeaponMeshOut{};
    out.grip = P({S.grip.x, S.grip.y, S.grip.z});
    out.support = P({S.support.x, S.support.y, S.support.z + st.pump});
    out.mag = P({S.mag.x, S.mag.y, S.mag.z});
    out.bolt = P({S.bolt.x, S.bolt.y, S.bolt.z});
    out.charging = P({S.charging.x, S.charging.y, S.charging.z + st.charging});
    out.port = P({S.port.x, S.port.y, S.port.z});
    out.sight_y = weapon_sight_y(w);

    // ------------------------------------------------------------------ incarcatorul
    auto magazine = [&](V3 at, double tilt, bool in_hand) {
        if (V.mag_shape == "internal") return;
        m.mat = MatPolymer;
        if (w.has_drum) {
            // toba: cilindru mare sub locas
            V3 c = at + V3{0, 0.07, 0.01};
            if (in_hand) {
                m.limb(st.hand_pos - f.R * V3{0.03, 0, 0}, st.hand_pos + f.R * V3{0.03, 0, 0}, 0.05, 0.05, sd12, kDark);
                return;
            }
            box(at + V3{0, -0.005 + 0.0, 0}, {0.0125, 0.03, 0.022}, kDark, rot_x(tilt + st.mag_spin));
            cyl(c + V3{-0.03, st.mag_drop, 0}, c + V3{0.03, st.mag_drop, 0}, 0.05, 0.05, sd12, kDark);
            return;
        }
        double extra = w.has_extmag ? 0.03 : 0.0;
        if (V.mag_shape == "pistol") {
            if (in_hand) { m.box(st.hand_pos + f.R * V3{0, -0.03, 0}, f.R.col(0) * 0.011, f.R.col(1) * 0.045, f.R.col(2) * 0.018, kDark); return; }
            box(at + V3{0, st.mag_drop + 0.03, 0}, {0.011, 0.045 + extra, 0.018}, kDark);
            return;
        }
        double up_len = 0.03 + extra * 0.4, lo_len = 0.035 + extra * 0.6;
        if (V.mag_shape == "straight") { up_len = rcv == "bolt" ? 0.02 : 0.05 + extra; lo_len = 0.0; }
        if (in_hand) {
            M3 R = f.R * rot_x(-0.2);
            m.box(st.hand_pos + R * V3{0, -0.03, 0}, R.col(0) * 0.0125, R.col(1) * up_len, R.col(2) * 0.026, kDark);
            if (lo_len > 0.0) m.box(st.hand_pos + R * V3{0, 0.03, 0.012}, (R * rot_x(-0.25)).col(0) * 0.0125, (R * rot_x(-0.25)).col(1) * lo_len,
                                    (R * rot_x(-0.25)).col(2) * 0.026, kDark);
            return;
        }
        V3 a0 = at + V3{0, st.mag_drop, 0};
        box(a0 + V3{0, -0.02, 0}, {0.0125, up_len, rcv == "bolt" ? 0.035 : 0.026}, kDark, rot_x(tilt + st.mag_spin));
        if (lo_len > 0.0) {
            box(a0 + V3{0, 0.04 + extra, 0.012 + extra * 0.5}, {0.0125, lo_len, 0.026}, kDark, rot_x(tilt - 0.25 + st.mag_spin));
            if (!lo) box(a0 + V3{0, 0.078 + extra * 2.0, 0.025 + extra}, {0.014, 0.006, 0.03}, kBlack, rot_x(tilt - 0.35 + st.mag_spin));
        }
    };

    // ------------------------------------------------------------------ dispozitivul de la gura tevii
    double barrel_end = S.muzzle.z;
    const double by = S.muzzle.y;
    auto muzzle_device = [&](double z0, double rbar) {
        m.mat = MatSteel;
        if (w.has_suppressor) {
            AccInfo ai;
            if (acc_model(AttachmentVisual::Suppressor, {0, by, z0}, identity(), &ai)) return z0 + ai.front;
            cyl({0, by, z0}, {0, by, z0 + 0.18}, rbar * 2.3, rbar * 2.3, sd12, Rgb{0.06f, 0.06f, 0.065f}, true, hot * 0.6f);
            if (!lo) cyl({0, by, z0 + 0.178}, {0, by, z0 + 0.181}, rbar * 0.9, rbar * 0.9, 8, Rgb{0.01f, 0.01f, 0.01f});
            return z0 + 0.18;
        }
        if (w.has_compensator) {
            cyl({0, by, z0}, {0, by, z0 + 0.05}, rbar * 1.4, rbar * 1.4, sd8, kMetal, true, hot);
            if (!lo) for (int k = 0; k < 3; ++k) box({0, by - rbar * 1.4, z0 + 0.012 + k * 0.012}, {0.008, 0.002, 0.003}, Rgb{0.01f, 0.01f, 0.01f});
            return z0 + 0.05;
        }
        if (w.has_brake) {
            // frana: corp lat cu deschideri laterale mari
            box({0, by, z0 + 0.028}, {rbar * 1.9, rbar * 1.4, 0.028}, kMetal, identity(), hot);
            if (!lo) for (int k = 0; k < 3; ++k)
                for (int s2 = -1; s2 <= 1; s2 += 2) box({s2 * rbar * 1.9, by, z0 + 0.01 + k * 0.016}, {0.0008, rbar * 1.0, 0.004}, Rgb{0.01f, 0.01f, 0.01f});
            return z0 + 0.056;
        }
        if (w.has_flashhider || rcv == "ar") {
            cyl({0, by, z0}, {0, by, z0 + 0.045}, rbar * 1.25, rbar * 1.25, 6, kDark, true, hot);
            if (!lo) for (int k = 0; k < 3; ++k) {
                double ang = k * 2.1;
                box({std::cos(ang) * rbar * 1.25, by + std::sin(ang) * rbar * 1.25, z0 + 0.03}, {0.0015, 0.0015, 0.012}, Rgb{0.01f, 0.01f, 0.01f});
            }
            return z0 + 0.045;
        }
        cyl({0, by, z0}, {0, by, z0 + 0.012}, rbar * 1.15, rbar * 1.15, 6, kMetal);
        return z0 + 0.012;
    };

    // ------------------------------------------------------------------ optica / catare
    auto sight = [&](double rail_y, double rail_z, double front_z) {
        const double oy = S.optic.y, oz = S.optic.z;
        // axa optica sta la sight_height deasupra sinei (y in jos): acolo ajunge ochiul la ochire
        const double sy = oy - w.sight_height;
        if (w.has_reddot) {
            // tub de tip Aimpoint pe o inaltatoare: baza pe sina, tubul centrat pe linia de ochire
            const double R = 0.019;
            m.mat = MatAluminum;
            const double base_top = sy + R * 0.7;
            box({0, (oy + base_top) * 0.5, oz}, {0.011, std::max(0.002, (oy - base_top) * 0.5), 0.02}, kBlack);
            box({0, oy - 0.003, oz}, {0.016, 0.003, 0.024}, kBlack);                      // clema pe sina
            cyl({0, sy, oz - 0.03}, {0, sy, oz + 0.03}, R, R, sd12, kBlack, false);       // tubul (deschis)
            ring({0, sy, oz - 0.029}, R - 0.0035, R + 0.0008, 0.003, lo ? 8 : 16, kBlack);  // marginile tubului
            ring({0, sy, oz + 0.029}, R - 0.0025, R + 0.0008, 0.003, lo ? 8 : 16, kBlack);
            if (!lo) {
                cyl({0, sy - R, oz}, {0, sy - R - 0.008, oz}, 0.007, 0.007, 8, kBlack);   // turela de reglaj sus
                cyl({R, sy, oz + 0.005}, {R + 0.009, sy, oz + 0.005}, 0.0075, 0.0075, 8, kBlack);   // butonul de luminozitate
            }
            m.mat = MatGlass;
            if (!lo && !st.see_through) cyl({0, sy, oz + 0.02}, {0, sy, oz + 0.022}, R - 0.002, R - 0.002, sd12, Rgb{0.03f, 0.05f, 0.06f});
            out.lens_c = P({0, sy, oz - 0.03});
            out.lens_r = R - 0.002;
            out.optic = true;
        } else if (w.has_holo) {
            // holografic: fereastra dreptunghiulara cu aparatoare, corpul cu bateria dedesubt
            const double hw = 0.019, hh = 0.014, hz = 0.034;
            m.mat = MatAluminum;
            box({0, (oy + sy + hh) * 0.5, oz + 0.005}, {0.017, std::max(0.002, (oy - sy - hh) * 0.5), hz}, kBlack);   // corpul
            box({0, sy - hh - 0.002, oz + 0.005}, {hw + 0.002, 0.002, hz * 0.9}, kBlack);                           // aparatoarea de sus
            for (int s2 = -1; s2 <= 1; s2 += 2) box({(hw + 0.001) * s2, sy - 0.002, oz + 0.005}, {0.002, hh + 0.001, hz * 0.9}, kBlack);
            if (!lo) box({0.012, (oy + sy + hh) * 0.5, oz + hz - 0.004}, {0.006, 0.005, 0.006}, kDark);              // butoanele
            m.mat = MatGlass;
            if (!lo && !st.see_through) box({0, sy, oz + 0.02}, {hw - 0.002, hh - 0.002, 0.001}, Rgb{0.03f, 0.05f, 0.06f});
            out.lens_c = P({0, sy, oz - 0.02});
            out.lens_r = 0.015;
            out.optic = true;
        } else if (w.has_scope) {
            AccInfo ai;
            if (acc_model(AttachmentVisual::Scope, {0, sy, oz}, identity(), &ai)) {
                // modelul extern: ocularul si obiectivul dupa capetele lui (imaginea marita se pune in ocular)
                out.lens_c = P({0, sy, oz + ai.back + 0.002});
                out.lens_r = std::max(0.008, ai.end_r * 0.8);
                out.objective_c = P({0, sy, oz + ai.front});
                out.scope = true;
                return;
            }
            // luneta: inele, tub, clopot obiectiv (fata) si ocular (spate), turele
            m.mat = MatAluminum;
            for (int k = -1; k <= 1; k += 2) {
                box({0, (oy + sy) * 0.5, oz + 0.035 * k}, {0.008, std::max(0.004, (oy - sy) * 0.5), 0.007}, kBlack);
                cyl({0, sy, oz + 0.035 * k - 0.006}, {0, sy, oz + 0.035 * k + 0.006}, 0.0155, 0.0155, sd12, kBlack, false);
            }
            cyl({0, sy, oz - 0.06}, {0, sy, oz + 0.08}, 0.0125, 0.0125, sd12, Rgb{0.04f, 0.042f, 0.045f});
            cyl({0, sy, oz + 0.08}, {0, sy, oz + 0.13}, 0.0125, 0.021, sd12, Rgb{0.04f, 0.042f, 0.045f});    // clopotul obiectivului
            cyl({0, sy, oz - 0.06}, {0, sy, oz - 0.1}, 0.0125, 0.019, sd12, Rgb{0.04f, 0.042f, 0.045f});     // ocularul
            if (!lo) {
                cyl({0, sy - 0.0125, oz}, {0, sy - 0.024, oz}, 0.008, 0.008, 8, kBlack);                       // turela de elevatie
                cyl({0.0125, sy, oz}, {0.024, sy, oz}, 0.008, 0.008, 8, kBlack);                               // turela de vant
            }
            m.mat = MatGlass;
            cyl({0, sy, oz + 0.128}, {0, sy, oz + 0.131}, 0.019, 0.019, sd12, Rgb{0.02f, 0.04f, 0.06f});
            cyl({0, sy, oz - 0.098}, {0, sy, oz - 0.101}, 0.017, 0.017, sd12, Rgb{0.015f, 0.02f, 0.03f});
            out.lens_c = P({0, sy, oz - 0.1});
            out.lens_r = 0.017;
            out.objective_c = P({0, sy, oz + 0.13});
            out.scope = true;
        } else {
            // catare: inaltator in spate, varf de catare in fata
            m.mat = MatSteel;
            box({0, rail_y - 0.006, rail_z}, {0.008, 0.01, 0.008}, kBlack);
            box({0, rail_y - 0.012, front_z}, {0.003, 0.016, 0.004}, kBlack);
        }
    };

    // ------------------------------------------------------------------ accesorii pe garda
    auto rail_accessories = [&](double hg_z, double hg_y, double radius) {
        V3 lf;
        if (w.has_light && acc_model(AttachmentVisual::Light, {radius, hg_y, hg_z + 0.02}, rot_z(kPi * 0.5), nullptr, &lf)) {
            out.light = P(lf + V3{0, 0, 0.001});
        } else if (w.has_light) {
            m.mat = MatAluminum;
            V3 c{radius + 0.005, hg_y + 0.008, hg_z + 0.02};
            if (!lo) box({radius - 0.002, hg_y + 0.008, hg_z + 0.04}, {0.008, 0.01, 0.015}, kBlack);
            cyl(c, c + V3{0, 0, 0.09}, 0.0125, 0.0125, sd8, kBlack);
            m.mat = MatGlass;
            cyl(c + V3{0, 0, 0.09}, c + V3{0, 0, 0.092}, 0.0105, 0.0105, sd8, Rgb{0.2f, 0.2f, 0.2f}, true,
                st.light_on ? Rgb{40.0f, 39.0f, 34.0f} : Rgb{});
            out.light = P(c + V3{0, 0, 0.092});
        }
        if (w.has_laser) {
            m.mat = MatPlastic;
            V3 c{-(radius + 0.004), hg_y + 0.008, hg_z + 0.05};
            box(c, {0.01, 0.012, 0.03}, Rgb{0.05f, 0.06f, 0.04f});
            box(c + V3{0, 0, 0.031}, {0.003, 0.003, 0.001}, Rgb{}, identity(), st.laser_on ? Rgb{18.0f, 0.4f, 0.3f} : Rgb{0.4f, 0.0f, 0.0f});
            out.laser = P(c + V3{0, 0, 0.032});
        }
        if (w.has_bipod) {
            // bipiedul: sub capatul garzii; strans de-a lungul tevii, desfacut (picioarele pe sol) culcat
            const double dz = std::max(hg_z + 0.06, S.muzzle.z - 0.36);
            if (!acc_model(AttachmentVisual::Bipod, {0, hg_y + radius, dz}, rot_x(kPi * 0.5 * (1.0 - st.bipod_deploy)))) {
                m.mat = MatSteel;
                for (int s2 = -1; s2 <= 1; s2 += 2) {
                    V3 top{0.012 * s2, hg_y + radius + 0.01, dz};
                    V3 leg = rot_x(kPi * 0.5 * (1.0 - st.bipod_deploy)) * V3{0.08 * s2, 0.2, 0.0};
                    cyl(top, top + leg, 0.005, 0.004, 6, kBlack);
                }
            }
        }
        if (w.has_foregrip && acc_model(AttachmentVisual::VerticalGrip, {0, hg_y + radius, S.support.z}, identity())) {
            // manerul vertical din modelul extern
        } else if (w.has_foregrip) {
            m.mat = MatPolymer;
            cyl({0, hg_y + radius, S.support.z}, {0, S.support.y + 0.02, S.support.z}, 0.012, 0.011, sd8, kBlack);
        }
        if (w.has_angledgrip) {
            m.mat = MatPolymer;
            box({0, hg_y + radius + 0.01, S.support.z + 0.01}, {0.012, 0.016, 0.04}, kBlack, rot_x(0.55));
        }
    };

    // ================================================================== model extern (licenta verificata)
    // Corpul armei vine din modelul OBJ; piesele lui (separate dupa piesele conectate) se animeaza:
    // incarcatorul (reincarcare), pompa, inchizatorul pistolului, manerul inchizatorului cu repetitie;
    // patul se ascunde la ochire, catarea / luneta proprie cand e montata optica noastra.
    if (!V.model.empty()) {
        if (const ObjModel* om = external_model(V.model)) {
            auto axis = [](const std::string& t) {
                V3 v{0, 0, 0};
                double sg = (!t.empty() && t[0] == '-') ? -1.0 : 1.0;
                char c = t.empty() ? 'x' : t.back();
                if (c == 'x' || c == 'X') v.x = sg; else if (c == 'y' || c == 'Y') v.y = sg; else v.z = sg;
                return v;
            };
            const V3 mf = axis(V.model_forward), mu = axis(V.model_up), mr = cross(mf, mu);
            // axele modelului -> sistemul armei: inainte -> +z, sus -> -y, dreapta -> +x
            M3 Ra{};
            const double rows[3][3] = {{mr.x, mr.y, mr.z}, {-mu.x, -mu.y, -mu.z}, {mf.x, mf.y, mf.z}};
            for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) Ra.m[r][c] = rows[r][c];
            const M3 Rm = rot_y(V.model_yaw) * rot_x(V.model_pitch) * rot_z(V.model_roll) * Ra;
            const V3 A{V.model_anchor[0], V.model_anchor[1], V.model_anchor[2]};
            const double sc = V.model_scale;
            auto local = [&](V3 q) { return Rm * ((q - A) * sc); };
            // mana stanga in coordonatele armei (incarcatorul tinut in mana)
            const V3 dh = st.hand_pos - f.origin;
            const V3 hand_local{dot(f.R.col(0), dh), dot(f.R.col(1), dh), dot(f.R.col(2), dh)};
            const V3 mag_pivot{S.mag.x, S.mag.y, S.mag.z};
            const V3 bolt_pivot{0.0, by, S.bolt.z};
            const bool optic_on = w.has_optic();
            const uint8_t keep_mat = m.mat;
            const size_t ntri = om->pos.size() / 3;
            for (size_t t = 0; t < ntri; ++t) {
                const V3 cc = om->tri_center.size() > t ? om->tri_center[t] : V3{0, 0, 0};
                const std::string* kind = nullptr;
                for (const auto& part : V.model_parts) {
                    // tipul terminat cu '@': se clasifica fiecare triunghi dupa propriul centru
                    // (piese lipite de alta piesa in model, ex. catarea din fata pe teava)
                    V3 c = cc;
                    if (part.per_tri) c = (om->pos[t * 3] + om->pos[t * 3 + 1] + om->pos[t * 3 + 2]) * (1.0 / 3.0);
                    if (c.x >= part.x0 && c.x <= part.x1 && c.y >= part.y0 && c.y <= part.y1) { kind = &part.kind; break; }
                }
                M3 Rp = identity();
                V3 pivot{0, 0, 0}, off{0, 0, 0};
                if (kind) {
                    const std::string& k = *kind;
                    if (k == "hide") continue;
                    if (k == "hide_optic" && optic_on) continue;
                    if (k == "stock" && !st.show_stock) continue;
                    if (k == "mag") {
                        if (st.mag_in_gun) { Rp = rot_x(st.mag_spin); pivot = mag_pivot; off = V3{0, st.mag_drop, 0}; }
                        else if (st.mag_in_hand) { Rp = rot_x(-0.2); pivot = mag_pivot; off = hand_local - mag_pivot + V3{0, 0.03, 0}; }
                        else continue;
                    } else if (k == "pump") {
                        off = V3{0, 0, st.pump};
                    } else if (k == "slide") {
                        off = V3{0, 0, -st.slide_back};
                    } else if (k == "bolt") {
                        // manerul se ridica (rotatie in jurul axei tevii) si se trage inapoi
                        Rp = rot_z(-1.1 * st.bolt_open);
                        pivot = bolt_pivot;
                        off = V3{0, 0, -0.07 * st.bolt_open};
                    } else if (k == "charging") {
                        off = V3{0, 0, st.charging};
                    }
                }
                uint32_t ids[3];
                for (int c = 0; c < 3; ++c) {
                    const size_t i = t * 3 + c;
                    V3 q = local(om->pos[i]);
                    q = pivot + Rp * (q - pivot) + off;
                    V3 n = norm(f.R * (Rp * (Rm * om->nrm[i])));
                    m.mat = om->mat[i];
                    Rgb col = om->col[i];
                    ids[c] = m.vertex(f.to(q), n, col, (kind && *kind == "barrel") ? hot : Rgb{});
                }
                m.tri(ids[0], ids[1], ids[2]);
            }
            m.mat = keep_mat;
            // gura tevii: modelul are deja dispozitivul lui; doar atasamentele montate adauga unul
            double mz = S.muzzle.z;
            if (w.has_suppressor || w.has_compensator || w.has_brake || w.has_flashhider)
                mz = muzzle_device(S.muzzle.z, rcv == "pistol" ? 0.006 : 0.0085);
            out.muzzle = P({0, by, mz});
            if (optic_on) sight(S.optic.y, S.optic.z - 0.1, S.muzzle.z - 0.03);
            // incarcatorul: piesa modelului (animata mai sus) sau cel procedural (ex. pistolul, la reincarcare)
            bool has_mag_part = false;
            for (const auto& part : V.model_parts) if (part.kind == "mag") has_mag_part = true;
            if (!has_mag_part) {
                if (st.mag_in_gun && !V.model_has_mag) magazine(mag_pivot, V.mag_tilt, false);
                else if (st.mag_in_hand) magazine(mag_pivot, V.mag_tilt, true);
            }
            if (rcv == "pistol") {
                // lanterna / laserul pe sina de sub cadru
                V3 plf;
                if (w.has_light && acc_model(AttachmentVisual::Light, {0, V.rail_y, V.handguard_z - 0.01}, rot_z(kPi), nullptr, &plf)) {
                    out.light = P(plf + V3{0, 0, 0.001});
                } else if (w.has_light) {
                    m.mat = MatPolymer;
                    box({0, V.rail_y + 0.012, V.handguard_z}, {0.012, 0.011, 0.03}, kBlack);
                    m.mat = MatGlass;
                    box({0, V.rail_y + 0.012, V.handguard_z + 0.031}, {0.009, 0.008, 0.001}, Rgb{0.2f, 0.2f, 0.2f}, identity(),
                        st.light_on ? Rgb{40.0f, 39.0f, 34.0f} : Rgb{});
                    out.light = P({0, V.rail_y + 0.012, V.handguard_z + 0.032});
                }
                if (w.has_laser) {
                    m.mat = MatPlastic;
                    box({0, V.rail_y + 0.014, V.handguard_z - 0.03}, {0.008, 0.008, 0.02}, Rgb{0.05f, 0.06f, 0.04f});
                    out.laser = P({0, V.rail_y + 0.014, V.handguard_z - 0.01});
                }
                if (!optic_on) out.lens_c = P({0, S.optic.y, S.optic.z});
            } else {
                rail_accessories(V.handguard_z, V.rail_y, V.rail_radius);
            }
            m.mat = MatDefault;
            return;
        }
    }
    // ================================================================== receptoarele
    if (rcv == "pistol") {
        double sz = -st.slide_back;
        m.mat = MatSteel;
        box({0, 0.028, 0.10 + sz}, {0.0135, 0.0155, 0.095}, kBlack);
        if (!lo) for (int k = 0; k < 5; ++k) box({0.0137, 0.028, 0.03 + sz + k * 0.007}, {0.0005, 0.012, 0.0015}, Rgb{0.02f, 0.02f, 0.02f});
        m.mat = MatPolymer;
        box({0, 0.052, 0.09}, {0.0125, 0.012, 0.085}, kPoly);
        box({0, 0.105, 0.0}, {0.014, 0.05, 0.022}, kPoly, rot_x(-0.25));
        if (!lo) {
            box({0, 0.072, 0.045}, {0.006, 0.003, 0.025}, kPoly);
            m.mat = MatSteel;
            box({0, 0.066, 0.045}, {0.0025, 0.009, 0.003}, kMetal, rot_x(0.2));
            box({0, 0.009, 0.185 + sz}, {0.002, 0.004, 0.003}, Rgb{0.6f, 0.6f, 0.55f});
            box({0, 0.009, 0.02 + sz}, {0.008, 0.004, 0.003}, kBlack);
        }
        cyl({0, 0.028, 0.19 + sz}, {0, 0.028, 0.2 + sz}, 0.006, 0.006, 6, Rgb{0.01f, 0.01f, 0.01f});
        double mz = S.muzzle.z;
        if (w.has_suppressor) {
            m.mat = MatSteel;
            cyl({0, 0.028, 0.2}, {0, 0.028, 0.36}, 0.016, 0.016, sd8 + 2, Rgb{0.06f, 0.06f, 0.065f});
            mz = 0.36;
        }
        if (w.has_light) {
            m.mat = MatPolymer;
            box({0, 0.07, 0.14}, {0.012, 0.011, 0.03}, kBlack);
            m.mat = MatGlass;
            box({0, 0.07, 0.171}, {0.009, 0.008, 0.001}, Rgb{0.2f, 0.2f, 0.2f}, identity(), st.light_on ? Rgb{40.0f, 39.0f, 34.0f} : Rgb{});
            out.light = P({0, 0.07, 0.172});
        }
        if (w.has_laser) {
            m.mat = MatPlastic;
            box({0, 0.072, 0.11}, {0.008, 0.008, 0.02}, Rgb{0.05f, 0.06f, 0.04f});
            out.laser = P({0, 0.072, 0.13});
        }
        out.muzzle = P({0, 0.028, mz});
        if (st.mag_in_gun) magazine({S.mag.x, S.mag.y, S.mag.z}, V.mag_tilt, false);
        else if (st.mag_in_hand) magazine({S.mag.x, S.mag.y, S.mag.z}, V.mag_tilt, true);
        out.lens_c = P({0, 0.012, 0.03});
        return;
    }

    if (rcv == "pump") {
        double pump = st.pump;
        m.mat = MatWood;
        box({0, 0.08, -0.14}, {0.02, 0.036, 0.11}, kWood);
        box({0, 0.07, -0.02}, {0.014, 0.04, 0.02}, kWood, rot_x(-0.35));
        m.mat = MatSteel;
        box({0, 0.058, 0.08}, {0.02, 0.03, 0.1}, kBlack);
        if (!lo) box({0, 0.092, 0.05}, {0.006, 0.003, 0.025}, kBlack);
        cyl({0, 0.04, 0.18}, {0, 0.04, S.muzzle.z}, 0.011, 0.011, sd8, kDark);
        cyl({0, 0.068, 0.18}, {0, 0.068, S.muzzle.z - 0.11}, 0.011, 0.011, sd8, kBlack);
        m.mat = MatWood;
        cyl({0, 0.068, 0.27 + pump}, {0, 0.068, 0.41 + pump}, 0.022, 0.022, sd8 + 2, kWood);
        if (!lo) for (int k = 0; k < 5; ++k) cyl({0, 0.068, 0.29 + pump + k * 0.024}, {0, 0.068, 0.295 + pump + k * 0.024}, 0.0225, 0.0225, 10, Rgb{0.09f, 0.05f, 0.03f}, false);
        m.mat = MatSteel;
        box({0, 0.025, S.muzzle.z - 0.02}, {0.003, 0.003, 0.003}, Rgb{0.7f, 0.7f, 0.6f});
        if (w.has_optic()) sight(0.03, 0.06, S.muzzle.z - 0.02);
        if (w.has_light) {
            m.mat = MatAluminum;
            cyl({0.03, 0.06, 0.38}, {0.03, 0.06, 0.45}, 0.012, 0.012, sd8, kBlack);
            m.mat = MatGlass;
            cyl({0.03, 0.06, 0.451}, {0.03, 0.06, 0.453}, 0.009, 0.009, sd8, Rgb{0.2f, 0.2f, 0.2f}, true, st.light_on ? Rgb{40.0f, 39.0f, 34.0f} : Rgb{});
            out.light = P({0.03, 0.06, 0.453});
        }
        out.muzzle = P({0, 0.04, S.muzzle.z});
        return;
    }

    if (rcv == "bolt") {
        // pusca cu repetitie: pat dintr-o bucata cu obrazar, receptor rotund, inchizator cu maner
        const double ry = 0.03;
        if (st.show_stock) {
            m.mat = MatPolymer;
            box({0, 0.06, -0.17}, {0.021, 0.042, 0.13}, kTan);                        // patul
            box({0, 0.03, -0.17}, {0.016, 0.012, 0.09}, kTan);                        // obrazarul
            m.mat = MatRubber;
            box({0, 0.065, -0.305}, {0.022, 0.05, 0.006}, kBlack);                    // talpa de cauciuc
        }
        m.mat = MatPolymer;
        box({0, 0.085, 0.0}, {0.016, 0.048, 0.024}, kTan, rot_x(S.grip_tilt));        // pistolul patului (manerul)
        box({0, 0.055, V.handguard_z}, {0.022, 0.024, V.handguard_len + 0.06}, kTan); // garda patului sub teava
        if (!lo) box({0, 0.083, 0.04}, {0.006, 0.003, 0.03}, kBlack);                // garda tragaciului
        m.mat = MatSteel;
        cyl({0, ry, -0.04}, {0, ry, 0.17}, 0.018, 0.018, sd12, kBlack);              // receptorul
        // inchizatorul: manerul se ridica (rotire) si se trage inapoi
        double bo = std::clamp(st.bolt_open, 0.0, 1.0);
        double lift = std::min(1.0, bo * 2.0), pull = std::max(0.0, bo * 2.0 - 1.0);
        double ang = -1.2 * lift;
        V3 root{0.016, ry, S.bolt.z - 0.07 * pull};
        V3 knob = root + V3{0.03 * std::cos(ang), 0.03 * std::sin(ang) * -1.0 + 0.012, 0.0};
        cyl(root, knob, 0.004, 0.004, 6, kMetal);
        m.limb(P(knob), P(knob + V3{0.004, 0.0, 0.0}), 0.008, 0.008, 8, kBlack, f.R * V3{0, -1, 0});
        if (bo > 0.05) box({0.018, ry - 0.004, 0.06 - 0.07 * pull}, {0.0008, 0.007, 0.028}, Rgb{0.01f, 0.01f, 0.01f});   // fereastra deschisa
        out.bolt = P(knob);
        out.charging = out.bolt;
        // teava (subtiata spre gura) + dispozitiv
        double bz0 = 0.17;
        cyl({0, by, bz0}, {0, by, barrel_end}, 0.0125, 0.0095, sd8 + 2, kDark, true, hot);
        double mz = muzzle_device(barrel_end, 0.0095);
        out.muzzle = P({0, by, mz});
        sight(ry - 0.018, 0.02, barrel_end - 0.03);
        rail_accessories(V.handguard_z, 0.055, 0.022);
        if (st.mag_in_gun) magazine({S.mag.x, S.mag.y, S.mag.z}, V.mag_tilt, false);
        else if (st.mag_in_hand) magazine({S.mag.x, S.mag.y, S.mag.z}, V.mag_tilt, true);
        return;
    }

    // ------------------------------------------------------------------ AR / SMG
    const double hg_z = V.handguard_z, hg_len = V.handguard_len;
    if (st.show_stock) {
        m.mat = MatPolymer;
        if (V.stock == "smg") {
            m.mat = MatSteel;
            box({0, 0.06, -0.14}, {0.019, 0.022, 0.08}, kBlack);
            box({0, 0.07, -0.225}, {0.02, 0.04, 0.008}, kBlack);
        } else if (w.has_light_stock) {
            // pat usor (schelet): cadru subtire
            m.mat = MatSteel;
            cyl({0, 0.042, -0.02}, {0, 0.042, -0.15}, 0.0125, 0.0125, sd8, kBlack);
            m.mat = MatPolymer;
            box({0, 0.04, -0.12}, {0.008, 0.006, 0.045}, kPoly);
            box({0, 0.085, -0.12}, {0.008, 0.006, 0.045}, kPoly);
            box({0, 0.062, -0.165}, {0.012, 0.03, 0.006}, kPoly);
        } else {
            m.mat = MatAluminum;
            cyl({0, 0.042, -0.02}, {0, 0.042, -0.15}, 0.0145, 0.0145, sd8, kBlack);
            m.mat = MatPolymer;
            if (w.has_stock) box({0, 0.06, -0.12}, {0.024, 0.042, 0.05}, kPoly);
            else box({0, 0.062, -0.12}, {0.021, 0.036, 0.045}, kPoly);
            if (!lo) box({0, 0.036, -0.12}, {0.018, 0.008, 0.04}, kPoly);
        }
    }
    // receptor inferior, locasul incarcatorului, tragaci
    m.mat = MatAluminum;
    box({0, 0.068, 0.07}, {0.019, 0.026, 0.09}, kDark);
    box({0, 0.095, 0.11}, {0.0175, 0.018, 0.032}, kDark);
    if (!lo) {
        box({0, 0.108, 0.035}, {0.006, 0.0025, 0.032}, kBlack);
        m.mat = MatSteel;
        box({S.trigger.x, S.trigger.y, S.trigger.z}, {0.0025, 0.011, 0.003}, kMetal, rot_x(0.2));
        cyl({-0.02, 0.06, 0.0}, {-0.025, 0.06, 0.0}, 0.005, 0.005, 6, kBlack);
        if (st.selector >= 0.0) {
            // selector ambidextru (partea dreapta): parghia arata modul de foc curent
            cyl({0.0195, 0.06, 0.0}, {0.0225, 0.06, 0.0}, 0.0045, 0.0045, 6, kBlack);
            V3 dir = rot_x(st.selector) * V3{0, 0, 1};
            box(V3{0.0232, 0.06, 0.0} + dir * 0.0065, {0.0012, 0.0024, 0.0065}, kMetal, rot_x(st.selector));
        }
    }
    // receptor superior, sina, fereastra de ejectie, manerul de armare
    m.mat = MatAluminum;
    box({0, 0.035, 0.09}, {0.0205, 0.0145, 0.12}, kBlack);
    box({0, 0.0185, 0.09}, {0.0105, 0.002, 0.12}, kBlack);
    if (!lo) for (int k = 0; k < 9; ++k) box({0, 0.0155, -0.02 + k * 0.026}, {0.0105, 0.0025, 0.0045}, kBlack);
    box({0.0208, 0.034, 0.105}, {0.0006, 0.0065, 0.026}, st.open_port ? Rgb{0.01f, 0.01f, 0.01f} : kDark);
    if (!lo) {
        box({0.02, 0.03, 0.07}, {0.004, 0.006, 0.01}, kBlack);
        cyl({0.017, 0.028, 0.035}, {0.03, 0.026, 0.035}, 0.006, 0.006, 6, kBlack);
        box({0, 0.022, -0.03 + st.charging}, {0.016, 0.005, 0.014}, kBlack);
    }
    // garda octogonala cu fante M-LOK si sina superioara
    cyl({0, 0.042, hg_z - hg_len}, {0, 0.042, hg_z + hg_len}, 0.027, 0.027, 8, kBlack, true, hot * 0.3f);
    if (!lo) for (int k = 0; k < 3; ++k)
        for (int s2 = -1; s2 <= 1; s2 += 2)
            box({0.0262 * s2, 0.048, hg_z - hg_len * 0.6 + k * hg_len * 0.6}, {0.0008, 0.005, 0.011}, Rgb{0.012f, 0.012f, 0.014f});
    box({0, 0.0145, hg_z}, {0.0105, 0.002, hg_len}, kBlack);
    // teava: de la garda pana la punctul gurii (teava lunga / scurta muta punctul)
    double bz = hg_z + hg_len;
    m.mat = MatSteel;
    box({0, by, bz + 0.012}, {0.012, 0.012, 0.012}, kMetal, identity(), hot);
    double bend = std::max(bz + 0.03, barrel_end);
    cyl({0, by, bz}, {0, by, bend}, 0.0085, 0.0085, sd8, kDark, true, hot);
    double mz = muzzle_device(bend, 0.0085);
    out.muzzle = P({0, by, mz});
    sight(0.004, 0.0, bz - 0.01);
    // maner + incarcator
    m.mat = MatPolymer;
    box({S.grip.x, S.grip.y, S.grip.z}, {0.0135, 0.046, 0.019}, kPoly, rot_x(S.grip_tilt));
    if (st.mag_in_gun) magazine({S.mag.x, S.mag.y, S.mag.z}, V.mag_tilt, false);
    else if (st.mag_in_hand) magazine({S.mag.x, S.mag.y, S.mag.z}, V.mag_tilt, true);
    rail_accessories(hg_z, 0.042, 0.027);
    (void)kPi;
}

}
