"""Small integration check: requires the built library and an OpenGL driver."""
import numpy as np
from backend import Simulation
from visuals import Viewer, opacity_values


def main():
    with Simulation(8) as simulation:
        initial = simulation.positive.copy()
        initial_total = simulation.stats[1] + simulation.stats[2]
        viewer = Viewer(simulation, off_screen=True)
        try:
            viewer.plot.show(interactive=False, auto_close=False)
            simulation.configure(focusing=1.4)
            viewer.single_step()
            assert simulation.stats[7] == 1
            assert not np.array_equal(initial, simulation.positive)
            assert np.isfinite(simulation.stats[1] + simulation.stats[2])
            np.testing.assert_allclose(initial_total, 1000, atol=1e-9)
            before = simulation.stats[7]
            viewer.tick(0)
            assert simulation.stats[7] == before, "Paused timer should not advance"
            viewer.toggle()
            viewer.tick(1)
            assert simulation.stats[7] == before + 1
            viewer.set_cut(3)
            assert not np.any(viewer.fields[4:, :, :, 1]), "Cutaway must hide upper layers"
            viewer.set_cut(8)
            assert np.any(viewer.fields[4:-1, 1:-1, 1:-1, 1]), "Restoring depth must restore data"
            viewer.set_display(opaque_at=1, softness=2)
            alpha = opacity_values([0, 0.25, 1, 2], 1, 2)
            np.testing.assert_allclose(alpha, [0, 0.5, 1, 1])
            renderer_alpha = viewer.fields[1:-1, 1:-1, 1:-1, 1].ravel()
            np.testing.assert_allclose(renderer_alpha, opacity_values(simulation.positive + simulation.negative, 1, 2), rtol=1e-6)
            viewer.reset()
            np.testing.assert_array_equal(simulation.positive, initial)
            assert simulation.stats[7] == 0
            assert simulation.parameters['focusing'] == 1.4
            viewer.plot.render()
            print("PASS: C++/Python bridge, playback, stepping, cutaway, opacity, deterministic reset, GPU render")
        finally:
            viewer.plot.close()


if __name__ == '__main__':
    main()
