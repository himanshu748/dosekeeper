# DoseKeeper — Devpost submission draft

**Tagline:** Kill the controller. The dose survives. A fault-tolerant infusion pump safety core on QNX 8.0.

**Category:** Robotics & Medical

## Inspiration

Infusion pumps are the classic embedded-safety nightmare: a software crash mid-dose can injure a patient. Most hackathon demos dodge the crash. We wanted to walk straight into it — build a pump controller on a real microkernel RTOS and then kill it, freeze it and starve it live on camera while the dose keeps flowing exactly.

## What it does

DoseKeeper runs a simulated infusion pump as five separate QNX 8.0 processes, each in its own address space:

- `pumpd` — the dose controller: a 100 Hz control thread plus a `/dev/pump` resource manager
- `motor_sim` — simulated pump hardware integrating volume in nanolitres, with a hard position interlock and a 100 ms hardware watchdog
- `guardian` — a watchdog that spawns, reaps and respawns the controller, and kills it if heartbeats stop
- `loadgen` — a CPU hog, rogue or running under a `SCHED_SPORADIC` budget
- `gateway` — an HTTP/JSON API so the live web console can inject faults

From the web console you can kill the controller (`SIGKILL`), freeze it (`SIGSTOP`), or flood every CPU with rogue or budgeted load — mid-infusion — and watch what the system does:

- **Kill:** the guardian respawns the controller in milliseconds; the new process reloads the prescription checkpoint and reads delivered volume back from the motor encoder. 0 nL lost or repeated.
- **Freeze:** heartbeats stop, and the hang is detected and recovered before the motor's hardware watchdog fires.
- **Rogue load:** the controller is starved, deadlines are missed, the hardware watchdog stops the pump — the infusion falls behind, but never overdoses.
- **Budgeted load:** the same load generator runs under a sporadic CPU budget. The console shows recent tick lateness, deadline events, and miss counts for the current controller generation.

## How we built it

QNX 8.0 (QNX Everywhere SDP) on QEMU/KVM. The controller uses `resmgr_attach()` and the `iofunc_*` layer to serve `/dev/pump`; processes communicate with `MsgSend()`/`MsgReceive()`/`MsgReply()` and timer pulses; the watchdog uses `posix_spawn()` and `waitpid()`; CPU control uses FIFO priorities 50–62 and `SCHED_SPORADIC` budgets. The dashboard is a small static web app proxied to the gateway process. Build and run instructions, including the exact SDP baseline and QEMU invocation, are in `docs/run.md`.

## Evidence it really runs on QNX

Previously documented in the [QEMU/KVM run notes](run.md#measured-on-qemukvm-4-vcpus) for a four-vCPU guest:

- SIGKILL restart: **18 ms**, dose resumed exactly
- SIGSTOP hang detection+recovery: **63 ms**
- Rogue load: **12 ms** restart and one simulated motor watchdog trip
- Infusion completed at exactly **2,000,000 nL** delivered

During the budgeted-load window the notes report about 1 ms of worst lateness per second and one 7 ms late tick when the load process started. The notes also describe idle QEMU scheduling noise. `web/evidence.html` presents the historical measurements as a read-only page. Portable host tests verify the command parser, simulated encoder, position interlock, and checkpoint logic.

## Challenges we ran into

- Making crash recovery exact: the respawned controller must reconstruct state from the checkpoint *and* the motor encoder without double-counting volume delivered mid-write.
- Distinguishing a hang from a crash: heartbeat timing had to beat the hardware watchdog without false positives under legitimate scheduling jitter.
- Demonstrating schedulability, not just correctness: the sporadic-budget comparison only means something if both runs use identical load.

## Accomplishments we're proud of

The recorded simulation finished at its configured volume after three controller restarts. The target console exposes the recovery intervals and motor watchdog events for that workload.

## What's next for DoseKeeper

A real hardware target (GPIO-driven pump on a supported board), dose-rate profiles with alarm bands, and a formal fault-injection matrix exportable as a test report.

## Built with

`c` `qnx` `qnx-8.0` `rtos` `resource-manager` `watchdog` `qemu` `embedded` `posix` `nodejs`

OpenAI Codex assisted with the reviewer evidence page and documentation checks.

## Links

- Repository: https://github.com/himanshu748/dosekeeper
- Build/run evidence: https://github.com/himanshu748/dosekeeper/blob/main/docs/run.md
- Devpost project: https://devpost.com/software/dosekeeper (draft)
- Public video: pending a native recording and public YouTube upload. Use the [one-take shotlist](../VIDEO_SHOTLIST.md) to capture the QNX guest in under three minutes.
