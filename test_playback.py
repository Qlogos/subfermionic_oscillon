"""Real desktop event-loop regression test; opens a window briefly.

An offscreen VTK interactor cannot create native timers, so this intentionally
uses the same desktop interactor as the viewer. No direct calls to tick/advance.
"""
import time
from unittest.mock import patch

from backend import Simulation
from visuals import Viewer


def pump(viewer, duration):
    deadline = time.perf_counter() + duration
    while time.perf_counter() < deadline:
        viewer.plot.iren.process_events()
        time.sleep(0.005)


def key(viewer, name, character):
    interactor = viewer.plot.iren.interactor
    interactor.SetKeyEventInformation(0, 0, character, 0, name)
    interactor.InvokeEvent('KeyPressEvent')


def wait_for_steps(viewer, count):
    initial = viewer.sim.stats[7]
    deadline = time.perf_counter() + 3
    while viewer.sim.stats[7] < initial + count and time.perf_counter() < deadline:
        pump(viewer, 0.02)
    assert viewer.sim.stats[6] >= initial + count, 'Native timer did not advance playback'


def main():
    with Simulation(8) as simulation:
        viewer = Viewer(simulation)
        try:
            # Exercise Viewer.show's real startup order, returning control only
            # so the test can pump OS events and check the actual key bindings.
            native_show = viewer.plot.show
            with patch.object(viewer.plot, 'show',
                              side_effect=lambda: native_show(interactive_update=True, auto_close=False)):
                viewer.show()
            wait_for_steps(viewer, 3)
            key(viewer, 'space', ' ')
            assert not viewer.running, 'Space should pause'
            paused_at = simulation.stats[7]
            pump(viewer, 0.2)
            assert simulation.stats[7] == paused_at, 'Pause should stop automatic steps'
            key(viewer, 'n', 'n')
            assert simulation.stats[7] == paused_at + 1, 'N should advance exactly once'
            pump(viewer, 0.1)
            assert simulation.stats[7] == paused_at + 1, 'N should leave playback paused'
            key(viewer, 'space', ' ')
            assert viewer.running, 'Space should resume'
            wait_for_steps(viewer, 3)
            print('PASS: native timer autoplays; Space pauses/resumes; N advances exactly once')
        finally:
            viewer.plot.close()


if __name__ == '__main__':
    main()
