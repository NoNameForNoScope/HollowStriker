#ifndef BODYCAM_LOADOUT_HPP
#define BODYCAM_LOADOUT_HPP

// Echipamentul unui operator (ales de pe tableta). Nu e un al doilea inventar:
// World::apply_loadout il transforma in sloturile de arma, incarcatoarele si
// inventarul real al agentului (Agent::slots / Agent::inv).
#include <string>
#include <vector>

namespace bodycam {

enum class MeleeKind { Buttstock = 0, Knife = 1 };
enum class GrenadeType { Frag = 0, Flash = 1, Smoke = 2 };
constexpr int kMaxThrowables = 3;

struct Loadout {
    std::string primary = "mk18";
    std::vector<std::string> primary_att = {"reddot", "light", "foregrip"};
    std::string secondary = "g17";
    std::vector<std::string> secondary_att = {"light"};
    MeleeKind melee = MeleeKind::Buttstock;
    int frags = 1, flashbangs = 1, smokes = 0;     // maxim kMaxThrowables in total
    bool drone = true;
    bool rc = true;
    bool rc_charge = false;                         // incarcatura exploziva pe masina RC
    bool valid = false;

    int throwables() const { return frags + flashbangs + smokes; }
};

}
#endif
