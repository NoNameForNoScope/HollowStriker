#ifndef BODYCAM_EVENTS_HPP
#define BODYCAM_EVENTS_HPP

#include <vector>
#include <string>

namespace bodycam {

enum class EventType {
    SPAWN, DETECT, LOSE_TRACK, STATE_CHANGE, SHOT, HIT, KILL,
    RELOAD_START, RELOAD_END, SOUND_HEARD, OBJECTIVE_REACHED, TIMEOUT
};

struct Event {
    int rep;
    double t;
    EventType type;
    int a;
    int b;
    double x;
    double y;
    double v1;
    double v2;
};

class EventLog {
public:
    void add(const Event& e) { events.push_back(e); }
    const std::vector<Event>& get_all() const { return events; }
    void clear() { events.clear(); }
private:
    std::vector<Event> events;
};

} // namespace bodycam
#endif
