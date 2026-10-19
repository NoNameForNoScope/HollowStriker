// Paginile STATISTICI si POLIGON ale tabletei. Interfata NU colecteaza si NU calculeaza date
// proprii: citeste inregistrarile sistemului de statistici si rezultatele functiilor de analiza
// (analytics.hpp). Toate graficele sunt desenate din date colectate in simulare.
#include "tablet_ui.hpp"
#include "tablet_canvas.hpp"
#include "../stats/analytics.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

namespace bodycam {

using namespace tui;

namespace {

// ----------------------------------------------------------------------------- grafice
struct Box {
    int x, y, w, h;
    double x0, x1, y0, y1;
    double sx(double v) const { return x + (v - x0) / std::max(1e-9, x1 - x0) * (w - 1); }
    double sy(double v) const { return y + h - 1 - (v - y0) / std::max(1e-9, y1 - y0) * (h - 1); }
};

std::string fmtd(const char* f, double v) {
    char b[32];
    std::snprintf(b, sizeof(b), f, v);
    return b;
}
std::string mmss(double t) {
    int s = static_cast<int>(std::max(0.0, t));
    char b[16];
    std::snprintf(b, sizeof(b), "%d:%02d", s / 60, s % 60);
    return b;
}

// Interval "rotund" pentru axa Y: pas 1/2/2.5/5 x 10^k, aproximativ `ticks` diviziuni
void nice_range(double lo, double hi, int ticks, double& y0, double& y1, int& grid) {
    if (hi - lo < 1e-9) hi = lo + 1.0;
    double raw = (hi - lo) / ticks;
    double mag = std::pow(10.0, std::floor(std::log10(raw)));
    double step = mag;
    for (double m : {1.0, 2.0, 2.5, 5.0, 10.0}) { step = m * mag; if (step >= raw) break; }
    y0 = std::floor(lo / step) * step;
    y1 = std::ceil(hi / step) * step;
    grid = std::max(1, static_cast<int>(std::lround((y1 - y0) / step)));
}

void chart_frame(Canvas& cv, const Box& b, const char* yfmt, int grid = 4) {
    cv.rect(b.x, b.y, b.w, b.h, {0.035f, 0.055f, 0.065f});
    for (int i = 0; i <= grid; ++i) {
        double v = b.y0 + (b.y1 - b.y0) * i / grid;
        int yy = static_cast<int>(b.sy(v));
        for (int xx = b.x; xx < b.x + b.w; xx += 2) cv.px(xx, yy, kGrid);
        if (i == 0 || i == grid || grid <= 4) cv.textr(b.x - 2, yy - 3, fmtd(yfmt, v).c_str(), kDim);
    }
    cv.frame(b.x, b.y, b.w, b.h, {0.14f, 0.2f, 0.22f});
}

void time_labels(Canvas& cv, const Box& b) {
    cv.text(b.x, b.y + b.h + 2, mmss(b.x0).c_str(), kDim);
    cv.textr(b.x + b.w, b.y + b.h + 2, mmss(b.x1).c_str(), kDim);
    cv.textc(b.x + b.w / 2, b.y + b.h + 2, mmss(0.5 * (b.x0 + b.x1)).c_str(), kDim);
}

void plot_line(Canvas& cv, const Box& b, const std::vector<double>& xs, const std::vector<double>& ys, Col c) {
    for (size_t i = 1; i < xs.size() && i < ys.size(); ++i) {
        double y0 = std::clamp(ys[i - 1], b.y0, b.y1), y1 = std::clamp(ys[i], b.y0, b.y1);
        cv.line(b.sx(xs[i - 1]), b.sy(y0), b.sx(xs[i]), b.sy(y1), c);
    }
}

// bare verticale cu interval de incredere (lo..hi) si numarul de observatii
void bar_chart(Canvas& cv, const Box& b, const std::vector<std::string>& labels, const std::vector<double>& v,
               const std::vector<double>& lo, const std::vector<double>& hi, const std::vector<int>& n, const char* vfmt,
               Col col, int highlight = -1) {
    chart_frame(cv, b, vfmt, 4);
    int k = static_cast<int>(v.size());
    if (k == 0) return;
    double slot = static_cast<double>(b.w) / k;
    int bw = std::max(3, static_cast<int>(slot * 0.55));
    for (int i = 0; i < k; ++i) {
        int cx = static_cast<int>(b.x + slot * (i + 0.5));
        bool empty = i < static_cast<int>(n.size()) && n[i] == 0;
        if (!empty) {
            double top = std::clamp(v[i], b.y0, b.y1);
            int y1 = static_cast<int>(b.sy(top)), y0 = static_cast<int>(b.sy(b.y0));
            Col c = i == highlight ? kWarn : col;
            cv.rect(cx - bw / 2, y1, bw, std::max(1, y0 - y1), {c.r * 0.55f, c.g * 0.55f, c.b * 0.55f});
            cv.rect(cx - bw / 2, y1, bw, 1, c);
            if (i < static_cast<int>(lo.size()) && i < static_cast<int>(hi.size()) && hi[i] > lo[i]) {
                int a = static_cast<int>(b.sy(std::clamp(hi[i], b.y0, b.y1))), z = static_cast<int>(b.sy(std::clamp(lo[i], b.y0, b.y1)));
                cv.line(cx, a, cx, z, kTxt);
                cv.line(cx - 2, a, cx + 2, a, kTxt);
                cv.line(cx - 2, z, cx + 2, z, kTxt);
            }
            std::string t = fmtd(vfmt, v[i]);
            if (static_cast<int>(t.size()) * 6 <= slot + 4) cv.textc(cx, std::max(b.y + 1, y1 - 9), t.c_str(), kTxt);
        }
        if (i < static_cast<int>(labels.size())) {
            std::string l = clip(labels[i], static_cast<size_t>(std::max(2.0, slot / 6.0)));
            cv.textc(cx, b.y + b.h + 2, l.c_str(), i == highlight ? kWarn : kDim);
        }
        if (i < static_cast<int>(n.size()) && slot >= 30) {
            std::string ns = "N=" + std::to_string(n[i]);
            cv.textc(cx, b.y + b.h + 11, ns.c_str(), {0.25f, 0.33f, 0.3f});
        }
    }
}

// Bare orizontale: eticheta intreaga in stanga, bara, valoarea in dreapta
void hbar_chart(Canvas& cv, int x, int y, int w, int row_h, const std::vector<std::string>& labels, const std::vector<double>& v,
                const std::vector<int>& n, double vmax, const char* vfmt, Col col, int highlight = -1) {
    const int lw = 68, vw = 34;
    const int bw = std::max(10, w - lw - vw);
    for (size_t i = 0; i < v.size(); ++i) {
        int yy = y + static_cast<int>(i) * row_h;
        bool hl = static_cast<int>(i) == highlight;
        cv.text(x, yy, clip(labels[i], 11).c_str(), hl ? kWarn : (n.size() > i && n[i] == 0 ? kDim : kTxt));
        cv.rect(x + lw, yy, bw, 7, {0.06f, 0.09f, 0.1f});
        if (n.size() > i && n[i] == 0) { cv.text(x + lw + bw + 3, yy, "-", kDim); continue; }
        int len = static_cast<int>(std::clamp(v[i] / std::max(1e-9, vmax), 0.0, 1.0) * bw);
        Col c = hl ? kWarn : col;
        cv.rect(x + lw, yy, len, 7, {c.r * 0.6f, c.g * 0.6f, c.b * 0.6f});
        cv.rect(x + lw + std::max(0, len - 1), yy, 1, 7, c);
        cv.text(x + lw + bw + 3, yy, fmtd(vfmt, v[i]).c_str(), kTxt);
    }
}

// Concluzia unei regresii pe datele brute (fara afirmatii nesustinute de date)
std::string verdict(const Regression& r, int min_n, const char* up, const char* down) {
    char b[96];
    if (r.n < min_n || !r.valid) {
        std::snprintf(b, sizeof(b), "DATE INSUFICIENTE (N=%d, MINIM %d)", r.n, min_n);
        return b;
    }
    if (r.p < 0.05) std::snprintf(b, sizeof(b), "SEMNIFICATIV: %s (R=%.2f, P=%.3f, N=%d)", r.slope > 0 ? up : down, r.r, r.p, r.n);
    else std::snprintf(b, sizeof(b), "FARA EFECT SEMNIFICATIV (R=%.2f, P=%.2f, N=%d)", r.r, r.p, r.n);
    return b;
}
Col verdict_col(const Regression& r, int min_n) {
    if (r.n < min_n || !r.valid) return kDim;
    return r.p < 0.05 ? kWarn : kTxt;
}

// ----------------------------------------------------------------------------- cache
// Agregarile se recalculeaza doar cand datele s-au schimbat (versiunea sistemului de statistici)
struct Cache {
    const StatisticsSystem* src = nullptr;
    uint32_t version = 0;
    size_t live_shots = 0, live_reloads = 0, live_eng = 0;
    std::vector<ShotRecord> shots;
    std::vector<ReloadRecord> reloads;
    std::vector<EngagementRecord> eng;
    OverallStats overall;
    std::vector<MapStats> maps;
};
Cache& cache_of(const StatisticsSystem& S) {
    static Cache c;
    if (c.src != &S || c.version != S.version || c.live_shots != S.live_shots().size()
        || c.live_reloads != S.live_reloads().size() || c.live_eng != S.live_engagements().size()) {
        c.src = &S;
        c.version = S.version;
        c.live_shots = S.live_shots().size();
        c.live_reloads = S.live_reloads().size();
        c.live_eng = S.live_engagements().size();
        c.shots = S.all_shots();
        c.reloads = S.all_reloads();
        c.eng = S.engagements;
        c.eng.insert(c.eng.end(), S.live_engagements().begin(), S.live_engagements().end());
        c.overall = overall(S.matches);
        c.maps = per_map(S.matches);
    }
    return c;
}

const char* map_name(const TabletInfo& info, int id, const std::vector<MatchRecord>* ms = nullptr) {
    if (info.catalog)
        if (const MapInfo* m = info.catalog->by_id(id)) return m->name.c_str();
    if (ms) for (const auto& r : *ms) if (r.map_id == id) return r.map_name.c_str();
    return "?";
}

// esantioanele afisate: meciul / sesiunea curenta sau ultimul meci salvat
const std::vector<PhysioSample>& source_samples(StatisticsSystem& S, bool history, std::string& label) {
    static const std::vector<PhysioSample> empty;
    if (!history && !S.live_samples().empty()) {
        label = S.in_match() ? "MECIUL CURENT" : "SESIUNEA CURENTA";
        return S.live_samples();
    }
    if (!S.matches.empty()) {
        const MatchRecord& m = S.matches.back();
        label = "MECIUL #" + std::to_string(m.id) + " " + m.map_name;
        return S.samples_of(m.id);
    }
    label = "FARA DATE";
    return empty;
}

// ============================================================================= pagini
void page_overview(Canvas& cv, StatisticsSystem& S, const World& w, const Agent* me, const TabletInfo& info, int y0) {
    char buf[128];
    const Cache& C = cache_of(S);
    const OverallStats& o = C.overall;
    int y = y0;
    auto L = [&](int x, const char* t, Col c) { cv.text(x, y, t, c); };
    std::snprintf(buf, sizeof(buf), "ISTORIC: %d MECIURI", o.matches);
    L(4, buf, kAccent); y += 10;
    std::snprintf(buf, sizeof(buf), "VICTORII %d  INFRANGERI %d", o.wins, o.losses); L(4, buf, kTxt); y += 9;
    std::snprintf(buf, sizeof(buf), "EGALURI %d  ABANDONATE %d", o.draws, o.abandoned); L(4, buf, kTxt); y += 9;
    std::snprintf(buf, sizeof(buf), "RATA VICTORIE %.0f%%", o.win_rate * 100.0); L(4, buf, kTxt);
    cv.bar(118, y + 1, 40, 5, o.win_rate, kGood); y += 9;
    std::snprintf(buf, sizeof(buf), "K/D %.2f  (%d / %d)", o.kd, o.kills, o.deaths); L(4, buf, kTxt); y += 9;
    std::snprintf(buf, sizeof(buf), "PRECIZIE %.1f%%  CAP %d", o.accuracy * 100.0, o.headshots); L(4, buf, kTxt); y += 9;
    std::snprintf(buf, sizeof(buf), "DAUNA PE MECI %.0f", o.dmg_avg); L(4, buf, kTxt); y += 9;
    std::snprintf(buf, sizeof(buf), "PULS %.0f BPM  STRES %.0f/100", o.hr_avg, o.stress_avg * 100.0); L(4, buf, kTxt); y += 9;
    std::snprintf(buf, sizeof(buf), "DURATA MEDIE %s", mmss(o.duration_avg).c_str()); L(4, buf, kTxt); y += 9;
    std::snprintf(buf, sizeof(buf), "SERIA %d %s, RECORD %d", std::abs(o.streak), o.streak >= 0 ? "VICTORII" : "INFRANGERI", o.best_win_streak);
    L(4, buf, kDim);

    // meciul / sesiunea curenta (live)
    int x = 172;
    y = y0;
    const MatchRecord& lv = S.live();
    L(x, S.in_match() ? "MECI CURENT (LIVE)" : (S.training() ? "POLIGON (LIVE)" : "FARA MECI"), kAccent); y += 10;
    std::snprintf(buf, sizeof(buf), "%s  %s", map_name(info, S.map_id()), mmss(S.match_time()).c_str()); L(x, buf, kTxt); y += 9;
    if (S.in_match()) {
        std::snprintf(buf, sizeof(buf), "RUNDE %d-%d  SCOR %d:%d", lv.rounds_won, lv.rounds_lost, w.game.score_blue, w.game.score_red);
        L(x, buf, kTxt); y += 9;
        std::snprintf(buf, sizeof(buf), "UCIDERI %d  MORTI %d", lv.kills, lv.deaths); L(x, buf, kTxt); y += 8;
        std::snprintf(buf, sizeof(buf), "ASISTENTE %d", lv.assists); L(x, buf, kTxt); y += 8;
        std::snprintf(buf, sizeof(buf), "FOCURI %d  LOVITE %d", lv.shots, lv.hits);
        L(x, buf, kTxt); y += 9;
        std::snprintf(buf, sizeof(buf), "DAUNA %.0f PRIMITA %.0f", lv.dmg_dealt, lv.dmg_taken); L(x, buf, kTxt); y += 9;
    } else {
        std::snprintf(buf, sizeof(buf), "FOCURI %d", static_cast<int>(S.live_shots().size())); L(x, buf, kTxt); y += 9;
    }
    if (me) {
        std::snprintf(buf, sizeof(buf), "PULS %.0f  STRES %.0f", me->phys.heart_rate, me->phys.stress * 100.0); L(x, buf, kTxt); y += 9;
        std::snprintf(buf, sizeof(buf), "STAMINA %.0f%%", me->stamina * 100.0); L(x, buf, kTxt); y += 9;
        std::snprintf(buf, sizeof(buf), "OBOSEALA %.0f%%", me->phys.fatigue * 100.0); L(x, buf, kTxt); y += 9;
    }

    // victorii vs infrangeri
    y = y0 + 103;
    cv.rect(2, y - 3, cv.s.w - 4, 1, kGrid);
    if (o.wins + o.losses == 0) {
        cv.text(4, y, "VICTORII VS INFRANGERI: INCA NU EXISTA MECIURI TERMINATE", kDim);
        cv.text(4, y + 10, "UN MECI = PRIMUL LA 3 RUNDE CASTIGATE (CONFIG/STATS.INI)", kDim);
        return;
    }
    std::snprintf(buf, sizeof(buf), "%-13s %7s %5s %8s %5s %4s", "MEDII PE MECI", "UCIDERI", "MORTI", "PRECIZIE", "STRES", "PULS");
    cv.text(4, y, buf, kDim); y += 10;
    std::snprintf(buf, sizeof(buf), "%-13s %7.1f %5.1f %7.0f%% %5.0f %4.0f", "IN VICTORII", o.kills_w, o.deaths_w, o.acc_w * 100.0, o.stress_w * 100.0, o.hr_w);
    cv.text(4, y, buf, kGood); y += 9;
    std::snprintf(buf, sizeof(buf), "%-13s %7.1f %5.1f %7.0f%% %5.0f %4.0f", "IN INFRANGERI", o.kills_l, o.deaths_l, o.acc_l * 100.0, o.stress_l * 100.0, o.hr_l);
    cv.text(4, y, buf, kBad);
}

void page_heart(Canvas& cv, TabletUiState& st, StatisticsSystem& S, const World& w, const Agent* me, const TabletClick& ck, int y0, int bottom) {
    char buf[128];
    const int W = cv.s.w;
    std::string label;
    const auto& smp = source_samples(S, st.stats_history, label);
    if (button(cv, ck, W - 92, y0 - 1, 90, 11, st.stats_history ? "ULTIMUL MECI" : "CURENT")) st.stats_history = !st.stats_history;
    cv.text(4, y0, label.c_str(), kAccent);
    std::vector<double> t, hr;
    for (const auto& s : smp) { t.push_back(s.t); hr.push_back(s.hr); }
    Summary sm = summarize(hr);
    HrRecovery rec = hr_recovery(smp);
    std::snprintf(buf, sizeof(buf), "ACUM %.0f  MEDIU %.0f  MIN %.0f  MAX %.0f BPM", me ? me->phys.heart_rate : 0.0, sm.mean, sm.min, sm.max);
    cv.text(4, y0 + 11, buf, kTxt);
    std::snprintf(buf, sizeof(buf), "REPAUS %.0f  MAXIM %.0f  URCARE %.2f/S  COBORARE %.2f/S", w.phys_cfg.hr_rest, w.phys_cfg.hr_max,
                  w.phys_cfg.hr_rise, w.phys_cfg.hr_fall);
    cv.text(4, y0 + 20, buf, kDim);
    if (rec.peaks > 0) std::snprintf(buf, sizeof(buf), "REVENIRE MASURATA: -%.0f BPM IN 30S (%d VARFURI)", rec.drop_avg, rec.peaks);
    else std::snprintf(buf, sizeof(buf), "REVENIRE: INCA NICIUN VARF >105 BPM + 30S");
    cv.text(4, y0 + 29, buf, kDim);
    if (smp.size() < 2) { cv.textc(W / 2, y0 + 80, "INCA NU EXISTA ESANTIOANE", kDim); return; }
    double ya, yb;
    int grid;
    nice_range(std::min(60.0, sm.min - 5.0), std::max(120.0, sm.max + 5.0), 4, ya, yb, grid);
    Box b{28, y0 + 40, W - 34, 78, t.front(), std::max(t.back(), t.front() + 1.0), ya, yb};
    chart_frame(cv, b, "%.0f", grid);
    // fundal: momentele sub foc / trageri / sprint (cauzele cresterii pulsului)
    for (size_t i = 0; i < smp.size(); ++i) {
        int x = static_cast<int>(b.sx(smp[i].t));
        Col c{0, 0, 0};
        bool any = true;
        if (smp[i].under_fire) c = kBad;
        else if (smp[i].firing) c = {1.0f, 0.55f, 0.2f};
        else if (smp[i].sprinting) c = kWarn;
        else any = false;
        if (any) for (int yy = b.y + b.h + 1; yy < b.y + b.h + 4; ++yy) cv.px(x, yy, c);
    }
    plot_line(cv, b, t, hr, {1.0f, 0.35f, 0.35f});
    int ly = b.y + b.h + 6;
    cv.text(b.x, ly + 4, mmss(b.x0).c_str(), kDim);
    cv.textr(b.x + b.w, ly + 4, mmss(b.x1).c_str(), kDim);
    cv.rect(b.x + 40, ly + 5, 5, 5, kBad); cv.text(b.x + 48, ly + 4, "SUB FOC", kDim);
    cv.rect(b.x + 100, ly + 5, 5, 5, {1.0f, 0.55f, 0.2f}); cv.text(b.x + 108, ly + 4, "TRAGE", kDim);
    cv.rect(b.x + 148, ly + 5, 5, 5, kWarn); cv.text(b.x + 156, ly + 4, "SPRINT", kDim);
    // timpul petrecut in zone de puls
    const double edges[6] = {0, 90, 115, 140, 160, 999};
    const char* zn[5] = {"<90", "90-115", "115-140", "140-160", ">160"};
    const Col zc[5] = {{0.3f, 0.6f, 0.9f}, kGood, kWarn, {1.0f, 0.5f, 0.2f}, kBad};
    int cnt[5] = {0, 0, 0, 0, 0};
    for (double h : hr) for (int z = 0; z < 5; ++z) if (h >= edges[z] && h < edges[z + 1]) { cnt[z]++; break; }
    int zy = std::min(bottom - 18, ly + 16), zx = 4, zw = W - 8;
    cv.text(zx, zy, "TIMP IN ZONE DE PULS:", kDim);
    zy += 9;
    int acc = 0;
    for (int z = 0; z < 5; ++z) {
        int wpx = static_cast<int>(static_cast<double>(cnt[z]) / hr.size() * zw);
        cv.rect(zx + acc, zy, wpx, 6, zc[z]);
        if (wpx > 40) {
            std::snprintf(buf, sizeof(buf), "%s %.0f%%", zn[z], 100.0 * cnt[z] / hr.size());
            cv.text(zx + acc + 2, zy - 0, buf, {0.02f, 0.03f, 0.03f});
        }
        acc += wpx;
    }
}

void page_stress(Canvas& cv, TabletUiState& st, StatisticsSystem& S, const World& w, const Agent* me, const TabletClick& ck, int y0) {
    char buf[128];
    const int W = cv.s.w;
    std::string label;
    const auto& smp = source_samples(S, st.stats_history, label);
    if (button(cv, ck, W - 92, y0 - 1, 90, 11, st.stats_history ? "ULTIMUL MECI" : "CURENT")) st.stats_history = !st.stats_history;
    cv.text(4, y0, label.c_str(), kAccent);
    std::vector<double> t, sv, hr, adr;
    for (const auto& s : smp) { t.push_back(s.t); sv.push_back(s.stress * 100.0); hr.push_back(s.hr); adr.push_back(s.adrenaline * 100.0); }
    Summary sm = summarize(sv);
    std::snprintf(buf, sizeof(buf), "ACUM %.0f %s  MEDIU %.0f  MIN %.0f  MAX %.0f", me ? me->phys.stress * 100.0 : 0.0,
                  me ? stress_level_name(me->phys.level) : "", sm.mean, sm.min, sm.max);
    cv.text(4, y0 + 11, buf, kTxt);
    if (smp.size() < 2) { cv.textc(W / 2, y0 + 70, "INCA NU EXISTA ESANTIOANE", kDim); return; }
    // stresul in timp, cu pulsul suprapus (axa proprie, dreapta)
    Box b{24, y0 + 24, 180, 70, t.front(), std::max(t.back(), t.front() + 1.0), 0.0, 100.0};
    chart_frame(cv, b, "%.0f");
    Summary sh = summarize(hr);
    Box bh = b;
    int gh;
    nice_range(sh.min - 5.0, sh.max + 5.0, 4, bh.y0, bh.y1, gh);
    plot_line(cv, bh, t, hr, {0.55f, 0.2f, 0.22f});
    plot_line(cv, b, t, adr, {0.35f, 0.65f, 1.0f});
    plot_line(cv, b, t, sv, kWarn);
    time_labels(cv, b);
    cv.text(b.x + 2, b.y + 2, "STRES", kWarn);
    cv.text(b.x + 40, b.y + 2, "PULS", {0.75f, 0.3f, 0.3f});
    cv.text(b.x + 72, b.y + 2, "ADRENALINA", {0.35f, 0.65f, 1.0f});
    // puls vs stres (dispersie + regresie)
    Box d{232, y0 + 24, W - 236, 70, std::floor(sh.min / 10.0) * 10.0, std::ceil((sh.max + 1.0) / 10.0) * 10.0, 0.0, 100.0};
    chart_frame(cv, d, "%.0f", 2);
    for (size_t i = 0; i < hr.size(); ++i) cv.px(static_cast<int>(d.sx(hr[i])), static_cast<int>(d.sy(sv[i])), {0.9f, 0.75f, 0.4f});
    Regression r = regress(hr, sv);
    if (r.valid && r.n >= 20) {
        // dreapta de regresie, doar portiunea din interiorul graficului
        const int steps = 48;
        for (int k = 0; k < steps; ++k) {
            const double xa = d.x0 + (d.x1 - d.x0) * k / steps, xb = d.x0 + (d.x1 - d.x0) * (k + 1) / steps;
            const double ya = r.intercept + r.slope * xa, yb = r.intercept + r.slope * xb;
            if (ya < d.y0 || ya > d.y1 || yb < d.y0 || yb > d.y1) continue;
            cv.line(d.sx(xa), d.sy(ya), d.sx(xb), d.sy(yb), kAccent);
        }
    }
    cv.text(d.x, d.y + d.h + 2, "PULS ->", kDim);
    cv.text(d.x - 2, d.y - 9, "PULS VS STRES", kDim);
    int y = b.y + b.h + 13;
    cv.text(4, y, ("PULS-STRES: " + verdict(r, 20, "CRESC IMPREUNA", "VARIAZA INVERS")).c_str(), verdict_col(r, 20));
    y += 10;
    // timpul petrecut in fiecare nivel de stres (aceleasi praguri ca modelul fiziologic)
    const PhysiologyConfig& pc = w.phys_cfg;
    const double th[6] = {0.0, pc.th_alert, pc.th_elevated, pc.th_high, pc.th_extreme, pc.th_panic};
    int lv[6] = {0, 0, 0, 0, 0, 0};
    for (double s : sv) {
        int k = 0;
        while (k < 5 && s / 100.0 >= th[k + 1]) k++;
        lv[k]++;
    }
    cv.text(4, y, "TIMP PETRECUT IN FIECARE NIVEL:", kDim);
    y += 9;
    const Col lc[6] = {{0.3f, 0.6f, 0.9f}, kGood, {0.85f, 0.85f, 0.3f}, kWarn, {1.0f, 0.45f, 0.2f}, kBad};
    for (int k = 0; k < 6; ++k) {
        const int cx = 4 + (k % 3) * 104, cy = y + (k / 3) * 9;
        cv.rect(cx, cy + 1, 5, 5, lc[k]);
        std::snprintf(buf, sizeof(buf), "%s %.0f%%", stress_level_name(static_cast<StressLevel>(k)), 100.0 * lv[k] / sv.size());
        cv.text(cx + 8, cy, buf, kTxt);
    }
}

// Stamina si oboseala: in timp, consum si refacere (din esantioanele simularii)
void page_stamina(Canvas& cv, TabletUiState& st, StatisticsSystem& S, const Agent* me, const TabletClick& ck, int y0, int bottom) {
    char buf[128];
    const int W = cv.s.w;
    std::string label;
    const auto& smp = source_samples(S, st.stats_history, label);
    if (button(cv, ck, W - 92, y0 - 1, 90, 11, st.stats_history ? "ULTIMUL MECI" : "CURENT")) st.stats_history = !st.stats_history;
    cv.text(4, y0, label.c_str(), kAccent);
    std::vector<double> t, sta, fat;
    for (const auto& s : smp) { t.push_back(s.t); sta.push_back(s.stamina * 100.0); fat.push_back(s.fatigue * 100.0); }
    Summary ss = summarize(sta), sf = summarize(fat);
    std::snprintf(buf, sizeof(buf), "STAMINA ACUM %.0f%%  MEDIE %.0f%%  MINIMA %.0f%%", me ? me->stamina * 100.0 : 0.0, ss.mean, ss.min);
    cv.text(4, y0 + 11, buf, kTxt);
    std::snprintf(buf, sizeof(buf), "OBOSEALA ACUM %.0f%%  MEDIE %.0f%%  MAXIMA %.0f%%", me ? me->phys.fatigue * 100.0 : 0.0, sf.mean, sf.max);
    cv.text(4, y0 + 20, buf, kTxt);
    if (smp.size() < 2) { cv.textc(W / 2, y0 + 80, "INCA NU EXISTA ESANTIOANE", kDim); return; }
    // stamina (verde) si oboseala (portocaliu) in timp; dedesubt: momentele de alergare
    Box b{24, y0 + 31, 172, std::max(40, bottom - 31 - (y0 + 31)), t.front(), std::max(t.back(), t.front() + 1.0), 0.0, 100.0};
    chart_frame(cv, b, "%.0f");
    for (size_t i = 0; i < smp.size(); ++i)
        if (smp[i].sprinting) {
            int x = static_cast<int>(b.sx(smp[i].t));
            for (int yy = b.y + b.h + 1; yy < b.y + b.h + 4; ++yy) cv.px(x, yy, kWarn);
        }
    plot_line(cv, b, t, fat, {1.0f, 0.55f, 0.2f});
    plot_line(cv, b, t, sta, kGood);
    cv.text(b.x + 2, b.y + 2, "STAMINA %", kGood);
    cv.text(b.x + 62, b.y + 2, "OBOSEALA %", {1.0f, 0.55f, 0.2f});
    int ly = b.y + b.h + 2;
    cv.text(b.x, ly, mmss(b.x0).c_str(), kDim);
    cv.textr(b.x + b.w, ly, mmss(b.x1).c_str(), kDim);
    cv.rect(b.x + 50, ly + 1, 5, 5, kWarn);
    cv.text(b.x + 58, ly, "ALERGARE", kDim);
    // consum si refacere masurate pe esantioane (pas fix de esantionare)
    double used = 0.0, rec = 0.0, rec_time = 0.0, sprint_t = 0.0, fat_gain = 0.0;
    std::vector<double> episodes;     // s: de la stamina sub 40% pana revine peste 90%
    double low_t = -1.0;
    for (size_t i = 1; i < smp.size(); ++i) {
        const double dt = smp[i].t - smp[i - 1].t;
        if (dt <= 0.0 || dt > 2.0) continue;
        const double d = smp[i].stamina - smp[i - 1].stamina;
        if (d < 0.0) used -= d; else rec += d;
        if (!smp[i].sprinting && d > 0.0) rec_time += dt;
        if (smp[i].sprinting) sprint_t += dt;
        const double df = smp[i].fatigue - smp[i - 1].fatigue;
        if (df > 0.0) fat_gain += df;
        if (smp[i].stamina < 0.4 && low_t < 0.0) low_t = smp[i].t;
        if (low_t >= 0.0 && smp[i].stamina > 0.9) { episodes.push_back(smp[i].t - low_t); low_t = -1.0; }
    }
    const int x = 204;
    int y = y0 + 31;
    cv.text(x, y, "CONSUM SI REFACERE", kAccent); y += 11;
    std::snprintf(buf, sizeof(buf), "CONSUMATA %.0f%%", used * 100.0); cv.text(x, y, buf, kTxt); y += 9;
    std::snprintf(buf, sizeof(buf), "REFACUTA %.0f%%", rec * 100.0); cv.text(x, y, buf, kTxt); y += 9;
    std::snprintf(buf, sizeof(buf), "REFACERE %.1f%%/S", rec_time > 0.0 ? rec * 100.0 / rec_time : 0.0); cv.text(x, y, buf, kTxt); y += 9;
    if (!episodes.empty()) {
        Summary se = summarize(episodes);
        std::snprintf(buf, sizeof(buf), "40->90%%: %.0f S", se.mean);
    } else std::snprintf(buf, sizeof(buf), "40->90%%: -");
    cv.text(x, y, buf, kTxt); y += 9;
    std::snprintf(buf, sizeof(buf), "ALERGARE %.0f S", sprint_t); cv.text(x, y, buf, kTxt); y += 9;
    std::snprintf(buf, sizeof(buf), "OBOSEALA +%.0f%%", fat_gain * 100.0); cv.text(x, y, buf, kTxt);
    // toate meciurile salvate: consumul pe meci si oboseala acumulata
    int n = 0;
    double u = 0.0, fg = 0.0, fm = 0.0;
    for (const auto& m : S.matches) if (m.counted()) { n++; u += m.stamina_used; fg += m.fatigue_gain; fm += m.fatigue_max; }
    const int yb = bottom - 10;
    if (n > 0) {
        std::snprintf(buf, sizeof(buf), "PE MECI (%d SALVATE): CONSUM %.0f%%, OBOSEALA +%.0f%%", n, u / n * 100.0, fg / n * 100.0);
        cv.text(4, yb, buf, kDim);
    } else cv.text(4, yb, "NICIUN MECI TERMINAT SALVAT INCA", kDim);
    (void)fm;
}

// Analizele de performanta: fiecare = o relatie masurabila din datele colectate
struct Analysis { const char* name; const char* x_name; };
const Analysis kAnalyses[] = {
    {"PRECIZIA IN TIMP (MECI)", "TIMP"}, {"STRES -> PRECIZIE", "STRES"}, {"PULS -> PRECIZIE", "PULS"},
    {"DISTANTA -> PRECIZIE", "DISTANTA M"}, {"SPRINT RECENT -> PRECIZIE", "SPRINT 5S"}, {"BRAT RANIT -> PRECIZIE", "RANA BRAT"},
    {"STRES -> REINCARCARE", "STRES"}, {"OBOSEALA -> REINCARCARE", "OBOSEALA"}, {"PULS -> REACTIE", "PULS"},
    {"STRES -> REACTIE", "STRES"}};
constexpr int kAnalysisCount = sizeof(kAnalyses) / sizeof(kAnalyses[0]);

bool ctx_ok(const ShotRecord& s, int filter) {
    if (filter == 1) return s.context == static_cast<int>(StatContext::Match);
    if (filter == 2) return s.context >= 10 && s.context < 20;
    return true;
}

void page_performance(Canvas& cv, TabletUiState& st, StatisticsSystem& S, const TabletClick& ck, int y0, int bottom) {
    char buf[128];
    const int W = cv.s.w;
    const Cache& C = cache_of(S);
    int shots = 0, hits = 0, head = 0;
    for (const auto& s : C.shots) if (ctx_ok(s, st.ctx_filter)) { shots++; hits += s.hit; head += (s.hit && s.zone == 0); }
    std::vector<double> react;
    for (const auto& e : C.eng) if (e.reaction >= 0.0) react.push_back(e.reaction);
    std::vector<double> rl;
    for (const auto& r : C.reloads) if (!r.interrupted) rl.push_back(r.duration);
    Summary sr = summarize(react), sl = summarize(rl);
    const OverallStats& o = C.overall;
    const char* fl[3] = {"TOATE", "MECIURI", "POLIGON"};
    if (button(cv, ck, W - 64, y0 - 1, 62, 11, fl[st.ctx_filter % 3])) st.ctx_filter = (st.ctx_filter + 1) % 3;
    std::snprintf(buf, sizeof(buf), "PRECIZIE %.1f%%  K/D %.2f  DAUNA %.0f", shots ? 100.0 * hits / shots : 0.0, o.kd, o.dmg_avg);
    cv.text(4, y0, buf, kTxt);
    std::snprintf(buf, sizeof(buf), "FOCURI %d  LOVITE %d  RATATE %d  CAP %.0f%%", shots, hits, shots - hits, hits ? 100.0 * head / hits : 0.0);
    cv.text(4, y0 + 9, buf, kTxt);
    std::snprintf(buf, sizeof(buf), "REACTIE MEDIE %.2fS (N=%d)  REINCARCARE %.2fS (N=%d)", sr.mean, sr.n, sl.mean, sl.n);
    cv.text(4, y0 + 18, buf, kTxt);
    // selectorul analizei
    int sel = ((st.stats_chart % kAnalysisCount) + kAnalysisCount) % kAnalysisCount;
    int ay = y0 + 28;
    if (button(cv, ck, 2, ay, 20, 12, "<")) sel = (sel + kAnalysisCount - 1) % kAnalysisCount;
    if (button(cv, ck, W - 22, ay, 20, 12, ">")) sel = (sel + 1) % kAnalysisCount;
    st.stats_chart = sel;
    cv.textc(W / 2, ay + 3, kAnalyses[sel].name, kAccent);

    std::vector<double> xs, ys, edges;
    std::vector<std::string> labels;
    bool binary = true;
    const char* vfmt = "%.0f";
    double ymax = 100.0;
    const char* up = "CRESTE";
    const char* down = "SCADE";
    auto shot_pairs = [&](auto fx, bool need_dist) {
        for (const auto& s : C.shots) {
            if (!ctx_ok(s, st.ctx_filter)) continue;
            if (need_dist && s.distance <= 0.0) continue;
            xs.push_back(fx(s));
            ys.push_back(s.hit);
        }
    };
    switch (sel) {
        case 0: {
            // meciul curent sau ultimul meci salvat
            const std::vector<ShotRecord>* src = &S.live_shots();
            std::vector<ShotRecord> last;
            if (src->empty() && !S.matches.empty()) {
                int64_t id = S.matches.back().id;
                for (const auto& s : S.shots) if (s.match == id && s.context == 0) last.push_back(s);
                src = &last;
            }
            double tmax = 0.0;
            for (const auto& s : *src) { xs.push_back(s.t); ys.push_back(s.hit); tmax = std::max(tmax, s.t); }
            double step = std::max(30.0, std::ceil(tmax / 6.0 / 30.0) * 30.0);
            for (double e = 0.0; e <= tmax + step; e += step) edges.push_back(e);
            for (size_t i = 0; i + 1 < edges.size(); ++i) labels.push_back(mmss(edges[i]));
            break;
        }
        case 1: shot_pairs([](const ShotRecord& s) { return s.stress; }, false);
                edges = {0, 0.2, 0.4, 0.6, 0.8, 1.0001}; labels = {"0-20", "20-40", "40-60", "60-80", "80+"}; break;
        case 2: shot_pairs([](const ShotRecord& s) { return s.hr; }, false);
                edges = {40, 90, 110, 130, 150, 250}; labels = {"<90", "90-110", "110-130", "130-150", "150+"}; break;
        case 3: shot_pairs([](const ShotRecord& s) { return s.distance; }, true);
                edges = {0, 10, 25, 50, 100, 400}; labels = {"<10", "10-25", "25-50", "50-100", "100+"}; break;
        case 4: shot_pairs([](const ShotRecord& s) { return static_cast<double>(s.recent_sprint); }, false);
                edges = {-0.5, 0.5, 1.5}; labels = {"FARA SPRINT", "DUPA SPRINT"}; break;
        case 5: shot_pairs([](const ShotRecord& s) { return s.arm_injury; }, false);
                edges = {0, 0.01, 0.3, 0.6, 1.0001}; labels = {"INTACT", "<30%", "30-60%", "60%+"}; break;
        case 6: case 7:
            binary = false; vfmt = "%.2f"; ymax = 0.0;
            for (const auto& r : C.reloads) {
                if (r.interrupted) continue;
                xs.push_back(sel == 6 ? r.stress : r.fatigue);
                ys.push_back(r.duration);
            }
            edges = {0, 0.2, 0.4, 0.6, 0.8, 1.0001}; labels = {"0-20", "20-40", "40-60", "60-80", "80+"};
            up = "MAI LENTA"; down = "MAI RAPIDA";
            break;
        default: {
            binary = false; vfmt = "%.2f"; ymax = 0.0;
            for (const auto& e : C.eng) {
                if (e.reaction < 0.0) continue;
                xs.push_back(sel == 8 ? e.hr_mean : e.stress_mean);
                ys.push_back(e.reaction);
            }
            // si probele de reactie din poligon (timp mediu, puls / stres mediu al sesiunii)
            for (const auto& r : S.range_sessions) {
                if (r.mode != static_cast<int>(RangeMode::Reaction) || r.react_n == 0) continue;
                xs.push_back(sel == 8 ? r.hr_mean : r.stress_mean);
                ys.push_back(r.react_mean);
            }
            if (sel == 8) { edges = {40, 90, 115, 140, 250}; labels = {"<90", "90-115", "115-140", "140+"}; }
            else { edges = {0, 0.25, 0.5, 0.75, 1.0001}; labels = {"0-25", "25-50", "50-75", "75+"}; }
            up = "MAI LENTA"; down = "MAI RAPIDA";
            break;
        }
    }
    std::vector<Bin> bins = bin_by(xs, ys, edges, binary);
    std::vector<double> v, lo, hi;
    std::vector<int> n;
    for (const auto& b : bins) {
        if (binary) { v.push_back(b.prop.p * 100.0); lo.push_back(b.prop.lo * 100.0); hi.push_back(b.prop.hi * 100.0); }
        else { v.push_back(b.mean_y); lo.push_back(b.mean_y - b.ci); hi.push_back(b.mean_y + b.ci); ymax = std::max(ymax, b.mean_y + b.ci); }
        n.push_back(b.n);
    }
    if (!binary) ymax = std::max(0.5, std::ceil(ymax * 1.15 * 2.0) / 2.0);
    Box box{26, ay + 25, W - 32, bottom - ay - 58, 0, 1, 0.0, ymax};
    bar_chart(cv, box, labels, v, lo, hi, n, vfmt, kAccent);
    cv.text(4, ay + 15, binary ? "PRECIZIE % (INTERVAL 95% WILSON)" : (sel <= 7 ? "DURATA S (INTERVAL 95%)" : "REACTIE S (INTERVAL 95%)"), kDim);
    Regression r = regress(xs, ys);
    int vy = box.y + box.h + 20;
    if (sel == 0) {
        std::snprintf(buf, sizeof(buf), "%d FOCURI IN MECI; PRECIZIA PE INTERVALE DE %s", static_cast<int>(xs.size()),
                      edges.size() > 1 ? mmss(edges[1] - edges[0]).c_str() : "-");
        cv.text(4, vy, buf, kDim);
    } else {
        cv.text(4, vy, verdict(r, 20, up, down).c_str(), verdict_col(r, 20));
    }
}

void page_maps(Canvas& cv, TabletUiState& st, StatisticsSystem& S, const TabletInfo& info, const TabletClick& ck, int y0, int bottom) {
    char buf[128];
    const int W = cv.s.w;
    const Cache& C = cache_of(S);
    std::map<int, MapStats> by;
    for (const auto& m : C.maps) by[m.map_id] = m;
    std::vector<int> ids;
    if (info.catalog) for (const auto& m : info.catalog->maps) if (!m.training()) ids.push_back(m.id);
    for (const auto& kv : by) if (std::find(ids.begin(), ids.end(), kv.first) == ids.end()) ids.push_back(kv.first);
    std::snprintf(buf, sizeof(buf), "%-11s %7s %9s %4s %8s %5s", "HARTA", "MECIURI", "VICTORII", "K/D", "PRECIZIE", "STRES");
    cv.text(2, y0, buf, kDim);
    int y = y0 + 10;
    for (int id : ids) {
        auto it = by.find(id);
        bool sel = st.compare ? (id == st.cmp_a || id == st.cmp_b) : id == st.map_sel;
        if (sel) cv.rect(1, y - 1, W - 2, 9, {0.08f, 0.16f, 0.2f});
        std::string nm = clip(map_name(info, id, &S.matches), 11);
        if (it == by.end()) std::snprintf(buf, sizeof(buf), "%-11s %7d %9s %4s %8s %5s", nm.c_str(), 0, "-", "-", "-", "-");
        else {
            const MapStats& m = it->second;
            char vw[16];
            std::snprintf(vw, sizeof(vw), "%d %.0f%%", m.wins, m.win_rate * 100.0);
            std::snprintf(buf, sizeof(buf), "%-11s %7d %9s %4.1f %7.0f%% %5.0f", nm.c_str(), m.matches, vw,
                          m.kd, m.accuracy * 100.0, m.stress_avg * 100.0);
        }
        cv.text(2, y, buf, it == by.end() ? kDim : kTxt);
        if (hit(ck, W, cv.s.h, 0, y - 1, W, 9)) {
            if (st.compare) { if (st.cmp_a < 0 || (st.cmp_a >= 0 && st.cmp_b >= 0)) { st.cmp_a = id; st.cmp_b = -1; } else if (id != st.cmp_a) st.cmp_b = id; }
            else st.map_sel = st.map_sel == id ? -1 : id;
        }
        y += 9;
    }
    y += 3;
    cv.rect(2, y - 2, W - 4, 1, kGrid);
    if (button(cv, ck, W - 66, y, 64, 11, "COMPARA", st.compare)) { st.compare = !st.compare; st.cmp_a = st.cmp_b = -1; }
    if (st.compare) {
        if (st.cmp_a < 0 || st.cmp_b < 0) { cv.text(4, y + 2, "ATINGE DOUA HARTI DIN TABEL", kDim); return; }
        y += 13;
        MapStats a = by.count(st.cmp_a) ? by[st.cmp_a] : MapStats{}, b = by.count(st.cmp_b) ? by[st.cmp_b] : MapStats{};
        std::snprintf(buf, sizeof(buf), "%-16s %10s %10s", "", clip(map_name(info, st.cmp_a, &S.matches), 10).c_str(),
                      clip(map_name(info, st.cmp_b, &S.matches), 10).c_str());
        cv.text(4, y, buf, kAccent); y += 9;
        struct Row { const char* n; double va, vb; const char* f; };
        Row rows[] = {{"MECIURI", double(a.matches), double(b.matches), "%10.0f"}, {"RATA VICTORIE %", a.win_rate * 100, b.win_rate * 100, "%10.0f"},
                      {"K/D", a.kd, b.kd, "%10.2f"}, {"PRECIZIE %", a.accuracy * 100, b.accuracy * 100, "%10.1f"},
                      {"PULS MEDIU", a.hr_avg, b.hr_avg, "%10.0f"}, {"STRES MEDIU", a.stress_avg * 100, b.stress_avg * 100, "%10.0f"},
                      {"DAUNA / MECI", a.dmg_avg, b.dmg_avg, "%10.0f"}, {"DISTANTA ANGAJARE", a.eng_dist_avg, b.eng_dist_avg, "%10.1f"},
                      {"DURATA S", a.duration_avg, b.duration_avg, "%10.0f"}};
        for (const auto& r : rows) {
            std::string sa = fmtd(r.f, r.va), sb = fmtd(r.f, r.vb);
            std::snprintf(buf, sizeof(buf), "%-17s %s %s", r.n, sa.c_str(), sb.c_str());
            cv.text(4, y, buf, kTxt);
            y += 8;
            if (y > bottom - 10) break;
        }
        // testul Welch pe stresul mediu pe meci (cele doua harti)
        std::vector<double> sa, sb;
        for (const auto& m : S.matches) if (m.counted()) { if (m.map_id == st.cmp_a) sa.push_back(m.stress_avg * 100); if (m.map_id == st.cmp_b) sb.push_back(m.stress_avg * 100); }
        Welch wt = welch(sa, sb);
        if (wt.na >= 2 && wt.nb >= 2) std::snprintf(buf, sizeof(buf), "STRES: DIFERENTA %+.1f, WELCH T=%.2f P=%.3f", wt.diff, wt.t, wt.p);
        else std::snprintf(buf, sizeof(buf), "TEST STRES: MINIM 2 MECIURI PE FIECARE HARTA");
        cv.text(4, std::min(bottom - 9, y + 1), buf, wt.p < 0.05 ? kWarn : kDim);
        return;
    }
    // grafic: metrica aleasa pe toate hartile (bare orizontale, nume intregi)
    const char* metrics[7] = {"RATA VICTORIE %", "K/D", "STRES MEDIU", "PULS MEDIU", "DISTANTA ANGAJARE M", "PRECIZIE %", "DURATA MEDIE S"};
    int sel = ((st.stats_chart % 7) + 7) % 7;
    if (button(cv, ck, 2, y, 16, 11, "<")) sel = (sel + 6) % 7;
    if (button(cv, ck, 140, y, 16, 11, ">")) sel = (sel + 1) % 7;
    st.stats_chart = sel;
    cv.textc(79, y + 2, clip(metrics[sel], 19).c_str(), kAccent);
    std::vector<std::string> labels;
    std::vector<double> v;
    std::vector<int> n;
    int hl = -1;
    double vmax = 1.0;
    for (int id : ids) {
        auto it = by.find(id);
        MapStats m = it == by.end() ? MapStats{} : it->second;
        double val[7] = {m.win_rate * 100, m.kd, m.stress_avg * 100, m.hr_avg, m.eng_dist_avg, m.accuracy * 100, m.duration_avg};
        if (id == st.map_sel) hl = static_cast<int>(labels.size());
        labels.push_back(map_name(info, id, &S.matches));
        v.push_back(val[sel]);
        n.push_back(m.matches);
        vmax = std::max(vmax, val[sel]);
    }
    if (sel == 0 || sel == 2 || sel == 5) vmax = 100.0;
    int rows = static_cast<int>(labels.size());
    int row_h = std::max(8, std::min(10, (bottom - y - 16) / std::max(1, rows)));
    hbar_chart(cv, 2, y + 15, 156, row_h, labels, v, n, vmax, sel == 1 ? "%.2f" : "%.0f", kAccent, hl);
    // geometria masurata a hartii selectate (sau a celei curente)
    int gid = st.map_sel > 0 ? st.map_sel : info.map_id;
    int x = 164, gy = y + 14;
    if (info.catalog)
        if (const MapInfo* mi = info.catalog->by_id(gid)) {
            auto G = [&](const char* t, Col c) { if (gy <= bottom - 8) cv.text(x, gy, t, c); gy += 8; };
            G((mi->name + " (MASURAT)").c_str(), kAccent);
            std::snprintf(buf, sizeof(buf), "%dX%d M, %d M2", mi->width, mi->height, mi->walkable); G(buf, kTxt);
            std::snprintf(buf, sizeof(buf), "CAMERE %d, USI %d", mi->room_count, mi->door_count); G(buf, kTxt);
            std::snprintf(buf, sizeof(buf), "OBIECTIVE %d, INAMICI %d", mi->objective_count, mi->enemy_count); G(buf, kTxt);
            std::snprintf(buf, sizeof(buf), "VIZIBILITATE %.0f%%", mi->openness * 100.0); G(buf, kTxt);
            std::snprintf(buf, sizeof(buf), "DISTANTA POSIBILA %.0f M", mi->eng_mean); G(buf, kTxt);
            std::snprintf(buf, sizeof(buf), "INTERVAL %.0f-%.0f M", mi->eng_p10, mi->eng_p90); G(buf, kDim);
            auto it = by.find(gid);
            if (it != by.end()) { std::snprintf(buf, sizeof(buf), "MASURATA IN JOC %.1f M", it->second.eng_dist_avg); G(buf, kWarn); }
        }
}

void page_weapons(Canvas& cv, TabletUiState& st, StatisticsSystem& S, const TabletClick& ck, int y0, int bottom) {
    char buf[128];
    const int W = cv.s.w;
    const Cache& C = cache_of(S);
    std::vector<ShotRecord> f;
    for (const auto& s : C.shots) if (ctx_ok(s, st.ctx_filter)) f.push_back(s);
    std::vector<WeaponStats> ws = per_weapon(f);
    const char* fl[3] = {"TOATE", "MECIURI", "POLIGON"};
    std::snprintf(buf, sizeof(buf), "%-8s %6s %6s %8s %3s %7s %5s", "ARMA", "FOCURI", "LOVITE", "PRECIZIE", "CAP", "UCIDERI", "DAUNA");
    cv.text(2, y0, buf, kDim);
    int y = y0 + 10;
    if (ws.empty()) cv.text(4, y, "NICIUN FOC INREGISTRAT INCA", kDim);
    for (const auto& w : ws) {
        std::snprintf(buf, sizeof(buf), "%-8s %6d %6d %7.0f%% %3d %7d %5.0f", clip(w.weapon, 8).c_str(), w.shots, w.hits,
                      w.acc.p * 100, w.head, w.kills, w.damage);
        cv.text(2, y, buf, kTxt);
        y += 9;
        if (y > y0 + 60) break;
    }
    const char* charts[3] = {"PRECIZIE PE ARMA % (INTERVAL 95%)", "DAUNA TOTALA PE ARMA", "UCIDERI PE ARMA"};
    int sel = ((st.stats_chart % 3) + 3) % 3;
    int cy = y0 + 66;
    if (button(cv, ck, 2, cy, 16, 11, "<")) sel = (sel + 2) % 3;
    if (button(cv, ck, 214, cy, 16, 11, ">")) sel = (sel + 1) % 3;
    if (button(cv, ck, W - 72, cy, 70, 11, fl[st.ctx_filter % 3])) st.ctx_filter = (st.ctx_filter + 1) % 3;
    st.stats_chart = sel;
    cv.textc(116, cy + 2, charts[sel], kAccent);
    std::vector<std::string> labels;
    std::vector<double> v, lo, hi;
    std::vector<int> n;
    double vmax = 1.0;
    for (const auto& w : ws) {
        labels.push_back(w.weapon);
        double val = sel == 0 ? w.acc.p * 100 : sel == 1 ? w.damage : w.kills;
        v.push_back(val);
        if (sel == 0) { lo.push_back(w.acc.lo * 100); hi.push_back(w.acc.hi * 100); }
        n.push_back(w.shots);
        vmax = std::max(vmax, val);
    }
    Box b{30, cy + 16, W - 36, bottom - cy - 34, 0, 1, 0.0, sel == 0 ? 100.0 : std::ceil(vmax * 1.15)};
    bar_chart(cv, b, labels, v, lo, hi, n, "%.0f", kAccent);
}

void page_history(Canvas& cv, TabletUiState& st, StatisticsSystem& S, const TabletInfo& info, const TabletClick& ck, int y0, int bottom) {
    char buf[160];
    const int W = cv.s.w;
    bool range = st.ctx_filter % 2 == 1;
    if (st.history_sel == 0 && button(cv, ck, W - 92, bottom - 15, 70, 12, range ? "POLIGON" : "MECIURI")) {
        st.ctx_filter = range ? 0 : 1;
        st.history_scroll = 0;
        range = !range;
    }
    if (st.history_sel != 0) {
        if (button(cv, ck, W - 60, y0 - 1, 58, 11, "INAPOI")) { st.history_sel = 0; return; }
        if (!range) {
            const MatchRecord* m = nullptr;
            for (const auto& r : S.matches) if (r.id == st.history_sel) m = &r;
            if (!m) { st.history_sel = 0; return; }
            std::snprintf(buf, sizeof(buf), "MECI #%03lld %s  %s  %s", static_cast<long long>(m->id), m->map_name.c_str(),
                          m->result == "W" ? "VICTORIE" : m->result == "L" ? "INFRANGERE" : m->result == "D" ? "EGAL" : "ABANDONAT",
                          m->timestamp.c_str());
            cv.text(4, y0, buf, m->result == "W" ? kGood : m->result == "L" ? kBad : kWarn);
            int y = y0 + 11;
            auto ln = [&](int x, const char* t) { cv.text(x, y, t, kTxt); };
            std::snprintf(buf, sizeof(buf), "RUNDE %d-%d  DURATA %s  SCOR %d", m->rounds_won, m->rounds_lost, mmss(m->duration).c_str(), m->score); ln(4, buf); y += 9;
            std::snprintf(buf, sizeof(buf), "UCIDERI %d  MORTI %d  ASISTENTE %d", m->kills, m->deaths, m->assists); ln(4, buf); y += 9;
            std::snprintf(buf, sizeof(buf), "FOCURI %d  LOVITE %d (%.0f%%)  IN CAP %d", m->shots, m->hits, m->accuracy() * 100, m->headshots);
            ln(4, buf); y += 9;
            std::snprintf(buf, sizeof(buf), "DAUNA %.0f / %.0f  RANI %d  REACTIE %.2fS", m->dmg_dealt, m->dmg_taken, m->injuries, m->reaction_avg); ln(4, buf); y += 9;
            std::snprintf(buf, sizeof(buf), "PULS %.0f (%.0f-%.0f)  STRES %.0f (MAXIM %.0f)  OBOSEALA %.0f%%", m->hr_avg, m->hr_min, m->hr_max,
                          m->stress_avg * 100, m->stress_max * 100, m->fatigue_max * 100); ln(4, buf); y += 9;
            std::snprintf(buf, sizeof(buf), "DISTANTA %.0f M  IN ALERGARE %.0f M  REINCARCARI %d (%.2f S)", m->distance, m->sprint_distance,
                          m->reloads, m->reload_avg); ln(4, buf); y += 9;
            std::snprintf(buf, sizeof(buf), "ANGAJAMENTE %d, DISTANTA MEDIE %.0f M", m->engagements, m->eng_dist_avg); ln(4, buf); y += 9;
            // pulsul si stresul in timpul meciului (esantioanele salvate)
            const auto& smp = S.samples_of(m->id);
            if (smp.size() >= 2) {
                std::vector<double> t, hr, sv;
                for (const auto& s : smp) { t.push_back(s.t); hr.push_back(s.hr); sv.push_back(s.stress * 100.0); }
                Summary sh = summarize(hr);
                Box b{26, y + 4, W - 32, bottom - y - 18, t.front(), std::max(t.back(), t.front() + 1.0),
                      std::floor((sh.min - 5) / 10) * 10, std::ceil((sh.max + 5) / 10) * 10};
                chart_frame(cv, b, "%.0f");
                Box bs = b; bs.y0 = 0; bs.y1 = 100;
                plot_line(cv, bs, t, sv, kWarn);
                plot_line(cv, b, t, hr, {1.0f, 0.35f, 0.35f});
                time_labels(cv, b);
                cv.text(b.x + 2, b.y + 2, "PULS", {1.0f, 0.4f, 0.4f});
                cv.text(b.x + 34, b.y + 2, "STRES (0-100)", kWarn);
            }
        } else {
            const RangeSessionRecord* r = nullptr;
            for (const auto& s : S.range_sessions) if (s.id == st.history_sel) r = &s;
            if (!r) { st.history_sel = 0; return; }
            std::snprintf(buf, sizeof(buf), "SESIUNE #%03lld %s  %s  %s", static_cast<long long>(r->id), range_mode_name(static_cast<RangeMode>(r->mode)),
                          r->weapon.c_str(), r->timestamp.c_str());
            cv.text(4, y0, buf, kAccent);
            int y = y0 + 12;
            auto ln = [&](const char* t) { cv.text(4, y, t, kTxt); y += 9; };
            const char* sz[3] = {"MICA", "MEDIE", "MARE"};
            std::snprintf(buf, sizeof(buf), "DISTANTA %.0f M  TINTA %s  DURATA %.1f S", r->distance, sz[std::clamp(r->size, 0, 2)], r->duration); ln(buf);
            std::snprintf(buf, sizeof(buf), "%.0f FOCURI PE MINUT", r->spm); ln(buf);
            std::snprintf(buf, sizeof(buf), "FOCURI %d  LOVITE %d (%.0f%%)  CAP %d  CORP %d", r->shots, r->hits, r->accuracy * 100, r->head, r->body); ln(buf);
            std::snprintf(buf, sizeof(buf), "ABATERE MEDIE FATA DE CENTRU %.0f CM", r->mre * 100); ln(buf);
            std::snprintf(buf, sizeof(buf), "PULS %.0f (MAX %.0f)  STRES %.0f", r->hr_mean, r->hr_max, r->stress_mean * 100); ln(buf);
            if (r->react_n > 0) {
                std::snprintf(buf, sizeof(buf), "REACTIE MEDIE %.3f S  MEDIANA %.3f S", r->react_mean, r->react_median); ln(buf);
                std::snprintf(buf, sizeof(buf), "ABATERE STANDARD %.3f S  N=%d  RATATE %d", r->react_sd, r->react_n, r->timeouts); ln(buf);
            }
            if (r->total_time > 0) { std::snprintf(buf, sizeof(buf), "TIMP TOTAL %.2fS", r->total_time); ln(buf); }
            if (r->acc_d0 >= 0) { std::snprintf(buf, sizeof(buf), "PRECIZIE 10M %.0f%%  25M %.0f%%  50M %.0f%%  100M %.0f%%", r->acc_d0 * 100, r->acc_d1 * 100, r->acc_d2 * 100, r->acc_d3 * 100); ln(buf); }
            if (r->shots_rest > 0) {
                std::snprintf(buf, sizeof(buf), "REPAUS: %d/%d  PULS %.0f  STRES %.0f  REACTIE %.2fS", r->hits_rest, r->shots_rest, r->hr_rest, r->stress_rest * 100, r->react_rest); ln(buf);
                std::snprintf(buf, sizeof(buf), "SPRINT: %d/%d  PULS %.0f  STRES %.0f  REACTIE %.2fS", r->hits_post, r->shots_post, r->hr_post, r->stress_post * 100, r->react_post); ln(buf);
            }
        }
        return;
    }
    // lista (cele mai recente primele)
    const int rows = (bottom - y0 - 26) / 9;
    int total = range ? static_cast<int>(S.range_sessions.size()) : static_cast<int>(S.matches.size());
    st.history_scroll = std::clamp(st.history_scroll, 0, std::max(0, total - rows));
    if (range) std::snprintf(buf, sizeof(buf), "%-3s %-9s %-6s %8s %8s %8s", "#", "MOD", "ARMA", "DISTANTA", "LOVITURI", "PRECIZIE");
    else std::snprintf(buf, sizeof(buf), "%-3s %-10s %-10s %5s %8s %6s", "#", "HARTA", "REZULTAT", "K-D", "PRECIZIE", "DURATA");
    cv.text(2, y0, buf, kDim);
    int y = y0 + 10;
    for (int i = st.history_scroll; i < total && i < st.history_scroll + rows; ++i) {
        int idx = total - 1 - i;
        int64_t id;
        Col c = kTxt;
        if (range) {
            const auto& r = S.range_sessions[idx];
            id = r.id;
            char lf[16];
            std::snprintf(lf, sizeof(lf), "%d/%d", r.hits, r.shots);
            std::snprintf(buf, sizeof(buf), "%03lld %-9s %-6s %7.0fM %8s %7.0f%%", static_cast<long long>(r.id),
                          range_mode_name(static_cast<RangeMode>(r.mode)), clip(r.weapon, 6).c_str(), r.distance, lf, r.accuracy * 100);
        } else {
            const auto& m = S.matches[idx];
            id = m.id;
            c = m.result == "W" ? kGood : m.result == "L" ? Col{1.0f, 0.5f, 0.45f} : kWarn;
            char kd[12];
            std::snprintf(kd, sizeof(kd), "%d-%d", m.kills, m.deaths);
            const char* rez = m.result == "W" ? "VICTORIE" : m.result == "L" ? "INFRANGERE" : m.result == "D" ? "EGAL" : "ABANDONAT";
            std::snprintf(buf, sizeof(buf), "%03lld %-10s %-10s %5s %7.0f%% %6s", static_cast<long long>(m.id),
                          clip(m.map_name, 10).c_str(), rez, kd, m.accuracy() * 100, mmss(m.duration).c_str());
        }
        cv.text(2, y, buf, c);
        if (hit(ck, W, cv.s.h, 0, y - 1, W - 20, 9)) st.history_sel = id;
        y += 9;
    }
    if (total == 0) cv.text(4, y, range ? "NICIO SESIUNE DE POLIGON SALVATA" : "NICIUN MECI SALVAT INCA", kDim);
    if (button(cv, ck, W - 18, y0 + 10, 16, 14, "^")) st.history_scroll = std::max(0, st.history_scroll - rows);
    if (button(cv, ck, W - 18, bottom - 30, 16, 14, "V")) st.history_scroll += rows;
    std::snprintf(buf, sizeof(buf), "%d INREGISTRARI - ATINGE UN RAND", total);
    cv.text(4, bottom - 12, buf, kDim);
    (void)info;
}

void page_experiments(Canvas& cv, TabletUiState& st, StatisticsSystem& S, const TabletInfo& info, TabletCommand& cmd,
                      const TabletClick& ck, int y0, int bottom) {
    char buf[160];
    const int W = cv.s.w;
    const ExperimentManager* em = info.experiments;
    if (!em || em->defs.empty()) { cv.text(4, y0, "EXPERIMENTE INDISPONIBILE (CONFIG/EXPERIMENTS.INI)", kDim); return; }
    const auto& defs = em->defs;
    st.exp_sel = std::clamp(st.exp_sel, 0, static_cast<int>(defs.size()) - 1);
    int y = y0;
    for (size_t i = 0; i < defs.size(); ++i) {
        bool act = static_cast<int>(i) == st.exp_sel;
        if (button(cv, ck, 2, y, 104, 11, clip(defs[i].name, 17).c_str(), act)) st.exp_sel = static_cast<int>(i);
        y += 12;
    }
    const ExperimentDef& d = defs[st.exp_sel];
    int x = 112;
    y = y0;
    cv.text(x, y, clip(d.question, 34).c_str(), kAccent); y += 10;
    std::string lv;
    for (double l : d.levels) lv += d.level_label(l) + " ";
    std::snprintf(buf, sizeof(buf), "FACTOR %s: %s", d.factor.c_str(), lv.c_str());
    cv.text(x, y, clip(buf, 34).c_str(), kTxt); y += 9;
    std::snprintf(buf, sizeof(buf), "%d NIVELURI X %d REPLICARI (CRN)", static_cast<int>(d.levels.size()), d.reps);
    cv.text(x, y, buf, kDim); y += 11;
    if (em->running()) {
        int a = em->active_def();
        std::snprintf(buf, sizeof(buf), "RULEAZA: %s", a >= 0 ? clip(defs[a].name, 20).c_str() : "");
        cv.text(x, y, buf, kWarn);
        cv.bar(x, y + 9, W - x - 64, 6, em->progress(), kWarn);
        if (button(cv, ck, W - 60, y + 4, 58, 12, "OPRESTE", false, kBad)) { cmd.cancel_experiment = true; cmd.confirm = true; }
    } else if (button(cv, ck, x, y, 100, 13, "RULEAZA", false, kGood)) {
        cmd.run_experiment = st.exp_sel;
        cmd.confirm = true;
    }
    y += 19;
    // rezultatele ultimei rulari a experimentului selectat
    std::vector<int64_t> runs = runs_of(S.trials, d.key);
    if (runs.empty()) { cv.text(x, y + 10, "NICIO RULARE SALVATA", kDim); }
    else {
        RunSummary rs = summarize_run(S.trials, runs.back(), d.measure);
        const char* unit = d.measure == "accuracy" ? "PRECIZIE %" : d.measure == "mre" ? "ABATERE M" : d.measure == "sway_rms" ? "INSTABILITATE MILIRADIANI"
                         : d.measure == "reload_mean" ? "REINCARCARE S" : d.measure == "tremor_rms" ? "TREMUR MILIRADIANI" : "PULS";
        std::snprintf(buf, sizeof(buf), "RULAREA %lld (%d PROBE): %s", static_cast<long long>(rs.run), rs.trials, unit);
        cv.text(x, y, clip(buf, 34).c_str(), kDim);
        std::vector<std::string> labels;
        std::vector<double> v, lo, hi;
        std::vector<int> n;
        double vmax = 0.0;
        for (const auto& l : rs.levels) {
            labels.push_back(d.level_label(l.level));
            v.push_back(l.mean); lo.push_back(l.mean - l.ci); hi.push_back(l.mean + l.ci); n.push_back(l.n);
            vmax = std::max(vmax, l.mean + l.ci);
        }
        double ymax = d.measure == "accuracy" ? 100.0 : std::max(0.5, std::ceil(vmax * 1.15 * 2.0) / 2.0);
        Box b{x + 22, y + 10, W - x - 26, bottom - y - 58, 0, 1, 0.0, ymax};
        bar_chart(cv, b, labels, v, lo, hi, n, ymax >= 20 ? "%.0f" : "%.2f", kAccent);
        int vy = b.y + b.h + 20;
        if (d.categorical()) {
            // cel mai bun nivel (masura mai mare = mai bine doar pentru precizie)
            int best = 0;
            for (size_t i = 1; i < rs.levels.size(); ++i) if ((rs.levels[i].mean > rs.levels[best].mean) == (d.measure == "accuracy")) best = static_cast<int>(i);
            if (!rs.levels.empty()) std::snprintf(buf, sizeof(buf), "CEL MAI BUN: %s (%.1f +- %.1f)", d.level_label(rs.levels[best].level).c_str(), rs.levels[best].mean, rs.levels[best].ci);
            cv.text(x, vy, clip(buf, 34).c_str(), kTxt);
        } else {
            Regression r;
            r.n = rs.trials; r.r = rs.r; r.p = rs.p; r.slope = rs.slope; r.valid = rs.trials >= 3;
            cv.text(x, vy, clip(verdict(r, 10, "CRESTE", "SCADE"), 34).c_str(), verdict_col(r, 10));
            std::snprintf(buf, sizeof(buf), "PANTA %.4g / UNITATE DE FACTOR", rs.slope);
            cv.text(x, vy + 9, buf, kDim);
        }
        if (runs.size() >= 2) {
            RunSummary prev = summarize_run(S.trials, runs[runs.size() - 2], d.measure);
            std::snprintf(buf, sizeof(buf), "RULARI: %d  ANTERIOARA: PANTA %.3g R=%.2f", static_cast<int>(runs.size()), prev.slope, prev.r);
            cv.text(x, vy + 18, clip(buf, 34).c_str(), kDim);
        }
    }
    // probele interactive: proba de stres din poligon (repaus vs dupa sprint, comparatie pereche)
    std::vector<double> ar, ap, hr0, hr1;
    for (const auto& r : S.range_sessions) {
        if (r.mode != static_cast<int>(RangeMode::Stress) || r.shots_rest == 0 || r.shots_post == 0) continue;
        ar.push_back(100.0 * r.hits_rest / r.shots_rest);
        ap.push_back(100.0 * r.hits_post / r.shots_post);
        hr0.push_back(r.hr_rest);
        hr1.push_back(r.hr_post);
    }
    int sy = y0 + 12 * static_cast<int>(defs.size()) + 4;
    cv.text(2, sy, "PROBA STRES (POLIGON)", kAccent);
    if (ar.empty()) { cv.text(2, sy + 9, "NICIO PROBA INCA", kDim); return; }
    Paired pc = paired_compare(ar, ap);
    Paired ph = paired_compare(hr0, hr1);
    std::snprintf(buf, sizeof(buf), "N=%d", pc.n); cv.text(2, sy + 9, buf, kTxt);
    std::snprintf(buf, sizeof(buf), "PRECIZIE %.0f->%.0f%%", pc.mean_a, pc.mean_b); cv.text(2, sy + 18, buf, kTxt);
    std::snprintf(buf, sizeof(buf), "PULS %.0f->%.0f", ph.mean_a, ph.mean_b); cv.text(2, sy + 27, buf, kTxt);
    if (pc.n >= 2) { std::snprintf(buf, sizeof(buf), "T=%.2f P=%.3f", pc.t, pc.p); cv.text(2, sy + 36, buf, pc.p < 0.05 ? kWarn : kDim); }
}

} // namespace

// ============================================================================
void tablet_stats_page(TabletScreen& scr, TabletUiState& st, const World& w, int local, const TabletClick& click,
                       double time, const TabletInfo& info, TabletCommand& cmd, int top, int bottom) {
    (void)time;
    Canvas cv{scr};
    const int W = scr.w;
    TabletClick ck = click;
    if (!info.stats) { cv.textc(W / 2, (top + bottom) / 2, "STATISTICI INDISPONIBILE", kBad); return; }
    StatisticsSystem& S = *info.stats;
    const Agent* me = (local >= 0 && local < static_cast<int>(w.agents.size())) ? &w.agents[local] : nullptr;
    // sectiunile, pe doua randuri (cuvinte intregi, latimea dupa lungimea cuvantului)
    struct TabDef { const char* name; StatsTab tab; };
    const TabDef rows[2][5] = {
        {{"REZUMAT", StatsTab::Overview}, {"PULS", StatsTab::Heart}, {"STRES", StatsTab::Stress}, {"STAMINA", StatsTab::Stamina},
         {"PERFORMANTA", StatsTab::Performance}},
        {{"HARTI", StatsTab::Maps}, {"ARME", StatsTab::Weapons}, {"ISTORIC", StatsTab::History}, {"EXPERIMENTE", StatsTab::Experiments},
         {nullptr, StatsTab::Count}}};
    for (int r = 0; r < 2; ++r) {
        int chars = 0, n = 0;
        for (const auto& t : rows[r]) if (t.name) { chars += static_cast<int>(std::string(t.name).size()) + 2; n++; }
        double x = 1.0;
        for (int i = 0; i < n; ++i) {
            const TabDef& t = rows[r][i];
            const int tw = static_cast<int>((W - 2) * (std::string(t.name).size() + 2.0) / chars);
            if (button(cv, ck, static_cast<int>(x), top + 1 + r * 12, tw - 2, 11, t.name, st.stats_tab == t.tab)) {
                if (st.stats_tab != t.tab) { st.stats_chart = 0; st.history_sel = 0; }
                st.stats_tab = t.tab;
                ck.clicked = false;
                cmd.confirm = true;
            }
            x += tw;
        }
    }
    const int y0 = top + 28;
    switch (st.stats_tab) {
        case StatsTab::Overview: page_overview(cv, S, w, me, info, y0); break;
        case StatsTab::Heart: page_heart(cv, st, S, w, me, ck, y0, bottom); break;
        case StatsTab::Stress: page_stress(cv, st, S, w, me, ck, y0); break;
        case StatsTab::Stamina: page_stamina(cv, st, S, me, ck, y0, bottom); break;
        case StatsTab::Performance: page_performance(cv, st, S, ck, y0, bottom); break;
        case StatsTab::Maps: page_maps(cv, st, S, info, ck, y0, bottom); break;
        case StatsTab::Weapons: page_weapons(cv, st, S, ck, y0, bottom); break;
        case StatsTab::History: page_history(cv, st, S, info, ck, y0, bottom); break;
        case StatsTab::Experiments: page_experiments(cv, st, S, info, cmd, ck, y0, bottom); break;
        default: break;
    }
}

// ============================================================================
void tablet_range_page(TabletScreen& scr, TabletUiState& st, const World& w, int local, const TabletClick& click,
                       double time, const TabletInfo& info, TabletCommand& cmd, int top, int bottom) {
    (void)time;
    Canvas cv{scr};
    const int W = scr.w;
    char buf[160];
    const Agent* me = (local >= 0 && local < static_cast<int>(w.agents.size())) ? &w.agents[local] : nullptr;
    const ShootingRange& R = w.range;
    if (!R.enabled || !me) {
        cv.textc(W / 2, top + 40, "POLIGONUL ESTE PE HARTA DE ANTRENAMENT", kDim);
        if (info.catalog)
            for (const auto& m : info.catalog->maps)
                if (m.training() && button(cv, click, W / 2 - 60, top + 60, 120, 14, "MERGI LA POLIGON", false, kGood)) {
                    cmd.new_match_map = m.id;
                    cmd.confirm = true;
                }
        return;
    }
    int y = top + 2;
    // modurile de test
    const char* modes[6] = {"LIBER", "PRECIZIE", "REACTIE", "VITEZA", "DISTANTA", "STRES"};
    for (int i = 0; i < 6; ++i)
        if (button(cv, click, 2 + i * 53, y, 51, 12, modes[i], st.range_mode == i)) st.range_mode = i;
    y += 15;
    cv.text(2, y + 3, "DISTANTA", kDim);
    for (int i = 0; i < static_cast<int>(R.cfg.distances.size()) && i < 4; ++i) {
        std::snprintf(buf, sizeof(buf), "%.0fM", R.cfg.distances[i]);
        if (button(cv, click, 52 + i * 34, y, 32, 12, buf, st.range_dist == i)) st.range_dist = i;
    }
    cv.text(194, y + 3, "TINTA", kDim);
    const char* sz[3] = {"S", "M", "L"};
    for (int i = 0; i < 3; ++i)
        if (button(cv, click, 228 + i * 30, y, 28, 12, sz[i], st.range_size == i)) st.range_size = i;
    y += 15;
    // arma testata (inventarul real: echipamentul se aplica imediat pe poligon)
    std::vector<int> opts;
    for (size_t i = 0; i < w.weapon_raw.size(); ++i) if (w.weapon_raw[i].model != WeaponModel::Pistol) opts.push_back(static_cast<int>(i));
    const WeaponData* cur = w.weapon_of(*me);
    cv.text(2, y + 3, "ARMA", kDim);
    if (!opts.empty()) {
        int ci = 0;
        Loadout L = w.current_loadout(*me);
        for (size_t i = 0; i < opts.size(); ++i) if (w.weapon_raw[opts[i]].id == L.primary) ci = static_cast<int>(i);
        int ni = -1;
        if (button(cv, click, 34, y, 16, 12, "<")) ni = (ci + static_cast<int>(opts.size()) - 1) % static_cast<int>(opts.size());
        if (button(cv, click, 130, y, 16, 12, ">")) ni = (ci + 1) % static_cast<int>(opts.size());
        cv.textc(90, y + 3, cur ? cur->name.c_str() : "-", kAccent);
        if (ni >= 0) {
            L.primary = w.weapon_raw[opts[ni]].id;
            L.primary_att.clear();
            cmd.loadout_apply = true;
            cmd.loadout = L;
            cmd.loadout.valid = true;
            cmd.confirm = true;
        }
    }
    bool active = R.session_active();
    if (!active) {
        if (button(cv, click, 160, y, 76, 12, st.range_mode == 0 ? "LIBER" : "START", false, kGood)) {
            if (st.range_mode == 0) cmd.range_stop = true;
            else { cmd.range_mode = st.range_mode; cmd.range_dist = st.range_dist; cmd.range_size = st.range_size; }
            cmd.confirm = true;
        }
    } else if (button(cv, click, 160, y, 76, 12, "OPRESTE", false, kBad)) {
        cmd.range_stop = true;
        cmd.confirm = true;
    }
    std::snprintf(buf, sizeof(buf), "PULS %.0f", me->phys.heart_rate);
    cv.text(244, y + 3, buf, kTxt);
    y += 16;
    cv.rect(2, y - 2, W - 4, 1, kGrid);
    cv.text(4, y + 1, clip(R.instruction(w), 52).c_str(), active ? kWarn : kTxt);
    y += 12;

    // tinta din culoarul operatorului: distributia impacturilor (urmele reale)
    int lane = 0;
    double best = 1e9;
    for (size_t l = 0; l < R.cfg.lane_y.size(); ++l)
        if (std::abs(R.cfg.lane_y[l] - me->y) < best) { best = std::abs(R.cfg.lane_y[l] - me->y); lane = static_cast<int>(l); }
    const RangeTarget* tg = nullptr;
    for (const auto& t : R.targets) if (t.lane == lane && t.dist_idx == st.range_dist) tg = &t;
    for (const auto& t : R.targets) if (t.lane == lane && t.want_up && !t.marks.empty()) tg = &t;
    int tx = 4, ty = y, th = bottom - y - 4, tw2 = 84;
    cv.rect(tx, ty, tw2, th, {0.02f, 0.03f, 0.035f});
    cv.frame(tx, ty, tw2, th, kGrid);
    if (tg) {
        double scale = (th - 8) / (R.cfg.head_z + R.cfg.head_r + 0.05);
        int cx = tx + tw2 / 2, base = ty + th - 4;
        auto P = [&](double u, double v, int& sx, int& sy) { sx = cx + static_cast<int>(u * scale); sy = base - static_cast<int>(v * scale); };
        int x0, y0, x1, y1;
        P(-R.cfg.body_w * 0.5, R.cfg.body_z1, x0, y0);
        P(R.cfg.body_w * 0.5, R.cfg.body_z0, x1, y1);
        cv.rect(x0, y0, x1 - x0, y1 - y0, {0.5f, 0.48f, 0.42f});
        P(-R.cfg.body_w * 0.21, R.cfg.body_z1 - 0.12, x0, y0);
        P(R.cfg.body_w * 0.21, R.cfg.body_z0 + 0.2, x1, y1);
        cv.frame(x0, y0, x1 - x0, y1 - y0, {0.32f, 0.3f, 0.26f});
        int hx, hy;
        P(0.0, R.cfg.head_z, hx, hy);
        cv.disc(hx, hy, R.cfg.head_r * scale, {0.55f, 0.22f, 0.16f});
        for (const auto& m : tg->marks) {
            int mx, my;
            P(m.u, m.v, mx, my);
            cv.rect(mx - 1, my - 1, 3, 3, m.head ? Col{1.0f, 1.0f, 0.3f} : Col{0.05f, 0.05f, 0.06f});
        }
        std::snprintf(buf, sizeof(buf), "%.0fM  %d", tg->distance, static_cast<int>(tg->marks.size()));
        cv.textc(cx, ty + 2, buf, kDim);
    }
    // rezultatul ultimei sesiuni (din simulare)
    int x = 94;
    if (R.result_seq > 0) {
        const RangeResult& r = R.result;
        std::snprintf(buf, sizeof(buf), "ULTIMA: %s  %s", range_mode_name(r.mode), r.weapon >= 0 && r.weapon < static_cast<int>(w.weapons.size()) ? w.weapons[r.weapon].name.c_str() : "");
        cv.text(x, y, buf, kAccent); y += 10;
        std::snprintf(buf, sizeof(buf), "LOVITE %d/%d (%.0f%%)  CAP %d  CORP %d", r.hits, r.shots, r.accuracy() * 100, r.head, r.body); cv.text(x, y, buf, kTxt); y += 9;
        std::snprintf(buf, sizeof(buf), "ABATERE %.0f CM  DURATA %.1fS", r.mre * 100, r.duration); cv.text(x, y, buf, kTxt); y += 9;
        std::snprintf(buf, sizeof(buf), "PULS %.0f (MAX %.0f)  STRES %.0f", r.hr_mean, r.hr_max, r.stress_mean * 100); cv.text(x, y, buf, kTxt); y += 9;
        if (!r.reactions.empty()) {
            std::vector<double> rv = r.reactions;
            Summary s = summarize(rv);
            std::snprintf(buf, sizeof(buf), "REACTIE %.3f S  MEDIANA %.3f S", s.mean, s.median); cv.text(x, y, buf, kTxt); y += 9;
            std::snprintf(buf, sizeof(buf), "ABATERE STANDARD %.3f S  N=%d", s.sd, s.n); cv.text(x, y, buf, kTxt); y += 9;
        }
        if (r.mode == RangeMode::Speed) { std::snprintf(buf, sizeof(buf), "TIMP TOTAL %.2fS  TINTE %d", r.total_time, static_cast<int>(r.splits.size())); cv.text(x, y, buf, kTxt); y += 9; }
        if (r.mode == RangeMode::Distance) {
            std::string ln;
            for (size_t d = 0; d < r.shots_d.size(); ++d) ln += fmtd("%.0fM:", R.cfg.distances[d]) + fmtd("%.0f%% ", r.shots_d[d] ? 100.0 * r.hits_d[d] / r.shots_d[d] : 0.0);
            cv.text(x, y, clip(ln, 37).c_str(), kTxt); y += 9;
        }
        if (r.mode == RangeMode::Stress) {
            std::snprintf(buf, sizeof(buf), "REPAUS %d/%d PULS %.0f REACTIE %.2fS", r.hits_rest, r.shots_rest, r.hr_rest, r.react_rest); cv.text(x, y, buf, kGood); y += 9;
            std::snprintf(buf, sizeof(buf), "SPRINT %d/%d PULS %.0f REACTIE %.2fS", r.hits_post, r.shots_post, r.hr_post, r.react_post); cv.text(x, y, buf, kBad); y += 9;
        }
    }
    // istoricul sesiunilor pe modul ales
    if (info.stats && st.range_mode > 0) {
        int n = 0;
        double acc = 0.0;
        for (const auto& s : info.stats->range_sessions) if (s.mode == st.range_mode) { n++; acc += s.accuracy; }
        std::snprintf(buf, sizeof(buf), "SESIUNI %s: %d  PRECIZIE MEDIE %.0f%%", modes[st.range_mode], n, n ? acc / n * 100 : 0.0);
        cv.text(x, bottom - 12, clip(buf, 37).c_str(), kDim);
    }
}

}
