// MNDX root-file parser for StarCraft II / Heroes of the Storm CASC storages.
// Ported to JavaScript from CascLib's CascRootFile_MNDX.cpp (Ladislav Zezula, MIT license).
// The name database is a LOUDS-style compressed trie; this port only enumerates it.
'use strict';

const INVALID = 0xFFFFFFFF;

// table[n*256 + byte] = bit position of the (n+1)-th set bit in `byte`, or 7 if there is none.
const SET_BIT_TABLE = (() => {
  const t = new Uint8Array(0x800);
  for (let n = 0; n < 8; n++) for (let b = 0; b < 256; b++) {
    let c = 0, r = 7;
    for (let i = 0; i < 8; i++) if (b & (1 << i)) { if (c === n) { r = i; break; } c++; }
    t[n * 256 + b] = r;
  }
  return t;
})();

function setBits(v) {
  v = ((v >>> 1) & 0x55555555) + (v & 0x55555555);
  v = ((v >>> 2) & 0x33333333) + (v & 0x33333333);
  v = ((v >>> 4) & 0x0F0F0F0F) + (v & 0x0F0F0F0F);
  const all = Math.imul(v, 0x01010101) >>> 0;
  return { l8: all & 0xFF, l16: (all >>> 8) & 0xFF, l24: (all >>> 16) & 0xFF, l32: (all >>> 24) & 0xFF };
}
function pop32(v) { return setBits(v).l32; }
function lowMask(n) { return n === 0 ? 0 : (0xFFFFFFFF >>> (32 - n)); }

class ByteStream {
  constructor(buf, pos = 0) { this.buf = buf; this.pos = pos; }
  u32() { const v = this.buf.readUInt32LE(this.pos); this.pos += 4; return v; }
  u64() { const v = Number(this.buf.readBigUInt64LE(this.pos)); this.pos += 8; return v; }
  rawArray(itemSize) {
    const bytes = this.u64();
    if (bytes % itemSize !== 0) throw new Error('array size ' + bytes + ' not a multiple of ' + itemSize);
    const data = this.buf.subarray(this.pos, this.pos + bytes);
    this.pos += bytes + ((-bytes) & 7);
    return data;
  }
  u8Array() { return new Uint8Array(this.rawArray(1)); }
  u32Array(itemSize = 4) {
    const raw = this.rawArray(itemSize);
    return new Uint32Array(raw.buffer.slice(raw.byteOffset, raw.byteOffset + raw.length));
  }
}

class BitEntryArray {
  load(s) {
    this.items = s.u32Array();
    this.bitsPerEntry = s.u32();
    if (this.bitsPerEntry > 32) throw new Error('bad BitEntryArray');
    this.mask = s.u32();
    this.total = s.u64();
    return this;
  }
  get(i) {
    const bit = i * this.bitsPerEntry;
    const idx = bit >>> 5, start = bit & 31, end = start + this.bitsPerEntry;
    let r;
    if (end > 32) r = (this.items[idx + 1] << (32 - start)) | (this.items[idx] >>> start);
    else r = this.items[idx] >>> start;
    return (r & this.mask) >>> 0;
  }
}

class SparseArray {
  load(s) {
    this.bits = s.u32Array();
    this.total = s.u32();
    this.valid = s.u32();
    if (this.valid > this.total) throw new Error('corrupt sparse array');
    this.base = s.u32Array(12);     // BASEVALS: BaseValue200 + two packed dwords
    this.idx0 = s.u32Array();
    this.idx1 = s.u32Array();
    return this;
  }
  isEmpty() { return this.total === 0; }
  present(i) { return (this.bits[i >>> 5] & (1 << (i & 31))) !== 0; }
  b200(g) { return this.base[g * 3]; }
  a40(g) { return this.base[g * 3 + 1] & 0x7F; }
  a80(g) { return (this.base[g * 3 + 1] >>> 7) & 0xFF; }
  aC0(g) { return (this.base[g * 3 + 1] >>> 15) & 0xFF; }
  a100(g) { return (this.base[g * 3 + 1] >>> 23) & 0x1FF; }
  a140(g) { return this.base[g * 3 + 2] & 0x1FF; }
  a180(g) { return (this.base[g * 3 + 2] >>> 9) & 0x1FF; }
  a1C0(g) { return (this.base[g * 3 + 2] >>> 18) & 0x1FF; }

