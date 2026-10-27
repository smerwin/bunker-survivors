// Generic M3 structure reader driven by the m3addon structure description (structures.xml).
// The XML (GPL-2.0, https://github.com/SC2Mapster/m3addon) is not part of this repository: it is downloaded
// into server/vendor/ on first use. Only the data layout is read from it; no code from the add-on is used.
'use strict';
const fs = require('fs');
const path = require('path');
const https = require('https');

const XML_URL = 'https://raw.githubusercontent.com/SC2Mapster/m3addon/master/structures.xml';
const XML_PATH = path.join(__dirname, 'vendor', 'm3addon-structures.xml');

function download(url, redirects = 0) {
  return new Promise((resolve, reject) => {
    https.get(url, res => {
      if (res.statusCode >= 300 && res.statusCode < 400 && res.headers.location && redirects < 5) {
        res.resume();
        return resolve(download(new URL(res.headers.location, url).href, redirects + 1));
      }
      if (res.statusCode !== 200) { res.resume(); return reject(new Error('HTTP ' + res.statusCode + ' for ' + url)); }
      const chunks = [];
      res.on('data', c => chunks.push(c));
      res.on('end', () => resolve(Buffer.concat(chunks)));
      res.on('error', reject);
    }).on('error', reject);
  });
}

// Makes sure the structure description is on disk (one small download, the first time only). Call before any
// effect is extracted: M3Layout reads the file synchronously.
async function ensureStructures() {
  if (fs.existsSync(XML_PATH)) return XML_PATH;
  console.log('Downloading the M3 structure description from ' + XML_URL);
  const xml = await download(XML_URL);
  if (!/<structure\s+name=/.test(xml.toString('utf8', 0, 4096))) throw new Error('unexpected content from ' + XML_URL);
  fs.mkdirSync(path.dirname(XML_PATH), { recursive: true });
  fs.writeFileSync(XML_PATH, xml);
  return XML_PATH;
}

// The browser edition fetches the XML itself and hands it over here (it has no disk to keep it on).
let xmlText = null;
function setStructuresXml(text) { xmlText = text; }

function loadXml() {
  if (xmlText) return xmlText;
  if (!fs.existsSync(XML_PATH)) throw new Error('M3 structure description missing: call ensureStructures() first (' + XML_PATH + ')');
  return fs.readFileSync(XML_PATH, 'utf8');
}

const PRIM = { uint8: 1, int8: 1, fixed8: 1, uint16: 2, int16: 2, uint32: 4, int32: 4, float: 4, tag: 4 };

// Parses the XML into { NAME: { versions: {n: size}, fields: [{name, type, size, since, till}] } }.
function parseDefs(xml) {
  const defs = {};
  xml = xml.replace(/<!--[\s\S]*?-->/g, '');
  const reStruct = /<structure\s+name="([^"]+)"[^>]*>([\s\S]*?)<\/structure>/g;
  let m;
  while ((m = reStruct.exec(xml))) {
    const [, name, body] = m;
    const versions = {};
    for (const v of body.matchAll(/<version\s+number="(\d+)"\s+size="(\d+)"/g)) versions[+v[1]] = +v[2];
    const fields = [];
    const fieldsBody = (body.match(/<fields>([\s\S]*)<\/fields>/) || [, ''])[1];
    for (const f of fieldsBody.matchAll(/<field\s+([^>]*?)\/?>/g)) {
      const attrs = {};
      for (const a of f[1].matchAll(/([\w-]+)\s*=\s*"([^"]*)"/g)) attrs[a[1]] = a[2];
      fields.push({ name: attrs.name, type: attrs.type, size: attrs.size ? +attrs.size : 0,
        since: attrs['since-version'] !== undefined ? +attrs['since-version'] : -1,
        till: attrs['till-version'] !== undefined ? +attrs['till-version'] : 1e9 });
    }
    defs[name] = { versions, fields };
  }
  return defs;
}

class M3Layout {
  constructor() { this.defs = parseDefs(loadXml()); this.cache = new Map(); }

  // Resolves "VEC3V0" -> (VEC3, 0), "FloatAnimationReference" -> (FloatAnimationReference, its only version).
  resolve(type) {
    if (this.defs[type]) { const vs = Object.keys(this.defs[type].versions); return [type, vs.length ? +vs[vs.length - 1] : 0]; }
    const m = type.match(/^(.*)V(\d+)$/);
    if (m && this.defs[m[1]]) return [m[1], +m[2]];
    throw new Error('Unknown M3 type ' + type);
  }

