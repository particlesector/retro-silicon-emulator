# SPDX-License-Identifier: Apache-2.0
from hashlib import sha256
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import struct

sys.dont_write_bytecode = True
bridge, sdk, generator, cli, takeover = map(Path, sys.argv[1:6])
sys.path.insert(0, str(sdk))
from stable_baselines3 import PPO
import torch
from rse_export_policy import export
from rse_gameplay import PROTOCOL, SignalRun, scripted


def rejected(args):
    result = subprocess.run(args, capture_output=True)
    assert result.returncode == 2, result.stderr


def fixture(bridge, rom, root):
    torch.set_num_threads(1)
    config = json.loads(PROTOCOL.read_bytes())
    env = SignalRun(bridge, rom.read_bytes(), "memory", config)
    try:
        # Use an authored policy to test native playback deterministically.
        # The target workstation uses a trained M17 checkpoint separately.
        ppo = PPO("MlpPolicy", env, device="cpu", n_steps=8, batch_size=8,
                  policy_kwargs={"net_arch": [64, 64]})
        with torch.no_grad():
            for layer in (ppo.policy.mlp_extractor.policy_net[0], ppo.policy.mlp_extractor.policy_net[2]):
                layer.weight.zero_()
                layer.bias.zero_()
                layer.weight[:12, :12].copy_(torch.eye(12))
            ppo.policy.action_net.weight.zero_()
            ppo.policy.action_net.bias.zero_()
            for pos in range(12):
                ppo.policy.action_net.weight[scripted(pos), pos] = 3
        checkpoint = root / "fixture.zip"
        ppo.save(checkpoint)
        bundle = root / "fixture.rsep"
        export(checkpoint, sha256(checkpoint.read_bytes()).hexdigest(), rom, bundle)
        return bundle
    finally:
        env.close()


with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    subprocess.run([generator, root], check=True)
    rom = root / "signal-run-v1.nes"
    bundle = fixture(bridge, rom, root)
    replay = root / "policy.resl"
    result = subprocess.run([cli, rom, bundle, "cpu", replay], text=True,
                            capture_output=True, check=True)
    assert "provider=cpu" in result.stdout and "completed=1 replay_match=1" in result.stdout
    assert replay.read_bytes().startswith(b"RESL")
    assert "completed=1" in subprocess.check_output([cli, "--replay", rom, replay], text=True)
    auto = subprocess.check_output([cli, rom, bundle, "auto", root / "auto.resl"], text=True)
    assert "completed=1 replay_match=1" in auto
    subprocess.run([takeover, rom, bundle], check=True)
    rejected([cli, root / "signal-run-defect-v1.nes", bundle, "cpu", root / "bad-rom.resl"])
    damaged = bytearray(bundle.read_bytes())
    damaged[-1] ^= 1
    (root / "bad.rsep").write_bytes(damaged)
    rejected([cli, rom, root / "bad.rsep", "cpu", root / "bad-model.resl"])
    damaged = bytearray(bundle.read_bytes())
    damaged[4] = 2
    (root / "bad-version.rsep").write_bytes(damaged)
    rejected([cli, rom, root / "bad-version.rsep", "cpu", root / "bad-version.resl"])
    damaged = bytearray(bundle.read_bytes())
    struct.pack_into("<f", damaged, 28, 99.0)
    (root / "bad-parity.rsep").write_bytes(damaged)
    rejected([cli, rom, root / "bad-parity.rsep", "cpu", root / "bad-parity.resl"])
    if len(sys.argv)==7:
        import shutil
        output = Path(sys.argv[6])
        output.mkdir(parents=True, exist_ok=True)
        for source in (rom, bundle, replay):
            shutil.copy2(source, output / source.name)
