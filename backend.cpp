#include "simulation.hpp"
#include <string>

#if defined(_WIN32)
#define API extern "C" __declspec(dllexport)
#else
#define API extern "C" __attribute__((visibility("default")))
#endif

namespace {
thread_local std::string error;
}
API const char *rs_error() { return error.c_str(); }
API void *rs_create(int n, unsigned seed, double mass) {
  try {
    return new Simulation(n, seed, mass);
  } catch (const std::exception &e) {
    error = e.what();
    return nullptr;
  }
}
API void rs_destroy(void *handle) { delete static_cast<Simulation *>(handle); }
API int rs_configure(void *handle, double field_mass, double focusing,
                     double saturation, double wave_speed, double damping,
                     double dt) {
  try {
    Parameters p{field_mass, focusing, saturation, wave_speed, damping, dt};
    Simulation::validate(p);
    static_cast<Simulation *>(handle)->params = p;
    return 0;
  } catch (const std::exception &e) {
    error = e.what();
    return -1;
  }
}
API int rs_step(void *handle, int steps) {
  try {
    if (steps < 0 || steps > 100000)
      throw std::invalid_argument("Invalid step count");
    for (int i = 0; i < steps; ++i)
      static_cast<Simulation *>(handle)->step();
    return 0;
  } catch (const std::exception &e) {
    error = e.what();
    return -1;
  }
}
API void rs_reset(void *handle, unsigned seed) {
  static_cast<Simulation *>(handle)->reset(seed);
}
API void rs_read(void *handle, double *positive, double *negative) {
  const auto &s = *static_cast<Simulation *>(handle);
  for (std::size_t i = 0; i < s.count; ++i) {
    positive[i] = s.mass_scale * s.u[i] * s.u[i];
    negative[i] = s.mass_scale * s.v[i] * s.v[i];
  }
}
API void rs_stats(void *handle, double *result) {
  static_cast<Simulation *>(handle)->statistics(result);
}
