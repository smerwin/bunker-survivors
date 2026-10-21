// Minimal pure-Node reader for a local Blizzard CASC storage (StarCraft II).
// Reads .build.info -> build config -> encoding file, then resolves content keys
// to encoding keys, locates them via the .idx files and BLTE-decodes from data.XXX.
'use strict';
const fs = require('fs');
const path = require('path');
const zlib = require('zlib');

function hex(buf) { return Buffer.from(buf).toString('hex'); }

class CascStorage {
  constructor(root) {
    this.root = root;               // e.g. C:\Program Files (x86)\StarCraft II
    this.dataDir = path.join(root, 'SC2Data', 'data');
    this.configDir = path.join(root, 'SC2Data', 'config');
    this.index = new Map();         // ekey[0..9) hex -> {archive, offset, size}
    this.encoding = new Map();      // ckey hex -> ekey hex
    this.fds = new Map();
    this.loadBuildInfo();
    this.loadIndices();
    this.loadBuildConfig();
    this.loadEncoding();
  }

  loadBuildInfo() {
    const txt = fs.readFileSync(path.join(this.root, '.build.info'), 'utf8');
    const lines = txt.split(/\r?\n/).filter(Boolean);
    const cols = lines[0].split('|').map(c => c.split('!')[0]);
    const rows = lines.slice(1).map(l => Object.fromEntries(l.split('|').map((v, i) => [cols[i], v])));
    const active = rows.find(r => r['Active'] === '1') || rows[0];
    this.buildKey = active['Build Key'];
    this.version = active['Version'];
    this.region = active['Branch'];
  }

  loadIndices() {
    // One .idx per bucket, several versions may coexist; use the newest per bucket.
    const files = fs.readdirSync(this.dataDir).filter(f => f.endsWith('.idx'));
    const best = new Map();
    for (const f of files) {
      const bucket = f.slice(0, 2), ver = parseInt(f.slice(2, 10), 16);
      if (!best.has(bucket) || best.get(bucket).ver < ver) best.set(bucket, { f, ver });
    }
    for (const { f } of best.values()) {
      const buf = fs.readFileSync(path.join(this.dataDir, f));
      const headerSize = buf.readUInt32LE(0);
      const version = buf.readUInt16LE(8);
      if (version !== 7) throw new Error(`unexpected idx version ${version} in ${f}`);
      const keyBytes = buf[14], offBytes = buf[13], sizeBytes = buf[12];
      const entriesStart = (8 + headerSize + 0x0f) & ~0x0f;      // header block padded to 16
      const entriesSize = buf.readUInt32LE(entriesStart);
      const entrySize = keyBytes + offBytes + sizeBytes;
      let p = entriesStart + 8;
      const end = p + entriesSize;
      for (; p + entrySize <= end; p += entrySize) {
        const key = buf.subarray(p, p + keyBytes).toString('hex');
        if (/^0+$/.test(key)) continue;
        let off = 0;
        for (let i = 0; i < offBytes; i++) off = off * 256 + buf[p + keyBytes + i];
        const size = buf.readUInt32LE(p + keyBytes + offBytes);
        const archive = Math.floor(off / 0x40000000), offset = off % 0x40000000;
        this.index.set(key, { archive, offset, size });
      }
    }
  }

  loadBuildConfig() {
    const k = this.buildKey;
    const p = path.join(this.configDir, k.slice(0, 2), k.slice(2, 4), k);
    const cfg = {};
    for (const line of fs.readFileSync(p, 'utf8').split(/\r?\n/)) {
      const m = line.match(/^([\w-]+)\s*=\s*(.*)$/);
      if (m) cfg[m[1]] = m[2].trim();
    }
    this.buildConfig = cfg;
    this.rootCKey = cfg.root.split(' ')[0];
    this.encodingEKey = cfg.encoding.split(' ')[1];
  }

