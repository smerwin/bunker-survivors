// Prints sequence names and bounds for SC2 M3 models, read straight from the CASC storage.
// Usage: node m3info.js assets/units/zerg/zergling/zergling.m3 [more...]
'use strict';
const { SC2Assets, findInstall } = require('./sc2');
const M3Parser = require('mdx-m3-viewer/dist/cjs/parsers/m3/model').default;

function refAll(ref) {
  if (!ref) return [];
  if (typeof ref.getAll === 'function') return ref.getAll() || [];
  const v = ref.get();
  if (Array.isArray(v)) return v;
  return v ? [v] : [];
}
function refStr(ref) { try { const v = ref.get(); return typeof v === 'string' ? v : String(v); } catch (e) { return '?'; } }

const sc2 = new SC2Assets(findInstall(process.env.SC2_DIR));
for (const name of process.argv.slice(2)) {
  const buf = sc2.read(name);
  if (!buf) { console.log(name + ': NOT FOUND'); continue; }
  const parser = new M3Parser();
  try {
    parser.load(new Uint8Array(buf.buffer, buf.byteOffset, buf.length));
  } catch (e) {
    console.log('## ' + name.split('/').pop() + ' PARSE ERROR: ' + e.message);
    console.log('   ' + String(e.stack).split('\n').slice(1, 7).map(s => s.trim().replace(/^at /, '').replace(/.*[\\/]parsers[\\/]m3[\\/]/, '')).join(' <- '));
    // Dump the raw index table so unsupported structures can be identified.
    const indexOffset = buf.readUInt32LE(4), indexCount = buf.readUInt32LE(8);
    const tags = [];
    for (let i = 0; i < indexCount; i++) {
      const p = indexOffset + i * 16;
      const tag = buf.toString('latin1', p, p + 4).split('').reverse().join('');
      tags.push(tag.trim() + ':v' + buf.readUInt32LE(p + 12) + 'x' + buf.readUInt32LE(p + 8));
    }
    console.log('   index: ' + tags.join(' '));
    continue;
  }
  if (process.env.DUMP_INDEX) {
    const indexOffset = buf.readUInt32LE(4), indexCount = buf.readUInt32LE(8);
    const tags = [];
    for (let i = 0; i < indexCount; i++) {
      const p = indexOffset + i * 16;
      const tag = buf.toString('latin1', p, p + 4).split('').reverse().join('');
      tags.push(tag.trim() + ':v' + buf.readUInt32LE(p + 12) + 'x' + buf.readUInt32LE(p + 8));
    }
    console.log('   index: ' + tags.join(' '));
  }
  const m = parser.model;
  const b = m.boundings;
  const seqs = refAll(m.sequences).map(s => {
    const bs = s.boundingSphere;
    return refStr(s.name) + '[' + s.interval[0] + '-' + s.interval[1] + 'ms' + (s.movementSpeed ? ' mv=' + s.movementSpeed.toFixed(2) : '') + (bs && bs.radius ? ' r=' + bs.radius.toFixed(2) : '') + ']';
  });
  console.log('## ' + name.split('/').pop() + ' v' + m.version + ' bounds min=' + (b.min ? Array.from(b.min).map(x => x.toFixed(2)).join(',') : '?') + ' max=' + (b.max ? Array.from(b.max).map(x => x.toFixed(2)).join(',') : '?') + ' r=' + (b.radius || 0).toFixed(2));
  console.log('   ' + seqs.join(' '));
}
