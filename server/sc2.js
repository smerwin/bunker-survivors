// SC2 asset access: opens the local StarCraft II CASC storage and resolves file names
// (from the MNDX root) to decoded file contents. Nothing is extracted or copied to disk.
'use strict';
const fs = require('fs');
const path = require('path');
const { CascStorage } = require('./casc');
const { parseMndxRoot } = require('./mndx');

const DEFAULT_INSTALL_CANDIDATES = [
  'C:/Program Files (x86)/StarCraft II',
  'C:/Program Files/StarCraft II',
  'D:/StarCraft II',
  'D:/Games/StarCraft II',
  '/Applications/StarCraft II',
];

function findInstall(explicit) {
  const candidates = explicit ? [explicit] : DEFAULT_INSTALL_CANDIDATES;
  for (const c of candidates) {
    if (fs.existsSync(path.join(c, '.build.info')) && fs.existsSync(path.join(c, 'SC2Data', 'data'))) return c;
  }
  return null;
}

class SC2Assets {
  constructor(installDir) {
    const t0 = Date.now();
    this.installDir = installDir;
    this.storage = new CascStorage(installDir);
    const root = this.storage.readByCKey(this.storage.rootCKey);
    const { files, packages } = parseMndxRoot(root);
    this.files = files;
    this.packages = packages;
    this.relative = buildRelativeIndex(files);
    this.loadMs = Date.now() - t0;
  }
  get version() { return this.storage.version; }

  // Resolves a full name ("mods/liberty.sc2mod/base.sc2assets/assets/...") or a path relative to a
  // mod's base.sc2assets folder ("assets/units/zerg/zergling/zergling.m3") to the full name, or null.
  resolve(name) {
    const n = normalize(name);
    if (this.files.has(n)) return n;
    const r = this.relative.get(n);
    return r ? r.full : null;
  }
  has(name) { return this.resolve(name) !== null; }
  info(name) { const full = this.resolve(name); return full ? this.files.get(full) : null; }

  // Returns a Buffer with the decoded file, or null if the name is unknown.
  read(name) {
    const e = this.info(name);
    if (!e) return null;
    return this.storage.readByCKey(e.ckey);
  }

  // Search names by substring or RegExp; returns up to `limit` names.
  find(query, limit = 200) {
    const out = [];
    const re = query instanceof RegExp ? query : null;
    const q = re ? null : String(query).toLowerCase();
    for (const name of this.files.keys()) {
      if (re ? re.test(name) : name.includes(q)) { out.push(name); if (out.length >= limit) break; }
    }
    return out;
  }
}

function normalize(name) { return String(name).replace(/\\/g, '/').replace(/^\/+/, '').toLowerCase(); }

// Which mod packages win when the same relative asset path exists in several of them.
const PACKAGE_PRIORITY = [
  'mods/liberty.sc2mod/base.sc2assets/',
  'mods/core.sc2mod/base.sc2assets/',
  'mods/swarm.sc2mod/base.sc2assets/',
  'mods/void.sc2mod/base.sc2assets/',
  'mods/liberty.sc2mod/base.sc2data/',
  'mods/core.sc2mod/base.sc2data/',
];

function buildRelativeIndex(files) {
  const rel = new Map();   // "assets/units/zerg/zergling/zergling.m3" -> {full, rank}
  for (const name of files.keys()) {
    const i = name.indexOf('/base.sc2');
    if (i < 0) continue;
    const j = name.indexOf('/', i + 1);
    if (j < 0) continue;
    const pkg = name.slice(0, j + 1);
    const r = name.slice(j + 1);
    let rank = PACKAGE_PRIORITY.indexOf(pkg);
    if (rank < 0) rank = pkg.startsWith('mods/') ? 50 : 100;
    const cur = rel.get(r);
    if (!cur || rank < cur.rank) rel.set(r, { full: name, rank });
  }
  return rel;
}

module.exports = { SC2Assets, findInstall };

if (require.main === module) {
  const install = findInstall(process.env.SC2_DIR);
  if (!install) { console.error('StarCraft II install not found'); process.exit(1); }
  const sc2 = new SC2Assets(install);
  console.log('SC2', sc2.version, 'at', install, '-', sc2.files.size, 'files, loaded in', sc2.loadMs + 'ms');
  const args = process.argv.slice(2);
  if (args[0] === 'read') {
    const buf = sc2.read(args[1]);
    console.log(args[1], buf ? buf.length + ' bytes, magic ' + buf.toString('latin1', 0, 4) : 'NOT FOUND');
    if (buf && args[2]) { fs.writeFileSync(args[2], buf); console.log('wrote', args[2]); }
  } else {
    for (const q of args) {
      const re = new RegExp(q, 'i');
      const hits = sc2.find(re, 40);
      console.log('== ' + q + ' (' + hits.length + ')');
      for (const h of hits) console.log('  ' + h + '  ' + sc2.files.get(h).size);
    }
  }
}
