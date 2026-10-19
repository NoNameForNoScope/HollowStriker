#ifndef BODYCAM_CHARACTER_PROFILE_HPP
#define BODYCAM_CHARACTER_PROFILE_HPP

// Profilul unui personaj (assets/config/characters.ini): aspectul (piele, uniforma,
// echipament, proportii) si echipamentul (arma + atasamente, arma secundara, abilitate).
// Acelasi schelet si acelasi sistem de arme pentru jucator, coechipieri si boti; profilele
// difera doar prin date.
#include <string>
#include <vector>

namespace bodycam {

struct ProfileColor { float r = 0.5f, g = 0.5f, b = 0.5f; };

enum class HelmetType { None = 0, Ballistic, Cap, Beanie };
enum class VestType { None = 0, PlateCarrier, ChestRig };
enum class GloveType { None = 0, Full, Fingerless };

struct CharacterProfile {
    std::string key;
    std::string label;
    // --- aspect ---
    ProfileColor skin{0.62f, 0.47f, 0.37f};
    ProfileColor top{0.10f, 0.11f, 0.12f};        // bluza / maneci
    ProfileColor pants{0.08f, 0.09f, 0.10f};
    ProfileColor vest_color{0.09f, 0.10f, 0.08f};
    ProfileColor helmet_color{0.07f, 0.08f, 0.07f};
    ProfileColor glove_color{0.085f, 0.07f, 0.052f};
    ProfileColor boots{0.03f, 0.028f, 0.025f};
    ProfileColor backpack_color{0.08f, 0.09f, 0.07f};
    HelmetType helmet = HelmetType::Ballistic;
    VestType vest = VestType::PlateCarrier;
    GloveType gloves = GloveType::Full;
    bool backpack = false;
    bool watch = false;
    bool beard = false;
    bool glasses = false;
    double height = 1.0;                          // scala inaltimii
    double build = 1.0;                           // scala latimii (umeri, trunchi)
    // --- echipament ---
    std::string weapon = "mk18";
    std::vector<std::string> attachments;
    std::string sidearm;
    double skill = 1.0;                           // multiplicator al erorii de ochire (mai mic = mai bun)
};

// Incarca profilele (o sectiune = un profil). Intoarce cel putin un profil implicit.
std::vector<CharacterProfile> load_character_profiles(const std::string& path);
int find_profile(const std::vector<CharacterProfile>& v, const std::string& key);

}
#endif
