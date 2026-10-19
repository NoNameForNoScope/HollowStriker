#include "analytics.hpp"
#include <algorithm>
#include <cmath>
#include <map>

namespace bodycam {

namespace {
// Fractia continua pentru functia beta incompleta regularizata (Numerical Recipes, betacf)
double betacf(double a, double b, double x) {
    const int kMaxIt = 200;
    const double kEps = 3e-12, kFpMin = 1e-300;
    double qab = a + b, qap = a + 1.0, qam = a - 1.0;
    double c = 1.0, d = 1.0 - qab * x / qap;
    if (std::abs(d) < kFpMin) d = kFpMin;
    d = 1.0 / d;
    double h = d;
    for (int m = 1; m <= kMaxIt; ++m) {
        int m2 = 2 * m;
        double aa = m * (b - m) * x / ((qam + m2) * (a + m2));
        d = 1.0 + aa * d; if (std::abs(d) < kFpMin) d = kFpMin;
        c = 1.0 + aa / c; if (std::abs(c) < kFpMin) c = kFpMin;
        d = 1.0 / d;
        h *= d * c;
        aa = -(a + m) * (qab + m) * x / ((a + m2) * (qap + m2));
        d = 1.0 + aa * d; if (std::abs(d) < kFpMin) d = kFpMin;
        c = 1.0 + aa / c; if (std::abs(c) < kFpMin) c = kFpMin;
        d = 1.0 / d;
        double del = d * c;
        h *= del;
        if (std::abs(del - 1.0) < kEps) break;
    }
    return h;
}
double incbeta(double a, double b, double x) {
    if (x <= 0.0) return 0.0;
    if (x >= 1.0) return 1.0;
    double bt = std::exp(std::lgamma(a + b) - std::lgamma(a) - std::lgamma(b) + a * std::log(x) + b * std::log(1.0 - x));
    if (x < (a + 1.0) / (a + b + 2.0)) return bt * betacf(a, b, x) / a;
    return 1.0 - bt * betacf(b, a, 1.0 - x) / b;
}
double mean_of(const std::vector<double>& v) {
    if (v.empty()) return 0.0;
    double s = 0.0;
    for (double x : v) s += x;
    return s / v.size();
}
}

double student_p_two_sided(double t, double df) {
    if (df <= 0.0 || !std::isfinite(t)) return 1.0;
    return std::clamp(incbeta(0.5 * df, 0.5, df / (df + t * t)), 0.0, 1.0);
}

double student_t_quantile975(double df) {
    if (df < 1.0) return 12.706;
    static const double table[30] = {12.706, 4.303, 3.182, 2.776, 2.571, 2.447, 2.365, 2.306, 2.262, 2.228,
                                     2.201, 2.179, 2.160, 2.145, 2.131, 2.120, 2.110, 2.101, 2.093, 2.086,
                                     2.080, 2.074, 2.069, 2.064, 2.060, 2.056, 2.052, 2.048, 2.045, 2.042};
    if (df <= 30.0) return table[static_cast<int>(df) - 1];
    // dezvoltarea Cornish-Fisher in jurul cuantilei normale
    const double z = 1.959964;
    return z + (z * z * z + z) / (4.0 * df) + (5 * std::pow(z, 5) + 16 * z * z * z + 3 * z) / (96.0 * df * df);
}

double Summary::ci95() const {
    if (n < 2) return 0.0;
    return student_t_quantile975(n - 1) * sd / std::sqrt(static_cast<double>(n));
}

Summary summarize(const std::vector<double>& v) {
    Summary s;
    s.n = static_cast<int>(v.size());
    if (v.empty()) return s;
    s.mean = mean_of(v);
    s.min = *std::min_element(v.begin(), v.end());
    s.max = *std::max_element(v.begin(), v.end());
    double ss = 0.0;
    for (double x : v) ss += (x - s.mean) * (x - s.mean);
    s.sd = s.n > 1 ? std::sqrt(ss / (s.n - 1)) : 0.0;
    std::vector<double> c = v;
    std::sort(c.begin(), c.end());
    s.median = c.size() % 2 ? c[c.size() / 2] : 0.5 * (c[c.size() / 2 - 1] + c[c.size() / 2]);
    return s;
}

Regression regress(const std::vector<double>& x, const std::vector<double>& y) {
    Regression r;
    size_t n = std::min(x.size(), y.size());
    r.n = static_cast<int>(n);
    if (n < 3) return r;
    double mx = 0, my = 0;
    for (size_t i = 0; i < n; ++i) { mx += x[i]; my += y[i]; }
    mx /= n; my /= n;
    double sxx = 0, syy = 0, sxy = 0;
    for (size_t i = 0; i < n; ++i) {
        sxx += (x[i] - mx) * (x[i] - mx);
        syy += (y[i] - my) * (y[i] - my);
        sxy += (x[i] - mx) * (y[i] - my);
    }
    if (sxx <= 1e-15) return r;
    r.slope = sxy / sxx;
    r.intercept = my - r.slope * mx;
    r.r = syy > 1e-15 ? sxy / std::sqrt(sxx * syy) : 0.0;
    r.r2 = r.r * r.r;
    double df = static_cast<double>(n) - 2.0;
    double denom = std::max(1e-15, 1.0 - r.r2);
    r.t = r.r * std::sqrt(df / denom);
    r.p = student_p_two_sided(r.t, df);
    r.valid = true;
    return r;
}

Proportion wilson(int k, int n) {
    Proportion p;
    p.k = k; p.n = n;
    if (n <= 0) return p;
    const double z = 1.959964, z2 = z * z;
    double ph = static_cast<double>(k) / n;
    double den = 1.0 + z2 / n;
    double c = (ph + z2 / (2.0 * n)) / den;
    double h = z * std::sqrt(ph * (1.0 - ph) / n + z2 / (4.0 * n * n)) / den;
    p.p = ph;
    p.lo = std::max(0.0, c - h);
    p.hi = std::min(1.0, c + h);
    return p;
}

std::vector<Bin> bin_by(const std::vector<double>& x, const std::vector<double>& y, const std::vector<double>& edges,
                        bool binary) {
    std::vector<Bin> out;
    if (edges.size() < 2) return out;
    std::vector<std::vector<double>> ys(edges.size() - 1);
    size_t n = std::min(x.size(), y.size());
    for (size_t i = 0; i < n; ++i) {
        for (size_t b = 0; b + 1 < edges.size(); ++b) {
            bool last = b + 2 == edges.size();
            if (x[i] >= edges[b] && (x[i] < edges[b + 1] || (last && x[i] <= edges[b + 1]))) { ys[b].push_back(y[i]); break; }
        }
    }
    for (size_t b = 0; b + 1 < edges.size(); ++b) {
        Bin bin;
        bin.x0 = edges[b]; bin.x1 = edges[b + 1];
        Summary s = summarize(ys[b]);
        bin.n = s.n;
        bin.mean_y = s.mean;
        bin.ci = s.ci95();
        if (binary) {
            int k = 0;
            for (double v : ys[b]) k += v > 0.5 ? 1 : 0;
            bin.prop = wilson(k, s.n);
        }
        out.push_back(bin);
    }
    return out;
}

std::vector<MapStats> per_map(const std::vector<MatchRecord>& matches) {
    std::map<int, MapStats> m;
    std::map<int, int> react_n;
    std::map<int, double> react_sum;
    for (const auto& r : matches) {
        if (!r.counted()) continue;
        MapStats& s = m[r.map_id];
        s.map_id = r.map_id;
        s.name = r.map_name;
        s.matches++;
        if (r.result == "W") s.wins++;
        else if (r.result == "L") s.losses++;
        else s.draws++;
        s.kills += r.kills; s.deaths += r.deaths; s.shots += r.shots; s.hits += r.hits;
        s.hr_avg += r.hr_avg; s.stress_avg += r.stress_avg; s.dmg_avg += r.dmg_dealt; s.dmg_taken_avg += r.dmg_taken;
        s.eng_dist_avg += r.eng_dist_avg; s.duration_avg += r.duration;
        if (r.reaction_avg >= 0.0) { react_n[r.map_id]++; react_sum[r.map_id] += r.reaction_avg; }
    }
    std::vector<MapStats> out;
    for (auto& kv : m) {
        MapStats s = kv.second;
        double n = std::max(1, s.matches);
        s.win_rate = static_cast<double>(s.wins) / n;
        s.kd = s.deaths > 0 ? static_cast<double>(s.kills) / s.deaths : s.kills;
        s.accuracy = s.shots > 0 ? static_cast<double>(s.hits) / s.shots : 0.0;
        s.hr_avg /= n; s.stress_avg /= n; s.dmg_avg /= n; s.dmg_taken_avg /= n;
        s.eng_dist_avg /= n; s.duration_avg /= n;
        s.kills_avg = s.kills / n; s.deaths_avg = s.deaths / n;
        if (react_n[kv.first] > 0) s.reaction_avg = react_sum[kv.first] / react_n[kv.first];
        out.push_back(s);
    }
    return out;
}

OverallStats overall(const std::vector<MatchRecord>& matches) {
    OverallStats o;
    int nw = 0, nl = 0;
    int run_w = 0, run_l = 0;
    for (const auto& r : matches) {
        if (r.result == "A") { o.abandoned++; continue; }
        if (!r.counted()) continue;
        o.matches++;
        o.kills += r.kills; o.deaths += r.deaths; o.shots += r.shots; o.hits += r.hits; o.headshots += r.headshots;
        o.dmg_avg += r.dmg_dealt; o.hr_avg += r.hr_avg; o.stress_avg += r.stress_avg; o.duration_avg += r.duration;
        if (r.result == "W") {
            o.wins++; nw++;
            o.kills_w += r.kills; o.deaths_w += r.deaths; o.acc_w += r.accuracy(); o.stress_w += r.stress_avg; o.hr_w += r.hr_avg;
            run_w++; run_l = 0;
        } else if (r.result == "L") {
            o.losses++; nl++;
            o.kills_l += r.kills; o.deaths_l += r.deaths; o.acc_l += r.accuracy(); o.stress_l += r.stress_avg; o.hr_l += r.hr_avg;
            run_l++; run_w = 0;
        } else {
            o.draws++;
            run_w = run_l = 0;
        }
        o.best_win_streak = std::max(o.best_win_streak, run_w);
        o.worst_loss_streak = std::max(o.worst_loss_streak, run_l);
    }
    o.streak = run_w > 0 ? run_w : -run_l;
    if (o.matches > 0) {
        double n = o.matches;
        o.win_rate = o.wins / n; o.loss_rate = o.losses / n;
        o.dmg_avg /= n; o.hr_avg /= n; o.stress_avg /= n; o.duration_avg /= n;
    }
    o.kd = o.deaths > 0 ? static_cast<double>(o.kills) / o.deaths : o.kills;
    o.accuracy = o.shots > 0 ? static_cast<double>(o.hits) / o.shots : 0.0;
    if (nw) { o.kills_w /= nw; o.deaths_w /= nw; o.acc_w /= nw; o.stress_w /= nw; o.hr_w /= nw; }
    if (nl) { o.kills_l /= nl; o.deaths_l /= nl; o.acc_l /= nl; o.stress_l /= nl; o.hr_l /= nl; }
    return o;
}

std::vector<WeaponStats> per_weapon(const std::vector<ShotRecord>& shots) {
    std::map<std::string, WeaponStats> m;
    std::map<std::string, int> dn;
    for (const auto& s : shots) {
        if (s.weapon.empty()) continue;
        WeaponStats& w = m[s.weapon];
        w.weapon = s.weapon;
        w.shots++;
        w.hits += s.hit;
        w.head += (s.hit && s.zone == 0) ? 1 : 0;
        w.kills += s.kill;
        w.damage += s.damage;
        if (s.hit && s.distance > 0.0) { w.dist_avg += s.distance; dn[s.weapon]++; }
    }
    std::vector<WeaponStats> out;
    for (auto& kv : m) {
        WeaponStats w = kv.second;
        if (dn[kv.first] > 0) w.dist_avg /= dn[kv.first];
        w.acc = wilson(w.hits, w.shots);
        out.push_back(w);
    }
    return out;
}

HrRecovery hr_recovery(const std::vector<PhysioSample>& s, double window, double min_peak) {
    HrRecovery out;
    if (s.size() < 3) return out;
    double drop_sum = 0.0;
    size_t i = 1;
    while (i + 1 < s.size()) {
        // varf local: pulsul nu mai creste in urmatoarele secunde
        if (s[i].hr >= min_peak && s[i].hr >= s[i - 1].hr && s[i].hr >= s[i + 1].hr) {
            size_t j = i;
            while (j + 1 < s.size() && s[j + 1].t - s[i].t <= window) ++j;
            if (s[j].t - s[i].t >= window * 0.9) {
                bool higher = false;
                for (size_t k = i + 1; k <= j; ++k) if (s[k].hr > s[i].hr) { higher = true; break; }
                if (!higher) {
                    drop_sum += s[i].hr - s[j].hr;
                    out.peaks++;
                    i = j;
                    continue;
                }
            }
        }
        ++i;
    }
    if (out.peaks > 0) {
        out.drop_avg = drop_sum / out.peaks;
        out.rate_avg = out.drop_avg / window;
    }
    return out;
}

std::vector<Bin> accuracy_over_time(const std::vector<ShotRecord>& shots, double bin_seconds) {
    std::vector<Bin> out;
    if (shots.empty() || bin_seconds <= 0.0) return out;
    double tmax = 0.0;
    for (const auto& s : shots) tmax = std::max(tmax, s.t);
    std::vector<double> edges;
    for (double e = 0.0; e <= tmax + bin_seconds; e += bin_seconds) edges.push_back(e);
    std::vector<double> x, y;
    for (const auto& s : shots) { x.push_back(s.t); y.push_back(s.hit); }
    return bin_by(x, y, edges, true);
}

Paired paired_compare(const std::vector<double>& a, const std::vector<double>& b) {
    Paired p;
    size_t n = std::min(a.size(), b.size());
    p.n = static_cast<int>(n);
    if (n == 0) return p;
    std::vector<double> d;
    for (size_t i = 0; i < n; ++i) d.push_back(b[i] - a[i]);
    p.mean_a = mean_of(std::vector<double>(a.begin(), a.begin() + n));
    p.mean_b = mean_of(std::vector<double>(b.begin(), b.begin() + n));
    Summary s = summarize(d);
    p.mean_diff = s.mean;
    p.sd_diff = s.sd;
    if (n >= 2 && s.sd > 1e-12) {
        p.t = s.mean / (s.sd / std::sqrt(static_cast<double>(n)));
        p.p = student_p_two_sided(p.t, n - 1.0);
    }
    return p;
}

Welch welch(const std::vector<double>& a, const std::vector<double>& b) {
    Welch w;
    Summary sa = summarize(a), sb = summarize(b);
    w.na = sa.n; w.nb = sb.n;
    w.mean_a = sa.mean; w.mean_b = sb.mean;
    w.diff = sb.mean - sa.mean;
    if (sa.n < 2 || sb.n < 2) return w;
    double va = sa.sd * sa.sd / sa.n, vb = sb.sd * sb.sd / sb.n;
    if (va + vb <= 1e-15) return w;
    w.t = w.diff / std::sqrt(va + vb);
    w.df = (va + vb) * (va + vb) / (va * va / (sa.n - 1) + vb * vb / (sb.n - 1));
    w.p = student_p_two_sided(w.t, w.df);
    return w;
}

}
