# SPDX-License-Identifier: Apache-2.0
"""Export a trusted Signal Run v1 memory PPO checkpoint for native playback."""

import argparse
from hashlib import sha256
from io import BytesIO
import json
from pathlib import Path
import struct

import onnx
from stable_baselines3 import PPO
import torch


def _fnv(data):
    result = 14695981039346656037
    for value in data:
        result = ((result ^ value) * 1099511628211) & ((1 << 64) - 1)
    return result


class _Actor(torch.nn.Module):
    def __init__(self, policy):
        super().__init__()
        self.actor = policy.mlp_extractor.policy_net
        self.head = policy.action_net

    def forward(self, position):
        return self.head(self.actor(position))


def export(checkpoint, checkpoint_sha256, rom, output):
    protocol = json.loads((Path(__file__).with_name("profiles") / "signal-run-v1.json").read_bytes())
    image = rom.read_bytes()
    if sha256(image).hexdigest() != protocol["rom_sha256"]:
        raise ValueError("Expected the pinned Signal Run v1 ROM")
    if sha256(checkpoint.read_bytes()).hexdigest() != checkpoint_sha256:
        raise ValueError("Checkpoint SHA-256 mismatch")
    trained = PPO.load(checkpoint, device="cpu")
    policy = trained.policy
    if (trained.observation_space.shape != (12,) or getattr(trained.action_space, "n", None) != 3
            or policy.features_extractor.__class__.__name__ != "FlattenExtractor"):
        raise ValueError("Only the Signal Run v1 memory policy is supported")
    actor = _Actor(policy).eval()
    positions = torch.eye(12)
    with torch.no_grad():
        expected = actor(positions)
    predicted, _ = trained.predict(positions.numpy(), deterministic=True)
    if list(predicted) != expected.argmax(dim=1).tolist() or not torch.isfinite(expected).all():
        raise ValueError("Actor logits disagree with trained action selection")
    graph = BytesIO()
    torch.onnx.export(actor, positions[:1], graph, dynamo=False, opset_version=17,
                      input_names=["position"], output_names=["logits"])
    model = graph.getvalue()
    onnx.checker.check_model(model)
    if not 0 < len(model) <= 4 * 1024 * 1024:
        raise ValueError("Model exceeds the native bundle size limit")
    header = struct.pack("<4sHHQQI", b"RSEP", 1, 1, _fnv(image), _fnv(model), len(model))
    bundle = header + struct.pack("<36f", *expected.flatten().tolist()) + model
    with output.open("xb") as stream:
        stream.write(bundle)
    print(json.dumps({"checkpoint_sha256": checkpoint_sha256, "onnx_sha256": sha256(model).hexdigest(),
                      "bundle_sha256": sha256(bundle).hexdigest(), "rom_sha256": protocol["rom_sha256"],
                      "task": "signal-run-v1-memory", "input": [1, 12], "output": [1, 3],
                      "preprocessing": "float32 one-hot cpu.ram:$10",
                      "actions": [[], ["right"], ["right", "a"]],
                      "selection": "argmax-first", "step_ticks": 714736,
                      "warmup_ticks": 757368, "max_steps": 20},
                     sort_keys=True))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--checkpoint", required=True, type=Path, help="Trusted local SB3 checkpoint")
    parser.add_argument("--checkpoint-sha256", required=True)
    parser.add_argument("--rom", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    export(args.checkpoint, args.checkpoint_sha256, args.rom, args.out)
