#ifndef BODYCAM_CSV_HPP
#define BODYCAM_CSV_HPP

#include "events.hpp"
#include <string>

namespace bodycam {
    void write_events_csv(const std::string& filename, const EventLog& log);
}
#endif
