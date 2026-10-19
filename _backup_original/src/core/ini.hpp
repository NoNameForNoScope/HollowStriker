#ifndef BODYCAM_INI_HPP
#define BODYCAM_INI_HPP

#include <string>
#include <map>

namespace bodycam {
class IniParser {
public:
    bool load(const std::string& filename);
    std::string get(const std::string& section, const std::string& key, const std::string& def = "") const;
private:
    std::map<std::string, std::map<std::string, std::string>> data;
};
}
#endif
