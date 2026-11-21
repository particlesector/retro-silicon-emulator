# SPDX-License-Identifier: Apache-2.0
import json
from io import StringIO
from pathlib import Path
import subprocess
import sys
import tempfile

import numpy as np
from stable_baselines3 import PPO
import torch

sys.dont_write_bytecode = True
bridge, sdk, generator = map(Path, sys.argv[1:4])
sys.path.insert(0, str(sdk))
from rse_gameplay import EpisodeRewards, MODES, PROTOCOL, SignalRun, scripted

protocol = json.loads(PROTOCOL.read_bytes())
torch.set_num_threads(1)
with tempfile.TemporaryDirectory() as temp:
    subprocess.run([generator, temp], check=True)
    rom = (Path(temp) / "signal-run-v1.nes").read_bytes()
    for mode in MODES:
        env = SignalRun(bridge, rom, mode, protocol)
        try:
            obs, info = env.reset(seed=1000, options={"start": 1})
            frames = set()
            while True:
                position = info["position"]
                assert env.observation_space.contains(obs)
                if mode != "memory":
                    screen = obs if mode == "screen" else obs["screen"]
                    sprite_row = screen.reshape(2, 12, 3)[0]
                    assert np.count_nonzero(np.any(sprite_row, axis=1)) == 1
                    assert np.any(sprite_row[position])
                    frames.add(screen.tobytes())
                obs, reward, won, timeout, info = env.step(scripted(position))
                assert reward > 0 and info["position"] == position + 1
                if won or timeout:
                    assert won and info["episode_steps"] == 10
                    break
            if mode != "memory":
                assert len(frames) == 10
            _, info = env.reset(seed=1002, options={"start": 5})
            for _ in range(protocol["episode_steps"]):
                _, reward, won, timeout, info = env.step(0)
                assert not won and reward == -0.01 and info["position"] == 5
            assert timeout
            # Exercise the policy wiring in each mode without running the
            # full pinned benchmark or using its evaluation episodes.
            model = PPO("MultiInputPolicy" if mode == "hybrid" else "MlpPolicy",
                        env, seed=7, device="cpu", n_steps=16, batch_size=8, n_epochs=1)
            curve = StringIO()
            model.learn(total_timesteps=64, callback=EpisodeRewards(curve))
            points = [json.loads(line) for line in curve.getvalue().splitlines()]
            assert points
            steps = [point["step"] for point in points]
            assert steps == sorted(set(steps)) and 0 < steps[0] <= steps[-1] <= 64
            assert all(np.isfinite(point["reward"]) for point in points)
            obs, _ = env.reset(seed=7)
            action, _ = model.predict(obs, deterministic=True)
            assert env.action_space.contains(action)
        finally:
            env.close()
