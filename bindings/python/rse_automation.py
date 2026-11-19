# SPDX-License-Identifier: Apache-2.0
"""In-process bindings for the optional Retro Silicon automation bridge."""

import ctypes as c
import json
import operator
import struct


_BOUNDARIES = ("master_tick", "cpu_cycle", "instruction", "scanline", "frame")
_REASONS = ("target", "budget", "paused", "step", "breakpoint", "watchpoint", "overflow")
_HEADER = struct.Struct("<4sHHIQ")
_RECORD = struct.Struct("<QQQBBBBIQQ")
_ptr, _u64, _u32, _size, _str, _int = (
    c.c_void_p, c.c_uint64, c.c_uint32, c.c_size_t, c.c_char_p, c.c_int
)


class _RunOptions(c.Structure):
    _fields_ = [
        ("target", _u64), ("budget", _u64), ("equals", _u64),
        ("capacity", _size), ("value_id", _str), ("event_id", _str), ("domain", _str),
        ("address", _u32), ("first", _u32), ("count", _u32),
        ("step", _int), ("breakpoint", _int), ("preview", _int),
    ]


def _uint(value, bits=64):
    value = operator.index(value)
    if not 0 <= value < 1 << bits:
        raise ValueError(f"expected an unsigned {bits}-bit integer")
    return value


def _id(value):
    data = value.encode("ascii")
    if not data or b"\x00" in data:
        raise ValueError("expected a nonempty ASCII identifier")
    return data


class _Bridge:
    def __init__(self, library):
        self.library = c.CDLL(str(library))
        p = c.POINTER
        commands = {
            "error": (_str, ()),
            "create": (_int, (_str, _ptr, _size, p(_ptr))),
            "destroy": (None, (_ptr,)),
            "status": (_int, (_ptr, p(_u64), p(_u64), p(_int))),
            "reset": (_int, (_ptr,)),
            "save": (_int, (_ptr, p(_ptr))),
            "restore": (_int, (_ptr, _ptr, _size)),
            "pause": (None, (_ptr,)),
            "resume": (_int, (_ptr,)),
            "input": (_int, (_ptr, _u64, _str, p(_str), _size)),
            "schema": (_int, (_ptr, p(_ptr))),
            "peek": (_int, (_ptr, _str, _u32, p(_u64), p(_int))),
            "run": (_int, (_ptr, p(_RunOptions), p(_u64), p(_int), p(_ptr))),
            "audio": (_int, (_ptr, p(_ptr))),
            "data": (_ptr, (_ptr,)),
            "size": (_size, (_ptr,)),
            "free": (None, (_ptr,)),
        }
        for name, (result, arguments) in commands.items():
            function = getattr(self.library, "rse_py_" + name)
            function.restype = result
            function.argtypes = arguments
            setattr(self, name, function)

    def call(self, command, *args):
        if getattr(self, command)(*args) != 0:
            raise RuntimeError(self.error().decode("utf-8", "replace"))

    def copy(self, buffer):
        try:
            size = self.size(buffer)
            return c.string_at(self.data(buffer), size) if size else b""
        finally:
            self.free(buffer)


class _Handle:
    def __init__(self, bridge, system, image):
        self.bridge = bridge
        self.pointer = _ptr()
        image = bytes(image)
        bridge.call("create", _id(system), c.cast(c.c_char_p(image), _ptr),
                    len(image), c.byref(self.pointer))

    def __del__(self):
        if self.pointer:
            self.bridge.destroy(self.pointer)


