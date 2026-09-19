#include "simulation.hpp"
#include <iostream>

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void check_mass(const Simulation& s) {
    double stats[8]; s.statistics(stats);
    require(std::abs(stats[1] - 500) < 1e-8 && std::abs(stats[2] - 500) < 1e-8,
            "Each substance must remain conserved");
    for (std::size_t i = 0; i < s.count; ++i)
        require(std::isfinite(s.positive[i]) && std::isfinite(s.negative[i]) &&
                s.positive[i] >= 0 && s.negative[i] >= 0, "Nonnegative finite state");
}
int main() {
    try {
        Simulation s(10, 42, 1000);
        check_mass(s);
        require(reinterpret_cast<std::uintptr_t>(s.positive) % 64 == 0, "Aligned positive array");
        require(reinterpret_cast<std::uintptr_t>(s.negative) % 64 == 0, "Aligned negative array");
        for (std::size_t i = 0; i < s.count; ++i)
            require(s.positive[i] == 0 || s.negative[i] == 0, "Initially one sign per cube");
        for (int i = 0; i < 1000; ++i) s.step();
        check_mass(s);
        s.params = {10, 1, 10, 0.05, 0.1};
        bool limited = false;
        for (int i = 0; i < 200; ++i) { s.step(); limited |= s.limited > 0; }
        require(limited, "Stress test must exercise the donor limiter");
        check_mass(s);

        Simulation uniform(4, 42, 1000);
        std::fill_n(uniform.positive, uniform.count, 0.5);
        std::fill_n(uniform.negative, uniform.count, 0.5);
        for (int i = 0; i < 20; ++i) uniform.step();
        for (std::size_t i = 0; i < uniform.count; ++i)
            require(uniform.positive[i] == 0.5 && uniform.negative[i] == 0.5, "Uniform equilibrium at walls");

        Simulation inertia(4, 42, 1000);
        inertia.params.dt = 0.001;
        const double initial = inertia.positive[0];
        inertia.step();
        const double after = inertia.positive[0];
        inertia.params.rate = 0;
        inertia.step();
        const double ratio = (inertia.positive[0] - after) / (after - initial);
        require(std::abs(ratio - std::exp(-0.001 / 0.4)) < 1e-8, "Flux memory decays exponentially after drive stops");
        inertia.params.memory = 0;
        const double before = inertia.positive[0];
        inertia.step();
        require(inertia.positive[0] == before, "Zero memory and zero drive stop transfer");
        inertia.reset(42);
        check_mass(inertia);
        require(inertia.steps == 0 && inertia.time == 0, "Reset clears the clock");
        std::cout << "PASS: conservation, positivity, limiter, alignment, closed-wall equilibrium, memory decay, reset\n";
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
