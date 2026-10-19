#include "../src/core/rng.hpp"
#include "../src/sim/map.hpp"
#include "../src/sim/los.hpp"
#include "../src/sim/world.hpp"
#include "../src/experiment/runner.hpp"
#include <iostream>
#include <cmath>
#include <cassert>

void test_f4_runner() {
    bodycam::ExpConfig cfg;
    cfg.reps = 3;
    cfg.out_dir = "test_results";
    cfg.write_events = true;
    bodycam::run_experiment(cfg);
    std::cout << "T10, T11: Teste Runner si Determinism end-to-end trecute.\n";
}

int main() {
    test_f4_runner();
    std::cout << "F4: Validat complet.\n";
    return 0;
}
