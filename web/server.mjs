// Serves the dashboard and proxies /api/* to the DoseKeeper gateway running on QNX.
//   QNX_URL=http://127.0.0.1:8080 PORT=3000 node web/server.mjs
import http from 'node:http';
import { readFile } from 'node:fs/promises';
import { extname, join, normalize } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('.', import.meta.url));
const target = new URL(process.env.QNX_URL || 'http://127.0.0.1:8080');
const port = Number(process.env.PORT || 3000);
const types = { '.html': 'text/html', '.css': 'text/css', '.js': 'text/javascript', '.svg': 'image/svg+xml', '.png': 'image/png' };

http.createServer(async (req, res) => {
  if (req.url.startsWith('/api/')) {
    const up = http.request({ host: target.hostname, port: target.port, path: req.url, method: req.method, timeout: 8000 }, (r) => {
      res.writeHead(r.statusCode, { 'content-type': 'application/json', 'cache-control': 'no-store' });
      r.pipe(res);
    });
    up.on('timeout', () => up.destroy(new Error('timeout')));
    up.on('error', (e) => { res.writeHead(502, { 'content-type': 'application/json' }); res.end(JSON.stringify({ ok: false, error: `QNX target unreachable: ${e.message}` })); });
    req.pipe(up);
    return;
  }
  let raw;
  try { raw = decodeURIComponent(req.url.split('?')[0]); } catch { res.writeHead(400); return res.end(); }
  const path = normalize(raw).replace(/^(\.\.[/\\])+/, '');
  const file = join(root, path === '/' ? 'index.html' : path);
  if (!file.startsWith(root) || file.endsWith('.mjs')) { res.writeHead(404); return res.end(); }
  try {
    const body = await readFile(file);
    res.writeHead(200, { 'content-type': types[extname(file)] || 'application/octet-stream' });
    res.end(body);
  } catch { res.writeHead(404); res.end('not found'); }
}).listen(port, () => console.log(`DoseKeeper dashboard on http://localhost:${port} -> ${target.href}`));
