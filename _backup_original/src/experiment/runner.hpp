#ifndef BODYCAM_RUNNER_HPP
#define BODYCAM_RUNNER_HPP

#include <string>
#include <vector>
#include <cstdint>

namespace bodycam {
    struct ExpConfig {
        std::string scenario_file;
        int reps = 1;
        uint64_t base_seed = 1234;
        std::string out_dir = "results";
        bool crn = true;
        bool write_events = false;
    };

    void run_experiment(const ExpConfig& config);
}
#endif
