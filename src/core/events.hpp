#ifndef BODYCAM_EVENTS_HPP
#define BODYCAM_EVENTS_HPP

#include <vector>
#include <string>
#include <cstdint>

namespace bodycam {

// Valorile numerice sunt folosite in CSV si in analize (SHOT = 4 etc.):
// tipurile noi se adauga DOAR la final.
enum class EventType {
    SPAWN, DETECT, LOSE_TRACK, STATE_CHANGE, SHOT, HIT, KILL,
    RELOAD_START, RELOAD_END, SOUND_HEARD, OBJECTIVE_REACHED, TIMEOUT,
    // --- prezentare / gameplay extins ---
    FOOTSTEP,        // a=agent, x,y, v1=suprafata, v2=intensitate
    IMPACT,          // a=tragator, b=agent lovit sau -1, x,y,z, v1=suprafata, v2=1 iesire, v3=1 ricoseu
    RELOAD_STAGE,    // a=agent, v1=etapa
    DRY_FIRE,        // a=agent
    LAND,            // a=agent, v1=viteza de impact
    JUMP,            // a=agent
    STUMBLE,         // a=agent, v1=forta, v2=StanceState
    WEAPON_SWITCH,   // a=agent, v1=slot
    GRENADE_THROW,   // a=agent, b=id grenada
    EXPLOSION,       // a=proprietar, x,y,z
    DOOR,            // x,y = celula, v1 = deschis (1) / inchis (0)
    PAWN_DEPLOY,     // a=agent, b=id pawn, v1=tip (1 drona, 2 vehicul), v2=1 posedat / 0 eliberat
    PAWN_DESTROYED,  // b=id pawn, v1=tip
    PROP_HIT,        // b=id obiect, x,y, v1=viteza
    ROUND_START,     // v1=runda
    ROUND_END,       // v1=castigator (0 Blue, 1 Red, 2 egal)
    GRENADE_BOUNCE,  // b=id grenada, x,y,z, v1=viteza
    CAPTURE,         // v1=progres
    NEAR_MISS,       // a=agentul langa care a trecut glontul, b=tragator, x,y, v1=distanta
    MALFUNCTION,     // a=agent (blocaj)
    DOWNED,          // b=agent cazut (incapacitat)
    REVIVE,          // a=salvator, b=salvat
    BANDAGE,         // a=agent
    DOOR_LOCKED,     // a=agent, x,y celula
    DOOR_KICK,       // a=agent, x,y celula, v1=1 daca a cedat
    LIGHT_SWITCH,    // a=agent, x,y, v1=stare
    POWER,           // a=agent, v1=curent (1) / avarie (0)
    TERMINAL,        // a=agent, v1=progres (1 = complet)
    PICKUP,          // a=agent, v1=tip
    MARKER,          // a=agent, x,y, v1=tip
    MAG_DROP,        // a=agent, x,y, v1=cartuse ramase in incarcator
    RELOAD_FUMBLE,   // a=agent
    WINDOW_BREAK,    // x,y celula
    INTERRUPT,       // a=agent, v1=ce s-a intrerupt (0 reincarcare, 1 interactiune, 2 bandaj)
    MANTLE,          // a=agent, x,y start, v1=durata
    GASP,            // a=agent (a cedat dupa respiratia tinuta)
    TABLET,          // a=agent, v1=TabletState noua, v2=TabletReason
    MELEE,           // a=agent, b=lovit (-1 ratat), x,y, v1=dauna
    GADGET,          // a=agent, b=id pawn, v1=tip (1 drona, 2 RC), v2=actiune (GadgetAction)
    LOADOUT,         // a=agent, v1=1 aplicat acum / 0 la runda urmatoare
    PIN_PULL,        // a=agent, v1=tip aruncabil
    // --- poligon / statistici ---
    TARGET_HIT,      // a=tragator, b=tinta, x,y,z impact, v1=zona (0 cap, 1 corp), v2=distanta (m), v3=abaterea fata de centru (m), shot
    TARGET_UP,       // b=tinta, x,y, v1=distanta (m) - tinta a aparut (timp de reactie)
    RANGE,           // a=agent, v1=RangeMode, v2=faza (RangePhase), v3=valoare (ex. numarul probei)
    FIRE_MODE,       // a=agent, v1=FireMode nou
    BOLT_CYCLE       // a=agent, v1=1 inceput (inchizatorul tras) / 2 sfarsit (cartus nou pe teava)
};

// Actiuni raportate prin evenimentul GADGET
enum class GadgetAction { Deploy = 1, Control = 2, Release = 3, Recall = 4, Retrieved = 5, Detonate = 6, LowBattery = 7, Denied = 8 };

struct Event {
    int rep = 0;
    double t = 0.0;
    EventType type = EventType::SPAWN;
    int a = -1;
    int b = -1;
    double x = 0.0;
    double y = 0.0;
    double v1 = 0.0;
    double v2 = 0.0;
    double z = 0.0;
    double v3 = 0.0;
    double nx = 0.0;   // normala suprafetei (IMPACT)
    double ny = 0.0;
    int shot = -1;     // al catelea foc al tragatorului (SHOT, HIT, KILL, TARGET_HIT): leaga lovitura de foc
    int weapon = -1;   // arma (index in World::weapons); -2 corp la corp, -3 explozie
};

class EventLog {
public:
    void add(const Event& e) { events.push_back(e); }
    const std::vector<Event>& get_all() const { return events; }
    void clear() { events.clear(); }
    size_t size() const { return events.size(); }
private:
    std::vector<Event> events;
};

} // namespace bodycam
#endif
