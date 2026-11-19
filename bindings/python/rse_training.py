# SPDX-License-Identifier: Apache-2.0
"""Optional Gymnasium environment for deterministic headless sessions."""

from dataclasses import dataclass
from hashlib import sha256
import math
import operator

import gymnasium as gym
from gymnasium import spaces
import numpy as np

from rse_automation import Session


def _integer(value, name, minimum, maximum):
    if isinstance(value, (bool, np.bool_)):
        raise ValueError(f"{name} must be an integer")
    value = operator.index(value)
    if not minimum <= value <= maximum:
        raise ValueError(f"{name} must be in {minimum}..{maximum}")
    return value


@dataclass(frozen=True)
class MemoryRange:
    id: str
    first: int
    count: int = 1


@dataclass(frozen=True)
class MemoryTask:
    """Reward a task-specific memory condition, independently of the machine."""

    id: str
    address: int
    equals: int
    success_reward: float = 1.0
    step_reward: float = 0.0

    def evaluate(self, session):
        matched = session.peek(self.id, self.address) == self.equals
        return float(self.step_reward + (self.success_reward if matched else 0)), matched


@dataclass(frozen=True)
class EnvironmentConfig:
    rom_sha256: str
    actions: tuple[tuple[str, ...], ...]
    task: MemoryTask
    memory: tuple[MemoryRange, ...] = ()
    mode: str = "memory"
    port: str = "controller-1"
    ticks_per_repeat: int = 357_368
    action_repeat: int = 1
    max_steps: int = 120
    warmup_ticks: tuple[int, ...] = (357_368,)
    system: str = "nes-ntsc"

    @classmethod
    def from_json(cls, data):
        fields = dict(data)
        fields["actions"] = tuple(tuple(action) for action in fields["actions"])
        fields["task"] = MemoryTask(**fields["task"])
        fields["memory"] = tuple(MemoryRange(**entry) for entry in fields.get("memory", ()))
        if "warmup_ticks" in fields:
            fields["warmup_ticks"] = tuple(fields["warmup_ticks"])
        return cls(**fields)


