#include "character_profile.hpp"
#include "../core/ini.hpp"
#include <sstream>

namespace bodycam {

namespace {
ProfileColor color(const IniParser& ini, const std::string& sec, const char* key, ProfileColor def) {
    std::string v = ini.get(sec, key, "");
    if (v.empty()) return def;
    std::stringstream ss(v);
    ProfileColor c = def;
    ss >> c.r >> c.g >> c.b;
    return c;
}
bool flag(const IniParser& ini, const std::string& sec, const char* key, bool def) {
    std::string v = ini.get(sec, key, "");
    if (v.empty()) return def;
    return v == "true" || v == "1" || v == "yes" || v == "da";
}
std::vector<std::string> list(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        tok.erase(0, tok.find_first_not_of(" \t"));
        tok.erase(tok.find_last_not_of(" \t\r") + 1);
        if (!tok.empty()) out.push_back(tok);
    }
    return out;
}
}

std::vector<CharacterProfile> load_character_profiles(const std::string& path) {
    std::vector<CharacterProfile> out;
    IniParser ini;
    if (ini.load(path)) {
        for (const auto& sec : ini.sections()) {
            CharacterProfile p;
            p.key = sec;
            p.label = ini.get(sec, "label", sec);
            p.skin = color(ini, sec, "skin", p.skin);
            p.top = color(ini, sec, "top", p.top);
            p.pants = color(ini, sec, "pants", p.pants);
            p.vest_color = color(ini, sec, "vest_color", p.vest_color);
            p.helmet_color = color(ini, sec, "helmet_color", p.helmet_color);
            p.glove_color = color(ini, sec, "glove_color", p.glove_color);
            p.boots = color(ini, sec, "boots", p.boots);
            p.backpack_color = color(ini, sec, "backpack_color", p.backpack_color);
            std::string h = ini.get(sec, "helmet", "ballistic");
            p.helmet = h == "none" ? HelmetType::None : h == "cap" ? HelmetType::Cap : h == "beanie" ? HelmetType::Beanie : HelmetType::Ballistic;
            std::string v = ini.get(sec, "vest", "plate");
            p.vest = v == "none" ? VestType::None : v == "rig" ? VestType::ChestRig : VestType::PlateCarrier;
            std::string g = ini.get(sec, "gloves", "full");
            p.gloves = g == "none" ? GloveType::None : g == "fingerless" ? GloveType::Fingerless : GloveType::Full;
            p.backpack = flag(ini, sec, "backpack", p.backpack);
            p.watch = flag(ini, sec, "watch", p.watch);
            p.beard = flag(ini, sec, "beard", p.beard);
            p.glasses = flag(ini, sec, "glasses", p.glasses);
            p.height = ini.get_d(sec, "height", p.height);
            p.build = ini.get_d(sec, "build", p.build);
            p.weapon = ini.get(sec, "weapon", p.weapon);
            p.attachments = list(ini.get(sec, "attachments", ""));
            p.sidearm = ini.get(sec, "sidearm", "");
            p.skill = ini.get_d(sec, "skill", p.skill);
            out.push_back(p);
        }
    }
    if (out.empty()) {
        CharacterProfile p;
        p.key = "default";
        p.label = "OPERATOR";
        out.push_back(p);
    }
    return out;
}

int find_profile(const std::vector<CharacterProfile>& v, const std::string& key) {
    for (size_t i = 0; i < v.size(); ++i) if (v[i].key == key) return static_cast<int>(i);
    return -1;
}

}