  fd(archive) {
    if (!this.fds.has(archive)) {
      const name = path.join(this.dataDir, 'data.' + String(archive).padStart(3, '0'));
      this.fds.set(archive, fs.openSync(name, 'r'));
    }
    return this.fds.get(archive);
  }

  // Read and decode a file by encoding key (full 16-byte hex or 9-byte prefix).
  readByEKey(ekeyHex) {
    const entry = this.index.get(ekeyHex.slice(0, 18));
    if (!entry) return null;
    const raw = Buffer.alloc(entry.size);
    fs.readSync(this.fd(entry.archive), raw, 0, entry.size, entry.offset);
    // 30-byte archive entry header: ekey reversed[16], size[4], flags[2], checksums[8]
    return decodeBLTE(raw.subarray(30));
  }

  readByCKey(ckeyHex) {
    const ekey = this.encoding.get(ckeyHex.toLowerCase());
    if (!ekey) return null;
    return this.readByEKey(ekey);
  }

  loadEncoding() {
    const buf = this.readByEKey(this.encodingEKey);
    if (!buf || buf.toString('latin1', 0, 2) !== 'EN') throw new Error('encoding file not found');
    const ckeyLen = buf[3], ekeyLen = buf[4];
    const cPageSize = buf.readUInt16BE(5) * 1024;
    const cPageCount = buf.readUInt32BE(9);
    const especSize = buf.readUInt32BE(18);
    let p = 22 + especSize;
    p += cPageCount * (ckeyLen + 16);          // page index (first key + md5)
    for (let page = 0; page < cPageCount; page++) {
      const base = p + page * cPageSize;
      let q = base;
      while (q + 6 + ckeyLen <= base + cPageSize) {
        const keyCount = buf[q];
        if (keyCount === 0) break;
        const ckey = buf.subarray(q + 6, q + 6 + ckeyLen).toString('hex');
        const ekey = buf.subarray(q + 6 + ckeyLen, q + 6 + ckeyLen + ekeyLen).toString('hex');
        this.encoding.set(ckey, ekey);
        q += 6 + ckeyLen + keyCount * ekeyLen;
      }
    }
  }
}

function decodeBLTE(buf) {
  if (buf.toString('latin1', 0, 4) !== 'BLTE') throw new Error('not BLTE');
  const headerSize = buf.readUInt32BE(4);
  const chunks = [];
  if (headerSize === 0) {
    chunks.push({ data: buf.subarray(8) });
  } else {
    const count = buf.readUIntBE(9, 3);
    let p = 12, dataPos = headerSize;
    for (let i = 0; i < count; i++, p += 24) {
      const comp = buf.readUInt32BE(p), decomp = buf.readUInt32BE(p + 4);
      chunks.push({ data: buf.subarray(dataPos, dataPos + comp), decomp });
      dataPos += comp;
    }
  }
  const out = [];
  for (const c of chunks) {
    const mode = String.fromCharCode(c.data[0]);
    const body = c.data.subarray(1);
    if (mode === 'N') out.push(body);
    else if (mode === 'Z') out.push(zlib.inflateSync(body));
    else if (mode === 'F') out.push(decodeBLTE(body));
    else if (mode === 'E') throw new Error('encrypted BLTE chunk (needs key)');
    else throw new Error('unknown BLTE mode ' + mode);
  }
  return Buffer.concat(out);
}

module.exports = { CascStorage, decodeBLTE };

if (require.main === module) {
  const t0 = Date.now();
  const st = new CascStorage(process.argv[2] || 'C:/Program Files (x86)/StarCraft II');
  console.log('version', st.version, 'region', st.region, 'index entries', st.index.size, 'encoding entries', st.encoding.size, (Date.now() - t0) + 'ms');
  const root = st.readByCKey(st.rootCKey);
  console.log('root size', root && root.length, 'magic', root && root.toString('latin1', 0, 4), 'head', root && root.subarray(0, 64).toString('hex'));
  fs.writeFileSync(path.join(process.env.TEMP || '.', 'sc2root.bin'), root);
}
