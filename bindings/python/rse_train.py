# SPDX-License-Identifier: Apache-2.0
"""Run PPO or deterministic baselines with an explicit environment profile."""

import argparse
from dataclasses import replace
from hashlib import sha256
import json
from pathlib import Path
import platform
import shutil
import subprocess
import time

import gymnasium
import numpy as np
import stable_baselines3
from stable_baselines3 import PPO
from stable_baselines3.common.callbacks import BaseCallback
from stable_baselines3.common.monitor import Monitor
import torch

from rse_training import EmulatorEnv, EnvironmentConfig


def _write_json(path, record):
    partial = path.with_suffix(path.suffix + ".tmp")
    partial.write_text(json.dumps(record, sort_keys=True, indent=2) + "\n", encoding="utf-8")
    partial.replace(path)


def _write_live(path, data):
    partial = path.with_suffix(".tmp")
    partial.write_bytes(data)
    try:
        partial.replace(path)
    except PermissionError:
        # A concurrent Windows reader can briefly block replacement. Keep
        # the previous complete sample and try again on the next interval.
        pass


def _device(requested):
    if requested == "cpu" or (requested == "auto" and not torch.cuda.is_available()):
        return "cpu", {"device": "cpu"}
    if not torch.cuda.is_available():
        raise RuntimeError("CUDA was requested but PyTorch cannot access a CUDA device")
    # Force a real kernel and copy its result back. Device enumeration alone
    # does not verify that the installed wheel can execute on the GPU.
    probe = torch.arange(16, device="cuda", dtype=torch.float32).square()
    if probe.device.type != "cuda" or probe.sum().item() != 1240:
        raise RuntimeError("CUDA arithmetic probe failed")
    details = {
        "device": "cuda", "gpu_name": torch.cuda.get_device_name(),
        "gpu_capability": list(torch.cuda.get_device_capability()),
        "gpu_memory_bytes": torch.cuda.get_device_properties(0).total_memory,
        "cuda_runtime": torch.version.cuda,
    }
    smi = shutil.which("nvidia-smi")
    if smi:
        try:
            driver = subprocess.run([smi, "--query-gpu=driver_version", "--format=csv,noheader"],
                                    capture_output=True, text=True, timeout=10, check=False)
        except (OSError, subprocess.TimeoutExpired):
            driver = None
        if driver is not None and driver.returncode == 0:
            details["nvidia_drivers"] = sorted(set(driver.stdout.split()))
    return "cuda", details


def _checkpoint(model, directory, identity, *, final=False):
    name = "checkpoint" if final else f"checkpoint-{model.num_timesteps}"
    path = directory / f"{name}.zip"
    partial = directory / f"{name}.tmp.zip"
    model.save(partial)
    metadata = {**identity, "timesteps": model.num_timesteps,
                "checkpoint_sha256": sha256(partial.read_bytes()).hexdigest()}
    partial.replace(path)
    _write_json(path.with_suffix(".json"), metadata)


def _load_checkpoint(path, identity, env, device):
    metadata = json.loads(path.with_suffix(".json").read_text(encoding="utf-8"))
    if any(metadata.get(key) != value for key, value in identity.items()):
        raise ValueError("Checkpoint environment or training settings differ")
    if sha256(path.read_bytes()).hexdigest() != metadata.get("checkpoint_sha256"):
        raise ValueError("Checkpoint SHA-256 mismatch")
    model = PPO.load(path, env=env, device=device)
    if model.num_timesteps != metadata.get("timesteps"):
        raise ValueError("Checkpoint timestep count does not match")
    return model


def _episode(out, step, reward, length, info):
    out.write(json.dumps({"step": step, "reward": float(reward), "length": length,
                          "tick": info["tick"], "frames": info["frames"]}) + "\n")
    out.flush()