  // Number of present items before `index` (rank), using the per-0x200 / per-0x40 shortcuts.
  valueAt(index) {
    const g = index >>> 9;
    let v = this.b200(g);
    switch (((index >>> 6) & 7) - 1) {
      case 0: v += this.a40(g); break;
      case 1: v += this.a80(g); break;
      case 2: v += this.aC0(g); break;
      case 3: v += this.a100(g); break;
      case 4: v += this.a140(g); break;
      case 5: v += this.a180(g); break;
      case 6: v += this.a1C0(g); break;
    }
    if (index & 0x20) v += pop32(this.bits[(index >>> 5) - 1]);
    return v + pop32(this.bits[index >>> 5] & lowMask(index & 31));
  }

  findGroup0(index) {
    let min = this.idx0[index >>> 9] >>> 9;
    let max = (this.idx0[(index >>> 9) + 1] + 0x1FF) >>> 9;
    if (max - min < 10) {
      while (index >= ((((min << 9) >>> 0) - this.b200(min + 1) + 0x200) >>> 0)) min++;
    } else {
      while (min + 1 < max) {
        const mid = (max + min) >>> 1;
        if (index < ((((max << 9) >>> 0) - this.b200(max)) >>> 0)) max = mid; else min = mid;
      }
    }
    return min;
  }

  findGroup1(index) {
    const g = index >>> 9;
    let start = this.idx1[g] >>> 9;
    let next = (this.idx1[g + 1] + 0x1FF) >>> 9;
    if (next - start < 10) {
      while (index >= this.b200(start + 1)) start++;
    } else if (start + 1 < next) {
      const mid = (next + start) >>> 1;
      if (index < this.b200(mid)) next = mid; else start = mid;
    }
    return start;
  }

  // Position of the index-th absent (zero) item.
  item0(index) {
    if ((index & 0x1FF) === 0) return this.idx0[index >>> 9];
    const g = this.findGroup0(index);
    let edx = (index + this.b200(g) - (g << 9)) >>> 0;
    let d = g << 4;
    if (edx < 0x100 - this.a100(g)) {
      if (edx < 0x80 - this.a80(g)) {
        if (edx >= 0x40 - this.a40(g)) { d += 2; edx = edx + this.a40(g) - 0x40; }
      } else if (edx < 0xC0 - this.aC0(g)) { d += 4; edx = edx + this.a80(g) - 0x80; }
      else { d += 6; edx = edx + this.aC0(g) - 0xC0; }
    } else if (edx < 0x180 - this.a180(g)) {
      if (edx < 0x140 - this.a140(g)) { d += 8; edx = edx + this.a100(g) - 0x100; }
      else { d += 10; edx = edx + this.a140(g) - 0x140; }
    } else if (edx < 0x1C0 - this.a1C0(g)) { d += 12; edx = edx + this.a180(g) - 0x180; }
    else { d += 14; edx = edx + this.a1C0(g) - 0x1C0; }

    let bg = (~this.bits[d]) >>> 0;
    let zb = setBits(bg);
    if (edx >= zb.l32) { bg = (~this.bits[++d]) >>> 0; edx -= zb.l32; zb = setBits(bg); }
    let item = d << 5;
    if (edx < zb.l16) {
      if (edx >= zb.l8) { bg >>>= 8; item += 8; edx -= zb.l8; }
    } else if (edx < zb.l24) { bg >>>= 16; item += 16; edx -= zb.l16; }
    else { bg >>>= 24; item += 24; edx -= zb.l24; }
    return SET_BIT_TABLE[(bg & 0xFF) + (edx << 8)] + item;
  }

  // Position of the index-th present (set) item.
  item1(index) {
    if ((index & 0x1FF) === 0) return this.idx1[index >>> 9];
    const g = this.findGroup1(index);
    let dist = (index - this.b200(g)) >>> 0;
    let d = g << 4;
    if (dist < this.a100(g)) {
      if (dist < this.a80(g)) {
        if (dist >= this.a40(g)) { dist -= this.a40(g); d += 2; }
      } else if (dist < this.aC0(g)) { dist -= this.a80(g); d += 4; }
      else { dist -= this.aC0(g); d += 6; }
    } else if (dist < this.a180(g)) {
      if (dist < this.a140(g)) { dist -= this.a100(g); d += 8; }
      else { dist -= this.a140(g); d += 10; }
    } else if (dist < this.a1C0(g)) { dist -= this.a180(g); d += 12; }
    else { dist -= this.a1C0(g); d += 14; }

    let bg = this.bits[d];
    let sb = setBits(bg);
    if (dist >= sb.l32) { bg = this.bits[++d]; dist -= sb.l32; sb = setBits(bg); }
    let item = d << 5;
    if (dist < sb.l16) {
      if (dist >= sb.l8) { item += 8; bg >>>= 8; dist -= sb.l8; }
    } else if (dist < sb.l24) { bg >>>= 16; item += 16; dist -= sb.l16; }
    else { bg >>>= 24; item += 24; dist -= sb.l24; }
    return SET_BIT_TABLE[(bg & 0xFF) + (dist << 8)] + item;
  }
}

