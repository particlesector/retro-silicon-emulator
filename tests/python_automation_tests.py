# SPDX-License-Identifier: Apache-2.0
import sys
import threading
import time

sys.dont_write_bytecode = True
sys.path.insert(0, sys.argv[2])
from rse_automation import Session, decode_observations


def rejects(error, action):
    try:
        action()
    except error:
        return
    raise AssertionError(f"Expected {error.__name__}")


header = b"NES\x1a\x01" + bytes(11)
rom = bytearray(16384)
rom[:5] = bytes([0xE6, 0x10, 0x4C, 0, 0x80])
rom[0x3FFC:0x3FFE] = b"\x00\x80"
with Session(sys.argv[1], header + rom) as machine:
    assert machine.schema()["values"]["cpu.ram"][3] == 2048
    assert "instruction" in machine.schema()["boundaries"]
    assert "start" in machine.schema()["input_ports"]["controller-1"]
    assert machine.peek("cpu.bus", 0x5000) is None
    rejects(ValueError, lambda: machine.run(-1, 100))
    rejects(RuntimeError, lambda: machine.run(200, 0))
    rejects(RuntimeError, lambda: machine.schedule_input(1, "controller-1", ["invalid"]))
    machine.schedule_input(1, "controller-1", ["a"])
    machine.pause()
    assert machine.run(90, 90)[:2] == (0, "paused")
    machine.resume()
    assert machine.run(90, 90, step="instruction")[:2] == (84, "step")
    tick, reason, data = machine.run(
        200, 200, watchpoint=("cpu.write", "cpu.bus", 0x10, 1), capacity=32
    )
    assert (tick, reason) == (132, "watchpoint")
    assert not decode_observations(data)[0]
    assert decode_observations(data)[1][-1][5:8] == ("cpu.write", 0x10, 0)
    rejects(ValueError, lambda: decode_observations(data[:-1]))
    snapshot = machine.snapshot()
    machine.run(3000, 3000)
    assert machine.take_audio()
    rejects(RuntimeError, lambda: machine.restore(snapshot[:-1]))
    assert machine.status()[0] == 3000
    machine.restore(snapshot)
    assert machine.status()[0] == 132 and machine.snapshot() == snapshot
    assert not machine.take_audio()
    machine.reset()
    assert machine.status() == (0, 0, False)
assert decode_observations(data)[1]
rejects(RuntimeError, machine.status)

# Observe the native command guard from Python while CDLL releases the GIL.
# Closing during execution leaves the worker's native handle alive.
machine = Session(sys.argv[1], header + rom)
results = []
worker = threading.Thread(target=lambda: results.append(
    machine.run(16_000_000, 16_000_000, capacity=1, preview=True)
))
worker.start()
deadline = time.monotonic() + 5
while time.monotonic() < deadline and worker.is_alive():
    try:
        machine.status()
    except RuntimeError as error:
        assert "command is active" in str(error)
        machine.pause()
        machine.close()
        break
    time.sleep(0.001)
worker.join(5)
assert not worker.is_alive() and len(results) == 1
assert results[0][0] < 16_000_000 and results[0][1] == "paused"
decode_observations(results[0][2])
