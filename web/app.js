// DoseKeeper live console: polls the QNX gateway and renders what the target reports.
const $ = (id) => document.getElementById(id);
const C = 2 * Math.PI * 92, HERO_C = 2 * Math.PI * 130;
const ml = (nl) => (nl / 1e6).toFixed(3);

let last = null, lastEventId = 0, startId = 0, session = null, prevNow = null, toastTimer;

function newSession(p) {
  return { rxId: p.rx_id, rateNlMs: p.rate_ml_h * 1e6 / 3.6e6, vtbi: p.vtbi_nl, runMs: 0, t0: null,
           points: [], faults: [], back: false, bolus: false, prevDelivered: 0 };
}

function toast(msg, err) {
  const t = $('toast');
  t.textContent = msg; t.className = 'toast show' + (err ? ' err' : '');
  clearTimeout(toastTimer); toastTimer = setTimeout(() => (t.className = 'toast'), 2600);
}

async function post(api) {
  try {
    const r = await fetch('/api/' + api, { method: 'POST' });
    const j = await r.json();
    if (!j.ok) toast(`QNX refused: ${j.error}`, true);
    return j.ok;
  } catch { toast('QNX target unreachable', true); return false; }
}

$('rx-form').addEventListener('submit', async (e) => {
  e.preventDefault();
  if (await post(`start?rate=${encodeURIComponent($('rate').value)}&vtbi=${encodeURIComponent($('vtbi').value)}`))
    toast(`Wrote "start ${$('rate').value} ${$('vtbi').value}" to /dev/pump`);
});
document.querySelectorAll('[data-api]').forEach((b) => b.addEventListener('click', async () => {
  b.disabled = true;
  const ok = await post(b.dataset.api);
  if (ok && b.classList.contains('fault')) toast(`${b.querySelector('b').textContent}: injected`);
  setTimeout(() => (b.disabled = false), 400);
}));

const fmtT = (ms) => { const s = ms / 1000; return `${Math.floor(s / 60)}:${(s % 60).toFixed(1).padStart(4, '0')}`; };
function describe(e) {
  switch (e.kind) {
    case 'boot': return `pumpd gen ${e.gen} started, no prescription`;
    case 'start': return `start ${(e.a / 10).toFixed(1)} mL/h, ${ml(e.b)} mL`;
    case 'pause': return `paused at ${ml(e.a)} mL`;
    case 'resume': return `resumed at ${ml(e.a)} mL`;
    case 'stop': return `stopped at ${ml(e.a)} mL`;
    case 'done': return `infusion complete: ${ml(e.a)} mL`;
    case 'resumed': return `gen ${e.gen} reloaded prescription from checkpoint`;
    case 'fault_kill': return `SIGKILL → pumpd pid ${e.a}`;
    case 'fault_hang': return `SIGSTOP → pumpd pid ${e.a}`;
    case 'died': return `guardian: controller died (signal ${e.a})`;
    case 'hung': return `guardian: no heartbeat for ${e.a} ms → SIGKILL`;
    case 'restarted': return `gen ${e.gen} in control after ${(e.a / 1000).toFixed(1)} ms`;
    case 'hw_trip': return `motor watchdog stopped pump (${e.a} ms silent)`;
    case 'deadline_miss': return `control tick ${(e.a / 1000).toFixed(1)} ms late`;
    case 'load': return e.a === 1 ? 'rogue load started (FIFO 60)' : e.a === 2 ? 'budgeted load started (sporadic)' : 'load stopped';
    case 'motor_lost': return 'motor link lost → ALARM';
    default: return e.kind;
  }
}

function flashNode(id, cls, ms) {
  const n = $(id); n.classList.add(cls);
  setTimeout(() => n.classList.remove(cls), ms);
}

function onEvents(events, nowMs) {
  const list = $('events');
  for (const e of events) {
    if (e.id <= lastEventId) continue;
    lastEventId = e.id;
    if (e.kind === 'start') startId = e.id;
    if (session && ['fault_kill', 'fault_hang', 'hung', 'hw_trip'].includes(e.kind))
      session.faults.push({ ms: session.runMs - (nowMs - e.t_ms), kind: e.kind });
    if (['fault_kill', 'fault_hang', 'died', 'hung'].includes(e.kind)) flashNode('n-pumpd', 'dead', 1500);
    if (e.kind === 'restarted') { $('m-rec').parentElement.classList.remove('flash'); void $('m-rec').offsetWidth; $('m-rec').parentElement.classList.add('flash'); }
    const li = document.createElement('li');
    li.className = 'k-' + e.kind;
    li.innerHTML = `<time>${fmtT(e.t_ms)}</time><span></span>`;
    li.lastChild.textContent = describe(e);
    list.prepend(li);
  }
  while (list.children.length > 60) list.lastChild.remove();
}

