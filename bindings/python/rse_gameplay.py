# SPDX-License-Identifier: Apache-2.0
"""Run the pinned Signal Run PPO experiment and deterministic baselines."""

import argparse
from hashlib import sha256
import json
from pathlib import Path
import platform
import time

import gymnasium as gym
from gymnasium import spaces
import numpy as np
import stable_baselines3
from stable_baselines3 import PPO
from stable_baselines3.common.callbacks import BaseCallback
import torch

from rse_train import _device, _write_json
from rse_training import EmulatorEnv, EnvironmentConfig, MemoryRange, MemoryTask


PROTOCOL = Path(__file__).with_name("profiles") / "signal-run-v1.json"
MODES = ("memory", "screen", "hybrid")
HAZARDS = (3, 7)


class EpisodeRewards(BaseCallback):
    """Record completed training episodes against learner steps."""

    def __init__(self, output):
        super().__init__()
        self.output = output
        self.reward = 0.0

    def _on_step(self):
        self.reward += float(self.locals["rewards"][0])
        if self.locals["dones"][0]:
            self.output.write(json.dumps({"step": self.num_timesteps, "reward": self.reward}) + "\n")
            self.reward = 0.0
        return True


def scripted(position):
    return 2 if position + 1 in HAZARDS else 1


class SignalRun(gym.Wrapper):
    def __init__(self, bridge, rom, mode, protocol):
        config = EnvironmentConfig(
            rom_sha256=protocol["rom_sha256"], actions=((), ("right",), ("right", "a")),
            task=MemoryTask("cpu.ram", 0x12, 1), memory=(MemoryRange("cpu.ram", 0x10, 4),),
            mode=mode, ticks_per_repeat=357_368, action_repeat=2,
            max_steps=protocol["episode_steps"] + max(protocol["held_out_starts"] + protocol["training_starts"]),
            warmup_ticks=(protocol["warmup_ticks"],),
        )
        super().__init__(EmulatorEnv(bridge, rom, config))
        self.mode = mode
        self.protocol = protocol
        # 12 cells, with RGB samples from the sprite and obstacle rows.
        screen = spaces.Box(0.0, 1.0, (72,), np.float32)
        memory = spaces.Box(0.0, 1.0, (12,), np.float32)
        self.observation_space = (spaces.Dict({"memory": memory, "screen": screen})
                                  if mode == "hybrid" else {"memory": memory, "screen": screen}[mode])
        self.ended = True

    def _transform(self, observation):
        values = observation if self.mode == "hybrid" else {self.mode: observation}
        result = {}
        if "screen" in values:
            world = values["screen"]
            result["screen"] = world[[91, 99]][:, 19:208:16].astype(np.float32).reshape(72) / 255.0
        if "memory" in values:
            one_hot = np.zeros(12, dtype=np.float32)
            one_hot[int(values["memory"][0])] = 1
            result["memory"] = one_hot
        return result if self.mode == "hybrid" else result[self.mode]

    def _counters(self):
        position, points, finish, collisions = self.env.inspect("cpu.ram", 0x10, 4)
        if position is None or not 0 <= position <= 11 or points != position or finish != int(position == 11):
            raise RuntimeError("Signal Run counters violate the fixture contract")
        return position, points, collisions

    def reset(self, *, seed=None, options=None):
        options = {} if options is None else dict(options)
        if options.keys() - {"start"}:
            raise ValueError("Unknown Signal Run reset option")
        allowed = self.protocol["training_starts"] + self.protocol["held_out_starts"]
        start = options.get("start")
        if "start" in options and (type(start) is not int or start not in allowed):
            raise ValueError("Invalid Signal Run start")
        self.ended = True
        obs, info = self.env.reset(seed=seed)
        if start is None:
            start = int(self.np_random.choice(self.protocol["training_starts"]))
        for position in range(start):
            obs, _, terminated, truncated, info = self.env.step(scripted(position))
            if terminated or truncated:
                raise RuntimeError("Signal Run start prefix terminated")
        self.position, self.points, self.collisions = self._counters()
        if self.position != start or self.collisions:
            raise RuntimeError("Signal Run start prefix produced the wrong state")
        self.start = start
        self.steps = 0
        self.ended = False
        return self._transform(obs), {**info, "position": start, "start": start, "episode_steps": 0}

    def step(self, action):
        if self.ended:
            raise gym.error.ResetNeeded("Reset is required before stepping")
        obs, _, terminated, base_truncated, info = self.env.step(action)
        position, points, collisions = self._counters()
        reward = (0.15 * (points - self.points) - 0.10 * (collisions - self.collisions)
                  - 0.01 + float(terminated))
        self.position, self.points, self.collisions = position, points, collisions
        self.steps += 1
        truncated = not terminated and (base_truncated or self.steps >= self.protocol["episode_steps"])
        self.ended = terminated or truncated
        return (self._transform(obs), reward, terminated, truncated, {
            **info, "position": position, "start": self.start, "episode_steps": self.steps,
            "collisions": collisions,
        })


