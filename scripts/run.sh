#!/usr/bin/env bash
# Boots the image built by build-image.sh in QEMU (user-mode networking, gateway on
# localhost:8080) and serves the dashboard on http://localhost:3000.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
VM=${VM_DIR:-$HOME/dosekeeper-vm}
IMAGE=$VM/output/disk-qemu.vmdk
CONSOLE=$VM/console.log
BOOT_TIMEOUT=${QNX_BOOT_TIMEOUT:-120}
GATEWAY=http://127.0.0.1:8080

if [[ ! $BOOT_TIMEOUT =~ ^[1-9][0-9]{0,3}$ ]]; then
  echo "QNX_BOOT_TIMEOUT must be a whole number from 1 to 9999 (seconds)" >&2
  exit 1
fi
if [[ ! -f $IMAGE || ! -r $IMAGE ]]; then
  echo "Cannot read QNX image at $IMAGE. Build it first with scripts/build-image.sh." >&2
  exit 1
fi
for command in qemu-system-x86_64 curl node; do
  if ! command -v "$command" >/dev/null; then
    echo "Required host command missing: $command" >&2
    exit 1
  fi
done
if (exec 3<>/dev/tcp/127.0.0.1/8080) 2>/dev/null; then
  echo "Port 8080 is already in use. Stop the other gateway first." >&2
  exit 1
fi

gateway_ready() {
  local status
  if ! status=$(curl --fail --silent --max-time 1 "$GATEWAY/api/status"); then
    echo "Gateway did not answer"
    return 1
  fi
  node -e '
    const fail = (reason) => { console.log(reason); process.exitCode = 1; };
    try {
      const { os, pump } = JSON.parse(require("node:fs").readFileSync(0, "utf8")) ?? {};
      if (os?.sysname !== "QNX" || !/^8\.0(?:\.|$)/.test(os.release)) {
        fail("Gateway did not report QNX 8.0");
      } else if (!Number.isInteger(pump?.pid) || pump.pid <= 0) {
        fail("Simulated controller is unavailable");
      } else if (pump.motor_ok !== true) {
        fail("Simulated motor is unavailable");
      }
    } catch {
      fail("Gateway returned invalid status JSON");
    }
  ' <<< "$status"
}

KVM=(); [ -w /dev/kvm ] && KVM=(-enable-kvm -cpu host)
qemu-system-x86_64 "${KVM[@]}" -smp "${CPUS:-4}" -m 1G -nographic \
  -drive file="$IMAGE",if=ide,id=drv0 \
  -netdev user,id=net0,hostfwd=tcp:127.0.0.1:8080-:8080 -device virtio-net-pci,netdev=net0 \
  -object rng-random,filename=/dev/urandom,id=rng0 -device virtio-rng-pci,rng=rng0 \
  -serial file:"$CONSOLE" -monitor none &
QEMU=$! NODE=
cleanup() {
  local pids=("$QEMU")
  if [[ -n $NODE ]]; then pids+=("$NODE"); fi
  kill "${pids[@]}" 2>/dev/null || true
  wait "${pids[@]}" 2>/dev/null || true
}
trap cleanup EXIT
trap 'exit 143' TERM
trap 'exit 130' INT

echo "QNX booting (console: $CONSOLE)"
deadline=$((SECONDS + BOOT_TIMEOUT))
until issue=$(gateway_ready); do
  if ! kill -0 "$QEMU" 2>/dev/null; then
    echo "QEMU exited before DoseKeeper was ready. Check $CONSOLE." >&2
    exit 1
  fi
  if (( SECONDS >= deadline )); then
    echo "DoseKeeper boot timed out after ${BOOT_TIMEOUT}s. $issue. Check $CONSOLE." >&2
    exit 1
  fi
  sleep 1
done
echo "QNX 8.0 gateway reports the simulated controller and motor are ready"
QNX_URL=$GATEWAY node "$ROOT/web/server.mjs" &
NODE=$!
wait "$NODE"
