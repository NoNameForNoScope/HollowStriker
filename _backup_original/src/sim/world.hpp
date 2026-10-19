#ifndef BODYCAM_WORLD_HPP
#define BODYCAM_WORLD_HPP

#include "map.hpp"
#include "params.hpp"
#include "agent.hpp"
#include "../core/events.hpp"
#include <vector>
#include <cstdint>

namespace bodycam {
    class World {
    public:
        Map map;
        Params params;
        std::vector<Agent> agents;
        EventLog event_log;
        uint64_t seed = 0;
        uint64_t step_count = 0;
        double current_time = 0.0;
        double dt = 1.0 / 60.0;

        World(uint64_t s) : seed(s) {}
        void step(const AgentControl* humanOverride = nullptr);
        bool finished() const;
    };
}
#endif
