// The asset routes the game client loads from: /asset/, /icon/, /fx/, /fxtex/, /font/. Shared by the Node server
// (server/index.js) and the in-browser worker (web/sc2-worker.js) so both editions serve exactly the same bytes.
'use strict';

const MIME = {
  '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.css': 'text/css; charset=utf-8',
  '.json': 'application/json', '.svg': 'image/svg+xml', '.png': 'image/png', '.jpg': 'image/jpeg', '.ico': 'image/x-icon',
  '.m3': 'application/octet-stream', '.dds': 'application/octet-stream', '.ogg': 'audio/ogg', '.wav': 'audio/wav',
  '.mp3': 'audio/mpeg', '.woff2': 'font/woff2', '.txt': 'text/plain; charset=utf-8', '.webmanifest': 'application/manifest+json',
  '.otf': 'font/otf',
};
const ext = name => { const m = /\.[^./]+$/.exec(name); return m ? m[0].toLowerCase() : ''; };

// A request path is a client error, not a crash, when it is not valid percent-encoding.
class BadRequest extends Error {}
function decodePath(reqPath) {
  try { return decodeURIComponent(reqPath); } catch (e) { throw new BadRequest('bad URL encoding'); }
}

// cached(key, make) memoises derived buffers (the server bounds it by size; the browser keeps results in Cache
// Storage instead). Returns handle(pathname) -> { status, type, body, gzip?, cache? } or null for other paths.
function assetRoutes(sc2, fxList, cached = (k, make) => make()) {
  const readCached = full => cached(full, () => sc2.storage.readByCKey(sc2.files.get(full).ckey));
  const notFound = msg => ({ status: 404, type: 'text/plain', body: Buffer.from(msg) });
  const resolve = p => sc2.resolve(decodePath(p));
  return function handle(pathname) {
    if (pathname.startsWith('/asset/')) {
      const full = resolve(pathname.slice(7));
      if (!full) return notFound('asset not found: ' + pathname.slice(7));
      let buf = readCached(full);
      if (!buf) return notFound('asset data missing: ' + full);
      if (full.endsWith('.wav')) {
        // Many SC2 wavs are IMA ADPCM, which browsers cannot decode: convert to PCM once.
        buf = cached('pcm:' + full, () => { const adpcm = require('./adpcm'); return adpcm.isAdpcm(buf) ? adpcm.decodeImaAdpcm(buf) : buf; });
      }
      return { status: 200, type: MIME[ext(full)] || 'application/octet-stream', body: buf, gzip: /\.(m3|dds)$/i.test(full), key: full, cache: 'public, max-age=86400', name: full };
    }
    if (pathname.startsWith('/icon/')) {
      // /icon/btn-unit-terran-marine.png -> assets/textures/btn-unit-terran-marine.dds decoded to PNG
      const base = pathname.slice(6).replace(/\.png$/i, '');
      const full = resolve('assets/textures/' + base + '.dds');
      if (!full) return notFound('icon not found');
      return { status: 200, type: 'image/png', body: cached('png:' + full, () => require('./dds').ddsToPng(readCached(full))), cache: 'public, max-age=86400' };
    }
    if (pathname.startsWith('/fx/')) {
      // /fx/<key>.json -> the SC2 particle effect's emitters, extracted from the M3 (see server/m3fx.js).
      const key = decodePath(pathname.slice(4)).replace(/\.json$/, '');
      const def = fxList[key];
      if (!def) return notFound('no such effect: ' + key);
      const json = cached('fx:' + key, () => {
        const fx = require('./m3fx').extractFxVariants(sc2, def[0], def[1]);
        fx.loop = !!def[2];
        return Buffer.from(JSON.stringify(fx));
      });
      return { status: 200, type: 'application/json', body: json, gzip: true, key: 'fx:' + key, cache: 'public, max-age=3600' };
    }
    if (pathname.startsWith('/fxtex/')) {
      // /fxtex/assets/textures/glow_yellow1.png -> that SC2 texture decoded to PNG.
      const rel = decodePath(pathname.slice(7)).replace(/\.png$/i, '.dds');
      const full = resolve(rel);
      if (!full) return notFound('texture not found: ' + rel);
      return { status: 200, type: 'image/png', body: cached('png:' + full, () => require('./dds').ddsToPng(readCached(full))), cache: 'public, max-age=86400' };
    }
    if (pathname.startsWith('/font/')) {
      // /font/eurostile-bol.otf -> StarCraft II's own UI fonts from the install.
      const name = pathname.slice(pathname.lastIndexOf('/') + 1);
      if (!/^eurostile[\w-]*\.otf$/.test(name)) return notFound('font not found');
      const full = resolve('mods/core.sc2mod/base.sc2data/ui/fonts/' + name);
      if (!full) return notFound('font not found');
      return { status: 200, type: 'font/otf', body: readCached(full), gzip: true, key: full, cache: 'public, max-age=86400' };
    }
    return null;
  };
}

module.exports = { assetRoutes, MIME, BadRequest, decodePath };
