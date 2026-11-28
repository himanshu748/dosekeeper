# Build and run on QNX 8.0 in QEMU

Tested on Ubuntu 22.04 x86_64 with KVM, QNX SDP 8.0 (baseline `com.qnx.qnx800`), QEMU 6.2 and Node.js 20.

## 1. Install SDP 8.0

Get a free non-commercial licence from [QNX Everywhere](https://www.qnx.com/products/everywhere), install the QNX Software Center, then install the `com.qnx.qnx800` baseline to `~/qnx800`. Headless:

```sh
qnxsoftwarecenter_clt -myqnx.user "$EMAIL" -myqnx.password "$PASSWORD" -syncLicenseKeys
qnxsoftwarecenter_clt -myqnx.user "$EMAIL" -myqnx.password "$PASSWORD" -mirror
qnxsoftwarecenter_clt -myqnx.user "$EMAIL" -myqnx.password "$PASSWORD" \
  -installBaseline com.qnx.qnx800 -destination ~/qnx800 -cleanInstall
```

## 2. Build the image

```sh
scripts/build-image.sh
```

This compiles the five processes with `qcc -Vgcc_ntox86_64`, adds them to `/system/dosekeeper/` through `mkqnximage` snippets (`local/snippets/system_files.custom`), and starts them at boot from `post_start.custom`. The image lands in `~/dosekeeper-vm/output/`.

## 3. Boot it and open the dashboard

```sh
scripts/run.sh
```

The launcher boots QEMU with user-mode networking and forwards the guest gateway
to `127.0.0.1:8080`. It serves the dashboard on <http://localhost:3000>
after `/api/status` reports QNX 8.0, a controller process and a connected
simulated motor. Startup errors point to the serial log at
`~/dosekeeper-vm/console.log` (or `$VM_DIR/console.log`). The startup
timeout is 120 seconds. On a slower host, run
`QNX_BOOT_TIMEOUT=240 scripts/run.sh`.

## 4. Drive it from the shell

```sh
G=http://127.0.0.1:8080/api
curl -XPOST "$G/start?rate=360&vtbi=2"   # 360 mL/h, 2 mL
curl -XPOST $G/fault/kill                # SIGKILL pumpd mid-dose
curl -XPOST $G/fault/hang                # SIGSTOP pumpd: guardian notices the missing heartbeat
curl -XPOST "$G/load?mode=rogue"         # 3 s of FIFO 60 on every CPU
curl -XPOST "$G/load?mode=budgeted"      # 15 s of SCHED_SPORADIC 2 ms / 10 ms on every CPU
curl -s $G/status
```

## Measured on QEMU/KVM, 4 vCPUs

One 2 mL dose at 360 mL/h, with a kill, a hang and a rogue load during it:

| Fault | Restart time | Dose after recovery |
|---|---|---|
| `SIGKILL` pumpd | 18 ms | encoder = delivered, no step lost |
| `SIGSTOP` pumpd (heartbeat timeout 40 ms) | 63 ms | same |
| Rogue FIFO 60 load starves pumpd | 12 ms; motor watchdog tripped once | same |

The dose finished at exactly 2,000,000 nL after three restarts. Under the 15 s budgeted load the controller's worst lateness stayed about 1 ms per second, with one 7 ms tick at the moment the load process spawned. QEMU itself adds a 5 ms late tick every few seconds even at idle, so treat deadline counts as indicative, not as hardware numbers.
