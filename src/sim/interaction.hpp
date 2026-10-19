#ifndef BODYCAM_INTERACTION_HPP
#define BODYCAM_INTERACTION_HPP

#include "fsm.hpp"

namespace bodycam {

// Cadru generic de interactiuni (echivalentul IInteractable): fiecare obiect
// declara tipul, pozitia, daca e instant sau "tinut apasat" si starea lui.
enum class InteractKind { Door = 0, LightSwitch, Breaker, Terminal, Cabinet, Pickup, Revive };
enum class PickupKind { Magazine = 0, Bandage, Grenade, Weapon };

struct Interactable {
    int id = 0;
    InteractKind kind = InteractKind::Door;
    double x = 0.0, y = 0.0, z = 1.0;
    int cx = -1, cy = -1;           // celula (usi)
    bool enabled = true;
    double hold_time = 0.0;         // 0 = instant; altfel secunde tinut apasat
    int state = 0;                  // intrerupator pornit, terminal descarcat, dulap cautat
    int room = -1;                  // incaperea controlata de intrerupator
    int lock_hp = 0;                // lovituri necesare pentru o usa incuiata
    PickupKind pickup = PickupKind::Magazine;
    int weapon = -1;                // index de arma (incarcator / arma)
    int rounds = 0;
    int target_agent = -1;          // resuscitare
    double progress = 0.0;          // progres comun (terminal)
};

// Marcaj tactic plasat de pe tableta, vizibil doar echipei
struct TacMarker {
    int id = 0;
    double x = 0.0, y = 0.0;
    int kind = 0;                   // 0 marcaj, 1 ping (expira), 2 pericol
    Faction faction = Faction::Blue;
    int owner = -1;
    double time = 0.0;
};

const char* interact_verb(InteractKind k);

}
#endif
