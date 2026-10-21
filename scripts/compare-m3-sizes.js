// Compares mdx-m3-viewer's M3 structure size table with the m3addon structures.xml definitions.
// Usage: node scripts/compare-m3-sizes.js <path to structures.xml>
'use strict';
const fs = require('fs');
const path = require('path');
const xml = fs.readFileSync(process.argv[2], 'utf8');
const lib = fs.readFileSync(path.join(__dirname, '..', 'node_modules', 'mdx-m3-viewer', 'dist', 'cjs', 'parsers', 'm3', 'indexentry.js'), 'utf8');

const defs = new Map();
for (const m of xml.matchAll(/<structure name="([^"]+)">([\s\S]*?)<\/structure>/g)) {
  const tag = m[1].padEnd(4, '_').slice(0, 4);
  const sizes = {};
  for (const v of m[2].matchAll(/<version number="(\d+)" size="(\d+)"/g)) sizes[v[1]] = +v[2];
  defs.set(tag, sizes);
}
const libMap = new Map();
for (const m of lib.matchAll(/^\s+(\w{4}|[A-Z0-9_]{3,4}): \[[^,]+, \{([^}]*)\}\]/gm)) {
  const sizes = {};
  for (const v of m[2].matchAll(/(\d+): (\d+)/g)) sizes[v[1]] = +v[2];
  libMap.set(m[1], sizes);
}
for (const [tag, sizes] of libMap) {
  const d = defs.get(tag);
  if (!d) { console.log(tag + ': not in structures.xml'); continue; }
  for (const [v, s] of Object.entries(sizes)) if (d[v] !== undefined && d[v] !== s) console.log(tag + ' v' + v + ': lib=' + s + ' xml=' + d[v] + '  <-- MISMATCH');
  const missing = Object.entries(d).filter(([v]) => sizes[v] === undefined).map(([v, s]) => v + ':' + s);
  if (missing.length) console.log(tag + ' versions only in xml: ' + missing.join(' '));
}
