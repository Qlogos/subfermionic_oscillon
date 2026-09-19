#pragma once
#include "arena.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <sstream>
#include <stdexcept>

struct Parameters {
    double attraction = 2.0;
    double crowding = 0.08;
    double rate = 1.0;
    double memory = 0.4;
    double dt = 0.01;
};

class Simulation {
public:
    const int side;
    const std::size_t count;
    const double mass_scale;
    Arena arena;
    Parameters params;
    double *positive, *negative;
    double time = 0.0;
    std::uint64_t steps = 0;
    std::size_t limited = 0;

    Simulation(int n, std::uint32_t seed, double mass, Parameters options = {})
        : side(validate_side(n)), count(std::size_t(side) * side * side),
          mass_scale(validate_mass(mass) / double(count)),
          arena(14 * count * sizeof(double) + 14 * Arena::alignment), params(options) {
        validate(params);
        positive = array(); negative = array();
        mu_p = array(); mu_n = array();
        for (int d = 0; d < 3; ++d) { flux_p[d] = array(); flux_n[d] = array(); }
        out_p = array(); out_n = array(); next_p = array(); next_n = array();
        reset(seed);
    }

    static void validate(const Parameters& p) {
        if (!std::isfinite(p.attraction) || p.attraction < 0 || p.attraction > 10 ||
            !std::isfinite(p.crowding) || p.crowding < 0 || p.crowding > 10 ||
            !std::isfinite(p.rate) || p.rate < 0 || p.rate > 10 ||
            !std::isfinite(p.memory) || p.memory < 0 || p.memory > 10 ||
            !std::isfinite(p.dt) || p.dt <= 0 || p.dt > 0.1)
            throw std::invalid_argument("Invalid parameters: a, b, k, tau in [0,10], dt in (0,0.1]");
    }

    void reset(std::uint32_t seed) {
        std::mt19937 random(seed);
        double total_p = 0, total_n = 0;
        for (std::size_t i = 0; i < count; ++i) {
            const double weight = 0.5 + double(random()) / double(random.max());
            // Guarantee at least one of each even for the smallest test box.
            const bool is_positive = i == 0 || (i != 1 && (random() & 1));
            positive[i] = is_positive ? weight : 0;
            negative[i] = is_positive ? 0 : weight;
            total_p += positive[i]; total_n += negative[i];
        }
        for (std::size_t i = 0; i < count; ++i) {
            positive[i] *= double(count) / (2 * total_p);
            negative[i] *= double(count) / (2 * total_n);
        }
        for (int d = 0; d < 3; ++d) {
            std::fill_n(flux_p[d], count, 0.0);
            std::fill_n(flux_n[d], count, 0.0);
        }
        time = 0; steps = 0; limited = 0;
    }

