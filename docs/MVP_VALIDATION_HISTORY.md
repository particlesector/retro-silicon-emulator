# MVP validation history

These are chronological records of M6-M8 development, including failures and
superseded implementations. Statements about pending checks or earlier buffer
sizes describe those builds, not the current accepted implementation. Keep this
evidence separate from the [accepted baseline](MVP_ACCEPTANCE.md#accepted-baseline).
Host-local logs referenced here are not bundled test assets.

## M7 desktop validation (superseded baseline)


All 15 CTest suites pass in Windows/MSVC and Linux/GCC Debug and Release;
Linux ASan/UBSan with leak detection also passes. Chip reference fixtures remain
unchanged. The desktop replay verifies 120 frames of identical CPU/frame/PCM
observations across different advancement chunks.

On November 14, 2026, an extracted Windows Release ZIP ran for 30 seconds on
Windows 11 build 26200, AMD Ryzen AI Max+ 395 / Radeon 8060S, SDL 3.4.14 and WASAPI.
It generated 1,801 frames, made two successful loads and one reset, recovered
from three expected file errors, passed five Vulkan readback checks, and reported
zero starvation. Startup buffering was then increased to 120-137 ms to cover
software Vulkan initialization; subsequent Windows smoke checks also pass.
The native Windows picker, visible emulated frame, reset/exit, and readable
welcome screen were inspected interactively.

The extracted Linux Release ZIP passed the same 30-second flow in
[CI run 43672447350](https://github.com/particlesector/retro-silicon-emulator/actions/runs/43672447350):
Ubuntu 24.04.5, SDL 3.4.14, Xvfb/Openbox, Mesa 25.2.8 lavapipe (llvmpipe), dummy
audio and a virtual standard gamepad. It generated 1,823 frames, passed five
Vulkan readback checks, and reported zero starvation with a peak source queue
of 13,116 bytes. Linux native GPU, physical audio/gamepad, Wayland, and native
file-portal behavior are not established by this virtual-display check.
M8 retains the frozen compatibility set and ten-minute physical-host sessions.

## M8 development and final acceptance


The set and expected observations were committed as `5005e45` before M8 desktop
testing. All 17 suites pass in Windows/MSVC Debug and Release, local Ubuntu/GCC
Release, and Linux CI Debug/Release plus ASan/UBSan with leak detection
([CI run 43678639983](https://github.com/particlesector/retro-silicon-emulator/actions/runs/43678639983)).
The CI desktop job also verifies the extracted Release package using the frozen
combined fixture; virtual display/audio remains distinct from physical playback.

The selected Linux endurance environment is Ubuntu 24.04.4 through WSLg 1.0.66
(WSL 2.6.1, kernel 6.6.87.2-microsoft-standard-WSL2) on the same Windows host.
It uses SDL 3.4.14, X11, PulseAudio and Mesa 25.2.8 software Vulkan (llvmpipe,
LLVM 20.1.2), not a native Linux GPU driver. Restarting WSL restored initially
missing display/audio sockets. The first five-second check failed with one
starvation request and 161,781 us maximum step work; the 30-second retry passed.
The first long session failed audible playback: the user heard silence with
occasional glitches, WSLg logged RDP sink queue overruns, and monitor captures
returned no samples. A separate `paplay` reference tone was also inaudible.
The stopped session reported 489,936 elapsed ms but only 77,169 simulated ms,
despite zero SDL starvation requests. This is retained as a failure, not a pass.
Restarting WSLg's PulseAudio service restored the reference tone, confirmed by
the user. The fresh emulator run initially sounded continuous, then glitched
again. It was stopped at 102,979 elapsed ms with 46,020 simulated ms and zero
starvation requests; WSLg again logged queue overruns. A recovered monitor
capture contained varying PCM, confirming delivery into the bridge before
the failure. A subsequent independent 120-second `paplay` tone sounded
continuous to the user, but the player failed to finish draining within its
150-second timeout (exit 124). These observations do not isolate the exact root
cause or establish successful Linux endurance. WSLg's reported
[pause/resume issue](https://github.com/microsoft/wslg/issues/1392) is a possible
lead, not a confirmed diagnosis. A standalone SDL tone probe, with the same
pause/clear/resume pattern but no emulation or Vulkan, also glitched and advanced
only 40,916 ms of generated PCM in 90,004 ms. This reproduces the failure outside
the emulator core; it does not identify which SDL/WSLg component is responsible.
These WSLg failures were not accepted as a substitute for native Linux testing.
The subsequent owner-approved native Ubuntu result is recorded below.
Logs are under
`build/m8/wslg-600s.*.log` and `wslg-recovered-600s.*.log` on the test host.

The first Windows session ran for 600,001 ms without a crash but failed the
strict zero-starvation check: 36,056 frames, 18 unsatisfied SDL requests,
599,941 simulated ms and a maximum frame work interval of 13,451 us. It overlapped
local builds and WSLg diagnostics. An instrumented quiet retry also failed:
18 requests between SDL ticks 383,841 and 384,011, a maximum loop gap of 291,496 us,
maximum whole-step work of 40,025 us and maximum frame work of 10,122 us. Both
results are retained as failures. Timing diagnostics include maximum whole-step,
event-pump, sleep and loop gaps plus first/last starvation ticks. Logs are under
`build/m8/windows-600s.*.log` and `windows-quiet-600s.*.log` on the test host.
The verified candidate requests high priority for the Windows audio-producing thread
only, with a nonfatal fallback if the OS rejects it. This is not a real-time or
system-wide policy. The extracted Release package at `6335078` passed its
600,001 ms run with Vulkan/WASAPI on Windows 11 Pro build 26200, an AMD Ryzen AI
Max+ 395 and Radeon 8060S (driver 32.0.31032.1003), using SDL 3.4.14 and MSVC 19.44.
It produced 36,068 frames and 28,806,774 samples (600,141 simulated ms), with zero
starvation requests, two loads, one reset, three expected errors and five GPU
readbacks. Peak audio/video queues were 13,116 bytes/12 frames; maximum frame,
whole-step, loop-gap, event-pump and sleep times were respectively 6,779, 38,602,
40,720, 3,271 and 3,368 us. The OS accepted high thread priority. Logs are
`build/m8/windows-priority-600s.*.log`. The Linux endurance gate remains open.

Subsequent Super Mario Bros. playtesting reported wrong colors and tearing.
The frontend candidate replaces the synthetic hue wheel with an explicit
64-color display palette and enables VSync. All 17 Windows Release suites pass.
Its 30-second Vulkan/WASAPI smoke check produced 1,812 frames in 30,012 ms,
30,146 simulated ms, zero starvation and five successful readbacks
(`build/m8-fixes/windows-vsync-30s.*.log`). This candidate has not repeated the
ten-minute endurance check or received visual gameplay confirmation. A local
ROM-only diagnostic observed 24-frame timer decrements during active Mario
play; chip clocks and frozen observations were not changed. No game assets
were added to the repository.

The user confirmed the corrected colors and absence of tearing, then reported
input lag. Instrumentation measured 141,297 us from the smoke key injection to
selection of its changed-color frame. Reducing the steady audio target from
120 ms to 50 ms, while retaining the larger startup/resize buffer, reduced the
observed interval to 79,251 us. VSync remains enabled. All 17 Release suites pass;
the 30,000 ms Windows smoke run reported zero starvation, five readbacks and
29,946 simulated ms (`build/m8-latency/low-latency-30s.*.log`). This measures
frontend frame selection, not physical input-to-display latency. Gameplay
confirmation and a new ten-minute Windows run remain pending for this candidate.

The 50 ms candidate was superseded by an independent frontend producer with
roughly 1 ms advancement chunks, a negotiated one-device-period + 2 ms PCM
target, and one latest completed frame. Core clocks and frozen captures remain
unchanged; VSync and the corrected palette are retained. All 18 Windows Debug
and Release suites pass, including the producer lifecycle/replay tests.
The 30-second run measured four input responses at 27,721-28,976 us (mean
28,243 us) with zero starvation. The extracted Release package then passed
600,001 ms: 36,050 frames, 599,857 simulated ms, 28,793,142 samples, zero
starvation, two loads, one reset, three expected errors and five readbacks.
Its source target was 1,152 bytes (12 ms), peak source queue 1,246 bytes,
and peak pending video one frame. Maximum chunk work was 1,782 us; even a
978,823 us event-pump stall did not starve audio. The scripted 150 ms GUI-block
check also passed. Four responses in this run measured 30,434-32,295 us (mean
31,401 us). These small samples are software observations, not a guaranteed
25 ms physical button-to-screen result. The user confirmed the Windows gameplay
feel, corrected colors and tear-free presentation. Logs are
`build/m8-responsive/windows-responsive-600s.*.log`. The tested executable's
SHA-256 is `86c3b882af2517a5a244f2b91b9b44807f4dde7932f78ee37e3ea1270ee05b07`.

### Native Linux manual approval

On November 15, 2026, project owner Chris confirmed that the smoke test ran for
ten continuous minutes without issues on a native Ubuntu 24.04 LTS machine:
the blue test screen and constant audio played successfully. He also reported
successful extended Super Mario Bros. gameplay and explicitly approved the
Linux acceptance result manually. The M8 Linux gate is accepted on this basis.

This is owner-reported manual validation. CPU/GPU/driver details, the exact
tested build identifier, pacing counters and the smoke log were not supplied;
Chris was away from the machine and requested approval without collecting
those details. No measurements are inferred from the gameplay report. The
failed WSLg sessions above remain separate evidence. Together with the recorded
Windows endurance run and passing automated checks, this closes M8 acceptance.