function setConn(cls, text) { $('conn').className = 'conn ' + cls; $('conn-text').textContent = text; }

function render(s) {
  if (prevNow !== null && s.now_ms < prevNow) {           /* target rebooted: its clock and event ids restart */
    lastEventId = 0; startId = 0; session = null; prevNow = null;
    $('events').replaceChildren();
  }
  const p = s.pump;
  setConn('up', `${s.os.sysname} ${s.os.release} · ${s.os.machine} · ${s.os.cpus} CPU`);
  $('h-os').textContent = `${s.os.sysname} ${s.os.release}`;
  onEvents(s.events, s.now_ms);

  const g = document.querySelector('.gauge');
  if (!p) {
    g.className = 'panel gauge down'; $('state').textContent = 'NO CONTROLLER';
    $('m-pid').textContent = '—';
    prevNow = s.now_ms;
    return;
  }

  if (!session || session.rxId === 0 || (startId && session.startId !== startId)) {
    if (p.state === 'RUNNING' || !session) {
      session = newSession(p); session.startId = startId;
      if (session.rateNlMs) session.runMs = p.delivered_nl / session.rateNlMs;   /* joined mid-infusion */
    }
  }
  if (prevNow !== null && p.state === 'RUNNING') session.runMs += s.now_ms - prevNow;
  prevNow = s.now_ms;
  session.rxId = p.rx_id;

  const d = p.delivered_nl, v = p.vtbi_nl || 1;
  if (d + 1 < session.prevDelivered) session.back = true;
  if (p.motor_rate_ml_h > p.rate_ml_h + 0.05) session.bolus = true;
  session.prevDelivered = d;
  const ideal = Math.min(v, session.rateNlMs * session.runMs);
  session.points.push({ ms: session.runMs, d, ideal });
  if (session.points.length > 4000) session.points.splice(0, 2);

  g.className = 'panel gauge ' + p.state.toLowerCase();
  $('state').textContent = p.state;
  $('delivered').textContent = ml(d);
  $('vtbi-show').textContent = (p.vtbi_nl / 1e6).toFixed(1);
  $('rate-show').textContent = `motor ${p.motor_rate_ml_h.toFixed(1)} mL/h · set ${p.rate_ml_h.toFixed(1)}`;
  $('dial-arc').style.strokeDashoffset = C * (1 - Math.min(1, d / v));
  $('dial-ideal').style.strokeDashoffset = C * (1 - Math.min(1, ideal / v));
  $('hero-arc').style.animation = 'none';
  $('hero-arc').style.strokeDashoffset = HERO_C * (1 - Math.min(1, d / v));

  const over = Math.max(0, d - p.vtbi_nl);
  const drift = session.rateNlMs ? Math.max(0, ideal - d) / session.rateNlMs / 1000 : 0;
  const set = (id, txt, cls = '') => { $(id).textContent = txt; $(id).className = cls; };
  set('i-over', `${over} nL`, over ? 'bad' : '');
  set('i-bolus', session.bolus ? 'YES' : 'never', session.bolus ? 'bad' : '');
  set('i-back', session.back ? 'YES' : 'never', session.back ? 'bad' : '');
  set('i-drift', `${drift.toFixed(2)} s`, drift > 0.5 ? 'warn' : '');
  if (p.state === 'DONE') set('i-final', d === p.vtbi_nl ? `exact · ${d} nL` : `${d - p.vtbi_nl} nL off`, d === p.vtbi_nl ? '' : 'bad');
  else set('i-final', p.rx_id ? `${ml(p.vtbi_nl - Math.min(d, p.vtbi_nl))} mL to go` : '—', 'warn');
  $('h-err').textContent = `${over} nL`;

  const rec = p.last_recovery_us ? `${(p.last_recovery_us / 1000).toFixed(1)} ms` : '—';
  $('m-rec').textContent = rec; $('h-rec').textContent = rec;
  $('m-recmax').textContent = `max ${p.max_recovery_us ? (p.max_recovery_us / 1000).toFixed(1) + ' ms' : '—'}`;
  $('m-restarts').textContent = p.restarts;
  $('m-pid').textContent = p.pid; $('m-gen').textContent = `generation ${p.generation}`;
  $('m-jit').textContent = `${(p.late_1s_us / 1000).toFixed(2)} ms`;
  $('m-miss').textContent = p.deadline_misses;
  $('m-trips').textContent = p.hw_trips;

  const busy = p.state === 'RUNNING' || p.state === 'PAUSED';
  $('b-start').disabled = busy; $('b-pause').disabled = p.state !== 'RUNNING';
  $('b-resume').disabled = p.state !== 'PAUSED'; $('b-stop').disabled = !busy && p.state !== 'ALARM';

  const lb = $('loadbar'), total = s.load.mode === 'rogue' ? 3000 : 15000;
  lb.style.setProperty('--c', s.load.mode === 'rogue' ? 'var(--violet)' : 'var(--green)');
  $('loadbar-fill').style.width = s.load.mode === 'off' ? '0' : `${(100 * s.load.remaining_ms) / total}%`;
  $('loadbar-text').textContent = s.load.mode === 'off' ? 'No load' : `${s.load.mode} load · ${(s.load.remaining_ms / 1000).toFixed(1)} s left · ${s.os.cpus} threads`;
  $('n-load').classList.toggle('hot', s.load.mode !== 'off');
  draw();
}

