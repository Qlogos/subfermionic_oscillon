# Resonator

This toy project now uses a cheap classical real-field oscillon model. C++ evolves
two real amplitudes on a cubic grid; Python displays their squared densities in an
interactive 3D volume. There are no complex numbers, quantum-mechanical operators,
or nonlocal interactions.

## Run

```powershell
.\.venv\Scripts\python.exe visuals.py
```

The viewer starts running. Space pauses/resumes, N advances one step, R resets to
the same seed, and S saves `preview.png`. Drag to orbit and scroll to zoom.

Orange is the squared first field, blue is the squared second field, and pale is
where both overlap. Empty cells are transparent. The default opacity threshold is
0.05 because the initial total density is normalized to 1000 over a localized
packet rather than spread uniformly through the million cells.

Display controls:

- **Opaque at** controls the density at which a cell reaches full opacity.
- **Fuzziness** reveals lower-density material with a smooth power curve.
- **Visible depth** cuts away upper layers for inspecting the interior.

## Local real-field model

Each cube stores `u`, `v` and their velocities `du`, `dv`. It reads only itself and
its six face-neighbors. With `rho = u*u + v*v`, the local potential is

```text
V(rho) = 0.5*m*m*rho - 0.25*focusing*rho*rho
         + (saturation/6)*rho*rho*rho
```

The negative quartic term focuses finite-amplitude regions; the positive sextic
term prevents unlimited collapse. The discrete equations are

```text
u'' = wave_speed^2 * laplacian(u)
     - (m^2 - focusing*rho + saturation*rho^2) * u - damping*u'

v'' = wave_speed^2 * laplacian(v)
     - (m^2 - focusing*rho + saturation*rho^2) * v - damping*v'
```

The Laplacian is the six-neighbor sum of `(neighbor - center)`. Reflective closed
walls are used: missing boundary neighbors contribute zero flux. Velocity-Verlet
updates the fields in two half-velocity steps. With zero damping this is an
energy-conserving classical lattice experiment up to time-step error.

The initial condition is a localized Gaussian packet. The second field receives a
quadrature velocity, so the pair rotates through its two-dimensional real field
space. The displayed internal angular momentum is the local sum of
`u*dv - v*du`. This is a classical internal-rotation diagnostic, not a derivation
of electron spin.

Localized, oscillating real scalar-field configurations are commonly called
oscillons. Their existence and lifetime depend strongly on the potential and
dimension; this project is an exploratory lattice model, not a physical electron
model. See [Oscillons in Scalar Field Theories](https://arxiv.org/abs/hep-th/0602187).

## Controls and parameters

- `field_mass` sets the linear oscillation scale.
- `focusing` strengthens attractive self-interaction.
- `saturation` sets high-density repulsion.
- `wave_speed` sets neighbor-to-neighbor propagation.
- `damping` removes energy gradually; leave it at zero when testing persistence.
- `dt` is checked against a 3D wave CFL limit.

The viewer exposes the four nonlinear/dynamical controls; field mass and `dt` are
available from the command line:

```powershell
.\.venv\Scripts\python.exe visuals.py --field-mass 1 --dt 0.02
.\.venv\Scripts\python.exe visuals.py --focusing 1.4 --saturation 0.1 --wave-speed 0.5
```

Use smaller `dt` and compare the same simulated time before treating a pattern as
physical. If damping is zero, energy should remain nearly constant. A packet may
radiate or decay; a persistent resonator is a behavior to measure, not a guarantee.

## Memory and rebuild

The arena holds six contiguous double arrays: `u`, `v`, their velocities, and two
acceleration buffers. At 100 cubed this is about 45.8 MiB plus alignment, with no
per-face current storage and no allocations in a simulation step. The Python bridge
receives squared-density views for the renderer.

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
.\.venv\Scripts\python.exe test_viewer.py
.\.venv\Scripts\python.exe test_playback.py
```

The CPU update is O(number of cells) with a fixed amount of work per cell. The
default 100 cubed run is intended for the compiled backend; use `--size 40` while
exploring parameter ranges.