def _evaluate(env, choose, protocol):
    episodes = []
    for episode in range(protocol["evaluation_episodes"]):
        seed = protocol["evaluation_seed_first"] + episode
        starts = protocol["held_out_starts"]
        start = starts[episode % len(starts)]
        obs, info = env.reset(seed=seed, options={"start": start})
        rng = np.random.default_rng(seed)
        total = 0.0
        while True:
            obs, reward, won, timeout, info = env.step(choose(obs, info, rng))
            total += reward
            if won or timeout:
                episodes.append({"seed": seed, "start": start, "completed": won,
                                 "steps": info["episode_steps"], "reward": total,
                                 "final_position": info["position"], "collisions": info["collisions"]})
                break
    return {
        "completed": sum(episode["completed"] for episode in episodes),
        "mean_reward": sum(episode["reward"] for episode in episodes) / len(episodes),
        "episodes": episodes,
    }


def run(bridge, rom, out, device, baselines_only, build_sha):
    protocol_bytes = PROTOCOL.read_bytes()
    protocol = json.loads(protocol_bytes)
    device, hardware = _device("cpu" if baselines_only else device)
    torch.set_num_threads(1)
    out.mkdir(parents=True, exist_ok=False)
    manifest = {
        **hardware, "build_sha": build_sha, "platform": platform.platform(),
        "torch": torch.__version__, "gymnasium": gym.__version__, "numpy": np.__version__,
        "stable_baselines3": stable_baselines3.__version__, "python": platform.python_version(),
        "protocol_sha256": sha256(protocol_bytes).hexdigest(),
        "rom_sha256": sha256(rom).hexdigest(), "bridge_sha256": sha256(bridge.read_bytes()).hexdigest(),
        "baselines_only": baselines_only,
    }
    _write_json(out / "manifest.json", manifest)
    env = SignalRun(bridge, rom, "memory", protocol)
    try:
        random_result = _evaluate(env, lambda _obs, _info, rng: int(rng.integers(3)), protocol)
        scripted_result = _evaluate(env, lambda _obs, info, _rng: scripted(info["position"]), protocol)
    finally:
        env.close()
    _write_json(out / "random.json", random_result)
    _write_json(out / "scripted.json", scripted_result)
    if scripted_result["completed"] != protocol["evaluation_episodes"]:
        raise RuntimeError("The scripted reference failed the fixture contract")
    if baselines_only:
        return True
    results = []
    for mode in MODES:
        for seed in protocol["training_seeds"]:
            started = time.monotonic()
            env = SignalRun(bridge, rom, mode, protocol)
            try:
                model = PPO("MultiInputPolicy" if mode == "hybrid" else "MlpPolicy", env,
                            seed=seed, device=device, n_steps=256, batch_size=128,
                            n_epochs=4, policy_kwargs={"net_arch": [64, 64]})
                curve_name = f"{mode}-{seed}-rewards.jsonl"
                with (out / curve_name).open("w", encoding="utf-8") as curve:
                    model.learn(total_timesteps=protocol["training_steps"], callback=EpisodeRewards(curve))
                model_path = out / f"{mode}-{seed}.zip"
                model.save(model_path)
                evaluation = _evaluate(
                    env, lambda obs, _info, _rng: int(model.predict(obs, deterministic=True)[0]), protocol)
                record = {"mode": mode, "seed": seed, "training_steps": model.num_timesteps,
                          "training_rewards": curve_name,
                          "device": str(model.device), "seconds": time.monotonic() - started,
                          "model_sha256": sha256(model_path.read_bytes()).hexdigest(), **evaluation}
            except Exception as error:
                record = {"mode": mode, "seed": seed, "error": f"{type(error).__name__}: {error}"}
            finally:
                env.close()
            _write_json(out / f"{mode}-{seed}.json", record)
            results.append({key: value for key, value in record.items() if key != "episodes"})
    memory = [entry for entry in results if entry["mode"] == "memory"]
    passed = device == "cuda" and "RTX 5080" in hardware.get("gpu_name", "") and all(
        result.get("completed", -1) >= protocol["completion_threshold"]
        and result["completed"] > random_result["completed"] for result in memory
    ) and all(result.get("device", "").split(":")[0] == "cuda" and "completed" in result for result in results)
    _write_json(out / "summary.json", {
        "accepted": passed, "random_completed": random_result["completed"],
        "scripted_completed": scripted_result["completed"], "policies": results,
    })
    return passed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bridge", required=True, type=Path)
    parser.add_argument("--rom", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--device", choices=("cpu", "cuda"), default="cuda")
    parser.add_argument("--baselines-only", action="store_true")
    parser.add_argument("--build-sha")
    args = parser.parse_args()
    if not run(args.bridge.resolve(), args.rom.read_bytes(), args.out, args.device,
               args.baselines_only, args.build_sha):
        raise SystemExit("M17 acceptance threshold not met. See summary.json.")


if __name__ == "__main__":
    main()
