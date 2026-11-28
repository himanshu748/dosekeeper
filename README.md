# DoseKeeper

**Kill the controller. The dose survives.**

DoseKeeper is a fault-tolerant infusion pump safety core for **QNX OS 8.0**. The dose controller runs as its own process behind a QNX resource manager (`/dev/pump`), a watchdog respawns it within milliseconds, CPU budgets keep its 100 Hz control loop on time under load, and a live web console lets you kill, freeze and starve it mid-infusion while it shows that the delivered volume stays exact and how long recovery took.

> A simulation built for the QNX Global Hackathon (Robotics & Medical). Not a medical device.

## Review the recorded QNX run

The [run notes](docs/run.md#measured-on-qemukvm-4-vcpus) document a QNX 8.0
QEMU/KVM run with 18 ms restart after controller exit, 63 ms recovery after a
hang, and a final simulated volume of 2,000,000 nL after three restarts. Open
`web/evidence.html` from the checkout to see the read-only evidence summary
without booting the VM. These are historical emulator measurements.

On a host with GCC, `make -C qnx test` runs the portable parser, encoder,
interlock, and checkpoint tests. Those checks also run on GitHub Actions.

## What it shows

| You do this in the console | QNX does this | You see |
|---|---|---|
| **Kill controller** (`SIGKILL pumpd`) | `guardian` gets `waitpid()`, respawns `pumpd`; the new process reloads the prescription checkpoint and reads the delivered volume back from the motor encoder | Recovery in milliseconds, 0 nL lost or repeated, motor never stopped |
| **Freeze controller** (`SIGSTOP pumpd`) | Heartbeat pulses stop; after 40 ms `guardian` kills and respawns it | Hang detected and recovered before the motor's 100 ms hardware watchdog fires |
| **Rogue CPU load** (FIFO priority 60 on every CPU) | The controller (priority 50) is starved; the motor's hardware watchdog stops the pump | Deadline misses, a watchdog trip, the infusion falling behind — but never an overdose |
| **Budgeted CPU load** (same load under `SCHED_SPORADIC`, 2 ms / 10 ms) | The scheduler drops the load below the controller once its budget is spent | 100 % CPU busy, zero deadline misses, tick jitter around the budget size |

## Architecture

Five processes, each in its own address space:

| Process | Role | Priority | QNX features |
|---|---|---|---|
| `motor_sim` | Simulated pump hardware: integrates volume in nanolitres, hard position interlock, 100 ms hardware watchdog | FIFO 62 | `name_attach()`, `MsgReceive()`/`MsgReply()`, timer pulses |
| `pumpd` | Dose controller: 100 Hz control thread + `/dev/pump` resource manager | FIFO 50 / 20 | `resmgr_attach()`, `iofunc_*`, `MsgSend()`, `MsgSendPulse()` heartbeat, priority-inheriting mutex |
| `guardian` | Watchdog: spawns, reaps and respawns `pumpd`; kills it if heartbeats stop | FIFO 55 | `posix_spawn()`, `waitpid()`, heartbeat and timer pulses |
| `loadgen` | CPU hog, rogue or budgeted | FIFO 60 or SPORADIC 60→8 | `SCHED_SPORADIC` budgets |
| `gateway` | HTTP/JSON API for the dashboard; talks to the controller only through `/dev/pump` | FIFO 10 | POSIX sockets, signals for fault injection |

Why the dose is safe across a crash:

1. **The controller holds no dose state in RAM.** Delivered volume = motor encoder − encoder value at start. The prescription (rate, volume, start position) is checkpointed in shared memory, double-buffered and CRC-32 protected, so a crash mid-write falls back to the previous slot.
2. **The motor refuses to overshoot.** Every command carries the absolute stop position; the motor stops exactly there even if the controller is dead.
3. **The motor stops itself** if it is not commanded for 100 ms, so a starved or dead controller can only ever under-deliver, never over-deliver.
4. **Input is validated at the trust boundary.** `/dev/pump` rejects rates above 999.9 mL/h, volumes above 1000 mL and anything malformed with `EINVAL`.

## Build and run

Requirements: Linux x86_64 host with KVM, **QNX SDP 8.0** (free [QNX Everywhere](https://www.qnx.com/products/everywhere) licence), Node.js 18+.

```sh
source ~/qnx800/qnxsdp-env.sh
make -C qnx                      # qcc -Vgcc_ntox86_64: motor_sim pumpd guardian loadgen gateway
make -C qnx test                 # host unit tests for the dose logic
```

The image and QEMU scripts are in `scripts/`; see [docs/run.md](docs/run.md).

Talk to the controller directly on the target:

```sh
echo "start 360 6" > /dev/pump   # 360 mL/h, 6 mL
cat /dev/pump                    # JSON status
echo "start 2000 6" > /dev/pump  # rejected: Invalid argument
```

## Codelab

[Your first QNX resource manager in 30 minutes](docs/tutorial-first-resource-manager.md) — build `/dev/tally`, the same pattern `/dev/pump` uses.

## Licence

MIT
