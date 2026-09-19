"""Interactive 3D valence box. C++ evolves the arrays; Python displays them."""
import argparse
from pathlib import Path
import sys
import time

import numpy as np
import pyvista as pv
from vtkmodules.vtkCommonDataModel import vtkPiecewiseFunction
from vtkmodules.vtkRenderingCore import vtkColorTransferFunction, vtkVolume, vtkVolumeProperty
from vtkmodules.vtkRenderingVolumeOpenGL2 import vtkOpenGLGPUVolumeRayCastMapper

from backend import Simulation


def opacity_values(density, opaque_at, softness):
    """Zero stays transparent; density >= opaque_at is fully opaque per unit cell.

    softness > 1 reveals faint densities, softness < 1 emphasizes dense cores.
    """
    return np.clip(np.asarray(density) / opaque_at, 0, 1) ** (1 / softness)


class Viewer:
    def __init__(self, simulation, opaque_at=1.0, softness=1.0, off_screen=False):
        self.sim = simulation
        self.opaque_at, self.softness = opaque_at, softness
        self.running = not off_screen
        self.cut = simulation.side
        self.last_ms = 0.0
        self.message = ""
        self.ready = False
        self.plot = pv.Plotter(shape=(1, 2), col_weights=[0.73, 0.27],
                               window_size=(1400, 880), off_screen=off_screen,
                               title="Resonator | valence flow", border=False)
        self.plot.set_background("#101821", all_renderers=True)
        self.plot.subplot(0, 0)
        n = simulation.side
        # A zero-density halo lets the nearest-sampled boundary voxels have full width.
        self.grid = pv.ImageData(dimensions=(n + 2,) * 3, origin=(-0.5,) * 3)
        self.grid.point_data["state"] = np.zeros(((n + 2)**3, 2), dtype=np.float32)
        self.fields = self.grid.point_data["state"].reshape(n + 2, n + 2, n + 2, 2)
        self.mapper = vtkOpenGLGPUVolumeRayCastMapper()
        self.mapper.SetInputData(self.grid)
        self.mapper.SetBlendModeToComposite()
        self.mapper.SetAutoAdjustSampleDistances(False)
        self.mapper.SetSampleDistance(0.5)
        self.property = vtkVolumeProperty()
        # Two floating-point components retain opacity well below 1/255.
        # Component 0 determines colour; component 1 determines opacity.
        self.property.SetIndependentComponents(False)
        self.property.SetInterpolationTypeToNearest()
        self.property.SetScalarOpacityUnitDistance(1.0)
        self.property.ShadeOff()
        colours = vtkColorTransferFunction()
        colours.AddRGBPoint(-1, 0.22, 0.57, 0.98)
        colours.AddRGBPoint(0, 0.89, 0.91, 0.88)
        colours.AddRGBPoint(1, 1.0, 0.42, 0.15)
        self.property.SetColor(colours)
        self.opacity = vtkPiecewiseFunction()
        self.property.SetScalarOpacity(self.opacity)
        self.volume = vtkVolume()
        self.volume.SetMapper(self.mapper)
        self.volume.SetProperty(self.property)
        self.plot.add_actor(self.volume)
        self.plot.add_mesh(pv.Box(bounds=(0, n, 0, n, 0, n)).outline(), color="#657887", line_width=1)
        self.plot.add_axes(color="#d9e3ed", viewport=(0.015, 0.17, 0.14, 0.32))
        self.plot.add_text("Resonator", position=(0.035, 0.925), viewport=True,
                           font_size=25, color="#f0f3f5", font="arial")
        self.plot.add_text(f"{n} x {n} x {n} cells   /   closed walls", position=(0.038, 0.885),
                           viewport=True, font_size=12, color="#acbdcb", font="arial")
        self.status = self.plot.add_text("", position=(0.038, 0.055), viewport=True,
                                         font_size=12, color="#d9e3ed", font="arial")
        self.plot.camera_position = "iso"
        self.plot.camera.zoom(0.85)

        self.plot.subplot(0, 1)
        self.plot.set_background("#192530")
        self.label("Positive", 0.96, "#ff8a50", x=0.09)
        self.label("Mixed", 0.96, "#e3e8e0", x=0.44)
        self.label("Negative", 0.96, "#63a9fa", x=0.69)
        self.label("DISPLAY", 0.9)
        self.threshold_label = self.label("", 0.86)
        self.slider(lambda v: self.set_display(opaque_at=10**v), (-4, 1), np.log10(opaque_at), 0.825)
        self.soft_label = self.label("", 0.77)
        self.slider(lambda v: self.set_display(softness=v), (0.25, 3), softness, 0.735)
        self.cut_label = self.label("", 0.68)
        self.slider(self.set_cut, (1, n), n, 0.645)
        self.label("FLOW", 0.585)
        for key, label, bounds, height in [
            ("attraction", "Opposite attraction", (0, 4), 0.545),
            ("crowding", "Crowding pressure", (0, 0.5), 0.455),
            ("memory", "Flow memory", (0, 2), 0.365),
            ("rate", "Flow speed", (0, 2), 0.275),
        ]:
            actor = self.label("", height)
            def configure(value, key=key, label=label, actor=actor):
                self.sim.configure(**{key: value})
                actor.SetInput(f"{label}  {value:.3g}")
            bounds = (bounds[0], max(bounds[1], self.sim.parameters[key]))
            self.slider(configure, bounds, self.sim.parameters[key], height - 0.035)
        self.label("Space  play / pause    N  step", 0.155, size=11)
        self.label("R  reset seed    S  save image", 0.12, size=11)
        self.label("Drag to orbit   Scroll to zoom", 0.085, size=11)
        self.label("Opacity uses positive + negative", 0.045, size=10)
        self.plot.subplot(0, 0)
        self.plot.add_key_event("space", self.toggle)
        self.plot.add_key_event("n", self.single_step)
        self.plot.add_key_event("r", self.reset)
        self.plot.add_key_event("s", self.save)
        self.ready = True
        self.set_display()
        self.refresh()

    def label(self, text, y, colour="#d9e3ed", x=0.09, size=12):
        return self.plot.add_text(text, position=(x, y), viewport=True, font_size=size,
                                  color=colour, font="arial")

    def slider(self, callback, bounds, value, y):
        widget = self.plot.add_slider_widget(callback, bounds, value=value,
                                           pointa=(0.1, y), pointb=(0.90, y),
                                           color="#b7c9d6", interaction_event="always",
                                           slider_width=0.018, tube_width=0.004)
        widget.GetRepresentation().ShowSliderLabelOff()
        return widget

    def set_display(self, **changes):
        for key, value in changes.items():
            setattr(self, key, value)
        if not self.ready:
            return
        self.opacity.RemoveAllPoints()
        # Store floating-point opacity directly, keeping the GPU lookup linear.
        # This preserves faint values without enormous nonlinear lookup textures.
        self.opacity.AddPoint(0.0, 0.0)
        self.opacity.AddPoint(1.0, 1.0)
        self.threshold_label.SetInput(f"Opaque at  {self.opaque_at:.4g}")
        self.soft_label.SetInput(f"Fuzziness  {self.softness:.2f}")
        self.refresh()
        self.plot.render()

    def set_cut(self, value):
        self.cut = int(round(value))
        if self.ready:
            self.refresh()
            self.plot.render()

    def refresh(self):
        positive, negative = self.sim.read()
        n = self.sim.side
        density = positive + negative
        balance = np.divide(positive - negative, density, out=np.zeros_like(density), where=density > 0)
        self.fields[1:-1, 1:-1, 1:-1, 0] = balance.reshape(n, n, n)
        self.fields[1:-1, 1:-1, 1:-1, 1] = opacity_values(density, self.opaque_at, self.softness).reshape(n, n, n)
        self.fields[self.cut + 1:, :, :, 1] = 0
        self.grid.GetPointData().GetArray("state").Modified()
        self.grid.Modified()
        self.cut_label.SetInput(f"Visible depth  {self.cut} / {n}")
        self.update_status()

    def update_status(self):
        t, p, n, peak, rms, limited, steps, memory = self.sim.stats
        state = "Running" if self.running else "Paused"
        self.status.SetInput(
            f"{state}    t = {t:.2f}    step {int(steps)}    {self.last_ms:.0f} ms / step\n"
            f"Positive {p:.6f}    Negative {n:.6f}    Total {p+n:.6f}\n"
            f"Peak density {peak:.5f}    Limited donors {limited:.2%}    Arena {memory:.1f} MiB"
            + (f"\n{self.message}" if self.message else ""))

    def advance(self):
        start = time.perf_counter()
        try:
            self.sim.step()
            self.last_ms = (time.perf_counter() - start) * 1000
            self.refresh()
        except RuntimeError as error:
            self.running = False
            self.message = str(error)
            self.update_status()

    def tick(self, _):
        if self.running:
            self.advance()

    def toggle(self):
        self.running = not self.running
        self.update_status()
        self.plot.render()

    def single_step(self):
        self.running = False
        self.advance()
        self.plot.render()

    def reset(self):
        self.sim.reset()
        self.message = ""
        self.refresh()
        self.plot.render()

    def save(self):
        path = Path(__file__).with_name("preview.png")
        self.plot.screenshot(str(path))
        self.message = "Saved preview.png"
        self.update_status()

    def show(self):
        # Windows needs an initialized native interactor before it can create
        # a repeating timer. Before initialization, timer creation returns 0.
        self.plot.iren.initialize()
        self.plot.add_timer_event(max_steps=1_000_000_000, duration=30, callback=self.tick)
        self.plot.show()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--size", type=int, default=100)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--mass", type=float, default=1000)
    parser.add_argument("--opaque-at", type=float, default=1.0)
    parser.add_argument("--fuzziness", type=float, default=1.0)
    parser.add_argument("--attraction", type=float, default=2.0)
    parser.add_argument("--crowding", type=float, default=0.08)
    parser.add_argument("--memory", type=float, default=0.4)
    parser.add_argument("--rate", type=float, default=1.0)
    parser.add_argument("--dt", type=float, default=0.01)
    parser.add_argument("--steps", type=int, default=0, help="Advance before opening the viewer")
    parser.add_argument("--save", type=Path, help="Render one image without opening the viewer")
    args = parser.parse_args()
    if not (1e-4 <= args.opaque_at <= 10 and 0.25 <= args.fuzziness <= 3):
        parser.error("Use opaque-at in [0.0001,10] and fuzziness in [0.25,3].")
    if args.steps < 0 or args.steps > 100000:
        parser.error("Use steps in [0,100000].")
    try:
        with Simulation(args.size, args.seed, args.mass) as simulation:
            simulation.configure(attraction=args.attraction, crowding=args.crowding,
                                 memory=args.memory, rate=args.rate, dt=args.dt)
            simulation.step(args.steps)
            viewer = Viewer(simulation, args.opaque_at, args.fuzziness, bool(args.save))
            if args.save:
                viewer.plot.show(screenshot=str(args.save), auto_close=True)
                print(f"Saved {args.save}")
            else:
                viewer.show()
    except (RuntimeError, OSError) as error:
        print(f"Resonator: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