class _Track(BaseCallback):
    def __init__(self, out, directory, identity, interval, stop_file=None, live=False):
        super().__init__()
        self.out, self.directory, self.identity, self.interval = out, directory, identity, interval
        self.saved = 0
        self.stop_file = stop_file
        self.live = live
        self.reward = None
        self.loss = None
        self.action = -1
        self.started = time.monotonic()
        self.last_publish = 0.0
        self.first_step = 0

    def _publish(self, force=False):
        now = time.monotonic()
        if not self.live or (not force and now - self.last_publish < 0.25):
            return
        self.last_publish = now
        fps = (self.num_timesteps - self.first_step) / max(now - self.started, 0.001)
        _write_live(
            self.directory / "live.txt",
            (
            f"RSE19 {self.num_timesteps} {fps:.3f} {int(self.reward is not None)} "
            f"{self.reward or 0:.6f} {int(self.loss is not None)} {self.loss or 0:.6f} {self.action}\n"
            ).encode("ascii"),
        )
        frame = self.training_env.env_method("render", indices=0)[0]
        if frame is not None and frame.shape == (240, 256, 3) and frame.dtype == np.uint8:
            _write_live(self.directory / "preview.rgb", frame.tobytes())

    def _on_step(self):
        if self.live:
            self.action = int(np.asarray(self.locals["actions"]).reshape(-1)[0])
        for info in self.locals["infos"]:
            if "episode" in info:
                episode = info["episode"]
                self.reward = float(episode["r"])
                _episode(self.out, self.num_timesteps, episode["r"], episode["l"], info)
        self._publish()
        return True

    def _on_rollout_end(self):
        # Rollout-end precedes its optimizer update. This checkpoint records
        # the last trained policy. The final save is after the last update.
        metrics = {key: float(value) for key, value in self.model.logger.name_to_value.items()
                   if key.startswith("train/") and isinstance(value, (int, float, np.number))
                   and np.isfinite(value)}
        if metrics:
            self.loss = metrics.get("train/loss", self.loss)
            self.out.write(json.dumps({"step": self.num_timesteps, "ppo": metrics}) + "\n")
            self.out.flush()

    def _on_rollout_start(self):
        if self.stop_file is not None and self.stop_file.exists():
            raise _StopRequested
        # Save between policy updates. The initial rollout has no new state.
        if self.num_timesteps - self.saved >= self.interval:
            _checkpoint(self.model, self.directory, self.identity)
            self.saved = self.num_timesteps


class _StopRequested(Exception):
    """Stop before collecting a new rollout, after the preceding update."""


