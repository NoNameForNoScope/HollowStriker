#ifndef BODYCAM_EXPERIMENTS_HPP
#define BODYCAM_EXPERIMENTS_HPP

// Experimente controlate (Simulare si Modelare). Un "tragator sintetic" (pilot automat) foloseste
// EXACT modelul jucatorului: aceeasi arma, acelasi recul, aceeasi fiziologie (tremur, puls,
// motricitate fina), aceleasi gloante si aceleasi tinte de poligon. Experimentul fixeaza toate
// conditiile, variaza UN factor pe niveluri si repeta fiecare nivel de R ori. Replicarile r ale
// tuturor nivelurilor folosesc aceeasi samanta (numere aleatoare comune - CRN), deci diferentele
// intre niveluri provin din factor, nu din zgomot.
//
// Factori: stress (nivelul stresului, mentinut), sprint (s de sprint inainte de tragere),
// hr (pulsul, mentinut), fatigue (oboseala), arm_injury (bratul ranit), distance (m),
// size (marimea tintei 0/1/2), weapon (indexul armei din lista).
#include "records.hpp"
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace bodycam {

struct ExperimentDef {
    std::string key;
    std::string name;
    std::string question;
    std::string factor;              // stress, sprint, hr, fatigue, arm_injury, distance, size, weapon
    std::string unit;
    std::vector<double> levels;
    std::vector<std::string> weapons = {"mk18"};
    std::string map_file = "maps/training.txt";
    std::string range_config = "config/range.ini";
    double distance = 25.0;
    int size = 1;
    int shots = 20;
    int reloads = 0;
    int reps = 10;
    double fire_interval = 0.45;
    double settle = 0.8;
    std::string measure = "accuracy"; // accuracy, mre, sway_rms, tremor_rms, reload_mean, hr_mean
    bool categorical() const { return factor == "weapon" || factor == "size"; }
    std::string level_label(double level) const;
};

std::vector<ExperimentDef> load_experiment_defs(const std::string& path);

// O proba (un nivel, o replicare) - simulare completa fara randare
ExperimentTrial run_trial(const ExperimentDef& def, double level, int rep, uint64_t seed, const std::string& asset_root);

// Valoarea masurii principale dintr-o proba
double trial_measure(const ExperimentTrial& t, const std::string& measure);

// Rezumatul unei rulari: pe fiecare nivel media masurii +- IC95, plus regresia masura ~ nivel
struct LevelSummary { double level = 0.0; int n = 0; double mean = 0.0, ci = 0.0, sd = 0.0; };
struct RunSummary {
    int64_t run = 0;
    std::string experiment, timestamp;
    std::vector<LevelSummary> levels;
    double slope = 0.0, r = 0.0, p = 1.0;   // regresie pe toate probele (nu doar pe medii)
    int trials = 0;
};
RunSummary summarize_run(const std::vector<ExperimentTrial>& all, int64_t run, const std::string& measure);
std::vector<int64_t> runs_of(const std::vector<ExperimentTrial>& all, const std::string& experiment);

// Ruleaza un experiment in fundal (jocul continua); rezultatele se preiau la final
class ExperimentManager {
public:
    std::vector<ExperimentDef> defs;
    ~ExperimentManager();
    bool load(const std::string& asset_root);
    bool start(int def_index, int64_t run_id, uint64_t base_seed);
    bool running() const { return running_; }
    double progress() const { return total_ > 0 ? static_cast<double>(done_) / total_ : 0.0; }
    int active_def() const { return active_; }
    // la terminare: muta probele in `out` (o singura data)
    bool fetch(std::vector<ExperimentTrial>& out);
    void cancel() { cancel_ = true; }

private:
    std::string root_;
    std::thread th_;
    std::atomic<bool> running_{false}, finished_{false}, cancel_{false};
    std::atomic<int> done_{0}, total_{0}, active_{-1};
    std::mutex mu_;
    std::vector<ExperimentTrial> results_;
};

// Ruleaza sincron toate nivelurile x replicarile (folosit si de bodycam_exp)
std::vector<ExperimentTrial> run_experiment_def(const ExperimentDef& def, int64_t run_id, uint64_t base_seed,
                                                const std::string& asset_root, std::atomic<int>* done = nullptr,
                                                const std::atomic<bool>* cancel = nullptr);

}
#endif
