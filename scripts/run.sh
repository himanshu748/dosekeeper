#!/usr/bin/env bash
# Boots the image built by build-image.sh and serves the dashboard on http://localhost:3000.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
QNX_SDP=${QNX_SDP:-$HOME/qnx800}
VM=${VM_DIR:-$HOME/dosekeeper-vm}
source "$QNX_SDP/qnxsdp-env.sh"
cd "$VM"
mkqnximage --run
IP=$(mkqnximage --getip)
echo "QNX target at $IP"
QNX_URL="http://$IP:8080" exec node "$ROOT/web/server.mjs"
