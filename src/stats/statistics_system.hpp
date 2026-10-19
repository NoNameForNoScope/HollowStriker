#ifndef BODYCAM_STATISTICS_SYSTEM_HPP
#define BODYCAM_STATISTICS_SYSTEM_HPP

// Sistemul central de statistici. Colecteaza date DOAR din simulare:
//   - evenimentele emise de sisteme (SHOT, HIT, KILL, RELOAD_*, DETECT, NEAR_MISS,
//     TARGET_HIT, RANGE, ROUND_END ...) -> inregistrari discrete (focuri, daune, reincarcari)
//   - starea corpului jucatorului, esantionata la interval fix (puls, stres, stamina,
//     oboseala...) -> serii de timp
// La sfarsitul rundei / meciului / sesiunii de poligon datele sunt agregate si salvate
// (DataStorage). Interfata (tableta) doar citeste ce s-a colectat.
#include "records.hpp"
#include "data_storage.hpp"
#include "../core/events.hpp"
#include "../sim/game_state.hpp"
#include <map>
#include <string>
#include <vector>

namespace bodycam {

class World;

// assets/config/stats.ini
struct StatsConfig {
    double sample_interval = 0.5;        // s intre esantioanele fiziologice
    double engagement_timeout = 5.0;     // s fara activitate de lupta => angajamentul s-a incheiat
    double under_fire_window = 1.5;      // s dupa un glont pe langa / lovitura
    double aim_cone = 0.35;              // rad: inamicul vizat la foc (distanta focurilor ratate)
    double assist_window = 15.0;         // s: dauna data inainte ca altcineva sa ucida tinta
    double sprint_memory = 5.0;          // s: "a sprintat recent" la momentul focului
    int rounds_to_win = 3;
    int max_rounds = 7;
    int score_kill = 100, score_assist = 50, score_headshot = 25, score_round = 150, score_objective = 200;
    double score_damage = 1.0;           // puncte pe punct de dauna
    bool load(const std::string& path);
};

class StatisticsSystem {
public:
    StatsConfig cfg;
    DataStorage storage;

    // --- istoricul (fisierele din data/) ---
    std::vector<MatchRecord> matches;
    std::vector<RoundRecord> rounds;
    std::vector<ShotRecord> shots;
    std::vector<ReloadRecord> reloads;
    std::vector<DamageRecord> damage;
    std::vector<EngagementRecord> engagements;
    std::vector<RangeSessionRecord> range_sessions;
    std::vector<ExperimentTrial> trials;
    uint32_t version = 0;                // creste la orice modificare (cache-urile interfetei)

    void init(const std::string& data_dir, const std::string& config_path);

    // --- ciclul meciului (apelat de sesiune) ---
    void begin_match(int map_id, const std::string& map_name, GameMode mode);
    void begin_session(int map_id, const std::string& map_name, bool training);   // poligon
    void begin_round(const World& w, int local);
    void on_step(const World& w, const std::vector<Event>& evs, int local);
    void end_match(const World& w, int local, const std::string& result);
    void shutdown(const World& w, int local);   // iesire din joc: meciul neterminat => "A"
    void add_trials(const std::vector<ExperimentTrial>& t);

    // --- date live (meciul / sesiunea curenta) ---
    bool in_match() const { return match_active_; }
    bool training() const { return training_; }
    int map_id() const { return map_id_; }
    const MatchRecord& live() const { return live_; }
    const std::vector<PhysioSample>& live_samples() const { return live_samples_; }
    const std::vector<ShotRecord>& live_shots() const { return live_shots_; }
    const std::vector<RoundRecord>& live_rounds() const { return live_rounds_; }
    const std::vector<EngagementRecord>& live_engagements() const { return live_eng_; }
    const std::vector<ReloadRecord>& live_reloads() const { return live_reloads_; }
    double match_time() const { return t_; }
    int64_t next_match_id() const { return next_id_; }
    // esantioanele unui meci salvat (citite din fisier o singura data)
    const std::vector<PhysioSample>& samples_of(int64_t match_id);
    // toate focurile (istoric + meciul curent), pentru analizele de corelatie
    std::vector<ShotRecord> all_shots() const;
    std::vector<ReloadRecord> all_reloads() const;

private:
    struct Pending { ShotRecord rec; int shot = -1; };
    bool match_active_ = false;
    bool training_ = false;
    int map_id_ = 0;
    std::string map_name_;
    GameMode mode_ = GameMode::Elimination;
    double t_ = 0.0, next_sample_ = 0.0, round_start_ = 0.0;
    int64_t next_id_ = 1;
    MatchRecord live_;
    std::vector<PhysioSample> live_samples_;
    std::vector<ShotRecord> live_shots_;
    std::vector<RoundRecord> live_rounds_;
    std::vector<EngagementRecord> live_eng_;
    std::vector<ReloadRecord> live_reloads_;
    std::vector<DamageRecord> live_damage_;
    std::vector<Pending> pending_;
    std::map<int, size_t> shot_index_;         // focul -> pozitia in live_shots_
    int64_t range_session_id_ = 0;             // sesiunea de poligon in desfasurare
    std::map<int, double> damaged_;            // inamic -> ultima dauna data de jucator
    std::map<int, int> eliminated_;            // inamici scosi din lupta de jucator (incapacitati), runda curenta
    std::map<int64_t, std::vector<PhysioSample>> sample_cache_;
    int round_ = 0;
    bool round_open_ = false;
    // acumulatori (meci)
    double hr_sum_ = 0, stress_sum_ = 0, stamina_sum_ = 0, fatigue_sum_ = 0, move_pen_sum_ = 0, hand_pen_sum_ = 0;
    double speed_sum_ = 0;
    long body_steps_ = 0;
    double prev_stamina_ = -1.0, prev_fatigue_ = -1.0;
    double last_threat_ = -100.0, last_sprint_ = -100.0, last_fire_ = -100.0;
    double reload_start_ = -1.0;
    ReloadRecord reload_cur_;
    // acumulatori (runda)
    RoundRecord rnd_;
    double r_hr_sum_ = 0, r_stress_sum_ = 0;
    long r_steps_ = 0;
    // angajament
    bool eng_active_ = false;
    EngagementRecord eng_;
    double eng_last_ = 0.0, eng_detect_ = -1.0;
    std::vector<double> eng_dist_;
    double eng_hr_ = 0, eng_stress_ = 0;
    int eng_n_ = 0;

    void reset_match_acc();
    void reset_round_acc(int round);
    void finish_round(int winner, int local_faction);
    void finish_shot(Pending& p, bool training_ctx);
    void engagement_activity(double now, double dist);
    void close_engagement();
    void persist_range_shots();
    void record_range_session(const World& w);
    double aimed_distance(const World& w, int local) const;
    int context_now(const World& w) const;
    void snapshot(const World& w, int local, ShotRecord& r) const;
};

}
#endif
