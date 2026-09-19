# TOY PROJECT

## That is to say, I'm just messing around with some late-night ideas I get with

## a lot of caffeine, and _agentic assistance_

# Resonator

A toy, local simulation: C++ evolves two conserved substances on a 100 x 100 x 100
grid; Python displays their density in an interactive 3D volume. No server.

## Run

The backend is built and the local Python environment is installed on this machine.
From this folder:

```powershell
.\.venv\Scripts\python.exe visuals.py
```

Or `./run.ps1`. The viewer starts running. Space pauses, N advances one step,
R resets to the same seed (keeping current parameters), S saves `preview.png`.
Drag the volume to orbit; scroll to zoom. Close the window to exit.

Orange means positive-dominated, blue means negative-dominated, pale means mixed.
Density is positive + negative; the two never cancel. Empty cells are invisible.
The box outline shows the domain. Voxels are nearest-sampled cubes, without a
million individual mesh objects or empty-cell wireframes.

Display controls:

- **Opaque at**: total density at which a cell is fully opaque. Default 1.
  The slider is logarithmic, from 0.0001 to 10.
- **Fuzziness**: default 1; larger values reveal faint material. Opacity is
  `min(density / opaque_at, 1) ** (1 / fuzziness)`. Zero is always transparent.
- **Visible depth**: cut away upper Z layers to see inside. Does not alter physics.

Opacity describes transmission through one cell width; translucent cells accumulate
along a viewing ray. A full box can obscure its interior even when individual cells
are translucent. Use the depth control. Floating-point render data avoids rounding
the initial dilute densities down to transparent 8-bit alpha values.

The mean density is only 0.001 with a total mass of 1000. To reveal more of it:

```powershell
.\.venv\Scripts\python.exe visuals.py --opaque-at 0.1 --fuzziness 1.2
```

The other sliders change attraction, crowding, memory time and flow speed live.
The displayed limiter fraction should stay small for an accurate trajectory.
If it stays high, reduce flow speed or restart with a smaller `--dt`.

## Model

Each cube starts with one random sign and a random magnitude. Each population is
normalized to mass 500, with roughly half the cells assigned each sign. Seed 42 is
the default. Later a cube can contain both. Closed walls have no outgoing faces.

Internal values are normalized by `mass / number_of_cells`, so average total
density is 1 and parameters are independent of that scale. For normalized fields
`x`, `y`, `r=x+y`, use pressures:

```text
mu_positive = x - a*y + b*r*r
mu_negative = y - a*x + b*r*r

delta = mu[i] - mu[j]
target_flux = k * delta * (value[i] if delta >= 0 else value[j])
flux = target_flux + (previous_flux - target_flux) * exp(-dt/tau)
```

With tau=0, use the target directly. Each shared face has one signed current per
substance. Positive current goes from the lower array index to the higher one.
All targets use the old state. All transfers update the next state symmetrically.
No annihilation, sources, sinks or added noise. Memory can carry flow temporarily
against the instantaneous pressure gradient.

If a cube's six outgoing transfers exceed its available material, scale all of
them by one donor factor before applying either side of any transfer. The limited
current becomes next step's memory, so empty donors cannot build up hidden current.
A tiny rounding margin prevents negative leftovers. There is no post-update clamp
or global renormalization that would conceal lost mass.

Defaults: a=2, b=0.08, k=1, tau=0.4, dt=0.01. Time and coefficients are abstract
simulation units. This is an explicit first-order numerical experiment: positivity
and conservation do not establish time-step convergence. Compare runs at half dt
and the same simulation time before interpreting an oscillation as physical.

Attraction can produce dense mixed regions; flow memory allows overshoot.
This model does not guarantee a self-sustaining resonator or smooth macroscopic
globs. There is no external energy drive or explicit interface penalty. Persistent
motion, if observed, needs further numerical checks, and small grid-scale spots are
possible. The two-pressure construction is a toy constitutive rule.

## Files and memory

- `simulation.hpp`: local pressures, persistent face currents, donor limiter.
- `arena.hpp`: fixed-capacity 64-byte-aligned allocation.
- `backend.cpp` / `backend.py`: small C interface / ctypes bridge.
- `visuals.py`: Python/PyVista/VTK GPU volume and controls.
- `main.cpp`: optional command-line simulation with CSV diagnostics.
- `tests.cpp`: conservation, positivity, memory and equilibrium checks.

The C++ arena holds 14 contiguous double arrays: two densities, two pressures,
six face-current arrays, two donor factors and two next-state arrays. At 100 cubed,
that is about 106.8 MiB plus alignment. There are no allocations inside a simulation
step. X is the contiguous axis. Visualization adds CPU/GPU buffers. A shared library
passes fresh arrays directly to Python, replacing the original binary-file demo.
The old `results.bin` is no longer used.

## Rebuild / install on another machine

Requires Python, CMake and a C++17 compiler; GPU volume rendering requires a working
OpenGL driver. Python 3.14 and the local MinGW compiler were used here.

```powershell
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements.txt
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

With Visual Studio use a fresh build folder, omit the generator argument, and use
`cmake --build build --config Release` and `ctest --test-dir build -C Release`.
Close the viewer before rebuilding its DLL.

```powershell
# 200 steps on the full grid, print CSV diagnostics
.\build\resonator.exe 200 100
# Compare memoryless behavior
.\build\resonator.exe 200 100 0
# Offscreen preview after 100 steps
.\.venv\Scripts\python.exe visuals.py --steps 100 --save preview.png
# Small, faster box for experimenting (same total mass)
.\.venv\Scripts\python.exe visuals.py --size 40
```

Implementation references: [VTK volume properties](https://vtk.org/doc/nightly/html/classvtkVolumeProperty.html),
[PyVista timers](https://docs.pyvista.org/api/plotting/_autosummary/pyvista.plotter.add_timer_event).
