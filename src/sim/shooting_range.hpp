#ifndef BODYCAM_SHOOTING_RANGE_HPP
#define BODYCAM_SHOOTING_RANGE_HPP

// Poligonul de tragere: tinte fizice (lovite de aceleasi gloante/proiectile ca agentii),
// culoare la distante fixe, tinte mobile / reactive / temporizate si modurile de test.
// Face parte din simulare (determinist, testabil fara randare); rezultatele unei sesiuni
// sunt preluate de sistemul de statistici prin evenimentul RANGE (faza Done).
#include "ballistics.hpp"
#include <string>
#include <vector>
#include <cstdint>

namespace bodycam {

class World;
struct Agent;

enum class RangeMode { Free = 0, Accuracy, Reaction, Speed, Distance, Stress, Count };
enum class RangePhase { Idle = 0, Countdown, Calm, RestShoot, Sprint, PostShoot, Running, Done };
const char* range_mode_name(RangeMode m);
const char* range_phase_name(RangePhase p);

// assets/config/range.ini
struct RangeConfig {
    double firing_line_x = 6.0;            // linia de tragere (m de la marginea hartii)
    std::vector<double> lane_y = {5.5, 9.5, 13.5, 17.5, 21.5, 25.5};
    std::vector<double> distances = {10.0, 25.0, 50.0, 100.0};
    std::vector<int> free_distance = {0, 1, 2, 3, 1, 2};   // indexul distantei ridicate pe fiecare culoar (mod liber)
    std::vector<int> moving_lanes = {4, 5};
    // silueta (marimea "M"): cap disc, corp dreptunghi
    double head_r = 0.095, head_z = 1.60;
    double body_w = 0.46, body_z0 = 0.86, body_z1 = 1.46;
    double sizes[3] = {0.65, 1.0, 1.4};    // S / M / L
    double move_speed = 1.1;               // m/s (tinte mobile)
    double move_amp = 1.4;                 // m (stanga-dreapta pe culoar)
    double raise_time = 0.3;               // s ridicare / coborare
    double reset_time = 2.5;               // s pana se ridica o tinta reactiva doborata
    int accuracy_rounds = 20;
    int reaction_trials = 10;
    double reaction_delay_min = 1.5, reaction_delay_max = 4.0;
    double reaction_expose = 3.0;
    int speed_targets = 6;
    int distance_shots = 8;
    int stress_shots = 10;
    double stress_calm_hr = 95.0;          // pulsul sub care incepe proba "in repaus"
    double stress_sprint_hr = 140.0;       // dupa sprint: pulsul minim (daca delta = 0)
    double stress_sprint_delta = 45.0;     // dupa sprint: pulsul cu cel putin atat peste media din faza de repaus
    double stress_sprint_stamina = 0.45;   // si stamina maxima (a alergat cu adevarat)
    double countdown = 3.0;
    bool load(const std::string& path);
};

struct RangeTarget {
    int id = 0;
    int lane = 0;
    int dist_idx = 0;
    double distance = 0.0;
    double x = 0.0, y = 0.0, base_y = 0.0;
    double size = 1.0;
    bool moving = false;
    double move_phase = 0.0;
    bool want_up = false;        // starea ceruta (sesiunea / modul liber)
    double raise = 0.0;          // 0 culcata .. 1 ridicata (animatie)
    double down_timer = 0.0;     // tinta reactiva: timpul pana se ridica din nou
    bool reactive = true;        // cade cand e lovita
    double appear_time = -1.0;   // cand a devenit vizibila (timp de reactie)
    double expose_until = -1.0;  // tinte temporizate
    int hits = 0, head_hits = 0;
    int last_shot = -1;          // un foc cu alice conteaza o singura data
    struct Mark { float u, v; uint8_t head; };
    std::vector<Mark> marks;     // urmele de pe tinta (u lateral, v inaltime, in m)
    bool hittable() const { return raise > 0.6; }
};

// Rezultatul unei sesiuni (calculat in simulare, salvat de sistemul de statistici)
struct RangeResult {
    RangeMode mode = RangeMode::Free;
    int weapon = -1;
    double distance = 0.0;
    int size_idx = 1;
    int shots = 0, hits = 0, head = 0, body = 0;
    double duration = 0.0;
    double mre = 0.0;                // abaterea medie a impacturilor fata de centrul corpului (m)
    double hr_mean = 0.0, hr_max = 0.0, stress_mean = 0.0;
    // reactie
    std::vector<double> reactions;   // s, de la aparitie la primul foc
    std::vector<double> hit_times;   // s, de la aparitie la lovire
    int timeouts = 0;
    // viteza
    double total_time = 0.0;
    std::vector<double> splits;
    // distanta (pe fiecare distanta din config)
    std::vector<int> shots_d, hits_d;
    std::vector<double> mre_d;
    // stres: repaus vs dupa sprint
    int shots_rest = 0, hits_rest = 0, shots_post = 0, hits_post = 0;
    double hr_rest = 0.0, hr_post = 0.0, stress_rest = 0.0, stress_post = 0.0;
    double react_rest = 0.0, react_post = 0.0, mre_rest = 0.0, mre_post = 0.0;
    double accuracy() const { return shots > 0 ? static_cast<double>(hits) / shots : 0.0; }
};

class ShootingRange {
public:
    RangeConfig cfg;
    bool enabled = false;
    std::vector<RangeTarget> targets;