def _baseline(env, method, seed, iterations, script_action, out):
    rng = np.random.default_rng(seed)
    env.reset(seed=seed)
    reward, length = 0.0, 0
    for step in range(1, iterations + 1):
        action = int(rng.integers(env.action_space.n)) if method == "random" else script_action
        _, value, terminated, truncated, info = env.step(action)
        reward += value
        length += 1
        if terminated or truncated:
            _episode(out, step, reward, length, info)
            reward, length = 0.0, 0
            if step < iterations:
                env.reset()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bridge", required=True, type=Path)
    parser.add_argument("--rom", required=True, type=Path)
    parser.add_argument("--profile", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--method", choices=("ppo", "random", "scripted"), default="ppo")
    parser.add_argument("--device", choices=("cpu", "cuda", "auto"), default="auto")
    parser.add_argument("--seed", default=0, type=int)
    parser.add_argument("--steps", default=256, type=int)
    parser.add_argument("--rollout", default=64, type=int)
    parser.add_argument("--checkpoint-every", default=256, type=int)
    parser.add_argument("--resume", type=Path, help="Load a trusted local PPO checkpoint")
    parser.add_argument("--script-action", default=0, type=int)
    parser.add_argument("--build-sha", help="Git revision of the native bridge")
    parser.add_argument("--observation", choices=("memory", "screen", "hybrid"))
    parser.add_argument("--stop-file", type=Path, help="Stop at the next PPO rollout boundary")
    parser.add_argument("--live", action="store_true", help="Publish bounded desktop telemetry and preview")
    args = parser.parse_args()
    if args.steps <= 0 or not 0 <= args.seed < 2**32 or args.rollout < 8 or args.checkpoint_every <= 0:
        parser.error("Invalid timestep budget, seed, rollout, or checkpoint interval")
    if args.resume and args.method != "ppo":
        parser.error("Only PPO accepts checkpoints")
    if (args.stop_file or args.live) and args.method != "ppo":
        parser.error("Live telemetry and cooperative stop require PPO")
    profile_bytes = args.profile.read_bytes()
    config = EnvironmentConfig.from_json(json.loads(profile_bytes))
    identity = {"rom_sha256": config.rom_sha256, "profile_sha256": sha256(profile_bytes).hexdigest(),
                "bridge_sha256": sha256(args.bridge.read_bytes()).hexdigest(),
                "seed": args.seed, "rollout": args.rollout}
    if args.observation:
        config = replace(config, mode=args.observation)
        identity["observation_mode"] = args.observation
    device, hardware = _device(args.device if args.method == "ppo" else "cpu")
    torch.set_num_threads(1)
    env = EmulatorEnv(args.bridge.resolve(), args.rom.read_bytes(), config,
                      render_mode="rgb_array" if args.live else None)
    start = time.monotonic()
    manifest = {
        **identity, **hardware, "format": 1, "status": "running", "method": args.method,
        "steps_requested": args.steps, "resume_from": str(args.resume) if args.resume else None,
        "checkpoint_every": args.checkpoint_every, "script_action": args.script_action,
        "platform": platform.platform(), "cpu": platform.processor(),
        "build_sha": args.build_sha, "python": platform.python_version(),
        "torch": torch.__version__, "gymnasium": gymnasium.__version__,
        "numpy": np.__version__, "stable_baselines3": stable_baselines3.__version__,
    }
    try:
        if args.method == "scripted" and not env.action_space.contains(args.script_action):
            raise ValueError("Scripted action is outside the environment action space")
        args.out.mkdir(parents=True, exist_ok=False)
        _write_json(args.out / "manifest.json", manifest)
        stopped = False
        with (args.out / "metrics.jsonl").open("w", encoding="utf-8") as out:
            if args.method == "ppo":
                monitored = Monitor(env)
                if args.resume:
                    model = _load_checkpoint(args.resume, identity, monitored, device)
                else:
                    policy = {"memory": "MlpPolicy", "screen": "CnnPolicy", "hybrid": "MultiInputPolicy"}
                    model = PPO(policy[config.mode], monitored, seed=args.seed, device=device,
                                n_steps=args.rollout, batch_size=min(args.rollout, 64), n_epochs=2)
                monitor = _Track(out, args.out, identity, args.checkpoint_every, args.stop_file, args.live)
                monitor.saved = model.num_timesteps
                monitor.first_step = model.num_timesteps
                try:
                    model.learn(total_timesteps=args.steps, reset_num_timesteps=not args.resume,
                                callback=monitor)
                except _StopRequested:
                    stopped = True
                monitor.num_timesteps = model.num_timesteps
                monitor.loss = model.logger.name_to_value.get("train/loss", monitor.loss)
                monitor._publish(force=True)
                _checkpoint(model, args.out, identity, final=True)
                manifest["timesteps"] = model.num_timesteps
                manifest["model_device"] = str(model.device)
                if device == "cuda":
                    manifest["cuda_peak_allocated_bytes"] = torch.cuda.max_memory_allocated()
            else:
                _baseline(env, args.method, args.seed, args.steps, args.script_action, out)
                manifest["timesteps"] = args.steps
        manifest["status"] = "stopped" if stopped else "complete"
        manifest["elapsed_seconds"] = time.monotonic() - start
        _write_json(args.out / "manifest.json", manifest)
    finally:
        env.close()


if __name__ == "__main__":
    main()
