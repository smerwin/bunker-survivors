#!/usr/bin/env node
// Patches mdx-m3-viewer's M3 parser for newer StarCraft II model structures, in both the CommonJS
// build (used by the Node tools) and the minified UMD bundle copied to client/lib/viewer.min.js.
// Run after `npm install`. Structure layouts come from the m3addon structures.xml definitions.
'use strict';
const fs = require('fs');
const path = require('path');

const root = path.join(__dirname, '..');
const cjs = path.join(root, 'node_modules', 'mdx-m3-viewer', 'dist', 'cjs');
const umdSrc = path.join(root, 'node_modules', 'mdx-m3-viewer', 'dist', 'umd', 'viewer.min.js');
const umdOut = path.join(root, 'client', 'lib', 'viewer.min.js');

function applyAll(src, edits, label) {
  for (const [fromSpec, to, marker] of edits) {
    if (src.includes(marker || to)) continue;       // already applied
    if (fromSpec instanceof RegExp) {
      if (!fromSpec.test(src)) throw new Error('patch anchor not found in ' + label + ': ' + fromSpec.source.slice(0, 80));
      src = src.replace(fromSpec, to);
      continue;
    }
    const alternatives = Array.isArray(fromSpec) ? fromSpec : [fromSpec];
    const from = alternatives.find(a => src.includes(a));
    if (!from) throw new Error('patch anchor not found in ' + label + ': ' + alternatives[0].slice(0, 80));
    src = src.replace(from, to);
  }
  return src;
}
function patchFile(file, edits) {
  const p = path.join(cjs, file);
  fs.writeFileSync(p, applyAll(fs.readFileSync(p, 'utf8'), edits, file));
}

// --- CommonJS build ---------------------------------------------------------------------------
patchFile('parsers/m3/indexentry.js', [
  // MAT_ v20 (SC2 5.x): three env multipliers inserted before the layer references.
  ["MAT_: [standardmaterial_1.default, { 15: 268, 16: 280, 17: 280, 18: 280, 19: 340 }]",
   "MAT_: [standardmaterial_1.default, { 15: 268, 16: 280, 17: 280, 18: 280, 19: 340, 20: 352 }]"],
  // Unknown tags (e.g. ' COL' colour arrays) and unknown versions are data we do not need; skip them.
  ["throw new Error(': Unsupported object tag - tag ' + tag + ' and version ' + version);",
   "this.entries = []; /* unknown tag skipped */"],
  [["throw new Error(': Unsupported object version - tag ' + tag + ' and version ' + version);",
    "this.entries = []; return; /* unknown version skipped */"],
   "this.entries = []; stream.seek(readerOffset); return; /* unknown version skipped */"],
]);
patchFile('parsers/m3/standardmaterial.js', [
  [/this\.emisMult = stream\.readFloat32\(\);(\r?\n\s*)this\.diffuseLayer\.load\(stream, index\);/,
   "this.emisMult = stream.readFloat32();$1if (version > 19) { this.envConstMult = stream.readFloat32(); this.envDiffMult = stream.readFloat32(); this.envSpecMult = stream.readFloat32(); }$1this.diffuseLayer.load(stream, index);",
   "if (version > 19) { this.envConstMult"],
]);
patchFile('parsers/m3/layer.js', [
  // LAYR v26 dropped the trailing uint32 that v25 has.
  [/this\.fresnelRotationPitch = stream\.readFloat32\(\);(\r?\n\s*)this\.unknown7 = stream\.readUint32\(\);/,
   "this.fresnelRotationPitch = stream.readFloat32();$1if (version < 26) this.unknown7 = stream.readUint32();",
   "if (version < 26) this.unknown7"],
]);

// --- Minified UMD bundle -----------------------------------------------------------------------
let umd = fs.readFileSync(umdSrc, 'utf8');
umd = applyAll(umd, [
  ["{15:268,16:280,17:280,18:280,19:340}", "{15:268,16:280,17:280,18:280,19:340,20:352}"],
  ["this.emisMult=e.readFloat32(),this.diffuseLayer.load(e,n)",
   "this.emisMult=e.readFloat32(),t>19&&(this.envConstMult=e.readFloat32(),this.envDiffMult=e.readFloat32(),this.envSpecMult=e.readFloat32()),this.diffuseLayer.load(e,n)"],
  ["this.fresnelRotationPitch=e.readFloat32(),this.unknown7=e.readUint32())",
   "this.fresnelRotationPitch=e.readFloat32(),t<26&&(this.unknown7=e.readUint32()))"],
], 'viewer.min.js');
// Unknown tag / version: skip instead of throwing. Variable names differ per build, so use regexes.
umd = umd.replace(/if\(!(\w+)\)throw new Error\(": Unsupported object version - tag "\+\w+\+" and version "\+\w+\);/,
  (m, v) => `if(!${v}){this.entries=[];return}`);
umd = umd.replace(/throw this\.entries=\[\],new Error\(": Unsupported object tag - tag "\+\w+\+" and version "\+\w+\);/,
  'this.entries=[];');
if (/Unsupported object (tag|version)/.test(umd)) console.warn('warning: some unsupported-structure throws remain in the bundle');
fs.mkdirSync(path.dirname(umdOut), { recursive: true });
fs.writeFileSync(umdOut, umd);
console.log('patched parser and wrote ' + umdOut + ' (' + umd.length + ' bytes)');
