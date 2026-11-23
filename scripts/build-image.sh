#!/usr/bin/env bash
# Builds the DoseKeeper binaries and a QNX 8.0 x86_64 QEMU image that starts them at boot.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
QNX_SDP=${QNX_SDP:-$HOME/qnx800}
VM=${VM_DIR:-$HOME/dosekeeper-vm}
source "$QNX_SDP/qnxsdp-env.sh"

make -C "$ROOT/qnx"
mkdir -p "$VM/local/snippets"
cd "$VM"
{
  for b in motor_sim pumpd guardian loadgen gateway; do
    echo "[perms=0755] dosekeeper/$b=$ROOT/qnx/build/$b"
  done
} > local/snippets/system_files.custom
cat > local/snippets/post_start.custom <<'SH'
/system/dosekeeper/motor_sim &
sleep 1
/system/dosekeeper/guardian /system/dosekeeper/pumpd &
/system/dosekeeper/gateway 8080 /system/dosekeeper/loadgen &
SH
mkqnximage --type=qemu --arch=x86_64 --ssh-ident=none --hostname=dosekeeper --build
