#!/usr/bin/env bash
# Boots the image built by build-image.sh in QEMU (user-mode networking, gateway on
# localhost:8080) and serves the dashboard on http://localhost:3000.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
VM=${VM_DIR:-$HOME/dosekeeper-vm}
KVM=(); [ -w /dev/kvm ] && KVM=(-enable-kvm -cpu host)
qemu-system-x86_64 "${KVM[@]}" -smp "${CPUS:-4}" -m 1G -nographic \
  -drive file="$VM/output/disk-qemu.vmdk",if=ide,id=drv0 \
  -netdev user,id=net0,hostfwd=tcp:127.0.0.1:8080-:8080 -device virtio-net-pci,netdev=net0 \
  -object rng-random,filename=/dev/urandom,id=rng0 -device virtio-rng-pci,rng=rng0 \
  -serial file:"$VM/console.log" -monitor none &
QEMU=$!
trap 'kill $QEMU 2>/dev/null' EXIT
echo "QNX booting (console: $VM/console.log)"
until curl -sf http://127.0.0.1:8080/api/status >/dev/null; do sleep 1; kill -0 $QEMU; done
echo "QNX gateway up"
QNX_URL=http://127.0.0.1:8080 node "$ROOT/web/server.mjs"
