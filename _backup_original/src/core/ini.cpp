#include "ini.hpp"
#include <fstream>
#include <sstream>

namespace bodycam {
bool IniParser::load(const std::string& filename) {
    std::ifstream in(filename);
    if (!in) return false;
    std::string line, current_section;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line[0] == '[') {
            auto pos = line.find(']');
            if (pos != std::string::npos) current_section = line.substr(1, pos - 1);
            continue;
        }
        auto pos = line.find('=');
        if (pos != std::string::npos) {
            std::string k = line.substr(0, pos);
            std::string v = line.substr(pos + 1);
            k.erase(k.find_last_not_of(" \t\r\n") + 1);
            v.erase(0, v.find_first_not_of(" \t\r\n"));
            data[current_section][k] = v;
        }
    }
    return true;
}

std::string IniParser::get(const std::string& section, const std::string& key, const std::string& def) const {
    auto sec_it = data.find(section);
    if (sec_it != data.end()) {
        auto key_it = sec_it->second.find(key);
        if (key_it != sec_it->second.end()) return key_it->second;
    }
    return def;
}
}
