#include "simulation.hpp"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    try {
        if (argc > 1 && std::string(argv[1]) == "--help") {
            std::cout << "resonator [steps=200] [side=100] [focusing=1] [dt=0.02]\n"
                         "Prints CSV diagnostics; launch visuals.py for interactive 3D.\n";
            return 0;
        }
        const int steps = argc > 1 ? std::stoi(argv[1]) : 200;
        const int side = argc > 2 ? std::stoi(argv[2]) : 100;
        Parameters p;
        if (argc > 3) p.focusing = std::stod(argv[3]);
        if (argc > 4) p.dt = std::stod(argv[4]);
        if (steps < 0) throw std::invalid_argument("Steps cannot be negative");
        Simulation simulation(side, 42, 1000, p);
        std::cout << "time,positive_density,negative_density,peak_density,density_rms,energy,angular_momentum,steps\n";
        auto report = [&]() {
            double stats[8];
            simulation.statistics(stats);
            for (int i = 0; i < 8; ++i) std::cout << (i ? "," : "") << std::setprecision(12) << stats[i];
            std::cout << '\n';
        };
        report();
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < steps; ++i) {
            simulation.step();
            if ((i + 1) % 20 == 0 || i + 1 == steps) report();
        }
        const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::cerr << steps << " steps in " << elapsed << " seconds\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