class Session:
    """Own a native session. Close releases it once outstanding calls return."""

    def __init__(self, library, image, system="nes-ntsc"):
        self._native = _Handle(_Bridge(library), system, image)

    def _handle(self):
        native = self._native
        if native is None:
            raise RuntimeError("Session is closed")
        return native

    def close(self):
        self._native = None

    def __enter__(self):
        self._handle()
        return self

    def __exit__(self, *_):
        self.close()

    def status(self):
        native = self._handle()
        tick, frames, faulted = _u64(), _u64(), _int()
        native.bridge.call("status", native.pointer, c.byref(tick), c.byref(frames), c.byref(faulted))
        return tick.value, frames.value, bool(faulted.value)

    def reset(self):
        native = self._handle()
        native.bridge.call("reset", native.pointer)

    def snapshot(self):
        native = self._handle()
        buffer = _ptr()
        native.bridge.call("save", native.pointer, c.byref(buffer))
        return native.bridge.copy(buffer)

    def restore(self, snapshot):
        native = self._handle()
        snapshot = bytes(snapshot)
        native.bridge.call("restore", native.pointer, c.cast(_str(snapshot), _ptr), len(snapshot))

    def pause(self):
        native = self._handle()
        native.bridge.pause(native.pointer)

    def resume(self):
        native = self._handle()
        native.bridge.call("resume", native.pointer)

    def schedule_input(self, tick, port, pressed):
        native = self._handle()
        actions = tuple(map(_id, pressed))
        array = (_str * len(actions))(*actions)
        native.bridge.call("input", native.pointer, _uint(tick), _id(port), array, len(actions))

    def schema(self):
        native = self._handle()
        buffer = _ptr()
        native.bridge.call("schema", native.pointer, c.byref(buffer))
        return json.loads(native.bridge.copy(buffer))

    def peek(self, identifier, address=0):
        native = self._handle()
        value, available = _u64(), _int()
        native.bridge.call("peek", native.pointer, _id(identifier), _uint(address, 32),
                           c.byref(value), c.byref(available))
        return value.value if available.value else None

    def run(self, target, budget, *, step=None, breakpoint=None, watchpoint=None,
            capacity=0, preview=False):
        """Return (committed tick, stop reason, observation bytes)."""
        native = self._handle()
        options = _RunOptions(target=_uint(target), budget=_uint(budget),
                              capacity=_uint(capacity, 32), preview=bool(preview),
                              step=-1 if step is None else _BOUNDARIES.index(step), breakpoint=-1)
        if breakpoint is not None:
            boundary, identifier, address, value = breakpoint
            options.breakpoint = _BOUNDARIES.index(boundary)
            options.value_id = _id(identifier)
            options.address, options.equals = _uint(address, 32), _uint(value)
        if watchpoint is not None:
            event, domain, first, count = watchpoint
            options.event_id, options.domain = _id(event), _id(domain)
            options.first, options.count = _uint(first, 32), _uint(count, 32)
        tick, reason, buffer = _u64(), _int(), _ptr()
        native.bridge.call("run", native.pointer, c.byref(options), c.byref(tick),
                           c.byref(reason), c.byref(buffer))
        return tick.value, _REASONS[reason.value], native.bridge.copy(buffer)

    def take_audio(self):
        """Return owned native-endian signed 16-bit PCM bytes."""
        native = self._handle()
        buffer = _ptr()
        native.bridge.call("audio", native.pointer, c.byref(buffer))
        return native.bridge.copy(buffer)


def decode_observations(data):
    """Return a lost count and record tuples with unavailable fields set to None."""
    if not data:
        return 0, []
    if len(data) < _HEADER.size:
        raise ValueError("truncated observation header")
    magic, version, reserved, count, lost = _HEADER.unpack_from(data)
    if magic != b"RSEO" or version != 1 or reserved or count > 65_536:
        raise ValueError("unsupported observation header")
    offset = _HEADER.size
    records = []
    for _ in range(count):
        if offset + _RECORD.size > len(data):
            raise ValueError("truncated observation record")
        sequence, tick, index, kind, phase, flags, length, address, value, previous = (
            _RECORD.unpack_from(data, offset)
        )
        offset += _RECORD.size
        end = offset + length
        if end > len(data) or kind > 2 or phase > 1 or flags & ~15:
            raise ValueError("invalid observation record")
        identifier = bytes(data[offset:end]).decode("ascii")
        records.append((sequence, tick, index, kind, phase, identifier,
                        address if flags & 1 else None, value if flags & 2 else None,
                        previous if flags & 4 else None, bool(flags & 8)))
        offset = end
    if offset != len(data):
        raise ValueError("trailing observation bytes")
    return lost, records
