#pragma once

#include "arena.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <stdexcept>

// Two real amplitudes on a local cubic lattice. The shared radial potential
// binds them into one classical localized excitation; no complex numbers are
// required. The frontend receives u^2 and v^2 as the two visible densities.
struct Parameters {
    double field_mass = 1.0;
    double focusing = 1.0;
    double saturation = 0.1;
    double wave_speed = 0.5;
    double damping = 0.0;
    double dt = 0.02;
};

class Simulation {
public:
    const int side;
    const std::size_t count;
    const double target_mass;
    double mass_scale = 0.0;
    Arena arena;
    Parameters params;
    double *u, *v, *du, *dv;
    double time = 0.0;
    std::uint64_t steps = 0;

    Simulation(int n, std::uint32_t seed, double mass, Parameters options = {})
        : side(validate_side(n)), count(std::size_t(side) * side * side),
          target_mass(validate_mass(mass)),
          arena(6 * count * sizeof(double) + 6 * Arena::alignment), params(options) {
        validate(params);
        u = array(); v = array(); du = array(); dv = array();
        acc_u = array(); acc_v = array();
        reset(seed);
    }

    static void validate(const Parameters& p) {
        if (!std::isfinite(p.field_mass) || p.field_mass <= 0 || p.field_mass > 10 ||
            !std::isfinite(p.focusing) || p.focusing < 0 || p.focusing > 10 ||
            !std::isfinite(p.saturation) || p.saturation <= 0 || p.saturation > 10 ||
            !std::isfinite(p.wave_speed) || p.wave_speed < 0 || p.wave_speed > 2 ||
            !std::isfinite(p.damping) || p.damping < 0 || p.damping > 1 ||
            !std::isfinite(p.dt) || p.dt <= 0 || p.dt > 0.1)
            throw std::invalid_argument("Invalid field parameters");
        if (p.wave_speed > 0 && p.dt * p.wave_speed * std::sqrt(3.0) > 0.9)
            throw std::invalid_argument("dt is too large for the 3D wave-speed CFL limit");
    }

    void reset(std::uint32_t seed) {
        std::mt19937 random(seed);
        std::uniform_real_distribution<double> noise(-1.0, 1.0);
        const double center = 0.5 * double(side - 1);
        const double sigma = std::max(2.5, 0.08 * double(side));
        constexpr double amplitude = 1.15;
        constexpr double internal_spin = 0.78;
        double raw_density = 0.0;
        const std::size_t n = std::size_t(side);
        for (std::size_t z = 0; z < n; ++z)
            for (std::size_t y = 0; y < n; ++y)
                for (std::size_t x = 0, i = (z * n + y) * n; x < n; ++x, ++i) {
                    const double dx = double(x) - center;
                    const double dy = double(y) - center;
                    const double dz = double(z) - center;
                    const double envelope = std::exp(-(dx * dx + dy * dy + dz * dz) /
                                                      (2.0 * sigma * sigma));
                    const double jitter = 1.0 + 0.015 * noise(random);
                    u[i] = amplitude * envelope * jitter;
                    v[i] = 0.01 * amplitude * envelope * noise(random);
                    du[i] = 0.0;
                    // Quadrature velocity gives internal rotation using only real values.
                    dv[i] = internal_spin * amplitude * envelope * jitter;
                    raw_density += u[i] * u[i] + v[i] * v[i];
                }
        if (mass_scale == 0.0) mass_scale = target_mass / raw_density;
        std::fill_n(acc_u, count, 0.0);
        std::fill_n(acc_v, count, 0.0);
        time = 0.0;
        steps = 0;
    }

    void step() {
        validate(params);
        acceleration();
        const double half_dt = 0.5 * params.dt;
        for (std::size_t i = 0; i < count; ++i) {
            du[i] += half_dt * acc_u[i];
            dv[i] += half_dt * acc_v[i];
            u[i] += params.dt * du[i];
            v[i] += params.dt * dv[i];
        }
        acceleration();
        for (std::size_t i = 0; i < count; ++i) {
            du[i] += half_dt * acc_u[i];
            dv[i] += half_dt * acc_v[i];
        }
        time += params.dt;
        ++steps;
    }

