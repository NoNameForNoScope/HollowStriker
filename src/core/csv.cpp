#include "csv.hpp"
#include <fstream>

namespace bodycam {
void write_events_csv(const std::string& filename, const EventLog& log) {
    std::ofstream out(filename);
    if (!out) return;
    out << "rep,t,type,a,b,x,y,v1,v2\n";
    for (const auto& e : log.get_all()) {
        out << e.rep << "," << e.t << "," << static_cast<int>(e.type) << ","
            << e.a << "," << e.b << "," << e.x << "," << e.y << ","
            << e.v1 << "," << e.v2 << "\n";
    }
}
}
