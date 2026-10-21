#!/usr/bin/env node
// Exports every StarCraft II asset the game uses from the local install into engine-friendly formats:
//   models -> .glb (skinned mesh, skeleton, all animations, PNG textures with team-colour mask in alpha)
//   icons  -> .png, sounds -> 16-bit PCM .wav, ground texture -> .png, plus manifest.json.
// Output goes to SC2Export/ in the project folder (gitignored), which the Unreal project imports from.
// (AppData is avoided on purpose: packaged apps can see a virtualised copy that other programs cannot.)
// These are derived copies of your own game files: keep them out of git and do not share them.
'use strict';
const fs = require('fs');
const path = require('path');
const vm = require('vm');
const { SC2Assets, findInstall } = require('../server/sc2');
const { convertM3 } = require('../server/m3gltf');
const { ddsToPng } = require('../server/dds');
const adpcm = require('../server/adpcm');

const root = path.join(__dirname, '..');
const outDir = path.resolve(process.argv[2] || path.join(root, 'SC2Export'));

// Reuse the game's own asset lists from client/js/units.js.
const ctx = { window: {} };
vm.runInNewContext(fs.readFileSync(path.join(root, 'client', 'js', 'units.js'), 'utf8'), ctx);
const BS = ctx.window.BS;

const icons = new Set();
for (const d of Object.values(BS.INFANTRY)) icons.add(d.icon);
for (const u of Object.values(BS.UPGRADES)) { if (u.icon) icons.add(u.icon); (u.icons || []).forEach(i => icons.add(i)); }
['btn-unit-terran-scv', 'btn-unit-terran-siegetank', 'btn-unit-terran-banshee', 'btn-ability-terran-repair', 'btn-ability-terran-stimpack',
 'btn-unit-zerg-zergling', 'btn-unit-zerg-baneling', 'btn-unit-zerg-roach', 'btn-unit-zerg-hydralisk', 'btn-unit-zerg-mutalisk',
 'btn-unit-zerg-lurker', 'btn-unit-zerg-ultralisk', 'btn-unit-zerg-broodlord',
 'btn-upgrade-terran-buildingarmor', 'btn-upgrade-terran-hisecautotracking', 'btn-unit-terran-battlecruiser'].forEach(i => icons.add(i));

function write(rel, buf) {
  const p = path.join(outDir, rel);
  fs.mkdirSync(path.dirname(p), { recursive: true });
  fs.writeFileSync(p, buf);
  return p;
}

const sc2 = new SC2Assets(findInstall(process.env.SC2_DIR));
console.log('StarCraft II ' + sc2.version + ' -> ' + outDir);
const manifest = { sc2Version: sc2.version, exportedAt: new Date().toISOString(), models: {}, icons: {}, sounds: {}, textures: {}, failures: [] };

for (const [key, m3] of Object.entries(BS.MODELS)) {
  try {
    const { glb, report } = convertM3(sc2, m3, { name: key });
    if (!report.stats.primitives) { manifest.failures.push({ key, m3, reason: 'no drawable geometry' }); console.log('  skip  ' + key + ' (no mesh)'); continue; }
    const rel = path.join('Models', key + '.glb');
    write(rel, glb);
    const json = JSON.parse(glb.subarray(20, 20 + glb.readUInt32LE(12)).toString('utf8'));
    manifest.models[key] = {
      file: rel.replace(/\\/g, '/'), source: m3, stats: report.stats,
      animations: (json.animations || []).map(a => ({ name: a.name, seconds: (a.extras.sc2Interval[1] - a.extras.sc2Interval[0]) / 1000, movementSpeed: a.extras.movementSpeed })),
      attachments: json.nodes.filter(n => n.name && n.name.startsWith('Attach_')).map(n => n.name.slice(7)),
      bounds: json.scenes[0].extras.sc2Bounds, warnings: report.warnings.concat(report.missing.map(x => 'missing texture ' + x), report.badTextures.map(x => 'bad texture ' + x)),
    };
    console.log('  model ' + key.padEnd(16) + (glb.length / 1024).toFixed(0).padStart(6) + ' KB  ' + report.stats.animations + ' anims' + (manifest.models[key].warnings.length ? '  [' + manifest.models[key].warnings.length + ' warnings]' : ''));
  } catch (e) {
    manifest.failures.push({ key, m3, reason: e.message });
    console.log('  FAIL  ' + key + ': ' + e.message);
  }
}

for (const name of icons) {
  const buf = sc2.read('assets/textures/' + name + '.dds');
  if (!buf) { manifest.failures.push({ key: name, reason: 'icon not found' }); continue; }
  const rel = path.join('Icons', name + '.png');
  write(rel, ddsToPng(buf));
  manifest.icons[name] = rel.replace(/\\/g, '/');
}
console.log('  icons ' + Object.keys(manifest.icons).length);

for (const [key, list] of Object.entries(BS.SOUNDS)) {
  manifest.sounds[key] = [];
  list.forEach((src, i) => {
    let buf = sc2.read(src);
    if (!buf) { manifest.failures.push({ key, src, reason: 'sound not found' }); return; }
    if (src.endsWith('.wav') && adpcm.isAdpcm(buf)) buf = adpcm.decodeImaAdpcm(buf);
    const rel = path.join('Sounds', key + (list.length > 1 ? '_' + i : '') + path.extname(src));
    write(rel, buf);
    manifest.sounds[key].push(rel.replace(/\\/g, '/'));
  });
}
console.log('  sounds ' + Object.values(manifest.sounds).reduce((s, l) => s + l.length, 0));

const groundBuf = sc2.read(BS.GROUND_TEXTURE);
if (groundBuf) { write('Textures/Ground_Diffuse.png', ddsToPng(groundBuf)); manifest.textures.ground = 'Textures/Ground_Diffuse.png'; }

write('manifest.json', Buffer.from(JSON.stringify(manifest, null, 2)));
console.log('done: ' + Object.keys(manifest.models).length + ' models, ' + manifest.failures.length + ' failures');
if (manifest.failures.length) console.log(JSON.stringify(manifest.failures, null, 1));
