#include "tablet_ui.hpp"
#include "tablet_canvas.hpp"
#include "font.hpp"
#include "../sim/player_sim.hpp"
#include "../core/ini.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <algorithm>

namespace bodycam {

using namespace tui;

namespace {
const char* status_of(const Agent& a) {
    if (!a.alive()) return "KIA";
    if (a.health.downed) return "CAZUT";
    if (a.hp < 60.0) return "RANIT";
    return "OK";
}
Col status_col(const Agent& a) {
    if (!a.alive()) return kDim;
    if (a.health.downed) return kBad;
    if (a.hp < 60.0) return kWarn;
    return kGood;
}

}

const char* tablet_page_name(TabletPage p) {
    switch (p) {
        case TabletPage::Home: return "ACASA";
        case TabletPage::Loadout: return "ECHIPAMENT";
        case TabletPage::Map: return "HARTA";
        case TabletPage::Objectives: return "OBIECTIVE";
        case TabletPage::Match: return "MECI";
        case TabletPage::Gadgets: return "GADGETURI";
        case TabletPage::Settings: return "SETARI";
        case TabletPage::Cameras: return "CAMERE ECHIPA";
        case TabletPage::Stats: return "STATISTICI";
        case TabletPage::Range: return "POLIGON";
        default: return "?";
    }
}

// ============================================================================
bool UserSettings::load(const std::string& path) {
    IniParser ini;
    if (!ini.load(path)) return false;
    mouse_sens = ini.get_d("input", "mouse_sens", mouse_sens);
    drone_sens = ini.get_d("input", "drone_sens", drone_sens);
    invert_y = ini.get_d("input", "invert_y", invert_y ? 1.0 : 0.0) > 0.5;
    tablet_brightness = ini.get_d("tablet", "brightness", tablet_brightness);
    fov = ini.get_d("video", "fov", fov);
    weapon_fov = ini.get_d("video", "weapon_fov", weapon_fov);
    lens = ini.get_d("video", "lens", lens ? 1.0 : 0.0) > 0.5;
    noise = ini.get_d("video", "noise", noise ? 1.0 : 0.0) > 0.5;
    motion_blur = ini.get_d("video", "motion_blur", motion_blur ? 1.0 : 0.0) > 0.5;
    compression = ini.get_d("video", "compression", compression ? 1.0 : 0.0) > 0.5;
    volume = ini.get_d("audio", "volume", volume);
    throw_assist = ini.get_d("gameplay", "throw_assist", throw_assist ? 1.0 : 0.0) > 0.5;
    drone_acro_default = ini.get_d("gameplay", "drone_acro", drone_acro_default ? 1.0 : 0.0) > 0.5;
    return true;
}

bool UserSettings::save(const std::string& path) const {
    std::ofstream f(path);
    if (!f) return false;
    f << "; Setari BODYCAM-MINI (scrise de pe tableta, pagina SETARI)\n";
    f << "[input]\nmouse_sens = " << mouse_sens << "\ndrone_sens = " << drone_sens << "\ninvert_y = " << (invert_y ? 1 : 0) << "\n";
    f << "[tablet]\nbrightness = " << tablet_brightness << "\n";
    f << "[video]\nfov = " << fov << "\nweapon_fov = " << weapon_fov << "\nlens = " << (lens ? 1 : 0) << "\nnoise = " << (noise ? 1 : 0)
      << "\nmotion_blur = " << (motion_blur ? 1 : 0) << "\ncompression = " << (compression ? 1 : 0) << "\n";
    f << "[audio]\nvolume = " << volume << "\n";
    f << "[gameplay]\nthrow_assist = " << (throw_assist ? 1 : 0) << "\ndrone_acro = " << (drone_acro_default ? 1 : 0) << "\n";
    return true;
}

// ============================================================================
TabletCommand tablet_ui(TabletScreen& scr, TabletUiState& st, const World& w, int local, const TabletClick& click_in,
                        double time, const TabletInfo& info) {
    TabletCommand cmd;
    if (scr.rgb.size() != static_cast<size_t>(scr.w) * scr.h * 3) scr.rgb.assign(static_cast<size_t>(scr.w) * scr.h * 3, 0.0f);
    Canvas cv{scr};
    const int W = scr.w, H = scr.h;
    const Agent* me = (local >= 0 && local < static_cast<int>(w.agents.size())) ? &w.agents[local] : nullptr;
    char buf[160];

    // --- Pornirea ecranului: negru, apoi sigla; fara interactiune pana e gata ---
    if (info.power < 0.999) {
        cv.rect(0, 0, W, H, {0.0f, 0.0f, 0.0f});
        float k = static_cast<float>(std::clamp((info.power - 0.3) / 0.7, 0.0, 1.0));
        Col logo{kAccent.r * k, kAccent.g * k, kAccent.b * k};
        cv.textc(W / 2, H / 2 - 8, "TACNET", logo);
        cv.bar(W / 2 - 40, H / 2 + 6, 80, 3, info.power, logo);
        return cmd;
    }
    TabletClick click = click_in;
    if (click.clicked) { cmd.tapped = true; st.last_tap_time = time; }
    const bool piloting = me && me->possess != Possession::Body;

    cv.rect(0, 0, W, H, kBg);
    // --- bara de stare: pagina, ora, bateria tabletei (reala) ---
    int tsec = static_cast<int>(info.clock_seconds);
    std::snprintf(buf, sizeof(buf), "TACNET // %s", piloting ? (me->possess == Possession::Drone ? "DRONA FPV" : "RC-1")
                                                              : tablet_page_name(st.page));
    cv.text(3, 2, buf, kDim);
    int bat = me ? static_cast<int>(me->inv.tablet_battery * 100.0) : 100;
    std::snprintf(buf, sizeof(buf), "%02d:%02d  %3d%%", (tsec / 3600) % 24, (tsec / 60) % 60, bat);
    cv.text(W - 3 - font::text_width(buf, 1), 2, buf, bat < 20 ? kBad : kDim);
    cv.rect(0, 10, W, 1, {0.1f, 0.15f, 0.17f});

    // bara de navigare pe doua randuri: cuvinte intregi (fara abrevieri)
    const int top = 12, nav_h = 27, bottom = H - nav_h;

    // =====================================================================
    // Pilotarea unui gadget: imaginea camerei lui ocupa ecranul tabletei
    // =====================================================================
    if (piloting && me) {
        bool drone = me->possess == Possession::Drone;
        int fh = H - top - 18;
        if (info.feed && info.feed->w > 0) {
            for (int y = 0; y < fh; ++y)
                for (int x = 0; x < W; ++x) {
                    int sx = x * info.feed->w / W, sy = y * info.feed->h / fh;
                    // rupturi de semnal analogic (distanta, pereti, baterie)
                    if (info.signal_loss > 0.02) {
                        uint32_t hsh = static_cast<uint32_t>(y / 3) * 2654435761u + static_cast<uint32_t>(time * 30.0) * 40503u;
                        if (((hsh >> 16) & 1023) / 1023.0 < info.signal_loss * 0.6)
                            sx = std::clamp(sx + static_cast<int>(((hsh >> 4) & 31) - 16), 0, info.feed->w - 1);
                    }
                    const float* p = info.feed->px(sx, sy);
                    float k = static_cast<float>(info.feed_exposure);
                    float r = p[0] * k / (1.0f + p[0] * k), g = p[1] * k / (1.0f + p[1] * k), b = p[2] * k / (1.0f + p[2] * k);
                    float n = static_cast<float>(((x * 37 + y * 91 + static_cast<int>(time * 60) * 13) % 17) / 17.0)
                            * (0.05f + 0.4f * static_cast<float>(info.signal_loss));
                    if (drone) { r = r * 1.05f; b = b * 1.08f; }   // senzor FPV: culori saturate
                    cv.px(x, top + y, {r + n, g + n, b + n});
                }
        } else {
            cv.rect(0, top, W, fh, {0.0f, 0.0f, 0.0f});
            cv.textc(W / 2, top + fh / 2, "FARA SEMNAL", kBad);
        }
        // OSD: telemetria gadgetului (din simulare, nu decor)
        const Col kOsd{1.0f, 1.0f, 1.0f};
        auto heading_txt = [&](double yaw) {
            // busola: nordul = susul hartii tabletei (-y); +x = est; grade in sensul acelor de ceas
            int deg = static_cast<int>(std::lround(std::fmod(yaw * 180.0 / 3.14159265358979 + 90.0 + 720.0, 360.0))) % 360;
            static const char* card[8] = {"N", "NE", "E", "SE", "S", "SV", "V", "NV"};
            std::snprintf(buf, sizeof(buf), "%s %03d", card[((deg + 22) / 45) % 8], deg);
            cv.textc(W / 2, top + 3, buf, kOsd);
        };
        if (drone && me->drone_id >= 0) {
            const Drone& d = w.drones[me->drone_id];
            cv.text(4, top + 3, d.acro ? "ACRO" : "ANGLE", kOsd);
            std::snprintf(buf, sizeof(buf), "GAZ %d%%", static_cast<int>(d.throttle * 100));
            cv.text(4, top + 13, buf, kOsd);
            std::snprintf(buf, sizeof(buf), "%.1fV %d%%", d.voltage(), static_cast<int>(d.battery * 100));
            cv.textr(W - 4, top + 3, buf, d.battery < 0.2 ? kBad : kOsd);
            std::snprintf(buf, sizeof(buf), "STRUCT %d%%", static_cast<int>(d.health() * 100));
            cv.textr(W - 4, top + 13, buf, d.health() < d.cfg.smoke_below ? kBad : kOsd);
            heading_txt(d.yaw);
            // orizont artificial: linia se inclina cu ruliul si urca / coboara cu tangajul
            {
                // pozitia orizontului real in imagine: camera inclinata (uptilt - tangaj), proiectie cu FOV-ul senzorului
                double hf = d.cfg.cam_fov * 3.14159265358979 / 180.0;
                double tv = std::tan(hf * 0.5) * static_cast<double>(d.cfg.cam_h) / std::max(1, d.cfg.cam_w);
                double camp = std::clamp(-d.pitch + d.cfg.cam_uptilt, -1.05, 1.05);
                double cx = W * 0.5, cy = top + fh * 0.5 + std::clamp((fh * 0.5) / tv * std::tan(camp), -fh * 0.42, fh * 0.42);
                double ca = std::cos(-d.roll), sa = std::sin(-d.roll);
                for (int sd = -1; sd <= 1; sd += 2) {
                    double r0 = 14, r1 = 46;
                    cv.line(cx + ca * r0 * sd, cy + sa * r0 * sd, cx + ca * r1 * sd, cy + sa * r1 * sd, kOsd);
                }
            }
            std::snprintf(buf, sizeof(buf), "ALTITUDINE %.1f M", d.z);
            cv.textr(W - 4, top + fh / 2 - 10, buf, kOsd);
            std::snprintf(buf, sizeof(buf), "URCARE %+.1f M/S", std::abs(d.vz) < 0.05 ? 0.0 : d.vz);
            cv.textr(W - 4, top + fh / 2, buf, kOsd);
            std::snprintf(buf, sizeof(buf), "%.1f M/S", std::hypot(d.vx, d.vy));
            cv.text(4, top + fh / 2 - 5, buf, kOsd);
            std::snprintf(buf, sizeof(buf), "DIST %.0fM", std::hypot(d.x - me->x, d.y - me->y));
            cv.textc(W / 2, top + fh - 10, buf, kOsd);
            if (d.battery < 0.2 && std::fmod(time, 0.8) < 0.5) cv.textc(W / 2, top + fh / 2 + 22, "BATERIE SCAZUTA", kBad);
            if (!d.alive) cv.textc(W / 2, top + fh / 2 + 10, "DRONA DISTRUSA", kBad);
        } else if (me->vehicle_id >= 0) {
            const Vehicle& v = w.vehicles[me->vehicle_id];
            const char* gear = v.speed < -0.05 ? "R" : (std::abs(v.speed) < 0.05 ? "N" : "D");
            std::snprintf(buf, sizeof(buf), "%s  %s", v.cfg.name.c_str(), gear);
            cv.text(4, top + 3, buf, kOsd);
            std::snprintf(buf, sizeof(buf), "TRACT %+d%%", static_cast<int>(v.throttle * 100));
            cv.text(4, top + 13, buf, kOsd);
            std::snprintf(buf, sizeof(buf), "%.1fV %d%%", v.voltage(), static_cast<int>(v.battery * 100));
            cv.textr(W - 4, top + 3, buf, v.battery < 0.2 ? kBad : kOsd);
            std::snprintf(buf, sizeof(buf), "STRUCT %d%%", static_cast<int>(v.health() * 100));
            cv.textr(W - 4, top + 13, buf, v.health() < v.cfg.smoke_below ? kBad : kOsd);
            heading_txt(v.yaw);
            // viteza mare la mijloc-jos + indicatorul de viraj
            std::snprintf(buf, sizeof(buf), "%.0f KM/H", std::abs(v.speed) * 3.6);
            cv.textc(W / 2, top + fh - 22, buf, kOsd);
            {
                int bx = W / 2 - 30, byy = top + fh - 11, bw = 60;
                cv.frame(bx, byy, bw, 5, {0.6f, 0.6f, 0.6f});
                double f = std::clamp(v.steer / std::max(0.05, v.cfg.steer_max), -1.0, 1.0);
                int sx = bx + bw / 2 - static_cast<int>(f * (bw / 2 - 2));   // viraj stanga -> indicatorul la stanga
                cv.rect(sx - 1, byy + 1, 3, 3, kAccent);
            }
            std::snprintf(buf, sizeof(buf), "TANGAJ %+d", static_cast<int>(std::lround(v.pitch * 180.0 / 3.14159265358979)));
            cv.textr(W - 4, top + fh / 2 - 10, buf, kOsd);
            std::snprintf(buf, sizeof(buf), "RULIU %+d", static_cast<int>(std::lround(v.roll * 180.0 / 3.14159265358979)));
            cv.textr(W - 4, top + fh / 2, buf, kOsd);
            std::snprintf(buf, sizeof(buf), "ODO %.0fM", v.odometer);
            cv.text(4, top + fh / 2 - 10, buf, kOsd);
            std::snprintf(buf, sizeof(buf), "DIST %.0fM", std::hypot(v.x - me->x, v.y - me->y));
            cv.text(4, top + fh / 2, buf, kOsd);
            if (v.charge) cv.textc(W / 2, top + 13, "INCARCATURA ARMATA", kBad);
            if (v.battery < 0.2 && std::fmod(time, 0.8) < 0.5) cv.textc(W / 2, top + fh / 2 + 22, "BATERIE SCAZUTA", kBad);
            if (!v.alive) cv.textc(W / 2, top + fh / 2 + 10, "VEHICUL DISTRUS", kBad);
        }
        std::snprintf(buf, sizeof(buf), "LQ %d%%", static_cast<int>((1.0 - info.signal_loss) * 100));
        cv.text(W - 4 - font::text_width(buf, 1), top + fh - 10, buf, info.signal_loss > 0.4 ? kWarn : Col{1, 1, 1});
        cv.textc(W / 2, top + fh / 2 - 3, "+", {1, 1, 1});
        if (std::fmod(time, 1.0) < 0.5) cv.disc(W / 2 - 62, top + 6, 2, kBad);
        cv.text(W / 2 - 56, top + 3, "REC", {1, 1, 1});
        // mini-harta: gadgetul si operatorul
        {
            int mx0 = 4, my0 = top + fh - 62, mw = 64, mh = 48;
            cv.rect(mx0, my0, mw, mh, {0.0f, 0.0f, 0.0f});
            cv.frame(mx0, my0, mw, mh, {0.3f, 0.4f, 0.4f});
            double gx = drone ? w.drones[me->drone_id].x : w.vehicles[me->vehicle_id].x;
            double gy = drone ? w.drones[me->drone_id].y : w.vehicles[me->vehicle_id].y;
            double cell = 3.0;
            for (int yy = 0; yy < mh - 2; ++yy)
                for (int xx = 0; xx < mw - 2; ++xx) {
                    double wx = gx + (xx - (mw - 2) * 0.5) / cell, wy = gy + (yy - (mh - 2) * 0.5) / cell;
                    int cx = static_cast<int>(std::floor(wx)), cy = static_cast<int>(std::floor(wy));
                    if (w.map.is_static_wall(cx, cy)) cv.px(mx0 + 1 + xx, my0 + 1 + yy, {0.45f, 0.5f, 0.5f});
                }
            auto mp = [&](double x, double y, double& sx, double& sy) { sx = mx0 + 1 + (mw - 2) * 0.5 + (x - gx) * cell; sy = my0 + 1 + (mh - 2) * 0.5 + (y - gy) * cell; };
            double sx, sy;
            mp(gx, gy, sx, sy);
            cv.disc(sx, sy, 1.5, kAccent);
            mp(me->x, me->y, sx, sy);
            if (sx > mx0 && sx < mx0 + mw && sy > my0 && sy < my0 + mh) cv.disc(sx, sy, 1.5, kGood);
        }
        int by = H - 16;
        if (button(cv, click, 2, by, 70, 14, "IESIRE")) cmd.gadget_exit = true;
        if (button(cv, click, 76, by, 80, 14, "RECHEAMA")) cmd.gadget_recall = true;
        if (drone) { if (button(cv, click, 160, by, 70, 14, "MOD")) cmd.drone_mode = true; }
        else if (button(cv, click, 160, by, 90, 14, "DETONEAZA", false, kBad)) cmd.gadget_detonate = true;
        if (cmd.gadget_exit || cmd.gadget_recall || cmd.drone_mode || cmd.gadget_detonate) cmd.confirm = true;
    } else {
        // =================================================================
        // Paginile tabletei
        // =================================================================
        // bara de navigare (jos); pe poligon pagina obiectivelor devine controlul poligonului
        const bool range_map = w.range.enabled;
        if (range_map && st.page == TabletPage::Objectives) st.page = TabletPage::Range;
        if (!range_map && st.page == TabletPage::Range) st.page = TabletPage::Objectives;
        const TabletPage nav[8] = {TabletPage::Home, TabletPage::Loadout, TabletPage::Map,
                                   range_map ? TabletPage::Range : TabletPage::Objectives,
                                   TabletPage::Match, TabletPage::Gadgets, TabletPage::Stats, TabletPage::Settings};
        const char* nav_lbl[8] = {"ACASA", "ECHIPAMENT", "HARTA", range_map ? "POLIGON" : "OBIECTIVE",
                                  "MECI", "GADGETURI", "STATISTICI", "SETARI"};
        int bw = W / 4;
        for (int i = 0; i < 8; ++i) {
            bool act = st.page == nav[i] || (st.page == TabletPage::Cameras && nav[i] == TabletPage::Gadgets);
            int bx = (i % 4) * bw + 1, by = bottom + 1 + (i / 4) * 13;
            if (button(cv, click, bx, by, bw - 2, 12, nav_lbl[i], act)) {
                st.page = nav[i];
                click.clicked = false;   // atingerea a fost consumata de bara
            }
        }
        if (click.wheel != 0 && st.page == TabletPage::Map) st.zoom = std::clamp(st.zoom * (click.wheel > 0 ? 1.25 : 0.8), 1.0, 5.0);

        if (!me) {
            cv.textc(W / 2, H / 2, "FARA OPERATOR", kBad);
        } else if (st.page == TabletPage::Home) {
            // --------------------------------------------------- ACASA: dale cu date reale
            const int tw = (W - 8) / 3, th = (bottom - top - 6) / 2;
            int alive = 0, total = 0, downed = 0, red_alive = 0;
            for (const auto& a : w.agents) {
                if (a.faction == me->faction) { total++; alive += a.active(); downed += a.health.downed ? 1 : 0; }
                else red_alive += a.active();
            }
            for (int k = 0; k < 6; ++k) {
                int x = 2 + (k % 3) * (tw + 2), y = top + 2 + (k / 3) * (th + 2);
                cv.rect(x, y, tw, th, kPanel);
                cv.frame(x, y, tw, th, {0.12f, 0.18f, 0.2f});
                int ty = y + 3;
                auto ln = [&](const char* t, Col c) { cv.text(x + 4, ty, t, c); ty += 9; };
                switch (k) {
                    case 0: {
                        ln("OBIECTIV", kAccent);
                        if (w.game.has_objective) {
                            std::snprintf(buf, sizeof(buf), "ZONA %d%%", static_cast<int>(w.game.capture_progress * 100));
                            ln(buf, w.game.objective_reached ? kGood : kWarn);
                            cv.bar(x + 4, ty, tw - 8, 4, w.game.capture_progress, kWarn); ty += 7;
                        }
                        std::snprintf(buf, sizeof(buf), "SUSPECTI: %d", red_alive);
                        ln(buf, red_alive ? kTxt : kGood);
                        int left = static_cast<int>(std::max(0.0, w.game.round_time_limit - w.game.round_time));
                        std::snprintf(buf, sizeof(buf), "TIMP %d:%02d", left / 60, left % 60);
                        ln(buf, left < 60 ? kBad : kTxt);
                        break;
                    }
                    case 1: {
                        ln("ECHIPA", kAccent);
                        std::snprintf(buf, sizeof(buf), "ACTIVI %d/%d", alive, total);
                        ln(buf, kTxt);
                        if (downed) { std::snprintf(buf, sizeof(buf), "CAZUTI %d", downed); ln(buf, kBad); }
                        int n = 0;
                        for (const auto& a : w.agents) {
                            if (a.faction != me->faction || n >= 3) continue;
                            std::snprintf(buf, sizeof(buf), "OP-%d %s", a.id + 1, status_of(a));
                            ln(buf, status_col(a));
                            n++;
                        }
                        break;
                    }
                    case 2: {
                        ln("MECI", kAccent);
                        std::snprintf(buf, sizeof(buf), "RUNDA %d", w.game.round);
                        ln(buf, kTxt);
                        std::snprintf(buf, sizeof(buf), "BLUE %d : %d RED", w.game.score_blue, w.game.score_red);
                        ln(buf, kTxt);
                        std::snprintf(buf, sizeof(buf), "K %d  D %d", me->kills, me->deaths);
                        ln(buf, kDim);
                        break;
                    }
                    case 3: {
                        ln("GADGETURI", kAccent);
                        if (me->drone_id >= 0 && me->drone_id < static_cast<int>(w.drones.size())) {
                            const Drone& d = w.drones[me->drone_id];
                            std::snprintf(buf, sizeof(buf), "DRONA %s %d%%", !d.alive ? "DISTRUSA" : d.stowed ? "RUCSAC" : "ACTIVA",
                                          static_cast<int>(d.battery * 100));
                            ln(buf, d.alive ? kTxt : kBad);
                        } else ln(me->inv.drone ? "DRONA IN RUCSAC" : "FARA DRONA", kDim);
                        if (me->vehicle_id >= 0 && me->vehicle_id < static_cast<int>(w.vehicles.size())) {
                            const Vehicle& v = w.vehicles[me->vehicle_id];
                            std::snprintf(buf, sizeof(buf), "RC %s %d%%", !v.alive ? "DISTRUS" : v.stowed ? "RUCSAC" : "ACTIV",
                                          static_cast<int>(v.battery * 100));
                            ln(buf, v.alive ? kTxt : kBad);
                        } else ln(me->inv.rc ? "RC IN RUCSAC" : "FARA RC", kDim);
                        break;
                    }
                    case 4: {
                        ln("STARE", kAccent);
                        std::snprintf(buf, sizeof(buf), "SANATATE %d%%", static_cast<int>(me->hp));
                        ln(buf, me->hp < 50 ? kBad : kTxt);
                        std::snprintf(buf, sizeof(buf), "PULS %.0f %s", me->phys.heart_rate, stress_level_name(me->phys.level));
                        ln(buf, me->phys.stress > 0.5 ? kWarn : kTxt);
                        const WeaponSlot* sl = me->cur_slot();
                        if (sl) {
                            int spare = static_cast<int>(sl->spares.size());
                            std::snprintf(buf, sizeof(buf), "INCARCATOARE %d", spare);
                            ln(buf, spare == 0 ? kBad : kTxt);
                        }
                        if (me->health.bleeding > 0.05) ln("SANGERARE!", kBad);
                        break;
                    }
                    default: {
                        ln("NOTIFICARI", kAccent);
                        if (info.notifications) {
                            int n = 0;
                            for (auto it = info.notifications->rbegin(); it != info.notifications->rend() && n < 6; ++it, ++n)
                                ln(clip(it->text, 15).c_str(), it->kind == 2 ? kBad : it->kind == 1 ? kWarn : it->kind == 3 ? kGood : kTxt);
                        }
                        break;
                    }
                }
                if (hit(click, W, H, x, y, tw, th)) {
                    const TabletPage dest[6] = {TabletPage::Objectives, TabletPage::Match, TabletPage::Match, TabletPage::Gadgets,
                                                TabletPage::Loadout, TabletPage::Home};
                    st.page = dest[k];
                    cmd.confirm = true;
                }
            }
        } else if (st.page == TabletPage::Loadout) {
            // --------------------------------------------------- ECHIPAMENT (inventarul real)
            if (!st.draft_init) {
                st.draft = me->pending_loadout.valid ? me->pending_loadout : w.current_loadout(*me);
                st.draft_init = true;
            }
            Loadout& L = st.draft;
            const char* slots[5] = {"PRINCIPALA", "SECUNDARA", "CORP LA CORP", "ARUNCABILE", "GADGETURI"};
            for (int i = 0; i < 5; ++i)
                if (button(cv, click, 2, top + 2 + i * 18, 92, 16, slots[i], st.loadout_sel == i)) st.loadout_sel = i;
            int x = 98, y = top + 3;
            auto row_text = [&](const char* t, Col c) { cv.text(x, y, t, c); y += 10; };
            if (st.loadout_sel <= 1) {
                bool prim = st.loadout_sel == 0;
                std::string& wid = prim ? L.primary : L.secondary;
                std::vector<std::string>& atts = prim ? L.primary_att : L.secondary_att;
                // armele disponibile de categoria potrivita
                std::vector<int> opts;
                for (size_t i = 0; i < w.weapon_raw.size(); ++i)
                    if ((w.weapon_raw[i].model == WeaponModel::Pistol) != prim) opts.push_back(static_cast<int>(i));
                int cur = 0;
                for (size_t i = 0; i < opts.size(); ++i) if (w.weapon_raw[opts[i]].id == wid) cur = static_cast<int>(i);
                if (!opts.empty()) {
                    if (button(cv, click, x, y, 16, 13, "<")) { cur = (cur + static_cast<int>(opts.size()) - 1) % static_cast<int>(opts.size()); wid = w.weapon_raw[opts[cur]].id; atts.clear(); }
                    if (button(cv, click, W - 20, y, 16, 13, ">")) { cur = (cur + 1) % static_cast<int>(opts.size()); wid = w.weapon_raw[opts[cur]].id; atts.clear(); }
                    const WeaponData& raw = w.weapon_raw[opts[cur]];
                    std::snprintf(buf, sizeof(buf), "%s  %s", raw.name.c_str(), raw.caliber.c_str());
                    cv.textc((x + W - 4) / 2, y + 3, buf, kTxt);
                    y += 15;
                    // modurile de foc ale armei (selectorul N in joc)
                    {
                        std::string modes;
                        for (FireMode fm : raw.fire_modes) { if (!modes.empty()) modes += " / "; modes += fire_mode_name(fm); }
                        std::snprintf(buf, sizeof(buf), "FOC: %s", modes.c_str());
                        cv.text(x, y, buf, kAccent);
                        y += 11;
                    }
                    // atasamente pe tipuri (un singur atasament pe tip, doar cele compatibile)
                    for (int t = 0; t < static_cast<int>(AttachmentType::Count); ++t) {
                        std::vector<const WeaponAttachment*> ch;
                        for (const auto& a : w.attachment_db)
                            if (static_cast<int>(a.type) == t && a.fits_model(raw.model_name())) ch.push_back(&a);
                        if (ch.empty()) continue;
                        int sel = -1;
                        for (size_t i = 0; i < ch.size(); ++i)
                            if (std::find(atts.begin(), atts.end(), ch[i]->name) != atts.end()) sel = static_cast<int>(i);
                        std::snprintf(buf, sizeof(buf), "%-10s %s", attachment_type_name(static_cast<AttachmentType>(t)),
                                      sel >= 0 ? ch[sel]->label.c_str() : "-");
                        cv.text(x, y + 1, buf, sel >= 0 ? kTxt : kDim);
                        if (button(cv, click, W - 20, y, 16, 10, ">")) {
                            if (sel >= 0) atts.erase(std::remove(atts.begin(), atts.end(), ch[sel]->name), atts.end());
                            int nsel = sel + 1;
                            if (nsel < static_cast<int>(ch.size())) atts.push_back(ch[nsel]->name);
                        }
                        y += 11;
                    }
                    // previzualizarea statisticilor (aceleasi formule ca in joc)
                    WeaponData pv = raw;
                    pv.attachments = atts;
                    pv.apply_attachments(&w.attachment_db);
                    // statisticile in coloana din stanga, sub categorii (8 tipuri de atasamente in dreapta)
                    int sy = top + 2 + 5 * 18 + 2;
                    auto stat = [&](const char* name, double v, double ref, bool low_good) {
                        double f = std::clamp(v / ref, 0.0, 1.5) / 1.5;
                        cv.text(2, sy, name, kDim);
                        cv.bar(52, sy + 1, 42, 5, f, low_good ? (f < 0.45 ? kGood : f < 0.7 ? kWarn : kBad) : kAccent);
                        sy += 9;
                    };
                    stat("RECUL", pv.recoil_pitch, raw.recoil_pitch * 1.2, true);
                    stat("OCHIRE", pv.ads_time, 0.4, true);
                    stat("GREUTATE", pv.weight, 5.0, true);
                    stat("ZGOMOT", pv.sound_radius, 40.0, true);
                    std::snprintf(buf, sizeof(buf), "INC. %d  %.2fM", pv.mag_size, pv.length);
                    cv.text(2, sy, buf, kTxt);
                }
            } else if (st.loadout_sel == 2) {
                row_text("ARMA ALBA", kAccent);
                if (button(cv, click, x, y, 100, 14, "PAT DE ARMA", L.melee == MeleeKind::Buttstock)) L.melee = MeleeKind::Buttstock;
                if (button(cv, click, x + 106, y, 100, 14, "CUTIT", L.melee == MeleeKind::Knife)) L.melee = MeleeKind::Knife;
                y += 20;
                row_text(L.melee == MeleeKind::Knife ? "DAUNA MARE, RAPID (BUTONUL MIJLOC)" : "IMPINGE SI DEZECHILIBREAZA", kDim);
            } else if (st.loadout_sel == 3) {
                row_text("ARUNCABILE (MAXIM 3)", kAccent);
                int* cnt[3] = {&L.frags, &L.flashbangs, &L.smokes};
                const char* nm[3] = {"FRAGMENTARE", "ASURZITOARE", "FUMIGENA"};
                for (int i = 0; i < 3; ++i) {
                    std::snprintf(buf, sizeof(buf), "%-12s %d", nm[i], *cnt[i]);
                    cv.text(x, y + 3, buf, kTxt);
                    if (button(cv, click, W - 44, y, 18, 13, "-") && *cnt[i] > 0) (*cnt[i])--;
                    if (button(cv, click, W - 22, y, 18, 13, "+")) {
                        if (L.throwables() < kMaxThrowables) (*cnt[i])++;
                        else cmd.error = true;
                    }
                    y += 16;
                }
                row_text("G TINUT: CUIUL SCOS, ELIBERAT: ARUNCA", kDim);
                row_text("5: SCHIMBA TIPUL", kDim);
            } else {
                row_text("GADGETURI", kAccent);
                if (button(cv, click, x, y, 100, 14, "DRONA FPV", L.drone)) L.drone = !L.drone;
                y += 18;
                if (button(cv, click, x, y, 100, 14, "MASINA RC", L.rc)) { L.rc = !L.rc; if (!L.rc) L.rc_charge = false; }
                if (button(cv, click, x + 106, y, 110, 14, "INCARCATURA", L.rc_charge, kBad)) { if (L.rc) L.rc_charge = !L.rc_charge; else cmd.error = true; }
                y += 20;
                row_text("CONTROL DOAR PRIN TABLETA (V / B)", kDim);
            }
            // aplicarea: in inventarul real (acum la spawn sau la runda urmatoare)
            int ay = bottom - 18;
            cv.text(2, ay - 10, info.loadout_now ? "SE APLICA ACUM (ZONA DE START)" : "SE APLICA LA RUNDA URMATOARE",
                    info.loadout_now ? kGood : kWarn);
            if (me->pending_loadout.valid && !info.loadout_now) cv.text(W - 96, ay - 10, "IN ASTEPTARE", kWarn);
            if (button(cv, click, 2, ay, 92, 15, "APLICA", false, kGood)) {
                cmd.loadout_apply = true;
                cmd.loadout = L;
                cmd.loadout.valid = true;
                cmd.confirm = true;
            }
            if (button(cv, click, 98, ay, 92, 15, "RESETEAZA")) { st.draft = w.current_loadout(*me); }
        } else if (st.page == TabletPage::Map) {
            // --------------------------------------------------- HARTA
            const Map& m = w.map;
            int area_h = bottom - top - 18;
            double base = std::min(static_cast<double>(W - 4) / m.width, static_cast<double>(area_h - 2) / m.height);
            double cell = base * st.zoom;
            double ccx = W * 0.5, ccy = top + area_h * 0.5;
            if (st.follow) { st.pan_x = st.zoom > 1.01 ? me->x : m.width * 0.5; st.pan_y = st.zoom > 1.01 ? me->y : m.height * 0.5; }
            double fx = st.pan_x, fy = st.pan_y;
            double rot = st.heading_up ? -me->facing - kPi * 0.5 : 0.0;
            double cr = std::cos(rot), sr = std::sin(rot);
            auto to_s = [&](double x, double y, double& sx, double& sy) {
                double dx = (x - fx) * cell, dy = (y - fy) * cell;
                sx = ccx + dx * cr - dy * sr;
                sy = ccy + dx * sr + dy * cr;
            };
            auto from_s = [&](double sx, double sy, double& x, double& y) {
                double dx = sx - ccx, dy = sy - ccy;
                double ux = dx * cr + dy * sr, uy = -dx * sr + dy * cr;
                x = fx + ux / cell; y = fy + uy / cell;
            };
            for (int y = top; y < top + area_h; ++y)
                for (int x = 0; x < W; ++x) {
                    double wx, wy;
                    from_s(x + 0.5, y + 0.5, wx, wy);
                    int cx = static_cast<int>(std::floor(wx)), cy = static_cast<int>(std::floor(wy));
                    if (cx < 0 || cy < 0 || cx >= m.width || cy >= m.height) continue;
                    char c = m.cell(cx, cy);
                    Col col;
                    if (m.is_window(cx, cy)) col = {0.3f, 0.6f, 0.75f};
                    else if (m.is_static_wall(cx, cy)) col = {0.5f, 0.56f, 0.58f};
                    else if (m.is_low(cx, cy)) col = {0.32f, 0.36f, 0.36f};
                    else if (c == 'D') {
                        bool open = m.door_state(cx, cy) > 0.5f;
                        col = m.is_locked(cx, cy) ? Col{0.8f, 0.2f, 0.15f} : Col{open ? 0.2f : 0.6f, open ? 0.7f : 0.42f, 0.2f};
                    } else if (m.is_outdoor(cx, cy)) col = {0.06f, 0.09f, 0.07f};
                    else col = {0.11f, 0.12f, 0.15f};
                    cv.px(x, y, col);
                }
            double sx, sy;
            for (const auto& sc : w.smokes) { to_s(sc.x, sc.y, sx, sy); cv.ring(sx, sy, sc.radius * cell * sc.density(), {0.6f, 0.6f, 0.6f}); }
            if (w.game.has_objective) {
                to_s(w.game.obj_x, w.game.obj_y, sx, sy);
                cv.ring(sx, sy, w.params.capture_radius * cell, {1.0f, 0.85f, 0.1f});
                if (std::fmod(time, 1.0) < 0.6) cv.disc(sx, sy, 2, {1.0f, 0.85f, 0.1f});
            }
            for (const auto& it : w.interactables) {
                if (!it.enabled) continue;
                to_s(it.x, it.y, sx, sy);
                switch (it.kind) {
                    case InteractKind::Terminal: cv.frame(static_cast<int>(sx) - 2, static_cast<int>(sy) - 2, 5, 5, it.state ? kGood : kAccent); break;
                    case InteractKind::Breaker: cv.rect(static_cast<int>(sx) - 1, static_cast<int>(sy) - 2, 3, 5, w.map.power ? Col{0.9f, 0.9f, 0.1f} : Col{1.0f, 0.2f, 0.1f}); break;
                    case InteractKind::Cabinet: if (!it.state) cv.rect(static_cast<int>(sx) - 1, static_cast<int>(sy) - 1, 3, 3, {0.5f, 0.6f, 0.5f}); break;
                    case InteractKind::Revive:
                        if (std::fmod(time, 0.6) < 0.35) { cv.line(sx - 3, sy - 3, sx + 3, sy + 3, kBad); cv.line(sx - 3, sy + 3, sx + 3, sy - 3, kBad); }
                        break;
                    default: break;
                }
            }
            for (const auto& mk : w.markers) {
                if (mk.faction != me->faction) continue;
                to_s(mk.x, mk.y, sx, sy);
                if (mk.kind == 1) {
                    double pr = 3.0 + std::fmod(w.current_time - mk.time, 1.0) * 6.0;
                    cv.ring(sx, sy, pr, {0.3f, 1.0f, 0.9f});
                    cv.disc(sx, sy, 1.5, {0.3f, 1.0f, 0.9f});
                } else {
                    Col c = mk.kind == 2 ? kBad : Col{1.0f, 0.9f, 0.2f};
                    cv.line(sx, sy - 3, sx + 3, sy, c); cv.line(sx + 3, sy, sx, sy + 3, c);
                    cv.line(sx, sy + 3, sx - 3, sy, c); cv.line(sx - 3, sy, sx, sy - 3, c);
                    if (mk.kind == 2) cv.textc(static_cast<int>(sx), static_cast<int>(sy) - 11, "!", kBad);
                }
            }
            if (w.intel_until > w.current_time && me->faction == Faction::Blue) {
                for (const auto& a : w.agents) {
                    if (a.faction == me->faction || !a.alive()) continue;
                    to_s(a.x, a.y, sx, sy);
                    if (std::fmod(time, 0.8) < 0.5) cv.disc(sx, sy, 1.5, kBad);
                }
            }
            for (const auto& a : w.agents) {
                if (a.faction != me->faction) continue;
                to_s(a.x, a.y, sx, sy);
                bool self = a.id == local;
                if (!a.alive()) { cv.disc(sx, sy, 1.5, {0.4f, 0.4f, 0.4f}); continue; }
                if (a.health.downed) { cv.disc(sx, sy, 2, kBad); continue; }
                Col c = self ? Col{0.4f, 0.8f, 1.0f} : Col{0.25f, 0.6f, 1.0f};
                cv.disc(sx, sy, self ? 2.5 : 2.0, c);
                cv.line(sx, sy, sx + std::cos(a.facing + rot) * 7, sy + std::sin(a.facing + rot) * 7, c);
            }
            for (const auto& d : w.drones) if (d.alive && !d.stowed && d.owner >= 0 && w.agents[d.owner].faction == me->faction) {
                to_s(d.x, d.y, sx, sy);
                cv.ring(sx, sy, 2, {0.2f, 0.9f, 1.0f});
            }
            for (const auto& v : w.vehicles) if (v.alive && !v.stowed && v.owner >= 0 && w.agents[v.owner].faction == me->faction) {
                to_s(v.x, v.y, sx, sy);
                cv.frame(static_cast<int>(sx) - 2, static_cast<int>(sy) - 2, 5, 5, {0.2f, 0.9f, 1.0f});
            }
            // butoane
            int by = bottom - 16;
            int bx = 2;
            auto mb = [&](int bw2, const char* t, bool act) { bool h2 = button(cv, click, bx, by, bw2, 14, t, act); bx += bw2 + 2; return h2; };
            if (mb(16, "-", false)) st.zoom = std::max(1.0, st.zoom * 0.8);
            if (mb(16, "+", false)) st.zoom = std::min(5.0, st.zoom * 1.25);
            if (mb(40, "ROTIRE", st.heading_up)) st.heading_up = !st.heading_up;
            const char* kinds[3] = {"MARCAJ", "PING", "PERICOL"};
            if (mb(46, kinds[st.marker_kind], true)) st.marker_kind = (st.marker_kind + 1) % 3;
            if (mb(40, "STERGE", false)) cmd.clear_markers = true;
            double step = 3.0 / st.zoom;
            if (mb(14, "<", false)) { st.follow = false; st.pan_x -= step; }
            if (mb(14, "^", false)) { st.follow = false; st.pan_y -= step; }
            if (mb(14, "V", false)) { st.follow = false; st.pan_y += step; }
            if (mb(14, ">", false)) { st.follow = false; st.pan_x += step; }
            if (mb(44, "CENTRU", st.follow)) st.follow = true;
            std::snprintf(buf, sizeof(buf), "X%.1f", st.zoom);
            cv.text(W - 26, by + 3, buf, kDim);
            int cpx = static_cast<int>(click.u * W), cpy = static_cast<int>(click.v * H);
            if (click.clicked && cpy >= top && cpy < top + area_h) {
                double wx, wy;
                from_s(cpx + 0.5, cpy + 0.5, wx, wy);
                cmd.place_marker = true;
                cmd.kind = st.marker_kind;
                cmd.mx = wx; cmd.my = wy;
                cmd.confirm = true;
            }
        } else if (st.page == TabletPage::Objectives) {
            // --------------------------------------------------- OBIECTIVE (din starea jocului)
            int y = top + 3;
            auto ln = [&](const char* t, Col c) { cv.text(4, y, t, c); y += 10; };
            ln(w.game.mode == GameMode::Objective ? "MOD: SECURIZAREA ZONEI" : "MOD: ELIMINARE", kAccent);
            int red_alive = 0, red_total = 0;
            for (const auto& a : w.agents) if (a.faction != me->faction) { red_total++; red_alive += a.active(); }
            if (w.game.has_objective) {
                std::snprintf(buf, sizeof(buf), "[%c] SECURIZEAZA ZONA  %d%%", w.game.objective_reached ? 'X' : ' ',
                              static_cast<int>(w.game.capture_progress * 100));
                ln(buf, w.game.objective_reached ? kGood : kWarn);
                cv.bar(16, y, 200, 4, w.game.capture_progress, kWarn); y += 8;
                std::snprintf(buf, sizeof(buf), "    DISTANTA %.0fM", std::hypot(w.game.obj_x - me->x, w.game.obj_y - me->y));
                ln(buf, kDim);
            }
            std::snprintf(buf, sizeof(buf), "[%c] NEUTRALIZEAZA SUSPECTII  %d/%d", red_alive == 0 ? 'X' : ' ', red_total - red_alive, red_total);
            ln(buf, red_alive == 0 ? kGood : kWarn);
            for (const auto& it : w.interactables) {
                if (it.kind != InteractKind::Terminal) continue;
                std::snprintf(buf, sizeof(buf), "[%c] OPTIONAL: DATELE DIN TERMINAL  %d%%", it.state ? 'X' : ' ', static_cast<int>(it.progress * 100));
                ln(buf, it.state ? kGood : kTxt);
            }
            if (w.intel_until > w.current_time) {
                std::snprintf(buf, sizeof(buf), "    INFORMATII ACTIVE: %.0fS", w.intel_until - w.current_time);
                ln(buf, kGood);
            }
            y += 3;
            int left = static_cast<int>(std::max(0.0, w.game.round_time_limit - w.game.round_time));
            std::snprintf(buf, sizeof(buf), "TIMP RAMAS %d:%02d", left / 60, left % 60);
            ln(buf, left < 60 ? kBad : kTxt);
            cv.bar(4, y, W - 8, 4, w.game.round_time_limit > 0 ? 1.0 - w.game.round_time / w.game.round_time_limit : 1.0, kAccent); y += 8;
            std::snprintf(buf, sizeof(buf), "CURENT: %s", w.map.power ? "PORNIT" : "OPRIT - AVARIE");
            ln(buf, w.map.power ? kTxt : kBad);
            int locked = 0, open = 0, total = 0;
            for (int yy = 0; yy < w.map.height; ++yy)
                for (int xx = 0; xx < w.map.width; ++xx)
                    if (w.map.is_door(xx, yy)) { total++; locked += w.map.is_locked(xx, yy); open += w.map.door_state(xx, yy) > 0.5f; }
            std::snprintf(buf, sizeof(buf), "USI: %d DESCHISE  %d INCUIATE  (%d)", open, locked, total);
            ln(buf, kTxt);
            int alive = 0, total_b = 0;
            for (const auto& a : w.agents) if (a.faction == me->faction) { total_b++; alive += a.active(); }
            std::snprintf(buf, sizeof(buf), "ECHIPA: %d/%d OPERATIONALI   RUNDA %d", alive, total_b, w.game.round);
            ln(buf, kTxt);
        } else if (st.page == TabletPage::Match) {
            // --------------------------------------------------- MECI (tabela de scor)
            int left = static_cast<int>(std::max(0.0, w.game.round_time_limit - w.game.round_time));
            std::snprintf(buf, sizeof(buf), "RUNDA %d   BLUE %d : %d RED   %d:%02d", w.game.round, w.game.score_blue, w.game.score_red, left / 60, left % 60);
            cv.textc(W / 2, top + 3, buf, kTxt);
            if (w.game.phase == MatchPhase::RoundEnd) {
                cv.textc(W / 2, top + 13, w.game.winner == 0 ? "RUNDA CASTIGATA" : w.game.winner == 1 ? "RUNDA PIERDUTA" : "EGALITATE",
                         w.game.winner == 0 ? kGood : kBad);
            }
            int y = top + 24;
            cv.text(4, y, "OPERATOR", kDim); cv.text(132, y, "STARE", kDim); cv.text(206, y, "UCIDERI", kDim); cv.text(254, y, "MORTI", kDim); cv.text(290, y, "CAZUT", kDim);
            y += 10;
            for (int pass = 0; pass < 2; ++pass) {
                bool mine = pass == 0;
                int idx = 1;
                for (const auto& a : w.agents) {
                    if ((a.faction == me->faction) != mine) continue;
                    if (y > bottom - 10) break;
                    if (mine) std::snprintf(buf, sizeof(buf), "OP-%d%s", a.id + 1, a.id == local ? " (TU)" : (a.is_human ? "" : " AI"));
                    else std::snprintf(buf, sizeof(buf), "SUSPECT-%d", idx);
                    idx++;
                    cv.text(4, y, buf, mine ? kAccent : Col{1.0f, 0.55f, 0.5f});
                    const char* stt;
                    Col sc;
                    if (mine) { stt = status_of(a); sc = status_col(a); }
                    else if (!a.alive()) { stt = "NEUTRALIZAT"; sc = kGood; }
                    else if (w.intel_until > w.current_time) { stt = "LOCALIZAT"; sc = kWarn; }
                    else { stt = "NECUNOSCUT"; sc = kDim; }
                    cv.text(132, y, stt, sc);
                    std::snprintf(buf, sizeof(buf), "%d", a.kills); cv.text(224, y, buf, kTxt);
                    std::snprintf(buf, sizeof(buf), "%d", a.deaths); cv.text(266, y, buf, kTxt);
                    std::snprintf(buf, sizeof(buf), "%d", a.downs); cv.text(302, y, buf, kTxt);
                    y += 9;
                }
                y += 4;
            }
            if (info.catalog && !info.catalog->maps.empty()) {
                const MapCatalog& cat = *info.catalog;
                if (st.next_map <= 0 || !cat.by_id(st.next_map)) st.next_map = info.map_id > 0 ? info.map_id : cat.maps[0].id;
                int by = bottom - 30;
                cv.rect(0, by - 3, W, 1, {0.1f, 0.15f, 0.17f});
                if (const MapInfo* cur = cat.by_id(info.map_id)) {
                    std::snprintf(buf, sizeof(buf), "HARTA %s: %dX%d M, %d M2, %d OBIECTIVE", cur->name.c_str(),
                                  cur->width, cur->height, cur->walkable, cur->objective_count);
                    cv.text(4, by, buf, kDim);
                }
                by += 11;
                int idx = std::max(0, cat.index_of(st.next_map));
                cv.text(4, by + 3, "MECI NOU PE:", kTxt);
                if (button(cv, click, 80, by, 16, 13, "<")) { idx = (idx + static_cast<int>(cat.maps.size()) - 1) % static_cast<int>(cat.maps.size()); st.next_map = cat.maps[idx].id; }
                cv.textc(150, by + 3, cat.maps[idx].name.c_str(), kAccent);
                if (button(cv, click, 204, by, 16, 13, ">")) { idx = (idx + 1) % static_cast<int>(cat.maps.size()); st.next_map = cat.maps[idx].id; }
                if (button(cv, click, 226, by, W - 230, 13, "INCEPE", false, kGood)) { cmd.new_match_map = st.next_map; cmd.confirm = true; }
            }
        } else if (st.page == TabletPage::Gadgets) {
            // --------------------------------------------------- GADGETURI
            for (int k = 0; k < 2; ++k) {
                bool drone = k == 0;
                int x = 2 + k * (W / 2), cw = W / 2 - 4, y = top + 2, ch = bottom - top - 24;
                cv.rect(x, y, cw, ch, kPanel);
                cv.frame(x, y, cw, ch, {0.12f, 0.18f, 0.2f});
                int ty = y + 4;
                auto ln = [&](const char* t, Col c) { cv.text(x + 4, ty, t, c); ty += 10; };
                ln(drone ? "DRONA FPV" : "MASINA RC", kAccent);
                bool has = drone ? me->inv.drone : me->inv.rc;
                int id = drone ? me->drone_id : me->vehicle_id;
                bool exists = id >= 0 && id < static_cast<int>(drone ? w.drones.size() : w.vehicles.size());
                bool alive = exists && (drone ? w.drones[id].alive : w.vehicles[id].alive);
                bool stowed = !exists || (drone ? w.drones[id].stowed : w.vehicles[id].stowed);
                bool recall = exists && (drone ? w.drones[id].recall : w.vehicles[id].recall);
                double batt = exists ? (drone ? w.drones[id].battery : w.vehicles[id].battery) : 1.0;
                double hp = exists ? (drone ? w.drones[id].health() : w.vehicles[id].health()) : 1.0;
                const char* stt = !has && !exists ? "INDISPONIBIL" : (exists && !alive) ? "DISTRUSA" : stowed ? "IN RUCSAC" : recall ? "SE INTOARCE" : "ACTIVA";
                ln(stt, (exists && !alive) || (!has && !exists) ? kBad : recall ? kWarn : kGood);
                cv.text(x + 4, ty, "BATERIE", kDim); cv.bar(x + 56, ty + 1, cw - 64, 5, batt, batt < 0.2 ? kBad : kGood); ty += 10;
                cv.text(x + 4, ty, "STRUCTURA", kDim); cv.bar(x + 56, ty + 1, cw - 64, 5, hp, hp < 0.4 ? kBad : kAccent); ty += 10;
                if (exists && alive && !stowed) {
                    double gx = drone ? w.drones[id].x : w.vehicles[id].x, gy = drone ? w.drones[id].y : w.vehicles[id].y;
                    std::snprintf(buf, sizeof(buf), "DISTANTA %.0fM", std::hypot(gx - me->x, gy - me->y));
                    ln(buf, kTxt);
                }
                if (drone && exists) ln(w.drones[id].acro ? "MOD: ACRO" : "MOD: ANGLE (STABILIZAT)", kTxt);
                if (!drone) ln(me->inv.rc_charge || (exists && w.vehicles[id].charge) ? "INCARCATURA: ARMATA" : "INCARCATURA: NU", kTxt);
                int by = y + ch - 50;
                bool usable = (has || exists) && (!exists || alive);
                if (button(cv, click, x + 4, by, cw - 8, 14, stowed ? "LANSEAZA" : "PREIA CONTROLUL", false, usable ? kGood : kBad)) {
                    if (usable) { cmd.gadget = drone ? 1 : 2; cmd.confirm = true; } else cmd.error = true;
                }
                if (button(cv, click, x + 4, by + 16, (cw - 12) / 2, 14, "RECHEAMA")) {
                    if (exists && alive && !stowed) { cmd.gadget_recall = true; cmd.confirm = true; } else cmd.error = true;
                }
                if (drone) {
                    if (button(cv, click, x + 8 + (cw - 12) / 2, by + 16, (cw - 12) / 2, 14, "MOD")) { cmd.drone_mode = true; cmd.confirm = true; }
                } else if (button(cv, click, x + 8 + (cw - 12) / 2, by + 16, (cw - 12) / 2, 14, "DETONEAZA", false, kBad)) {
                    if (exists && alive && !stowed && w.vehicles[id].charge) { cmd.gadget_detonate = true; cmd.confirm = true; } else cmd.error = true;
                }
                if (k == 0 && button(cv, click, x + 4, by + 32, cw - 8, 14, "CAMERE ECHIPA")) st.page = TabletPage::Cameras;
            }
            cv.text(4, bottom - 18, "V / B: CONTROL DIRECT  R: RECHEAMA", kDim);
        } else if (st.page == TabletPage::Cameras) {
            // --------------------------------------------------- CAMERE (bodycam-urile coechipierilor)
            int fh = bottom - top - 20;
            if (info.feed && info.feed->w > 0) {
                for (int y = 0; y < fh; ++y)
                    for (int x = 0; x < W - 8; ++x) {
                        int sx = x * info.feed->w / (W - 8), sy = y * info.feed->h / fh;
                        const float* p = info.feed->px(sx, sy);
                        float k = static_cast<float>(info.feed_exposure);
                        float r = p[0] * k / (1.0f + p[0] * k), g = p[1] * k / (1.0f + p[1] * k), b = p[2] * k / (1.0f + p[2] * k);
                        float n = static_cast<float>(((x * 37 + y * 91 + static_cast<int>(time * 60) * 13) % 17) / 17.0) * 0.06f;
                        cv.px(4 + x, top + 2 + y, {r + n, g + n, b + n});
                    }
                if (std::fmod(time, 1.0) < 0.5) cv.disc(10, top + 8, 2, {1.0f, 0.1f, 0.1f});
                cv.text(16, top + 5, info.feed_label.c_str(), {1, 1, 1});
            } else {
                for (int y = 0; y < fh; ++y)
                    for (int x = 0; x < W - 8; ++x) {
                        float n = static_cast<float>(((x * 1103515245u + y * 12345u + static_cast<unsigned>(time * 60) * 2654435761u) >> 16) & 255) / 255.0f * 0.4f;
                        cv.px(4 + x, top + 2 + y, {n, n, n});
                    }
                cv.textc(W / 2, top + fh / 2, "FARA SEMNAL", kBad);
            }
            if (button(cv, click, 2, bottom - 16, 70, 14, "INAPOI")) st.page = TabletPage::Gadgets;
            if (button(cv, click, 76, bottom - 16, 70, 14, "URMATOR")) { st.feed++; cmd.confirm = true; }
            std::snprintf(buf, sizeof(buf), "%d SURSE", info.camera_count);
            cv.text(152, bottom - 13, buf, kDim);
        } else if (st.page == TabletPage::Stats) {
            tablet_stats_page(scr, st, w, local, click, time, info, cmd, top, bottom);
        } else if (st.page == TabletPage::Range) {
            tablet_range_page(scr, st, w, local, click, time, info, cmd, top, bottom);
        } else if (st.page == TabletPage::Settings && info.settings) {
            // --------------------------------------------------- SETARI
            UserSettings& S = *info.settings;
            int y = top + 3;
            bool changed = false;
            auto num = [&](const char* name, double& v, double step, double lo, double hi, const char* fmt) {
                cv.text(4, y + 3, name, kTxt);
                std::snprintf(buf, sizeof(buf), fmt, v);
                cv.text(W - 96, y + 3, buf, kAccent);
                if (button(cv, click, W - 42, y, 18, 12, "-")) { v = std::max(lo, v - step); changed = true; }
                if (button(cv, click, W - 22, y, 18, 12, "+")) { v = std::min(hi, v + step); changed = true; }
                y += 14;
            };
            auto tog = [&](const char* name, bool& v, const char* on, const char* off) {
                cv.text(4, y + 3, name, kTxt);
                if (button(cv, click, W - 70, y, 66, 12, v ? on : off, v)) { v = !v; changed = true; }
                y += 14;
            };
            num("SENSIBILITATE MOUSE", S.mouse_sens, 0.1, 0.2, 3.0, "%.1f");
            num("SENSIBILITATE DRONA", S.drone_sens, 0.1, 0.3, 2.5, "%.1f");
            num("LUMINOZITATE TABLETA", S.tablet_brightness, 0.1, 0.4, 1.6, "%.1f");
            num("CAMP VIZUAL", S.fov, 2.0, 80.0, 110.0, "%.0f");
            num("CAMP VIZUAL ARMA", S.weapon_fov, 2.0, 56.0, 100.0, "%.0f");
            num("VOLUM", S.volume, 0.1, 0.0, 1.0, "%.1f");
            tog("DISTORSIUNE LENTILA", S.lens, "DA", "NU");
            tog("ZGOMOT SENZOR", S.noise, "DA", "NU");
            tog("MOTION BLUR", S.motion_blur, "DA", "NU");
            tog("COMPRESIE VIDEO", S.compression, "DA", "NU");
            tog("ASISTENTA ARUNCARE", S.throw_assist, "DA", "NU");
            tog("MOD DRONA LA LANSARE", S.drone_acro_default, "ACRO", "ANGLE");
            tog("INVERSARE Y", S.invert_y, "DA", "NU");
            if (changed) { cmd.settings_changed = true; cmd.confirm = true; }
        }
    }

    // --- notificare recenta: banda sub bara de stare ---
    if (info.notifications && !info.notifications->empty()) {
        const TabletNotification& n = info.notifications->back();
        double age = time - n.time;
        if (age >= 0.0 && age < 4.0) {
            float a = static_cast<float>(std::min(1.0, std::min(age * 6.0, (4.0 - age) * 2.0)));
            Col c = n.kind == 2 ? kBad : n.kind == 1 ? kWarn : n.kind == 3 ? kGood : kAccent;
            // banda opaca (textul de dedesubt nu are voie sa se amestece cu notificarea)
            // pe bara de stare: nu acopera continutul paginii (sectiunile de sus raman atingibile)
            float bg = a > 0.5f ? 0.97f : a * 1.94f;
            for (int y = 0; y < 11; ++y) for (int x = 0; x < W; ++x) cv.blend(x, y, {0.03f, 0.04f, 0.055f}, bg);
            for (int x = 0; x < W; ++x) cv.blend(x, 10, c, a);
            Col tc{c.r * a, c.g * a, c.b * a};
            cv.textc(W / 2, 2, clip(n.text, 52).c_str(), tc);
        }
    }

    // --- cursorul (unde va atinge degetul) ---
    int cx = static_cast<int>(scr.cursor_u * W), cy = static_cast<int>(scr.cursor_v * H);
    for (int k = -4; k <= 4; ++k) {
        cv.blend(cx + k, cy, {1, 1, 1}, 0.7f);
        cv.blend(cx, cy + k, {1, 1, 1}, 0.7f);
    }
    if (time - st.last_tap_time < 0.25) cv.ring(cx, cy, 2.0 + (time - st.last_tap_time) * 30.0, {1, 1, 1});
    return cmd;
}

}
