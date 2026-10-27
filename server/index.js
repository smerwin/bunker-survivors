#!/usr/bin/env node
// Bunker Survivors companion server.
// Serves the game client and streams StarCraft II art straight out of the player's own
// SC2 install (CASC storage) over the LAN, so phones and browsers can load the real models.
// No game files are copied, extracted or redistributed.
'use strict';
const http = require('http');
const fs = require('fs');
const os = require('os');
const path = require('path');
const zlib = require('zlib');
const { SC2Assets, findInstall } = require('./sc2');
const { loadGameData } = require('./gamedata');
const { ensureStructures } = require('./m3struct');
const { assetRoutes, MIME, BadRequest, decodePath } = require('./routes');

const PORT = parseInt(process.env.PORT || '8642', 10);
const HOST = process.env.HOST || '0.0.0.0';
const CLIENT_DIR = path.join(__dirname, '..', 'client');

function lanAddresses() {
  const out = [];
  for (const [ifname, addrs] of Object.entries(os.networkInterfaces())) {
    for (const a of addrs || []) {
      if (a.family !== 'IPv4' || a.internal) continue;
      if (/vEthernet|VirtualBox|VMware|WSL|Docker/i.test(ifname)) continue;
      out.push(a.address);
    }
  }
  return out;
}

async function main() {
  const install = findInstall(process.env.SC2_DIR);
  if (!install) {
    console.error([
      'Could not find a StarCraft II install (the folder with .build.info and SC2Data in it).',
      'Point SC2_DIR at it and retry, for example:',
      '  Windows:  set SC2_DIR=E:\\Games\\StarCraft II  then  npm start',
      '  macOS:    SC2_DIR="/Volumes/Games/StarCraft II" npm start',
    ].join('\n'));
    process.exit(1);
  }
  console.log('Opening StarCraft II storage at ' + install + ' ...');
  const sc2 = new SC2Assets(install);
  console.log('SC2 ' + sc2.version + ': ' + sc2.files.size + ' files indexed in ' + sc2.loadMs + 'ms');
  // Particle effects need the M3 structure description (downloaded once); without it only /fx/ fails.
  try { await ensureStructures(); } catch (e) { console.warn('Particle effects unavailable: ' + e.message); }

  // One bounded cache for decoded files and everything derived from them (gzip, PNG, PCM, glTF, effect JSON),
  // evicted oldest first.
  const cache = new Map();            // key -> Buffer
  let cacheBytes = 0;
  const CACHE_LIMIT = 400 * 1024 * 1024;
  function cachePut(key, buf) {
    cache.set(key, buf);
    cacheBytes += buf.length;
    while (cacheBytes > CACHE_LIMIT && cache.size > 1) {
      const [k, v] = cache.entries().next().value;
      cache.delete(k); cacheBytes -= v.length;
    }
    return buf;
  }
  function cached(key, make) {
    const hit = cache.get(key);
    if (hit) return hit;
    const made = make();
    return made ? cachePut(key, made) : made;      // a missing file (null) is not cached
  }

  function resolveAsset(reqPath) {
    return sc2.resolve(decodePath(reqPath));
  }

  const fxList = loadGameData().fxList();
  const assets = assetRoutes(sc2, fxList, cached);
  const ips = lanAddresses();
  const hostname = os.hostname().toLowerCase();
  // When bound to one address (e.g. a Tailscale IP), only that address is reachable, so advertise just it.
  const urls = HOST !== '0.0.0.0'
    ? ['http://' + HOST + ':' + PORT + '/']
    : ['http://' + hostname + '.local:' + PORT + '/', ...ips.map(ip => 'http://' + ip + ':' + PORT + '/')];

  const server = http.createServer((req, res) => {
    const u = new URL(req.url, 'http://x');
    // Same-origin only: no CORS header, so a page on another site cannot read the player's game files through
    // this server.
    const send = (code, type, body, extra) => {
      res.writeHead(code, Object.assign({ 'Content-Type': type }, extra || {}));
      res.end(body);
    };
    // Compressible payloads (models, effect data) are gzipped once and cached: phones on weak wifi load much faster.
    const gzOk = /\bgzip\b/.test(req.headers['accept-encoding'] || '');
    const sendGz = (key, type, buf, extra) => {
      if (!gzOk || buf.length < 1024) return send(200, type, buf, extra);
      const gz = cached('gz:' + key, () => zlib.gzipSync(buf, { level: 6 }));
      return send(200, type, gz, Object.assign({ 'Content-Encoding': 'gzip', 'Vary': 'Accept-Encoding' }, extra || {}));
    };
    try {
      const r = assets(u.pathname);
      if (r) {
        const extra = Object.assign(r.cache ? { 'Cache-Control': r.cache } : {}, r.name ? { 'X-SC2-Name': r.name } : {});
        return r.gzip && r.status === 200 ? sendGz(r.key, r.type, r.body, extra) : send(r.status, r.type, r.body, extra);
      }
      if (u.pathname.startsWith('/gltf/')) {
        // /gltf/assets/units/zerg/zergling/zergling.glb -> converted from the matching .m3
        const rel = decodePath(u.pathname.slice(6)).replace(/\.glb$/i, '.m3');
        const full = resolveAsset(rel);
        if (!full) return send(404, 'text/plain', 'model not found: ' + rel);
        const glb = cached('glb:' + full, () => require('./m3gltf').convertM3(sc2, full).glb);
        return send(200, 'model/gltf-binary', glb, { 'Cache-Control': 'no-cache' });
      }
      if (u.pathname === '/api/find') {
        const q = (u.searchParams.get('q') || '').toLowerCase();
        const limit = Math.min(500, parseInt(u.searchParams.get('limit'), 10) || 100);
        let re = null;
        try { if (u.searchParams.get('re') === '1') re = new RegExp(q, 'i'); } catch (e) { return send(400, 'text/plain', 'bad regex'); }
        const hits = q ? sc2.find(re || q, limit) : [];
        return send(200, 'application/json', JSON.stringify(hits.map(n => ({ name: n, size: sc2.files.get(n).size }))));
      }
      if (u.pathname === '/api/status') {
        return send(200, 'application/json', JSON.stringify({ sc2Version: sc2.version, install, files: sc2.files.size, urls, hostname }));
      }
      if (u.pathname === '/qr.svg') {
        const QRCode = require('qrcode');
        const target = u.searchParams.get('u') || urls[1] || urls[0];
        return QRCode.toString(target, { type: 'svg', margin: 1 }, (err, svg) => err ? send(500, 'text/plain', String(err)) : send(200, 'image/svg+xml', svg));
      }
      // Static client files.
      let file = u.pathname === '/' ? '/index.html' : decodePath(u.pathname);
      file = path.normalize(path.join(CLIENT_DIR, file));
      if (!file.startsWith(CLIENT_DIR + path.sep)) return send(403, 'text/plain', 'forbidden');
      if (!fs.existsSync(file) || fs.statSync(file).isDirectory()) return send(404, 'text/plain', 'not found');
      return send(200, MIME[path.extname(file).toLowerCase()] || 'application/octet-stream', fs.readFileSync(file), { 'Cache-Control': 'no-cache' });
    } catch (err) {
      if (err instanceof BadRequest) return send(400, 'text/plain', err.message);
      console.error(err);
      return send(500, 'text/plain', String(err && err.stack || err));
    }
  });

  server.listen(PORT, HOST, () => {
    console.log('\nBunker Survivors is up. Open on this PC:   http://localhost:' + PORT + '/');
    console.log('On your phone (same Wi-Fi), open one of:');
    for (const x of urls) console.log('   ' + x);
    try {
      const QRCode = require('qrcode');
      QRCode.toString(urls[1] || urls[0], { type: 'terminal', small: true }, (err, s) => { if (!err) console.log(s); });
    } catch (e) { /* QR is optional */ }
    console.log('If Windows Firewall asks, allow Node.js on private networks so the phone can connect.\n');
  });
}

main().catch(err => { console.error(err); process.exit(1); });