function draw() {
  const cv = $('chart'), dpr = devicePixelRatio || 1, w = cv.clientWidth, h = 220;
  if (cv.width !== w * dpr) { cv.width = w * dpr; cv.height = h * dpr; }
  const x = cv.getContext('2d');
  x.setTransform(dpr, 0, 0, dpr, 0, 0); x.clearRect(0, 0, w, h);
  if (!session || !session.points.length) return;
  const pts = session.points, maxMs = Math.max(10000, pts[pts.length - 1].ms * 1.08), maxV = session.vtbi * 1.05;
  const L = 44, B = h - 24, X = (ms) => L + (ms / maxMs) * (w - L - 8), Y = (nl) => B - (nl / maxV) * (B - 8);
  x.strokeStyle = 'rgba(148,163,184,.12)'; x.fillStyle = '#8a9ab3'; x.font = '11px JetBrains Mono'; x.lineWidth = 1;
  for (let i = 0; i <= 4; i++) {
    const v = (session.vtbi * i) / 4, y = Y(v);
    x.beginPath(); x.moveTo(L, y); x.lineTo(w - 8, y); x.stroke();
    x.fillText((v / 1e6).toFixed(1), 6, y + 4);
  }
  x.fillText(`${(maxMs / 1000).toFixed(0)} s`, w - 40, h - 6); x.fillText('0 s', L, h - 6);
  for (const f of session.faults) {
    x.strokeStyle = f.kind === 'hw_trip' ? 'rgba(251,191,36,.7)' : 'rgba(251,113,133,.8)';
    x.setLineDash([3, 3]); x.beginPath(); x.moveTo(X(f.ms), 8); x.lineTo(X(f.ms), B); x.stroke();
  }
  x.setLineDash([6, 5]); x.strokeStyle = 'rgba(96,165,250,.6)'; x.lineWidth = 1.5; x.beginPath();
  pts.forEach((p, i) => (i ? x.lineTo(X(p.ms), Y(p.ideal)) : x.moveTo(X(p.ms), Y(p.ideal)))); x.stroke();
  x.setLineDash([]);
  const grad = x.createLinearGradient(0, 0, 0, B); grad.addColorStop(0, 'rgba(94,234,212,.35)'); grad.addColorStop(1, 'rgba(94,234,212,0)');
  x.beginPath(); pts.forEach((p, i) => (i ? x.lineTo(X(p.ms), Y(p.d)) : x.moveTo(X(p.ms), Y(p.d))));
  x.strokeStyle = '#5eead4'; x.lineWidth = 2.2; x.stroke();
  x.lineTo(X(pts[pts.length - 1].ms), B); x.lineTo(X(pts[0].ms), B); x.fillStyle = grad; x.fill();
}

async function poll() {
  const ctl = new AbortController(), slow = setTimeout(() => setConn('down', 'QNX target not responding (CPU starved?)'), 900);
  const hard = setTimeout(() => ctl.abort(), 8000);
  try {
    const r = await fetch('/api/status', { cache: 'no-store', signal: ctl.signal });
    const s = await r.json();
    if (s.ok === false) throw new Error(s.error);
    last = s; render(s);
  } catch (e) {
    setConn('down', e.name === 'AbortError' ? 'QNX target not responding' : String(e.message || e).slice(0, 80));
  } finally { clearTimeout(slow); clearTimeout(hard); setTimeout(poll, 150); }
}
poll();
addEventListener('resize', draw);

const io = new IntersectionObserver((es) => es.forEach((e) => e.isIntersecting && (e.target.classList.add('in'), io.unobserve(e.target))), { threshold: 0.15 });
document.querySelectorAll('.reveal').forEach((el) => io.observe(el));
