#!/usr/bin/env bash
# Boots the image built by build-image.sh in QEMU (user-mode networking, gateway on
# localhost:8080) and serves the dashboard on http://localhost:3000.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
VM=${VM_DIR:-$HOME/dosekeeper-vm}
if (exec 3<>/dev/tcp/127.0.0.1/8080) 2>/dev/null; then
  echo "port 8080 is already in use; stop the other gateway first" >&2; exit 1
fi
KVM=(); [ -w /dev/kvm ] && KVM=(-enable-kvm -cpu host)
qemu-system-x86_64 "${KVM[@]}" -smp "${CPUS:-4}" -m 1G -nographic \
  -drive file="$VM/output/disk-qemu.vmdk",if=ide,id=drv0 \
  -netdev user,id=net0,hostfwd=tcp:127.0.0.1:8080-:8080 -device virtio-net-pci,netdev=net0 \
  -object rng-random,filename=/dev/urandom,id=rng0 -device virtio-rng-pci,rng=rng0 \
  -serial file:"$VM/console.log" -monitor none &
QEMU=$! NODE=
trap 'kill $QEMU $NODE 2>/dev/null' EXIT
trap 'exit 143' TERM
trap 'exit 130' INT
echo "QNX booting (console: $VM/console.log)"
until kill -0 $QEMU && curl -sf http://127.0.0.1:8080/api/status >/dev/null; do sleep 1; done
echo "QNX gateway up"
QNX_URL=http://127.0.0.1:8080 node "$ROOT/web/server.mjs" &
NODE=$!
wait $NODE
