#ifndef BODYCAM_INI_HPP
#define BODYCAM_INI_HPP

#include <string>
#include <map>
#include <vector>

namespace bodycam {
class IniParser {
public:
    bool load(const std::string& filename);
    std::string get(const std::string& section, const std::string& key, const std::string& def = "") const;
    double get_d(const std::string& section, const std::string& key, double def) const;
    // sectiunile in ordinea din fisier
    const std::vector<std::string>& sections() const { return order; }
private:
    std::map<std::string, std::map<std::string, std::string>> data;
    std::vector<std::string> order;
};
}
#endif
