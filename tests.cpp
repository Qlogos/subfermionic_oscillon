#include "simulation.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void check_finite(const Simulation& simulation) {
    for (std::size_t i = 0; i < simulation.count; ++i)
        require(std::isfinite(simulation.u[i]) && std::isfinite(simulation.v[i]) &&
                std::isfinite(simulation.du[i]) && std::isfinite(simulation.dv[i]),
                "Field state must remain finite");
}

int main() {
    try {
        Simulation simulation(12, 42, 1000);
        require(reinterpret_cast<std::uintptr_t>(simulation.u) % 64 == 0, "u is aligned");
        require(reinterpret_cast<std::uintptr_t>(simulation.v) % 64 == 0, "v is aligned");
        require(simulation.mass_scale > 0, "Density scale is positive");

        double initial[8];
        simulation.statistics(initial);
        require(std::abs(initial[1] + initial[2] - 1000) < 1e-8, "Initial density is normalized");
        require(std::abs(initial[6]) > 1e-4, "Quadrature seed carries internal angular momentum");
        const double initial_energy = initial[5];
        for (int i = 0; i < 1000; ++i) simulation.step();
        double after[8];
        simulation.statistics(after);
        check_finite(simulation);
        require(std::abs(after[0] - 20.0) < 1e-10, "Time advances by dt");
        require(std::isfinite(after[5]) && after[5] > 0, "Energy remains finite and positive");
        require(std::abs(after[5] - initial_energy) / initial_energy < 1e-2, "Undamped field energy is stable");

        Simulation repeat_a(12, 42, 1000);
        Simulation repeat_b(12, 42, 1000);
        for (int i = 0; i < 50; ++i) { repeat_a.step(); repeat_b.step(); }
        for (std::size_t i = 0; i < repeat_a.count; ++i)
            require(repeat_a.u[i] == repeat_b.u[i] && repeat_a.v[i] == repeat_b.v[i], "Seed is deterministic");

        simulation.reset(42);
        double reset_stats[8];
        simulation.statistics(reset_stats);
        require(simulation.steps == 0 && simulation.time == 0, "Reset clears time");
        require(std::abs(reset_stats[1] + reset_stats[2] - 1000) < 1e-8, "Reset preserves normalization");
        std::cout << "PASS: real-field normalization, alignment, finite evolution, energy stability, angular seed, deterministic reset\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