    void step() {
        validate(params);
        const auto& p = params;
        // Exact exponential relaxation for a target held fixed during one step.
        const double blend = p.memory == 0 ? 1.0 : -std::expm1(-p.dt / p.memory);
        for (std::size_t i = 0; i < count; ++i) {
            const double r = positive[i] + negative[i];
            const double pressure = p.crowding * r * r;
            mu_p[i] = positive[i] - p.attraction * negative[i] + pressure;
            mu_n[i] = negative[i] - p.attraction * positive[i] + pressure;
            out_p[i] = out_n[i] = 0;
        }
        // Exactly one stored current per internal face and substance. Walls have none.
        faces([&](std::size_t i, std::size_t j, int d) {
            relax(i, j, positive, mu_p, flux_p[d][i], out_p, blend);
            relax(i, j, negative, mu_n, flux_n[d][i], out_n, blend);
        });
        limited = 0;
        for (std::size_t i = 0; i < count; ++i) {
            // Retain a rounding margin when the donor would otherwise be exhausted.
            // Limit all outgoing faces together, never independently per face.
            // Also retain a normal-range residue: repeated emptying must not let
            // subnormal rounding turn a tiny positive donor into a negative one.
            const double floor = 16 * std::numeric_limits<double>::min();
            const double available_p = std::max(0.0, positive[i] - std::max(positive[i] * 1e-12, floor));
            const double available_n = std::max(0.0, negative[i] - std::max(negative[i] * 1e-12, floor));
            if (out_p[i] > available_p) ++limited;
            if (out_n[i] > available_n) ++limited;
            out_p[i] = out_p[i] > available_p ? available_p / out_p[i] : 1;
            out_n[i] = out_n[i] > available_n ? available_n / out_n[i] : 1;
            next_p[i] = positive[i]; next_n[i] = negative[i];
        }
        faces([&](std::size_t i, std::size_t j, int d) {
            transfer(i, j, flux_p[d][i], out_p, next_p);
            transfer(i, j, flux_n[d][i], out_n, next_n);
        });
        for (std::size_t i = 0; i < count; ++i) {
            if (!std::isfinite(next_p[i]) || !std::isfinite(next_n[i]) ||
                next_p[i] < 0 || next_n[i] < 0) {
                std::ostringstream message;
                message << "Invalid state at step " << steps << ", cell " << i
                        << ": p=" << next_p[i] << ", n=" << next_n[i]
                        << "; reset and reduce dt/rate/attraction";
                throw std::runtime_error(message.str());
            }
        }
        std::swap(positive, next_p); std::swap(negative, next_n);
        time += p.dt; ++steps;
    }

    // Physical units: time, positive mass, negative mass, peak density,
    // RMS change from mean density, limited donor fraction, steps, arena MiB.
    void statistics(double* result) const {
        double sp = 0, sn = 0, peak = 0, variance = 0;
        for (std::size_t i = 0; i < count; ++i) {
            sp += positive[i]; sn += negative[i];
            const double r = positive[i] + negative[i];
            peak = std::max(peak, r);
            variance += (r - 1) * (r - 1);
        }
        result[0] = time; result[1] = sp * mass_scale; result[2] = sn * mass_scale;
        result[3] = peak * mass_scale;
        result[4] = std::sqrt(variance / double(count)) * mass_scale;
        result[5] = double(limited) / (2 * double(count));
        result[6] = double(steps); result[7] = double(arena.used_bytes()) / (1024 * 1024);
    }

private:
    double *mu_p, *mu_n, *flux_p[3], *flux_n[3], *out_p, *out_n, *next_p, *next_n;
    double* array() { return arena.allocate<double>(count); }
    static int validate_side(int n) {
        if (n < 2 || n > 160) throw std::invalid_argument("Box side must be in [2,160]");
        return n;
    }
    static double validate_mass(double mass) {
        if (!std::isfinite(mass) || mass <= 0) throw std::invalid_argument("Mass must be positive and finite");
        return mass;
    }
    template<class F> void faces(F&& f) {
        const auto n = std::size_t(side);
        for (std::size_t z = 0; z < n; ++z)
            for (std::size_t y = 0; y < n; ++y)
                for (std::size_t x = 0, i = (z * n + y) * n; x < n; ++x, ++i) {
                    if (x + 1 < n) f(i, i + 1, 0);
                    if (y + 1 < n) f(i, i + n, 1);
                    if (z + 1 < n) f(i, i + n * n, 2);
                }
    }
    void relax(std::size_t i, std::size_t j, const double* value,
               const double* mu, double& flux, double* outgoing, double blend) {
        const double difference = mu[i] - mu[j];
        const double target = params.rate * difference * (difference >= 0 ? value[i] : value[j]);
        flux += blend * (target - flux);
        if (!std::isfinite(flux)) throw std::runtime_error("Flow overflow; reset with smaller parameters");
        outgoing[flux >= 0 ? i : j] += params.dt * std::abs(flux);
    }
    void transfer(std::size_t i, std::size_t j, double& flux,
                  const double* scale, double* next) {
        flux *= scale[flux >= 0 ? i : j];
        const double amount = params.dt * flux;
        next[i] -= amount; next[j] += amount;
    }
};
