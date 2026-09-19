"""Small ctypes bridge. The arena and all simulation updates live in C++."""
import ctypes as ct
from pathlib import Path
import sys
import numpy as np


class Simulation:
    def __init__(self, side=100, seed=42, mass=1000.0):
        root = Path(__file__).resolve().parent
        name = {"win32": "resonator_core.dll", "darwin": "resonator_core.dylib"}.get(sys.platform, "resonator_core.so")
        candidates = [root / "build" / name, root / "build" / "Release" / name]
        path = next((p for p in candidates if p.is_file()), None)
        if path is None:
            raise RuntimeError("Build the C++ backend first: cmake --build build --config Release")
        self.lib = ct.CDLL(str(path))
        pointer = ct.POINTER(ct.c_double)
        signatures = {
            "rs_error": ([], ct.c_char_p),
            "rs_create": ([ct.c_int, ct.c_uint, ct.c_double], ct.c_void_p),
            "rs_destroy": ([ct.c_void_p], None),
            "rs_configure": ([ct.c_void_p] + [ct.c_double] * 5, ct.c_int),
            "rs_step": ([ct.c_void_p, ct.c_int], ct.c_int),
            "rs_reset": ([ct.c_void_p, ct.c_uint], None),
            "rs_read": ([ct.c_void_p, pointer, pointer], None),
            "rs_stats": ([ct.c_void_p, pointer], None),
        }
        for key, (args, result) in signatures.items():
            function = getattr(self.lib, key)
            function.argtypes, function.restype = args, result
        self.handle = self.lib.rs_create(side, seed, mass)
        if not self.handle:
            raise RuntimeError(self.lib.rs_error().decode())
        self.side, self.seed, self.mass = side, seed, mass
        self.positive = np.empty(side**3, dtype=np.float64)
        self.negative = np.empty_like(self.positive)
        self.stats = np.empty(8, dtype=np.float64)
        self.parameters = dict(attraction=2.0, crowding=0.08, rate=1.0, memory=0.4, dt=0.01)
        self.read()

    def check(self, status):
        if status:
            raise RuntimeError(self.lib.rs_error().decode())

    def configure(self, **changes):
        parameters = self.parameters | changes
        self.check(self.lib.rs_configure(self.handle, *(parameters[k] for k in ("attraction", "crowding", "rate", "memory", "dt"))))
        self.parameters = parameters

    def step(self, count=1):
        self.check(self.lib.rs_step(self.handle, count))

    def read(self):
        pointer = ct.POINTER(ct.c_double)
        self.lib.rs_read(self.handle, self.positive.ctypes.data_as(pointer), self.negative.ctypes.data_as(pointer))
        self.lib.rs_stats(self.handle, self.stats.ctypes.data_as(pointer))
        return self.positive, self.negative

    def reset(self):
        self.lib.rs_reset(self.handle, self.seed)
        self.read()

    def close(self):
        if self.handle:
            self.lib.rs_destroy(self.handle)
            self.handle = None

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()
