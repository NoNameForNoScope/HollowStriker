#ifndef BODYCAM_ANALYTICS_HPP
#define BODYCAM_ANALYTICS_HPP

// Analiza statistica a datelor colectate (functii pure, fara stare): statistici descriptive,
// corelatie / regresie liniara cu test de semnificatie, intervale de incredere pentru
// proportii (Wilson), grupari pe intervale, agregari pe harti / arme / victorii-infrangeri.
// Interfata tabletei doar CITESTE rezultatele acestor functii.
#include "records.hpp"
#include <string>
#include <vector>

namespace bodycam {

struct Summary {
    int n = 0;
    double mean = 0.0, sd = 0.0, min = 0.0, max = 0.0, median = 0.0;
    double ci95() const;          // semi-latimea intervalului de incredere 95% pentru medie (t Student)
};
Summary summarize(const std::vector<double>& v);

struct Regression {
    int n = 0;
    double r = 0.0;               // coeficientul de corelatie Pearson
    double slope = 0.0, intercept = 0.0, r2 = 0.0;
    double t = 0.0, p = 1.0;      // testul pantei (H0: panta = 0), bilateral
    bool valid = false;
};
Regression regress(const std::vector<double>& x, const std::vector<double>& y);

// p bilateral pentru statistica t cu df grade de libertate (functia beta incompleta)
double student_p_two_sided(double t, double df);
double student_t_quantile975(double df);

struct Proportion {
    int k = 0, n = 0;
    double p = 0.0, lo = 0.0, hi = 0.0;   // interval Wilson 95%
};
Proportion wilson(int k, int n);

struct Bin {
    double x0 = 0.0, x1 = 0.0;
    int n = 0;
    double mean_y = 0.0, ci = 0.0;        // media lui y (si semi-latimea IC 95%)
    Proportion prop;                       // pentru y binar (lovit / ratat)
};
// grupeaza perechile (x, y) pe intervalele date de margini (edges.size() - 1 intervale)
std::vector<Bin> bin_by(const std::vector<double>& x, const std::vector<double>& y, const std::vector<double>& edges,
                        bool binary);

// --- Agregari pe meciuri ---
struct MapStats {
    int map_id = 0;
    std::string name;
    int matches = 0, wins = 0, losses = 0, draws = 0;
    int kills = 0, deaths = 0, shots = 0, hits = 0;
    double win_rate = 0.0, kd = 0.0, accuracy = 0.0;
    double hr_avg = 0.0, stress_avg = 0.0, dmg_avg = 0.0, dmg_taken_avg = 0.0;
    double eng_dist_avg = 0.0, duration_avg = 0.0, kills_avg = 0.0, deaths_avg = 0.0, reaction_avg = -1.0;
};
std::vector<MapStats> per_map(const std::vector<MatchRecord>& matches);

struct OverallStats {
    int matches = 0, wins = 0, losses = 0, draws = 0, abandoned = 0;
    double win_rate = 0.0, loss_rate = 0.0, kd = 0.0, accuracy = 0.0, dmg_avg = 0.0;
    double hr_avg = 0.0, stress_avg = 0.0, duration_avg = 0.0;
    int kills = 0, deaths = 0, shots = 0, hits = 0, headshots = 0;
    int streak = 0;                        // > 0 victorii consecutive curente, < 0 infrangeri
    int best_win_streak = 0, worst_loss_streak = 0;
    // victorii vs infrangeri (medii)
    double kills_w = 0, kills_l = 0, deaths_w = 0, deaths_l = 0, acc_w = 0, acc_l = 0;
    double stress_w = 0, stress_l = 0, hr_w = 0, hr_l = 0;
};
OverallStats overall(const std::vector<MatchRecord>& matches);

struct WeaponStats {
    std::string weapon;
    int shots = 0, hits = 0, head = 0, kills = 0;
    double damage = 0.0;
    double dist_avg = 0.0;
    Proportion acc;
};
std::vector<WeaponStats> per_weapon(const std::vector<ShotRecord>& shots);

// Revenirea pulsului: dupa fiecare varf (> min_peak), cat scade in `window` secunde (BPM)
struct HrRecovery { int peaks = 0; double drop_avg = 0.0; double rate_avg = 0.0; };
HrRecovery hr_recovery(const std::vector<PhysioSample>& s, double window = 30.0, double min_peak = 105.0);

// Precizia pe intervale de timp (din focurile meciului)
std::vector<Bin> accuracy_over_time(const std::vector<ShotRecord>& shots, double bin_seconds);

// Comparatie pereche (ex. repaus vs dupa sprint): media diferentelor + t pereche
struct Paired { int n = 0; double mean_a = 0, mean_b = 0, mean_diff = 0, sd_diff = 0, t = 0, p = 1.0; };
Paired paired_compare(const std::vector<double>& a, const std::vector<double>& b);

// Comparatie a doua esantioane independente (Welch)
struct Welch { int na = 0, nb = 0; double mean_a = 0, mean_b = 0, diff = 0, t = 0, df = 0, p = 1.0; };
Welch welch(const std::vector<double>& a, const std::vector<double>& b);

}
#endif
