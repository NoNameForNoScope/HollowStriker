#ifndef BODYCAM_GAME_STATE_HPP
#define BODYCAM_GAME_STATE_HPP

namespace bodycam {

enum class GameMode { Elimination = 0, Objective = 1, Training = 2 };
enum class MatchPhase { Warmup = 0, Live = 1, RoundEnd = 2 };

// Starea meciului (echivalentul AGameState): runda, timp, obiectiv, scor.
// Separata de starea agentilor si a armelor.
struct GameState {
    GameMode mode = GameMode::Elimination;
    MatchPhase phase = MatchPhase::Live;
    int round = 1;
    double round_time = 0.0;
    double round_time_limit = 0.0;   // 0 = fara limita
    double phase_timer = 0.0;
    int score_blue = 0;
    int score_red = 0;
    int winner = -1;                 // 0 Blue, 1 Red, 2 egal
    bool has_objective = false;
    double obj_x = 0.0, obj_y = 0.0;
    double capture_progress = 0.0;   // 0..1
    bool objective_reached = false;
    bool restart_requested = false;
    bool auto_round_end = false;     // jocul interactiv gestioneaza rundele
};

}
#endif
