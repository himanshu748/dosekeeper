# DoseKeeper demo video — one-take shotlist (target 2:30, hard limit 3:00)

The QNX hackathon requires a video under 3 minutes showing the project functioning on QNX 8.0. Film on the machine that has QNX SDP 8.0 installed; every scene below is real, nothing staged.

## Before filming

1. Build and boot per `docs/run.md` §1–3 — QEMU/KVM running, dashboard open in a browser at the console URL, terminal with the QEMU session beside it (side-by-side or quick alt-tab).
2. Start an infusion from the console so the dose is mid-flight before you record.
3. Font large enough to read in a 1080p frame. No notifications, no other windows.

## Scenes

| Time | Screen | Action | Narration/caption |
|---|---|---|---|
| 0:00–0:15 | Dashboard | Let it run: volume climbing, tick health green | "A 100 Hz dose controller running as its own QNX process. The motor is hardware. The watchdog is watching." |
| 0:15–0:35 | Terminal | `pidin` (or `ls /proc` fallback) showing pumpd, guardian, motor_sim, loadgen, gateway as separate processes | "Five processes, separate address spaces — this is QNX, not a Linux container." Caption: `QNX 8.0, QEMU/KVM` |
| 0:35–1:05 | Console | Press **Kill controller**. Show recovery time and volume continuity in the same frame | "SIGKILL. The guardian respawns it; the prescription reloads, the encoder tells the truth. Recovery measured in milliseconds — zero nanolitres lost or repeated." |
| 1:05–1:30 | Console | Press **Freeze controller**. Show hang detection beat the motor's 100 ms hardware watchdog | "SIGSTOP — a hang, not a crash. The heartbeat stops, the guardian fires before the hardware watchdog does." |
| 1:30–2:00 | Console | **Rogue load** then **budgeted load** back-to-back: deadline misses appear, then disappear under the sporadic budget | "Uncontrolled load starves the controller and the pump falls behind — but never overdoses. The same load under a CPU budget: 100% busy, zero misses." |
| 2:00–2:20 | Console | Let the infusion complete; show exact final volume | "The dose finishes exact — the number from docs/run.md, not a demo script." |
| 2:20–2:30 | Terminal | `uname -a` showing QNX, then the repo URL as end card | "QNX OS 8.0. DoseKeeper — github.com/himanshu748/dosekeeper" |

## Rules while filming

- Show the fault-injection buttons actually being pressed and the console reacting in the same continuous shot — no cut between cause and effect.
- If a recovery number differs from docs/run.md, show the real number. Never edit measurements.
- No music with rights problems; silence or a generated bed is fine.
- Keep it under 3:00 — judges are not required to watch past that.
- Upload to YouTube as public; paste the URL into the Devpost submission.
