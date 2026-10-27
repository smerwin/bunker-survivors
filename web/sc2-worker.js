// In-browser stand-in for the companion server, for the public web edition. The player picks their StarCraft II
// folder; the page hands the folder's File objects to this worker, which reads them in place (nothing is uploaded or
// copied) with the same CASC reader and asset routes as server/index.js. The service worker (client/sw.js) relays the
// game's /asset/, /fx/, /fxtex/, /icon/ and /font/ requests here and keeps the answers in the browser's cache.
// Built into client/gen/sc2-worker.js by scripts/build-web.js.
'use strict';
const { SC2Assets } = require('../server/sc2');
const { assetRoutes } = require('../server/routes');
const { setStructuresXml, XML_URL } = require('../server/m3struct');

let routes = null;

// A CASC source (see server/casc.js) over the picked folder. Paths are matched case-insensitively, relative to the
// install root, whichever of the install folder, its SC2Data folder or a parent of it was picked.
function fileSource(entries) {
  let prefix = null;
  for (const { path } of entries) {
    const i = path.toLowerCase().indexOf('sc2data/data/');
    if (i >= 0 && /\.idx$/i.test(path)) { prefix = path.slice(0, i); break; }
  }
  if (prefix === null) throw new Error('That folder has no SC2Data/data in it. Pick the StarCraft II folder itself.');
  const files = new Map(), dirs = new Map();
  for (const { path, file } of entries) {
    if (!path.startsWith(prefix)) continue;
    const rel = path.slice(prefix.length), parts = rel.split('/');
    files.set(rel.toLowerCase(), file);
    for (let i = 0; i < parts.length; i++) {
      const dir = parts.slice(0, i).join('/').toLowerCase();
      if (!dirs.has(dir)) dirs.set(dir, new Set());
      dirs.get(dir).add(parts[i]);
    }
  }
  const reader = new FileReaderSync();
  const get = rel => { const f = files.get(rel.toLowerCase()); if (!f) throw new Error('missing ' + rel); return f; };
  return {
    exists: rel => files.has(rel.toLowerCase()) || dirs.has(rel.toLowerCase()),
    list: rel => { const d = dirs.get(rel.toLowerCase()); if (!d) throw new Error('no folder ' + rel); return [...d]; },
    read: rel => Buffer.from(reader.readAsArrayBuffer(get(rel))),
    readRange: (rel, offset, size) => Buffer.from(reader.readAsArrayBuffer(get(rel).slice(offset, offset + size))),
  };
}

async function open(entries) {
  const t0 = Date.now();
  const src = fileSource(entries);
  // The game's effect table (BS.fxList) lives in units.js, which only assigns into window.BS.
  self.window = self;
  importScripts('/js/units.js');
  const xml = await fetch(XML_URL).then(r => { if (!r.ok) throw new Error('HTTP ' + r.status + ' for ' + XML_URL); return r.text(); });
  setStructuresXml(xml);
  const sc2 = new SC2Assets(src);
  routes = assetRoutes(sc2, self.BS.fxList());
  return { version: sc2.version, files: sc2.files.size, ms: Date.now() - t0 };
}

function get(pathname) {
  if (!routes) return { status: 503, type: 'text/plain', body: new TextEncoder().encode('StarCraft II folder not linked').buffer };
  let r;
  try { r = routes(pathname); } catch (e) { r = { status: 500, type: 'text/plain', body: Buffer.from(String(e && e.stack || e)) }; }
  if (!r) r = { status: 404, type: 'text/plain', body: Buffer.from('not found') };
  const b = r.body;
  return { status: r.status, type: r.type, body: b.buffer.slice(b.byteOffset, b.byteOffset + b.byteLength) };
}

self.onmessage = async e => {
  const m = e.data;
  if (m.type === 'open') {
    try { self.postMessage({ type: 'opened', ok: true, ...(await open(m.entries)) }); }
    catch (err) { self.postMessage({ type: 'opened', ok: false, error: String(err && err.message || err) }); }
  } else if (m.type === 'get') {
    const r = get(m.path);
    m.port.postMessage(r, [r.body]);
  }
};