    // --- sesiunea curenta ---
    RangeMode mode = RangeMode::Free;
    RangePhase phase = RangePhase::Idle;
    int shooter = -1;
    int lane = 0;
    int dist_idx = 1;
    int size_idx = 1;
    double phase_start = 0.0;
    double session_start = 0.0;
    int trial = 0;
    double next_event = 0.0;
    int phase_shots = 0;
    RangeResult result;              // ultima sesiune terminata
    uint32_t result_seq = 0;         // creste la fiecare sesiune terminata

    // Creeaza tintele (harta de poligon); fara configuratie => dezactivat
    void setup(World& w, const std::string& config_path);
    void start(World& w, RangeMode m, int agent, int dist_index, int size_index);
    void stop(World& w);
    void update(World& w);
    void on_shot(World& w, const Agent& a, int shot);
    void on_impact(World& w, const Agent& shooter, const BulletImpact& imp, int shot, int weapon);
    void add_bodies(std::vector<TargetBody>& out) const;
    // id-ul entitatii (gloante) -> indexul tintei; -1 daca nu e tinta
    static int target_index(int entity_id);
    bool session_active() const { return phase != RangePhase::Idle && phase != RangePhase::Done; }
    // pulsul care trebuie atins prin sprint (relativ la faza de repaus a sesiunii)
    double sprint_target() const {
        return cfg.stress_sprint_delta > 0.0 && cur_.hr_rest > 0.0 ? cur_.hr_rest + cfg.stress_sprint_delta : cfg.stress_sprint_hr;
    }
    // Experimente controlate: doar tinta (culoar, distanta, marime) ridicata, fixa
    int show_only(int lane_i, int dist_i, int size_i);
    // textul afisat in OSD / pe tableta pentru faza curenta
    std::string instruction(const World& w) const;

private:
    RangeResult cur_;
    double hr_sum_ = 0.0, stress_sum_ = 0.0;
    int phys_n_ = 0;
    double mre_sum_ = 0.0;
    int mre_n_ = 0;
    std::vector<double> mre_sum_d_;
    std::vector<int> mre_n_d_;
    double phase_hr_sum_ = 0.0, phase_stress_sum_ = 0.0;
    int phase_phys_n_ = 0;
    double phase_mre_sum_ = 0.0;
    int phase_mre_n_ = 0;
    int phase_hits_ = 0;
    double phase_first_shot_ = -1.0;
    int last_counted_shot_ = -1;
    int last_hit_shot_ = -1;
    int active_target_ = -1;          // reactie / precizie / distanta: tinta curenta
    double trial_appear_ = -1.0;
    bool trial_shot_ = false;
    int dist_step_ = 0;               // modul distanta: distanta curenta
    int speed_left_ = 0;

    void lower_all();
    void raise_target(World& w, int idx, double expose = -1.0);
    int find_target(int lane_i, int dist_i) const;
    void set_phase(World& w, RangePhase p);
    void finish(World& w);
    void free_mode_targets();
    void begin_shoot_phase(World& w, RangePhase p);
    void end_shoot_phase(bool rest);
};

}
#endif