  // Field offsets for one structure version: [{name, type, off, size}], plus the total size.
  layout(name, version) {
    const key = name + ':' + version;
    if (this.cache.has(key)) return this.cache.get(key);
    const def = this.defs[name];
    if (!def) throw new Error('Unknown M3 structure ' + name);
    const out = [];
    let off = 0;
    for (const f of def.fields) {
      if (version < f.since || version > f.till) continue;
      let size, sub = null;
      if (f.size) size = f.size;
      else if (PRIM[f.type]) size = PRIM[f.type];
      else { sub = this.resolve(f.type); size = this.layout(sub[0], sub[1]).size; }
      out.push({ name: f.name, type: f.type, off, size, sub });
      off += size;
    }
    const expect = def.versions[version];
    if (expect !== undefined && expect !== off) throw new Error(`M3 layout ${name} v${version}: computed ${off} bytes, expected ${expect}`);
    const res = { fields: out, size: off };
    this.cache.set(key, res);
    return res;
  }
}

// Reads an M3 file: the index table and typed access to any referenced structure.
class M3File {
  constructor(layout, buf) {
    this.L = layout; this.buf = buf;
    const indexOffset = buf.readUInt32LE(4), indexCount = buf.readUInt32LE(8);
    this.index = [];
    for (let i = 0; i < indexCount; i++) {
      const p = indexOffset + i * 16;
      this.index.push({ tag: buf.toString('latin1', p, p + 4).split('').reverse().join('').replace(/[\0 ]/g, ''), offset: buf.readUInt32LE(p + 4),
        count: buf.readUInt32LE(p + 8), version: buf.readUInt32LE(p + 12) });
    }
    const md = this.index[0];
    const mdRef = this.read('MD34', md.version, md.offset);
    this.model = this.deref(mdRef.model)[0];
  }

  // Decodes one structure at a byte offset into a plain object.
  read(name, version, off) {
    const { fields } = this.L.layout(name, version);
    const o = {};
    const b = this.buf;
    for (const f of fields) {
      const p = off + f.off;
      switch (f.type) {
        case 'uint8': o[f.name] = b.readUInt8(p); break;
        case 'int8': o[f.name] = b.readInt8(p); break;
        case 'fixed8': o[f.name] = (b.readUInt8(p) / 255) * 2 - 1; break;
        case 'uint16': o[f.name] = b.readUInt16LE(p); break;
        case 'int16': o[f.name] = b.readInt16LE(p); break;
        case 'uint32': o[f.name] = b.readUInt32LE(p); break;
        case 'int32': o[f.name] = b.readInt32LE(p); break;
        case 'float': o[f.name] = b.readFloatLE(p); break;
        default:
          if (f.sub) o[f.name] = this.read(f.sub[0], f.sub[1], p);
          else o[f.name] = null; // raw bytes we never need
      }
    }
    return o;
  }

  // Follows a Reference {entries, index, flags} to its index entry and decodes every element.
  deref(ref) {
    if (!ref || !ref.entries) return [];
    const e = this.index[ref.index];
    if (!e) return [];
    const n = Math.min(ref.entries, e.count);
    if (e.tag === 'CHAR') return [this.buf.toString('latin1', e.offset, e.offset + n).replace(/\0.*$/s, '')];
    const prim = { U8__: [1, 'readUInt8'], I16_: [2, 'readInt16LE'], U16_: [2, 'readUInt16LE'], I32_: [4, 'readInt32LE'], U32_: [4, 'readUInt32LE'], REAL: [4, 'readFloatLE'] }[e.tag];
    if (prim) { const out = []; for (let i = 0; i < n; i++) out.push(this.buf[prim[1]](e.offset + i * prim[0])); return out; }
    const { size } = this.L.layout(e.tag, e.version);
    const out = [];
    for (let i = 0; i < n; i++) out.push(this.read(e.tag, e.version, e.offset + i * size));
    return out;
  }

  str(ref) { return this.deref(ref)[0] || ''; }
}

module.exports = { M3Layout, M3File, ensureStructures, setStructuresXml, XML_URL };