class PathFragmentTable {
  load(s) {
    this.fragments = s.u8Array();
    this.marks = new SparseArray().load(s);
    return this;
  }
  copy(out, off) {
    if (this.marks.isEmpty()) {
      while (this.fragments[off] !== 0) out.push(this.fragments[off++]);
    } else {
      while (!this.marks.present(off)) out.push(this.fragments[off++]);
    }
  }
}

class FileNameDatabase {
  load(s) {
    this.collision = new SparseArray().load(s);
    this.fileNameIndexes = new SparseArray().load(s);
    this.collisionHiBits = new SparseArray().load(s);
    this.loBits = s.u8Array();
    this.hiBits = new BitEntryArray().load(s);
    this.fragTable = new PathFragmentTable().load(s);
    this.child = null;
    if (this.collisionHiBits.valid !== 0 && this.fragTable.fragments.length === 0) {
      this.child = new FileNameDatabase().load(s);
    }
    this.hash = s.u32Array(12);          // HASH_ENTRY: NodeIndex, NextIndex, FragmentOffset
    this.hashMask = (this.hash.length / 3) - 1;
    this.field214 = s.u32();
    this.bitMask = s.u32();
    return this;
  }
  isSingleChar(frag) { return (frag >>> 8) === 0xFFFFFF; }
  fragOffset1(lo) {
    const hi = this.collisionHiBits.valueAt(lo);
    return ((this.hiBits.get(hi) << 8) | this.loBits[lo]) >>> 0;
  }
  fragOffset2(stop, lo) {
    if (stop.hi === INVALID) stop.hi = this.collisionHiBits.valueAt(lo); else stop.hi++;
    return ((this.hiBits.get(stop.hi) << 8) | this.loBits[lo]) >>> 0;
  }
  copyByIndex(out, tableIndex) {
    for (;;) {
      const h = (tableIndex & this.hashMask) * 3;
      const node = this.hash[h], next = this.hash[h + 1], frag = this.hash[h + 2];
      if (tableIndex === next) {
        if (!this.isSingleChar(frag)) {
          if (this.child) this.child.copyByIndex(out, frag); else this.fragTable.copy(out, frag);
        } else out.push(frag & 0xFF);
        tableIndex = node;
        if (tableIndex === 0) return;
      } else {
        if (this.collisionHiBits.present(tableIndex)) {
          const fo = this.fragOffset1(tableIndex);
          if (this.child) this.child.copyByIndex(out, fo); else this.fragTable.copy(out, fo);
        } else out.push(this.loBits[tableIndex]);
        if (tableIndex <= this.field214) return;
        tableIndex = (0xFFFFFFFF - tableIndex + this.collision.item1(tableIndex)) >>> 0;
      }
    }
  }
  get fileNameCount() { return this.fileNameIndexes.valid; }

  // Depth-first enumeration of every name in the database: yields {name, index}.
  *enumerate() {
    const buf = [];
    const stops = [{ lo: 0, f4: 0, count: 0, hi: INVALID, f10: INVALID }];
    let itemCount = 1;
    if (this.fileNameIndexes.present(0)) yield { name: '', index: this.fileNameIndexes.valueAt(0) };
    for (;;) {
      if (itemCount === stops.length) {
        const last = stops[stops.length - 1];
        const col = this.collision.item0(last.lo) + 1;
        stops.push({ lo: col - last.lo - 1, f4: col, count: 0, hi: INVALID, f10: INVALID });
      }
      const st = stops[itemCount];
      if (this.collision.present(st.f4++)) {
        itemCount++;
        if (this.collisionHiBits.present(st.lo)) {
          const fo = this.fragOffset2(st, st.lo);
          if (this.child) this.child.copyByIndex(buf, fo); else this.fragTable.copy(buf, fo);
        } else buf.push(this.loBits[st.lo]);
        st.count = buf.length;
        if (this.fileNameIndexes.present(st.lo)) {
          if (st.f10 === INVALID) st.f10 = this.fileNameIndexes.valueAt(st.lo); else st.f10++;
          yield { name: Buffer.from(buf).toString('latin1'), index: st.f10 };
        }
      } else {
        if (itemCount === 1) return;
        stops[itemCount - 1].lo++;
        buf.length = stops[itemCount - 2].count;
        itemCount--;
      }
    }
  }
}

