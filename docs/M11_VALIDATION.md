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

Windows MSVC 19.44 Debug/Release passed all 19 suites; Ubuntu 24.04 WSL/GCC 13.3
passed 18 frontend-free Debug suites and 19 desktop-enabled Release suites.
Both hosts passed the separate fake-system Vulkan readback test. The
[implementation CI run](https://github.com/particlesector/retro-silicon-emulator/actions/runs/43816855432)
passed Debug, Release, ASan/UBSan and Linux package/render/smoke/early-exit checks.

The extracted Windows package completed 600,002 ms using Vulkan/WASAPI, with
zero starvation, five readbacks and four responses averaging 26,569 us
(25,046-27,574 us). It produced 35,992 frames and 598,896 simulated ms, with a
1,152-byte source target, 1,248-byte peak and one buffered video frame.

A subsequent back-to-back 30-second comparison used the same host and fixture:

| Build | Elapsed ms | Simulated ms | Mean response us | Starvation |
| --- | ---: | ---: | ---: | ---: |
| Accepted M10 | 30,001 | 29,876 | 21,913 | 0 |
| M11 package | 30,002 | 29,867 | 19,034 | 0 |

Both retained the 1,152-byte source target, five readbacks and four responses.
The observed response/pacing meets the comparison plan; refresh phase and host
load still affect these small software samples. No physical latency guarantee
or subjective listening approval is inferred from counters.

Audio-only playback completed five seconds with zero starvation on both
WASAPI and Linux dummy audio. The first command-hosted Linux endurance attempt
was interrupted at 596,403 ms; the early-exit guard correctly failed it.

The detached WSL/Xvfb/dummy-audio rerun also **failed acceptance**: it recorded
one starvation request (1,810 bytes) and reached the external 630-second timeout
with only 596,157 ms on the application's timer (`ENDURANCE_EXIT=124`). It had
five readbacks and four responses, but those do not override the failure.
The recorded maximum UI sleep was 40,417 us. The root cause of the long-run
virtual-host timing/continuity failure is not established; short CI checks do
not establish ten-minute Linux reliability. Queue targets have not been enlarged
to hide the result.

Native Ubuntu 24.04 ten-minute playback/audio testing remains required before
M11 is complete. No fresh native approval has been received; the PR stays draft.

Logs and packages on the development host are under `build/m11/`, including
`final-windows-endurance*.log`, `comparison-*.log`, `linux-endurance-detached*.log`
and `*audio-only.log`. The PR's Linux CI artifact supplies the distributable package.

For native Ubuntu testing, extract the Linux ZIP and the PR CI's
`acceptance-set-v1` artifact. Alternatively generate the legal fixture set with
`rse-acceptance-tests --write-set acceptance-set` from a local build. Then run:

```sh
./rse-desktop --smoke /path/to/acceptance-set/rse-av-input-v1.nes --seconds 600
```

Confirm continuous tone, correct displayed output and normal exit, and retain
the final counters. The generated unsupported/invalid sibling fixtures must stay
beside the main fixture for the scripted recovery checks.
