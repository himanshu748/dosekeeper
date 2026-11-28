import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { test } from 'node:test';
import { fileURLToPath } from 'node:url';

const launcher = fileURLToPath(new URL('./run.sh', import.meta.url));

function launch(t, mode, timeout = '1') {
  const dir = mkdtempSync(join(tmpdir(), 'dosekeeper-boot-'));
  t.after(() => rmSync(dir, { recursive: true, force: true }));
  mkdirSync(join(dir, 'output'));
  if (mode !== 'missing-image') writeFileSync(join(dir, 'output/disk-qemu.vmdk'), '');
  const command = (name, text) => writeFileSync(join(dir, name), text, { mode: 0o755 });
  command('qemu-system-x86_64', `#!/bin/bash
echo "$$" > "$BOOT_FIXTURE/qemu-pid"
[[ $BOOT_CASE == early-exit ]] && exit 1
exec sleep 10
`);
  command('curl', `#!/bin/bash
case "$BOOT_CASE" in
  early-exit) exit 7 ;;
  unrelated) echo '{"ok":true}' ;;
  warming)
    if [[ -e $BOOT_FIXTURE/probed ]]; then
      touch "$BOOT_FIXTURE/ready"
      echo '{"os":{"sysname":"QNX","release":"8.0.0"},"pump":{"pid":42,"motor_ok":true}}'
    else
      touch "$BOOT_FIXTURE/probed"
      echo '{"os":{"sysname":"QNX","release":"8.0.0"},"pump":null}'
    fi ;;
  *) echo '{"os":{"sysname":"QNX","release":"8.0.0"},"pump":{"pid":42,"motor_ok":false}}' ;;
esac
`);
  command('node', `#!/bin/bash
if [[ $1 == */web/server.mjs ]]; then
  touch "$BOOT_FIXTURE/dashboard-started"
  [[ -e $BOOT_FIXTURE/ready ]]
else
  exec "$REAL_NODE" "$@"
fi
`);
  const result = spawnSync('bash', [launcher], {
    env: { ...process.env, PATH: `${dir}:${process.env.PATH}`, VM_DIR: dir,
      QNX_BOOT_TIMEOUT: timeout, BOOT_CASE: mode, BOOT_FIXTURE: dir,
      REAL_NODE: process.execPath },
    encoding: 'utf8',
    timeout: 6000,
  });
  assert.ifError(result.error);
  const pidPath = join(dir, 'qemu-pid');
  if (existsSync(pidPath)) {
    const pid = Number(readFileSync(pidPath, 'utf8'));
    assert.throws(() => process.kill(pid, 0), { code: 'ESRCH' }, 'QEMU child should be cleaned up');
  }
  return { ...result, started: existsSync(join(dir, 'dashboard-started')), vmStarted: existsSync(pidPath) };
}

test('waits for the simulated controller and motor before starting the console', (t) => {
  const result = launch(t, 'warming', '4');
  assert.equal(result.status, 0, result.stdout + result.stderr);
  assert.equal(result.started, true);
});

test('times out if only the gateway responds', (t) => {
  const result = launch(t, 'no-motor');
  assert.equal(result.status, 1);
  assert.equal(result.started, false);
  assert.match(result.stderr, /timed out.*motor/i);
  assert.match(result.stderr, /console\.log/);
});

test('waits for a QNX 8.0 status response', (t) => {
  const result = launch(t, 'unrelated');
  assert.equal(result.status, 1);
  assert.equal(result.started, false);
  assert.match(result.stderr, /timed out.*QNX 8\.0/i);
});

test('reports a guest exit before readiness', (t) => {
  const result = launch(t, 'early-exit', '4');
  assert.equal(result.status, 1);
  assert.equal(result.started, false);
  assert.match(result.stderr, /QEMU exited.*console\.log/);
});

test('reports a missing image before starting QEMU', (t) => {
  const result = launch(t, 'missing-image');
  assert.equal(result.status, 1);
  assert.equal(result.vmStarted, false);
  assert.match(result.stderr, /build-image\.sh/);
});
