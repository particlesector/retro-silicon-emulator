# M11 application-session validation

M11 moves CLI, desktop and audio-only execution onto the session contract.
The deterministic NES scheduler and frozen M8 fixtures remain unchanged.

## Comparison plan

Before migration, the accepted M10 Windows Release executable ran the generated
`rse-av-input-v1` smoke fixture for 30 seconds on this host. It reported four
software input responses: 27,982-28,974 us, mean 28,535 us, zero starvation and a
1,152-byte source target. Before comparison, a mean increase of at most 5 ms was
selected as acceptable host/refresh variation; any starvation or scripted
failure remains unacceptable. These measurements stop at software observation,
not physical controller-to-photon latency.

The final packages must retain the source margin, VSync, controls, colors and
independent producer. Test both extracted packages for ten minutes. Windows uses
Vulkan/WASAPI; local Linux uses Xvfb/Openbox/lavapipe and dummy audio. A fresh
native Ubuntu audible-continuity approval is still required; M8's manual approval
does not automatically cover this changed playback path.

## Automated coverage

- Windows Debug/Release and Ubuntu Release: 19 CTest suites; frontend-free
  Ubuntu Debug: 18 suites.
- Frozen fixture bytes/captures; legacy CLI arguments, JSON and exit codes;
  long CLI runs drain discarded PCM rather than filling the adapter's queue.
- Generic CLI/playback with a test-only 1 MHz machine, 3x2 RGBA video, 3:2 aspect,
  32 kHz stereo, missing outputs, no sound device, named inputs, atomic validation,
  released omitted ports, reset/replacement, owned frames and fault recovery.
- Producer continues while the GUI is blocked. Silent playback is paced even
  when GUI notifications arrive frequently.
- Separate Vulkan fake-system readback checks exercise source dimensions,
  letterboxing and texture recreation. Linux CI runs these under Xvfb.

## Acceptance record

Final package endurance and CI results are being collected. Native Linux audio
approval remains pending. Logs and packages on the development host are under
`build/m11/`; the PR's Linux CI artifact supplies the distributable package.

For native Ubuntu testing, extract the Linux ZIP and the PR CI's
`acceptance-set-v1` artifact. Alternatively generate the legal fixture set with
`rse-acceptance-tests --write-set acceptance-set` from a local build. Then run:

```sh
./rse-desktop --smoke /path/to/acceptance-set/rse-av-input-v1.nes --seconds 600
```

Confirm continuous tone, correct displayed output and normal exit, and retain
the final counters. The generated unsupported/invalid sibling fixtures must stay
beside the main fixture for the scripted recovery checks.
