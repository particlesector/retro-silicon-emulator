# SPDX-License-Identifier: Apache-2.0
from dataclasses import replace
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time

sys.dont_write_bytecode = True
bridge, sdk, fixture_generator = map(Path, sys.argv[1:4])
sys.path.insert(0, str(sdk))

from gymnasium.utils.env_checker import check_env
import numpy as np
from rse_training import EmulatorEnv, EnvironmentConfig, MemoryRange


def rejects(error, fn):
    try:
        fn()
    except error:
        return
    raise AssertionError(f"Expected {error.__name__}")


def replay(env):
    observation, info = env.reset(seed=19)
    records = [(observation, info)]
    for action in (1, 2, 0, 1, 0, 2, 0, 1):
        obs, reward, terminated, truncated, info = env.step(action)
        records.append((obs, reward, terminated, truncated, info))
        if terminated or truncated:
            break
    return records


def comparable(records):
    def as_bytes(observation):
        if isinstance(observation, dict):
            return tuple((key, value.tobytes()) for key, value in observation.items())
        return observation.tobytes()
    return [(as_bytes(record[0]), *record[1:]) for record in records]


with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    subprocess.run([fixture_generator, "--write-set", root], check=True)
    rom_path = root / "rse-loop-v1.nes"
    profile = sdk / "profiles" / "nrom-smoke.json"
    rom = rom_path.read_bytes()
    config = EnvironmentConfig.from_json(json.loads(profile.read_bytes()))
    rejects(ValueError, lambda: EmulatorEnv(bridge, rom + b"!", config))
    rejects(ValueError, lambda: EmulatorEnv(bridge, rom, replace(config, actions=(("bad",),))))
    rejects(ValueError, lambda: EmulatorEnv(
        bridge, rom, replace(config, memory=(MemoryRange("cpu.ram", 2047, 2),))))
    for mode in ("screen", "memory", "hybrid"):
        env = EmulatorEnv(bridge, rom, replace(config, mode=mode, warmup_ticks=(0, 357368)))
        try:
            check_env(env, skip_render_check=True)
            obs, info = env.reset(seed=1, options={"warmup_ticks": 357368})
            assert env.observation_space.contains(obs) and info["frames"] == 1
            if mode != "memory":
                pixels = obs if mode == "screen" else obs["screen"]
                assert pixels.shape == (240, 256, 3) and pixels.dtype == np.uint8
                assert np.any(pixels)
            rejects(ValueError, lambda: env.reset(options={"warmup_ticks": 1}))
            rejects(ValueError, lambda: env.step(True))
            first = replay(env)
            assert first[1][-1]["tick"] - first[0][-1]["tick"] == 7168
            assert comparable(first) == comparable(replay(env))
            assert first[-1][2] or first[-1][3]
        finally:
            env.close()

    ended = EmulatorEnv(bridge, rom, replace(config, task=replace(config.task, address=0, equals=0)))
    ended.reset(seed=0)
    assert ended.step(0)[1:4] == (1.01, True, False)
    ended.close()

    def train(name, *args):
        out = root / name
        subprocess.run([sys.executable, "-B", sdk / "rse_train.py", "--bridge", bridge, "--rom", rom_path,
                        "--profile", profile, "--out", out, "--rollout", "16", "--steps", "32",
                        "--checkpoint-every", "16", "--device", "cpu", *args], check=True)
        manifest = json.loads((out / "manifest.json").read_bytes())
        assert manifest["status"] == "complete" and manifest["device"] == "cpu"
        assert (out / "metrics.jsonl").stat().st_size
        return out, manifest

    out, manifest = train("first")
    assert manifest["model_device"] == "cpu" and manifest["timesteps"] == 32
    assert (out / "checkpoint-16.zip").exists()
    _, manifest = train("resumed", "--resume", out / "checkpoint.zip")
    assert manifest["timesteps"] == 64
    for method in ("random", "scripted"):
        _, manifest = train(method, "--method", method)
        assert manifest["method"] == method and manifest["timesteps"] == 32

    live = root / "live"
    stop = root / "stop.request"
    trainer = subprocess.Popen([sys.executable, "-B", sdk / "rse_train.py", "--bridge", bridge,
                                "--rom", rom_path, "--profile", profile, "--out", live, "--device",
                                "cpu", "--steps", "32768", "--rollout", "16", "--live",
                                "--stop-file", stop])
    try:
        deadline = time.monotonic() + 45
        while not (live / "live.txt").exists() and trainer.poll() is None and time.monotonic() < deadline:
            time.sleep(0.02)
        assert (live / "live.txt").read_text().startswith("RSE19 ")
        stop.touch()
        assert trainer.wait(timeout=30) == 0
        manifest = json.loads((live / "manifest.json").read_bytes())
        assert manifest["status"] == "stopped"
        assert 0 < manifest["timesteps"] < 32768 and manifest["timesteps"] % 16 == 0
        assert (live / "preview.rgb").stat().st_size == 256 * 240 * 3
        assert (live / "checkpoint.zip").is_file()
    finally:
        if trainer.poll() is None:
            trainer.kill()
            trainer.wait()
