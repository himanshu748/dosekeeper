# DoseKeeper one-take recording

Target 2:30. Film the QNX 8.0 simulation using the actual console and keep the published video under three minutes.

## Set up

1. Follow [the build and run instructions](docs/run.md#1-install-sdp-80) on a Linux x86-64 host with QNX SDP 8.0, QEMU/KVM and Node.js. Open the target console and confirm it shows the QNX version reported by the gateway.
2. Rehearse with a simulation workload that stays active for about 110 seconds from the Start click. Keep the workload active through both 3-second rogue and 15-second budgeted windows. Begin the actual take before clicking Start.
3. Keep the OS version, simulated volume, recovery time, watchdog trips, deadline counts and event log legible at 1080p. Deadline counts reset when the controller restarts. Compare counts only within one controller generation and use the event log across restarts.

## One continuous take

| Time | Action | Keep on screen |
|---|---|---|
| 0:00–0:20 | Show QNX 8.0 and the ready controller. Click **Start infusion** in the simulator. | Target OS, controller PID and simulated volume increasing. |
| 0:20–0:40 | Click **Kill controller** and wait for recovery. | Old and new controller generation, recovery time and volume. |
| 0:40–1:00 | Click **Freeze controller** and wait for recovery. | Hang detection and watchdog counter. |
| 1:00–1:20 | Click **Rogue CPU load** and wait for the load to end and the console to recover. | Controller generation, deadline events and any motor watchdog trip. |
| 1:20–1:50 | Click **Budgeted CPU load** and wait for the full 15-second window. | Before and after deadline counts and observed tick lateness. |
| 1:50–2:30 | Let the simulated run complete and show the repository URL. | Terminal state, requested and delivered volume, event history. |

If a metric differs from the earlier run notes, show what the target reports. After recording, save frames from the same take showing the QNX identity, controller recovery, the two load windows and final simulated volume. Upload the continuous take publicly to YouTube for Devpost.
