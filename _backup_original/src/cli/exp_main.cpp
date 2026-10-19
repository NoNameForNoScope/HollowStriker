#include "../experiment/runner.hpp"
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    bodycam::ExpConfig cfg;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--reps" && i + 1 < argc) cfg.reps = std::stoi(argv[++i]);
        else if (arg == "--seed" && i + 1 < argc) cfg.base_seed = std::stoull(argv[++i]);
        else if (arg == "--out" && i + 1 < argc) cfg.out_dir = argv[++i];
        else if (arg == "--events") cfg.write_events = true;
    }
    
    std::cout << "Pornire campanie experimentala: " << cfg.reps << " replicari...\n";
    bodycam::run_experiment(cfg);
    std::cout << "Gata. Rapoartele CSV sunt in: " << cfg.out_dir << "\n";
    return 0;
}
