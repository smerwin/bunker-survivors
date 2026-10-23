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

const PORT = parseInt(process.env.PORT || '8642', 10);
const HOST = process.env.HOST || '0.0.0.0';
const CLIENT_DIR = path.join(__dirname, '..', 'client');

const MIME = {
  '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.css': 'text/css; charset=utf-8',
  '.json': 'application/json', '.svg': 'image/svg+xml', '.png': 'image/png', '.jpg': 'image/jpeg', '.ico': 'image/x-icon',
  '.m3': 'application/octet-stream', '.dds': 'application/octet-stream', '.ogg': 'audio/ogg', '.wav': 'audio/wav',
  '.mp3': 'audio/mpeg', '.woff2': 'font/woff2', '.txt': 'text/plain; charset=utf-8', '.webmanifest': 'application/manifest+json',
};

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
  function readCached(full) {
    return cached(full, () => sc2.storage.readByCKey(sc2.files.get(full).ckey));
  }

  // A request path is a client error, not a crash, when it is not valid percent-encoding.
  class BadRequest extends Error {}
  function decodePath(reqPath) {
    try { return decodeURIComponent(reqPath); } catch (e) { throw new BadRequest('bad URL encoding'); }
  }
  function resolveAsset(reqPath) {
    return sc2.resolve(decodePath(reqPath));
  }

  const fxList = loadGameData().fxList();
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
      if (u.pathname.startsWith('/asset/')) {
        const full = resolveAsset(u.pathname.slice(7));
        if (!full) return send(404, 'text/plain', 'asset not found: ' + u.pathname.slice(7));
        let buf = readCached(full);
        if (!buf) return send(404, 'text/plain', 'asset data missing: ' + full);
        if (full.endsWith('.wav')) {
          // Many SC2 wavs are IMA ADPCM, which browsers cannot decode: convert to PCM once.
          buf = cached('pcm:' + full, () => { const adpcm = require('./adpcm'); return adpcm.isAdpcm(buf) ? adpcm.decodeImaAdpcm(buf) : buf; });
        }
        const type = MIME[path.extname(full)] || 'application/octet-stream';
        const extra = { 'Cache-Control': 'public, max-age=86400', 'X-SC2-Name': full };
        return /\.(m3|dds)$/i.test(full) ? sendGz(full, type, buf, extra) : send(200, type, buf, extra);
      }
      if (u.pathname.startsWith('/gltf/')) {
        // /gltf/assets/units/zerg/zergling/zergling.glb -> converted from the matching .m3
        const rel = decodePath(u.pathname.slice(6)).replace(/\.glb$/i, '.m3');
        const full = resolveAsset(rel);
        if (!full) return send(404, 'text/plain', 'model not found: ' + rel);
        const glb = cached('glb:' + full, () => require('./m3gltf').convertM3(sc2, full).glb);
        return send(200, 'model/gltf-binary', glb, { 'Cache-Control': 'no-cache' });
      }
      if (u.pathname.startsWith('/icon/')) {
        // /icon/btn-unit-terran-marine.png -> assets/textures/btn-unit-terran-marine.dds decoded to PNG
        const base = u.pathname.slice(6).replace(/\.png$/i, '');
        const full = resolveAsset('assets/textures/' + base + '.dds');
        if (!full) return send(404, 'text/plain', 'icon not found');
        const png = cached('png:' + full, () => require('./dds').ddsToPng(readCached(full)));
        return send(200, 'image/png', png, { 'Cache-Control': 'public, max-age=86400' });
      }
      if (u.pathname.startsWith('/fx/')) {
        // /fx/<key>.json -> the SC2 particle effect's emitters, extracted from the M3 (see server/m3fx.js).
        const key = decodePath(u.pathname.slice(4)).replace(/\.json$/, '');
        const def = fxList[key];
        if (!def) return send(404, 'text/plain', 'no such effect: ' + key);
        const json = cached('fx:' + key, () => {
          const fx = require('./m3fx').extractFxVariants(sc2, def[0], def[1]);
          fx.loop = !!def[2];
          return Buffer.from(JSON.stringify(fx));
        });
        return sendGz('fx:' + key, 'application/json', json, { 'Cache-Control': 'public, max-age=3600' });
      }
      if (u.pathname.startsWith('/fxtex/')) {
        // /fxtex/assets/textures/glow_yellow1.png -> that SC2 texture decoded to PNG.
        const rel = decodePath(u.pathname.slice(7)).replace(/\.png$/i, '.dds');
        const full = resolveAsset(rel);
        if (!full) return send(404, 'text/plain', 'texture not found: ' + rel);
        const png = cached('png:' + full, () => require('./dds').ddsToPng(readCached(full)));
        return send(200, 'image/png', png, { 'Cache-Control': 'public, max-age=86400' });
      }
      if (u.pathname.startsWith('/font/')) {
        // /font/eurostile-bol.otf -> StarCraft II's own UI fonts from the install.
        const name = path.basename(u.pathname);
        if (!/^eurostile[\w-]*\.otf$/.test(name)) return send(404, 'text/plain', 'font not found');
        const full = resolveAsset('mods/core.sc2mod/base.sc2data/ui/fonts/' + name);
        if (!full) return send(404, 'text/plain', 'font not found');
        return sendGz(full, 'font/otf', readCached(full), { 'Cache-Control': 'public, max-age=86400' });
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
