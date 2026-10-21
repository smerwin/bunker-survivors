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
const { SC2Assets, findInstall } = require('./sc2');

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

function main() {
  const install = findInstall(process.env.SC2_DIR);
  if (!install) {
    console.error('Could not find a StarCraft II install. Set SC2_DIR=<path to the StarCraft II folder> and retry.');
    process.exit(1);
  }
  console.log('Opening StarCraft II storage at ' + install + ' ...');
  const sc2 = new SC2Assets(install);
  console.log('SC2 ' + sc2.version + ': ' + sc2.files.size + ' files indexed in ' + sc2.loadMs + 'ms');

  const cache = new Map();            // full name -> Buffer
  let cacheBytes = 0;
  const CACHE_LIMIT = 400 * 1024 * 1024;
  function readCached(full) {
    let buf = cache.get(full);
    if (buf) return buf;
    buf = sc2.storage.readByCKey(sc2.files.get(full).ckey);
    if (buf) {
      cache.set(full, buf);
      cacheBytes += buf.length;
      while (cacheBytes > CACHE_LIMIT && cache.size) {
        const [k, v] = cache.entries().next().value;
        cache.delete(k); cacheBytes -= v.length;
      }
    }
    return buf;
  }

  function resolveAsset(reqPath) {
    return sc2.resolve(decodeURIComponent(reqPath));
  }

  const ips = lanAddresses();
  const hostname = os.hostname().toLowerCase();
  // When bound to one address (e.g. a Tailscale IP), only that address is reachable, so advertise just it.
  const urls = HOST !== '0.0.0.0'
    ? ['http://' + HOST + ':' + PORT + '/']
    : ['http://' + hostname + '.local:' + PORT + '/', ...ips.map(ip => 'http://' + ip + ':' + PORT + '/')];

  const server = http.createServer((req, res) => {
    const u = new URL(req.url, 'http://x');
    const send = (code, type, body, extra) => {
      res.writeHead(code, Object.assign({ 'Content-Type': type, 'Access-Control-Allow-Origin': '*' }, extra || {}));
      res.end(body);
    };
    try {
      if (u.pathname.startsWith('/asset/')) {
        const full = resolveAsset(u.pathname.slice(7));
        if (!full) return send(404, 'text/plain', 'asset not found: ' + u.pathname.slice(7));
        let buf = readCached(full);
        if (!buf) return send(404, 'text/plain', 'asset data missing: ' + full);
        if (full.endsWith('.wav')) {
          // Many SC2 wavs are IMA ADPCM, which browsers cannot decode: convert to PCM once.
          const key = 'pcm:' + full;
          let pcm = cache.get(key);
          if (!pcm) { const adpcm = require('./adpcm'); pcm = adpcm.isAdpcm(buf) ? adpcm.decodeImaAdpcm(buf) : buf; cache.set(key, pcm); }
          buf = pcm;
        }
        return send(200, MIME[path.extname(full)] || 'application/octet-stream', buf, { 'Cache-Control': 'public, max-age=86400', 'X-SC2-Name': full });
      }
      if (u.pathname.startsWith('/gltf/')) {
        // /gltf/assets/units/zerg/zergling/zergling.glb -> converted from the matching .m3
        const rel = decodeURIComponent(u.pathname.slice(6)).replace(/\.glb$/i, '.m3');
        const full = resolveAsset(rel);
        if (!full) return send(404, 'text/plain', 'model not found: ' + rel);
        const key = 'glb:' + full;
        let glb = cache.get(key);
        if (!glb) { glb = require('./m3gltf').convertM3(sc2, full).glb; cache.set(key, glb); }
        return send(200, 'model/gltf-binary', glb, { 'Cache-Control': 'no-cache' });
      }
      if (u.pathname.startsWith('/icon/')) {
        // /icon/btn-unit-terran-marine.png -> assets/textures/btn-unit-terran-marine.dds decoded to PNG
        const base = u.pathname.slice(6).replace(/\.png$/i, '');
        const full = resolveAsset('assets/textures/' + base + '.dds');
        if (!full) return send(404, 'text/plain', 'icon not found');
        const key = 'png:' + full;
        let png = cache.get(key);
        if (!png) { png = require('./dds').ddsToPng(readCached(full)); cache.set(key, png); }
        return send(200, 'image/png', png, { 'Cache-Control': 'public, max-age=86400' });
      }
      if (u.pathname === '/api/find') {
        const q = (u.searchParams.get('q') || '').toLowerCase();
        const limit = Math.min(500, parseInt(u.searchParams.get('limit') || '100', 10));
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
      let file = u.pathname === '/' ? '/index.html' : u.pathname;
      file = path.normalize(path.join(CLIENT_DIR, file));
      if (!file.startsWith(CLIENT_DIR)) return send(403, 'text/plain', 'forbidden');
      if (!fs.existsSync(file) || fs.statSync(file).isDirectory()) return send(404, 'text/plain', 'not found');
      return send(200, MIME[path.extname(file).toLowerCase()] || 'application/octet-stream', fs.readFileSync(file), { 'Cache-Control': 'no-cache' });
    } catch (err) {
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

main();