// Parses an MNDX root buffer; returns {files: Map<fullName, {ckey, size}>, packages: string[]}.
// Names are lower-cased with '/' separators, e.g. "mods/core.sc2mod/base.sc2assets/assets/units/zerg/zergling/zergling.m3".
function parseMndxRoot(root) {
  if (root.toString('latin1', 0, 4) !== 'MNDX') throw new Error('not an MNDX root');
  const headerVersion = root.readUInt32LE(4), formatVersion = root.readUInt32LE(8);
  if (headerVersion > 2 || formatVersion < 1 || formatVersion > 2) throw new Error('unsupported MNDX version');
  const p = 12 + (headerVersion === 2 ? 8 : 0);
  const marInfoOffset = root.readUInt32LE(p), marInfoCount = root.readUInt32LE(p + 4), marInfoSize = root.readUInt32LE(p + 8);
  const ckeyOffset = root.readUInt32LE(p + 12), ckeyCount = root.readUInt32LE(p + 16);
  const fileNameCount = root.readUInt32LE(p + 20), ckeySize = root.readUInt32LE(p + 24);
  if (marInfoCount > 3 || marInfoSize !== 20 || ckeySize !== 24) throw new Error('unexpected MNDX layout');

  const loadMar = (i) => {
    const q = marInfoOffset + i * marInfoSize;
    const off = root.readUInt32LE(q + 12);
    if (root.toString('latin1', off, off + 3) !== 'MAR' || root[off + 3] !== 0) throw new Error('bad MAR signature');
    return new FileNameDatabase().load(new ByteStream(root, off + 4));
  };
  const packagesDb = loadMar(0);
  const namesDb = loadMar(1);
  if (namesDb.fileNameCount !== fileNameCount) throw new Error('file name count mismatch');

  // Group starts: the first CKey entry of each file name group (groups end with the 0x80000000 flag).
  const groupStart = new Uint32Array(fileNameCount + 1);
  let n = 0;
  groupStart[n++] = 0;
  for (let i = 0; i < ckeyCount && n <= fileNameCount; i++) {
    if (root.readUInt32LE(ckeyOffset + i * 24) & 0x80000000) groupStart[n++] = i + 1;
  }
  if (n - 1 !== fileNameCount) throw new Error('bad CKey grouping');

  const packages = [];
  for (const { name, index } of packagesDb.enumerate()) packages[index] = name;

  const files = new Map();
  for (const { name, index } of namesDb.enumerate()) {
    if (index >= fileNameCount) continue;
    for (let i = groupStart[index]; i < ckeyCount; i++) {
      const e = ckeyOffset + i * 24;
      const flags = root.readUInt32LE(e);
      const pkg = packages[flags & 0xFFFFFF];
      if (pkg !== undefined) {
        files.set((pkg + '/' + name).toLowerCase(), { ckey: root.toString('hex', e + 4, e + 20), size: root.readUInt32LE(e + 20) });
      }
      if (flags & 0x80000000) break;
    }
  }
  return { files, packages };
}

module.exports = { parseMndxRoot };

if (require.main === module) {
  const fs = require('fs');
  const t0 = Date.now();
  const root = fs.readFileSync(process.argv[2]);
  const { files, packages } = parseMndxRoot(root);
  console.log('packages', packages.length, 'files', files.size, (Date.now() - t0) + 'ms');
  console.log('packages:', packages.slice(0, 12).join(' | '));
  const q = (process.argv[3] || 'zergling').toLowerCase();
  let shown = 0;
  for (const [name, v] of files) {
    if (name.includes(q) && (name.endsWith('.m3') || name.endsWith('.dds')) && shown++ < 25) console.log(name, v.size);
  }
}
