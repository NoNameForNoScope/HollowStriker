#include "pawn_mesh.hpp"
#include <cmath>
#include <algorithm>

namespace bodycam {

namespace {
constexpr double kPi = 3.14159265358979323846;
Rgb scale(Rgb c, float k) { return c * k; }
}

// ============================================================================
// Masina RC
// ============================================================================
void build_vehicle_mesh(Mesh& m, const Vehicle& v, double time) {
    const VehicleConfig& c = v.cfg;
    const bool dead = !v.alive;
    const float burn = dead ? 0.22f : (v.health() < c.smoke_below ? 0.7f : 1.0f);   // caroserie arsa / afumata
    const V3 F0{std::cos(v.yaw), std::sin(v.yaw), 0.0}, L0{-std::sin(v.yaw), std::cos(v.yaw), 0.0}, U0{0, 0, 1};
    // caroseria: tangaj (botul sus = pozitiv) apoi ruliu (negativ = partea dreapta jos)
    double pitch = v.pitch, roll = dead ? 0.35 : v.roll;
    V3 F = rotate_axis(F0, L0, -pitch), U = rotate_axis(U0, L0, -pitch), L = L0;
    L = rotate_axis(L, F, -roll);
    U = rotate_axis(U, F, -roll);
    const double R = c.wheel_radius;
    const double half_wb = c.wheelbase * 0.5, half_tr = c.track * 0.5;
    const V3 base{v.x, v.y, 0.0};
    const V3 pivot = base + U0 * (R + c.ride_height * 0.55);
    auto B = [&](double f, double l, double u) { return pivot + F * f + L * l + U * u; };

    const Rgb paint = scale(Rgb{0.075f, 0.085f, 0.07f}, burn);       // vopsea mata (verde inchis)
    const Rgb black = scale(Rgb{0.018f, 0.018f, 0.02f}, burn);
    const Rgb steel = scale(Rgb{0.32f, 0.32f, 0.33f}, burn);
    const Rgb tire = Rgb{0.022f, 0.021f, 0.02f};
    const Rgb rim = scale(Rgb{0.12f, 0.12f, 0.13f}, burn);
    const Rgb spring = scale(Rgb{0.55f, 0.08f, 0.05f}, burn);

    // --- sasiu si caroserie
    m.mat = MatAluminum;
    m.box(B(0, 0, -0.012), F * (c.length * 0.45), L * 0.07, U * 0.005, steel);               // placa sasiului
    m.mat = MatPlastic;
    m.box(B(-0.01, 0, 0.022), F * 0.15, L * 0.085, U * 0.03, paint);                          // carcasa
    m.box(B(0.05, 0, 0.058), F * 0.07, L * 0.06, U * 0.012, paint * 0.9f);                    // capota (electronica)
    m.box(B(-0.09, 0, 0.05), F * 0.045, L * 0.07, U * 0.02, black);                            // bateria (in spate)
    m.mat = MatRubber;
    m.box(B(c.length * 0.5, 0, -0.005), F * 0.018, L * 0.1, U * 0.022, black);                // bara fata
    m.box(B(-c.length * 0.48, 0, 0.0), F * 0.015, L * 0.09, U * 0.018, black);                // bara spate
    // eleron spate (pe doi suporti)
    m.mat = MatPlastic;
    for (int sd = -1; sd <= 1; sd += 2) m.limb(B(-0.15, 0.04 * sd, 0.04), B(-0.18, 0.045 * sd, 0.1), 0.004, 0.004, 4, black);
    m.box(B(-0.185, 0, 0.102), F * 0.035, L * 0.1, U * 0.003, paint);
    // cusca de protectie (tevi de otel)
    m.mat = MatSteel;
    for (int sd = -1; sd <= 1; sd += 2) {
        m.limb(B(0.09, 0.075 * sd, 0.04), B(0.03, 0.06 * sd, 0.1), 0.0045, 0.0045, 5, steel);
        m.limb(B(0.03, 0.06 * sd, 0.1), B(-0.08, 0.06 * sd, 0.1), 0.0045, 0.0045, 5, steel);
        m.limb(B(-0.08, 0.06 * sd, 0.1), B(-0.12, 0.075 * sd, 0.04), 0.0045, 0.0045, 5, steel);
    }
    m.limb(B(0.03, -0.06, 0.1), B(0.03, 0.06, 0.1), 0.0045, 0.0045, 5, steel);
    m.limb(B(-0.08, -0.06, 0.1), B(-0.08, 0.06, 0.1), 0.0045, 0.0045, 5, steel);

    // --- camera pe catarg (inaltimea si inclinarea din config)
    {
        const double cam_u = c.cam_height - (R + c.ride_height * 0.55);
        V3 cam = B(c.cam_forward, 0, cam_u);
        m.mat = MatAluminum;
        m.limb(B(c.cam_forward - 0.01, 0, 0.06), cam - U * 0.012, 0.005, 0.005, 5, steel);
        m.mat = MatPlastic;
        V3 cf = rotate_axis(F, L, -c.cam_tilt);
        m.box(cam, cf * 0.016, L * 0.018, U * 0.014, black);
        m.mat = MatGlass;
        m.limb(cam + cf * 0.014, cam + cf * 0.024, 0.008, 0.007, 8, Rgb{0.01f, 0.012f, 0.02f}, U);
        if (!dead) m.box(cam + cf * 0.017 + L * 0.012 + U * 0.008, F * 0.002, L * 0.002, U * 0.002, Rgb{}, Rgb{6.0f, 0.2f, 0.2f});   // LED inregistrare
    }
    // --- antena (bici): se apleaca inapoi cu viteza si oscileaza
    {
        V3 a0 = B(-0.12, -0.05, 0.03);
        double lean = std::clamp(v.speed * 0.06, -0.25, 0.4) + (dead ? 0.9 : 0.0);
        double wob = std::sin(time * 9.0) * 0.04 * std::min(1.0, std::abs(v.speed) * 0.3 + v.last_impact * 0.2);
        V3 a1 = a0 + U * 0.14 - F * (0.14 * lean) + L * (0.14 * wob);
        V3 a2 = a1 + U * 0.12 - F * (0.12 * lean * 1.6) + L * (0.12 * wob * 2.0);
        m.mat = MatPlastic;
        m.limb(a0, a1, 0.0025, 0.002, 4, black);
        m.limb(a1, a2, 0.002, 0.0015, 4, black);
        m.ellipsoid(a2, F * 0.004, L * 0.004, U * 0.004, 2, 4, Rgb{0.6f, 0.08f, 0.04f});
    }
    // --- lumini: faruri (alb), stopuri (rosu, mai puternice la franare)
    if (!dead && v.battery > 0.0) {
        const bool braking = (v.throttle < -0.05 && v.speed > 0.2) || (v.throttle > 0.05 && v.speed < -0.2);
        m.mat = MatGlass;
        for (int sd = -1; sd <= 1; sd += 2) {
            m.box(B(c.length * 0.5 + 0.019, 0.055 * sd, 0.012), F * 0.002, L * 0.012, U * 0.008, Rgb{0.2f, 0.2f, 0.2f}, Rgb{40.0f, 38.0f, 32.0f});
            float k = braking ? 14.0f : 3.0f;
            m.box(B(-c.length * 0.48 - 0.016, 0.06 * sd, 0.012), F * 0.002, L * 0.01, U * 0.006, Rgb{0.1f, 0.0f, 0.0f}, Rgb{k, 0.2f, 0.1f});
        }
    }
    // --- incarcatura exploziva (pe spate): bloc + fire
    if (v.charge) {
        m.mat = MatFabric;
        m.box(B(-0.03, 0, 0.085), F * 0.05, L * 0.04, U * 0.018, Rgb{0.32f, 0.29f, 0.2f});
        m.mat = MatRubber;
        m.limb(B(-0.03, 0.03, 0.1), B(0.04, 0.03, 0.07), 0.002, 0.002, 4, Rgb{0.5f, 0.05f, 0.03f});
        m.limb(B(-0.03, -0.03, 0.1), B(0.04, -0.03, 0.07), 0.002, 0.002, 4, Rgb{0.05f, 0.1f, 0.5f});
    }

    // --- roti + suspensie (rotile raman pe sol, caroseria se misca deasupra lor)
    for (int wi = 0; wi < 4; ++wi) {
        const bool front = wi < 2;
        const double sd = (wi % 2 == 0) ? 1.0 : -1.0;
        if (dead && wi == 0) continue;                                   // roata smulsa
        const double fx = front ? half_wb : -half_wb;
        V3 Fw = front ? rotate_axis(F0, U0, v.steer) : F0;
        V3 Aw = norm(cross(U0, Fw));                                     // axa rotii (spre stanga)
        // compresia: cat de jos e coltul caroseriei fata de pozitia de repaus
        V3 corner = B(fx, half_tr * 0.55 * sd, -0.01);
        double rest_z = pivot.z - 0.01;
        double comp = std::clamp(rest_z - corner.z, -c.susp_travel, c.susp_travel);
        V3 hub = base + F0 * fx + L0 * (half_tr * sd) + U0 * (R + comp * 0.3);
        // anvelopa: cilindru pe axa + crampoane care se rotesc
        m.mat = MatRubber;
        m.limb(hub - Aw * 0.024, hub + Aw * 0.024, R, R, 14, tire, Fw, 1.0, true);
        for (int k = 0; k < 10; ++k) {
            double ang = v.wheel_phase + k * (2.0 * kPi / 10.0);
            V3 rdir = Fw * std::sin(ang) + U0 * std::cos(ang);
            V3 tang = Fw * std::cos(ang) - U0 * std::sin(ang);
            m.box(hub + rdir * (R + 0.002), Aw * 0.022, rdir * 0.004, tang * 0.006, tire * 0.8f);
        }
        // janta cu 5 spite pe fata exterioara (arata rotatia)
        V3 out_face = hub + Aw * (0.0245 * sd);
        m.mat = MatAluminum;
        m.limb(out_face, out_face + Aw * (0.002 * sd), R * 0.62, R * 0.62, 10, rim, Fw, 1.0, true);
        for (int k = 0; k < 5; ++k) {
            double ang = v.wheel_phase + k * (2.0 * kPi / 5.0);
            V3 rdir = Fw * std::sin(ang) + U0 * std::cos(ang);
            V3 tang = Fw * std::cos(ang) - U0 * std::sin(ang);
            m.box(out_face + Aw * (0.003 * sd) + rdir * (R * 0.3), rdir * (R * 0.28), tang * 0.004, Aw * 0.002, steel);
        }
        m.limb(out_face, out_face + Aw * (0.006 * sd), 0.007, 0.007, 6, steel, Fw);      // butucul
        // brate (jos) si amortizor (arc rosu) intre caroserie si butuc
        m.mat = MatSteel;
        V3 arm_in = B(fx * 0.85, 0.035 * sd, -0.012);
        m.limb(arm_in, hub - Aw * (0.02 * sd), 0.004, 0.004, 4, black);
        V3 shock_top = B(fx * 0.7, 0.05 * sd, 0.05);
        m.limb(shock_top, hub - Aw * (0.018 * sd) + U0 * 0.01, 0.007, 0.006, 6, spring);
    }
    m.mat = MatDefault;
}

// ============================================================================
// Drona FPV
// ============================================================================
void build_drone_mesh(Mesh& m, const Drone& d) {
    const DroneConfig& c = d.cfg;
    const bool dead = !d.alive;
    const float burn = dead ? 0.3f : 1.0f;
    const V3 F0{std::cos(d.yaw), std::sin(d.yaw), 0.0}, L0{-std::sin(d.yaw), std::cos(d.yaw), 0.0}, U0{0, 0, 1};
    // tangaj pozitiv = botul in jos; ruliu pozitiv = partea dreapta in jos
    V3 F = rotate_axis(F0, L0, d.pitch), U = rotate_axis(U0, L0, d.pitch), L = L0;
    L = rotate_axis(L, F, -d.roll);
    U = rotate_axis(U, F, -d.roll);
    const V3 C{d.x, d.y, d.z};
    auto B = [&](double f, double l, double u) { return C + F * f + L * l + U * u; };
    const Rgb carbon = scale(Rgb{0.025f, 0.025f, 0.028f}, burn);
    const Rgb motor_c = scale(Rgb{0.32f, 0.06f, 0.05f}, burn);     // clopote anodizate (rosu)
    const Rgb prop_c = scale(Rgb{0.05f, 0.09f, 0.12f}, burn);      // elice din policarbonat fumuriu

    // --- placile cadrului (sus / jos) + distantiere
    m.mat = MatPlastic;
    m.box(B(0, 0, -0.006), F * 0.05, L * 0.022, U * 0.002, carbon);
    m.box(B(0, 0, 0.022), F * 0.045, L * 0.02, U * 0.0015, carbon);
    m.mat = MatAluminum;
    for (int i = 0; i < 4; ++i) {
        double sf = (i < 2) ? 1.0 : -1.0, sl = (i % 2 == 0) ? 1.0 : -1.0;
        m.limb(B(0.035 * sf, 0.015 * sl, -0.004), B(0.035 * sf, 0.015 * sl, 0.021), 0.0025, 0.0025, 4, Rgb{0.4f, 0.12f, 0.1f} * burn);
    }
    // stiva electronica (controler + ESC) intre placi
    m.mat = MatPlastic;
    m.box(B(0, 0, 0.007), F * 0.015, L * 0.015, U * 0.007, Rgb{0.03f, 0.08f, 0.05f} * burn);
    // --- bateria (pe sus) cu banda de prindere
    m.box(B(-0.005, 0, 0.042), F * 0.038, L * 0.017, U * 0.018, Rgb{0.12f, 0.11f, 0.02f} * burn);
    m.mat = MatFabric;
    m.box(B(-0.005, 0, 0.042), F * 0.006, L * 0.0185, U * 0.0195, Rgb{0.4f, 0.04f, 0.04f} * burn);
    // --- bratele (X) si motoarele cu elice
    const int skip_arm = dead ? 1 : -1;                                   // un brat rupt la impact
    for (int i = 0; i < 4; ++i) {
        double ang = kPi * 0.25 + i * kPi * 0.5;                         // 45, 135, 225, 315 grade
        V3 dir = F * std::cos(ang) + L * std::sin(ang);
        V3 perp = norm(cross(U, dir));
        double len = c.arm_length;
        if (i == skip_arm) len *= 0.45;
        m.mat = MatPlastic;
        m.box(C + dir * (len * 0.5) - U * 0.004, dir * (len * 0.5), perp * 0.008, U * 0.003, carbon);
        if (i == skip_arm) continue;
        V3 mp = C + dir * len;
        m.mat = MatAluminum;
        m.limb(mp - U * 0.002, mp + U * 0.016, 0.0135, 0.0125, 10, motor_c, dir);           // clopotul motorului
        m.limb(mp + U * 0.016, mp + U * 0.022, 0.003, 0.003, 6, Rgb{0.5f, 0.5f, 0.52f} * burn, dir);   // axul + piulita
        // elice cu 3 pale; sensuri alternante (diagonalele se rotesc la fel)
        const double spin = (i == 0 || i == 2) ? 1.0 : -1.0;
        m.mat = MatPlastic;
        for (int b = 0; b < 3; ++b) {
            double pa = d.prop_phase * spin + b * (2.0 * kPi / 3.0) + i * 0.7;
            V3 bd = rotate_axis(dir, U, pa);
            V3 bp = norm(cross(U, bd));
            V3 hub = mp + U * 0.019;
            // pala: putin rasucita (unghi de atac), mai lata la mijloc
            V3 tw = rotate_axis(bp, bd, 0.25 * spin);
            m.box(hub + bd * (c.prop_radius * 0.5), bd * (c.prop_radius * 0.5), tw * 0.0075, U * 0.0012, prop_c);
        }
    }
    // --- camera FPV in fata, inclinata in sus
    {
        V3 cf = rotate_axis(F, L, -c.cam_uptilt);
        V3 cu = norm(cross(cf, L));
        V3 cam = B(0.05, 0, 0.008);
        m.mat = MatFabric;   // suport TPU
        for (int sd = -1; sd <= 1; sd += 2) m.box(cam + L * (0.016 * sd), F * 0.012, L * 0.002, U * 0.012, Rgb{0.4f, 0.08f, 0.05f} * burn);
        m.mat = MatPlastic;
        m.box(cam, cf * 0.01, L * 0.0135, cu * 0.0135, carbon);
        m.mat = MatGlass;
        m.limb(cam + cf * 0.009, cam + cf * 0.02, 0.0085, 0.007, 8, Rgb{0.01f, 0.012f, 0.025f}, cu);
    }
    // --- antena video (spate, in V) + LED-uri de pozitie
    m.mat = MatRubber;
    for (int sd = -1; sd <= 1; sd += 2) {
        V3 a0 = B(-0.05, 0.008 * sd, 0.025);
        V3 a1 = a0 - F * 0.035 + U * 0.03 + L * (0.02 * sd);
        m.limb(a0, a1, 0.002, 0.0018, 4, Rgb{0.02f, 0.02f, 0.02f});
        m.ellipsoid(a1, F * 0.006, L * 0.006, U * 0.008, 2, 5, Rgb{0.03f, 0.03f, 0.03f});
    }
    if (!dead && d.battery > 0.0) {
        m.mat = MatGlass;
        // rosu la stanga, verde la dreapta (ca la avioane), alb clipitor in spate
        m.box(B(-0.052, 0.02, -0.006), F * 0.004, L * 0.006, U * 0.0025, Rgb{}, Rgb{18.0f, 0.4f, 0.4f});
        m.box(B(-0.052, -0.02, -0.006), F * 0.004, L * 0.006, U * 0.0025, Rgb{}, Rgb{0.4f, 18.0f, 0.4f});
    }
    m.mat = MatDefault;
}

}