class EmulatorEnv(gym.Env):
    metadata = {"render_modes": ["rgb_array"]}

    def __init__(self, library, rom, config, render_mode=None):
        if sha256(rom).hexdigest() != config.rom_sha256:
            raise ValueError("ROM SHA-256 does not match the task configuration")
        self.config = config
        self.render_mode = render_mode
        if render_mode not in (None, "rgb_array"):
            raise ValueError("Unsupported render mode")
        self._session = Session(library, rom, config.system)
        try:
            self._configure(self._session.schema())
        except Exception:
            self._session.close()
            raise
        self._ended = True

    def _configure(self, schema):
        cfg = self.config
        if cfg.mode not in ("screen", "memory", "hybrid"):
            raise ValueError("Unknown observation mode")
        self._has_screen = cfg.mode in ("screen", "hybrid")
        self._has_memory = cfg.mode in ("memory", "hybrid")
        choices = schema["input_ports"].get(cfg.port, ())
        if not choices or not cfg.actions or any(
            len(set(action)) != len(action) or not set(action).issubset(choices)
            for action in cfg.actions
        ):
            raise ValueError("Action mapping does not match the selected input port")
        if len({frozenset(action) for action in cfg.actions}) != len(cfg.actions):
            raise ValueError("Action mapping contains duplicate pressed sets")
        self.action_space = spaces.Discrete(len(cfg.actions))
        tick_max = (1 << 63) - 1
        _integer(cfg.ticks_per_repeat, "ticks_per_repeat", 1, tick_max)
        _integer(cfg.action_repeat, "action_repeat", 1, 1024)
        _integer(cfg.max_steps, "max_steps", 1, tick_max)
        if not cfg.warmup_ticks:
            raise ValueError("At least one warmup start is required")
        for start in cfg.warmup_ticks:
            _integer(start, "warmup tick", 0, tick_max)
        if max(cfg.warmup_ticks) + cfg.max_steps * cfg.action_repeat * cfg.ticks_per_repeat > tick_max:
            raise ValueError("Episode exceeds the supported tick range")
        self._shape = None
        if schema["video"]:
            width, height = schema["video"]
            self._shape = (height, width, 3)
        if (self._has_screen or self.render_mode) and not self._shape:
            raise ValueError("Screen observations require video")
        observation = {}
        if self._has_screen:
            observation["screen"] = spaces.Box(0, 255, self._shape, np.uint8)
        if self._has_memory:
            total = sum(_integer(entry.count, "memory count", 1, 4096) for entry in cfg.memory)
            _integer(total, "total memory values", 1, 4096)
            for entry in cfg.memory:
                descriptor = schema["values"].get(entry.id)
                if not descriptor or descriptor[0] != 0 or descriptor[2] != 8:
                    raise ValueError(f"Expected an 8-bit memory domain for {entry.id}")
                _integer(entry.first, "memory start", 0, descriptor[3] - entry.count)
            # Code 256 distinguishes unavailable mapped addresses from bytes.
            observation["memory"] = spaces.Box(0, 256, (total,), np.uint16)
        self.observation_space = (spaces.Dict(observation) if cfg.mode == "hybrid"
                                  else observation[cfg.mode])
        task = cfg.task
        metadata = schema["values"].get(task.id)
        if not metadata:
            raise ValueError("Task value ID is unavailable")
        _integer(task.address, "task address", 0, metadata[3] - 1)
        _integer(task.equals, "task value", 0, (1 << metadata[2]) - 1)
        if not all(math.isfinite(value) for value in (
            task.success_reward, task.step_reward, task.success_reward + task.step_reward
        )):
            raise ValueError("Task rewards must be finite")
        clock_num, clock_den = schema["clock_hz"]
        self._drain_ticks = max(1, clock_num // (clock_den * 64))

    def _advance(self, ticks):
        tick = self._session.status()[0]
        while ticks:
            amount = min(ticks, self._drain_ticks)
            tick += amount
            committed, reason, _ = self._session.run(tick, amount)
            if reason != "target" or committed != tick:
                raise RuntimeError(f"Headless advance stopped at {committed}: {reason}")
            self._session.take_audio()
            ticks -= amount

    def _rgb(self):
        frame = self._session.frame_rgba()
        if not frame:
            return np.zeros(self._shape, dtype=np.uint8)
        return np.frombuffer(frame, dtype=np.uint8).reshape(
            self._shape[0], self._shape[1], 4)[:, :, :3].copy()

    def _observation(self):
        values = {}
        if self._has_memory:
            values["memory"] = np.asarray([
                value if value is not None else 256
                for entry in self.config.memory
                for value in self._session.read(entry.id, entry.first, entry.count)
            ], dtype=np.uint16)
        if self._has_screen:
            values["screen"] = self._rgb()
        return values if self.config.mode == "hybrid" else values[self.config.mode]

    def _info(self):
        tick, frames, _ = self._session.status()
        return {"tick": tick, "frames": frames, "steps": self._steps, "start_tick": self._start_tick}

    def reset(self, *, seed=None, options=None):
        options = {} if options is None else dict(options)
        if options.keys() - {"warmup_ticks"}:
            raise ValueError("Unknown reset option")
        if "warmup_ticks" in options:
            warmup = _integer(options["warmup_ticks"], "warmup tick", 0, (1 << 63) - 1)
            if warmup not in self.config.warmup_ticks:
                raise ValueError("Warmup tick is not in the configured starts")
        else:
            warmup = None
        super().reset(seed=seed)
        if seed is not None:
            self.action_space.seed(seed)
        self._ended = True
        self._session.reset()
        if warmup is None:
            warmup = self.config.warmup_ticks[int(self.np_random.integers(len(self.config.warmup_ticks)))]
        self._advance(warmup)
        self._start_tick = warmup
        self._steps = 0
        output = self._observation(), self._info()
        self._ended = False
        return output

    def step(self, action):
        if self._ended:
            raise gym.error.ResetNeeded("Reset is required before stepping")
        if isinstance(action, (bool, np.bool_)) or not self.action_space.contains(action):
            raise ValueError("Action is outside the action space")
        # A native/observation failure may follow committed advancement.
        self._ended = True
        tick, _, _ = self._session.status()
        self._session.schedule_input(tick + 1, self.config.port, self.config.actions[int(action)])
        self._advance(self.config.ticks_per_repeat * self.config.action_repeat)
        self._steps += 1
        reward, terminated = self.config.task.evaluate(self._session)
        truncated = not terminated and self._steps == self.config.max_steps
        output = self._observation(), reward, terminated, truncated, self._info()
        self._ended = terminated or truncated
        return output

    def render(self):
        return self._rgb() if self.render_mode == "rgb_array" else None

    def inspect(self, identifier, first, count):
        """Read task diagnostics independently of policy observations."""
        return self._session.read(identifier, first, count)

    def close(self):
        self._ended = True
        self._session.close()