    // time, positive density, negative density, peak density, density RMS,
    // field energy, internal angular momentum, steps.
    void statistics(double* result) const {
        double positive = 0.0, negative = 0.0, peak = 0.0, square_error = 0.0;
        double energy = 0.0, angular = 0.0;
        const double mean = target_mass / double(count);
        for (std::size_t i = 0; i < count; ++i) {
            const double uu = u[i] * u[i];
            const double vv = v[i] * v[i];
            const double density = mass_scale * (uu + vv);
            positive += mass_scale * uu;
            negative += mass_scale * vv;
            peak = std::max(peak, density);
            square_error += (density - mean) * (density - mean);
            energy += 0.5 * (du[i] * du[i] + dv[i] * dv[i]);
            energy += 0.5 * params.field_mass * params.field_mass * (uu + vv);
            const double rho = uu + vv;
            energy -= 0.25 * params.focusing * rho * rho;
            energy += (params.saturation / 6.0) * rho * rho * rho;
            angular += u[i] * dv[i] - v[i] * du[i];
        }
        const double c2 = params.wave_speed * params.wave_speed;
        for_each_face([&](std::size_t i, std::size_t j) {
            const double du_face = u[j] - u[i];
            const double dv_face = v[j] - v[i];
            energy += 0.5 * c2 * (du_face * du_face + dv_face * dv_face);
        });
        result[0] = time;
        result[1] = positive;
        result[2] = negative;
        result[3] = peak;
        result[4] = std::sqrt(square_error / double(count));
        result[5] = energy;
        result[6] = angular;
        result[7] = double(steps);
    }

private:
    double *acc_u, *acc_v;

    double* array() { return arena.allocate<double>(count); }
    static int validate_side(int n) {
        if (n < 2 || n > 160) throw std::invalid_argument("Box side must be in [2,160]");
        return n;
    }
    static double validate_mass(double mass) {
        if (!std::isfinite(mass) || mass <= 0) throw std::invalid_argument("Total mass must be positive");
        return mass;
    }

    template <class F>
    void for_each_face(F&& function) const {
        const std::size_t n = std::size_t(side);
        for (std::size_t z = 0; z < n; ++z)
            for (std::size_t y = 0; y < n; ++y)
                for (std::size_t x = 0, i = (z * n + y) * n; x < n; ++x, ++i) {
                    if (x + 1 < n) function(i, i + 1);
                    if (y + 1 < n) function(i, i + n);
                    if (z + 1 < n) function(i, i + n * n);
                }
    }

    void acceleration() {
        const std::size_t n = std::size_t(side);
        const double c2 = params.wave_speed * params.wave_speed;
        const double m2 = params.field_mass * params.field_mass;
        for (std::size_t z = 0; z < n; ++z)
            for (std::size_t y = 0; y < n; ++y)
                for (std::size_t x = 0, i = (z * n + y) * n; x < n; ++x, ++i) {
                    double lap_u = 0.0, lap_v = 0.0;
                    auto neighbor = [&](std::size_t j) {
                        lap_u += u[j] - u[i];
                        lap_v += v[j] - v[i];
                    };
                    if (x > 0) neighbor(i - 1);
                    if (x + 1 < n) neighbor(i + 1);
                    if (y > 0) neighbor(i - n);
                    if (y + 1 < n) neighbor(i + n);
                    if (z > 0) neighbor(i - n * n);
                    if (z + 1 < n) neighbor(i + n * n);
                    const double rho = u[i] * u[i] + v[i] * v[i];
                    const double local = m2 - params.focusing * rho +
                                         params.saturation * rho * rho;
                    acc_u[i] = c2 * lap_u - local * u[i] - params.damping * du[i];
                    acc_v[i] = c2 * lap_v - local * v[i] - params.damping * dv[i];
                }
    }
};
