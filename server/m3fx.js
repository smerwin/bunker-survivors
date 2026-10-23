// Extracts StarCraft II particle effects (PAR_ emitters) from an M3 model into a compact JSON description that
// the Unreal game replays with its own sprite renderer. Animated emitter properties are sampled per sequence.
// Usage: node m3fx.js <m3 path> [sequence name]   (prints the JSON)
'use strict';
const { M3Layout, M3File, ensureStructures } = require('./m3struct');

const SD_NAMES = ['sdev', 'sd2v', 'sd3v', 'sd4q', 'sdcc', 'sdr3', 'unknownRef8', 'sds6', 'sdu6', 'unknownRef11', 'sdu3', 'sdfg', 'sdmb'];
const FPS = 30;
let layout = null;

function lerp(a, b, t) {
  if (typeof a === 'number') return a + (b - a) * t;
  const o = {};
  for (const k of Object.keys(a)) o[k] = a[k] + (b[k] - a[k]) * t;
  return o;
}

class Animator {
  constructor(f, seqIndex) {
    this.f = f;
    const M = f.model;
    const stgs = f.deref(M.sequenceTransformationGroups);
    const stcs = f.deref(M.sequenceTransformationCollections);
    const stg = stgs[seqIndex];
    this.stcs = stg ? f.deref(stg.stcIndices).map(i => stcs[i]) : [];
    this.lookup = this.stcs.map(stc => {
      const ids = f.deref(stc.animIds), refs = f.deref(stc.animRefs), map = new Map();
      ids.forEach((id, i) => map.set(id >>> 0, [refs[i] >>> 16, refs[i] & 0xffff]));
      return map;
    });
    this.sdCache = new Map();
  }

  track(stcIndex, type, idx) {
    const key = stcIndex + ':' + type + ':' + idx;
    if (this.sdCache.has(key)) return this.sdCache.get(key);
    const stc = this.stcs[stcIndex];
    const sd = this.f.deref(stc[SD_NAMES[type]])[idx];
    const t = sd ? { frames: this.f.deref(sd.frames), keys: this.f.deref(sd.keys) } : null;
    this.sdCache.set(key, t);
    return t;
  }

  // Value of an animation reference at time t (ms) within this sequence.
  value(ref, t) {
    if (!ref || !ref.header) return ref;
    const id = ref.header.animId >>> 0;
    for (let s = 0; s < this.lookup.length; s++) {
      const hit = this.lookup[s].get(id);
      if (!hit) continue;
      const tr = this.track(s, hit[0], hit[1]);
      if (!tr || !tr.frames.length) break;
      const { frames, keys } = tr;
      if (t <= frames[0]) return keys[0];
      if (t >= frames[frames.length - 1]) return keys[keys.length - 1];
      let i = 0;
      while (i < frames.length - 1 && frames[i + 1] < t) i++;
      if (ref.header.interpolationType === 0) return keys[i];
      return lerp(keys[i], keys[i + 1], (t - frames[i]) / Math.max(1, frames[i + 1] - frames[i]));
    }
    return ref.initValue;
  }

  // Raw keyframes of a reference (for event-like properties such as partEmit bursts).
  keys(ref) {
    const id = ref.header.animId >>> 0;
    for (let s = 0; s < this.lookup.length; s++) {
      const hit = this.lookup[s].get(id);
      if (hit) { const tr = this.track(s, hit[0], hit[1]); return tr ? tr.frames.map((fr, i) => [fr, tr.keys[i]]) : []; }
    }
    return [];
  }
}

// Quaternion helpers (x, y, z, w).
const qmul = (a, b) => ({ x: a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, y: a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
  z: a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, w: a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z });
function qrot(q, v) {
  const u = { x: q.x, y: q.y, z: q.z }, s = q.w;
  const dot = u.x * v.x + u.y * v.y + u.z * v.z, uu = u.x * u.x + u.y * u.y + u.z * u.z;
  const c = { x: u.y * v.z - u.z * v.y, y: u.z * v.x - u.x * v.z, z: u.x * v.y - u.y * v.x };
  return { x: 2 * dot * u.x + (s * s - uu) * v.x + 2 * s * c.x, y: 2 * dot * u.y + (s * s - uu) * v.y + 2 * s * c.y, z: 2 * dot * u.z + (s * s - uu) * v.z + 2 * s * c.z };
}
function qnorm(q) { const l = Math.hypot(q.x, q.y, q.z, q.w) || 1; return { x: q.x / l, y: q.y / l, z: q.z / l, w: q.w / l }; }

function boneWorld(bones, anim, i, t, memo) {
  if (memo[i]) return memo[i];
  const b = bones[i];
  const loc = anim.value(b.location, t), rot = qnorm(anim.value(b.rotation, t)), scl = anim.value(b.scale, t);
  let w = { p: { x: loc.x, y: loc.y, z: loc.z }, q: rot, s: { x: scl.x, y: scl.y, z: scl.z } };
  if (b.parent >= 0) {
    const P = boneWorld(bones, anim, b.parent, t, memo);
    const sp = { x: w.p.x * P.s.x, y: w.p.y * P.s.y, z: w.p.z * P.s.z };
    const rp = qrot(P.q, sp);
    w = { p: { x: P.p.x + rp.x, y: P.p.y + rp.y, z: P.p.z + rp.z }, q: qmul(P.q, w.q), s: { x: P.s.x * w.s.x, y: P.s.y * w.s.y, z: P.s.z * w.s.z } };
  }
  memo[i] = w;
  return w;
}

const col = c => [c.red / 255, c.green / 255, c.blue / 255, c.alpha / 255];
const r3 = x => Math.round(x * 1000) / 1000;

// Samples a property over the sequence; collapses to a constant when it never changes.
function sampled(anim, ref, dur, map = x => x) {
  const n = Math.max(1, Math.ceil(dur / 1000 * FPS));
  const vals = [];
  for (let i = 0; i <= n; i++) vals.push(map(anim.value(ref, Math.min(dur, i * 1000 / FPS))));
  return collapse(vals);
}

// A curve that barely moves (under 3% of its range) is stored as its average, which keeps the JSON small.
function collapse(vals) {
  const flatNums = v => Array.isArray(v) ? v : [v];
  const first = flatNums(vals[0]);
  let changes = false;
  for (let k = 0; k < first.length && !changes; k++) {
    let lo = Infinity, hi = -Infinity, sum = 0;
    for (const v of vals) { const x = flatNums(v)[k]; lo = Math.min(lo, x); hi = Math.max(hi, x); sum += Math.abs(x); }
    const scale = Math.max(1e-3, sum / vals.length);
    if (hi - lo > 0.03 * scale) changes = true;
  }
  if (changes) return { fps: FPS, v: vals };
  const avg = first.map((_, k) => r3(vals.reduce((a, v) => a + flatNums(v)[k], 0) / vals.length));
  return Array.isArray(vals[0]) ? avg : avg[0];
}

// All usable sequence variants of an effect (ones that actually emit something), for random variety at runtime.
function extractFxVariants(sc2, m3Path, seqFilter) {
  layout = layout || new M3Layout();
  const f = new M3File(layout, sc2.read(m3Path));
  const names = f.deref(f.model.sequences).map(s => f.str(s.name));
  const re = seqFilter ? new RegExp(seqFilter, 'i') : null;
  const variants = [], textures = new Set();
  for (const n of names) {
    if (re && !re.test(n)) continue;
    const fx = extractFx(sc2, m3Path, n);
    const emits = fx.emitters.some(e => e.bursts.length || (typeof e.rate === 'number' ? e.rate > 0 : e.rate.v.some(x => x > 0)));
    if (!emits) continue;
    fx.textures.forEach(t => textures.add(t));
    variants.push({ sequence: fx.sequence, duration: fx.duration, emitters: fx.emitters });
  }
  // An exact filter ("^death$") that finds nothing that emits falls back to the looser one ("^death").
  if (!variants.length && seqFilter && seqFilter.endsWith('$')) return extractFxVariants(sc2, m3Path, seqFilter.slice(0, -1));
  return { source: m3Path, variants, textures: [...textures] };
}

function extractFx(sc2, m3Path, seqName) {
  layout = layout || new M3Layout();
  const buf = sc2.read(m3Path);
  if (!buf) throw new Error('not found: ' + m3Path);
  const f = new M3File(layout, buf);
  const M = f.model;
  const seqs = f.deref(M.sequences).map((s, i) => ({ i, name: f.str(s.name), start: s.animStartInMS, end: s.animEndInMS }));
  const seq = (seqName && seqs.find(s => s.name.toLowerCase() === seqName.toLowerCase())) || seqs[0];
  if (!seq) throw new Error('no sequences');
  const dur = Math.max(1, seq.end - seq.start);
  const anim = new Animator(f, seq.i);
  const bones = f.deref(M.bones);
  const matRefs = f.deref(M.materialReferences);
  const stdMats = f.deref(M.standardMaterials);
  const textures = new Set();

  const emitters = f.deref(M.particles).map((p, pi) => {
    const mr = matRefs[p.materialReferenceIndex];
    const mat = mr && mr.materialType === 1 ? stdMats[mr.materialIndex] : null;
    let tex = '', layerCols = 0, layerRows = 0, flipUv = false;
    if (mat) {
      for (const ln of ['diffuseLayer', 'emissiveLayer', 'emissive2Layer']) {
        const L = f.deref(mat[ln])[0];
        const img = L ? f.str(L.imagePath) : '';
        if (img) { tex = img.toLowerCase().replace(/\\/g, '/'); layerCols = L.flipBookColumns; layerRows = L.flipBookRows; flipUv = L.uvSource === 6; break; }
      }
    }
    if (tex) textures.add(tex);
    // Emitter position and orientation (local Z is the emission axis) over the sequence.
    const n = Math.max(1, Math.ceil(dur / 1000 * FPS));
    const pos = [], axis = [];
    for (let i = 0; i <= n; i++) {
      const w = boneWorld(bones, anim, p.boneIndex, Math.min(dur, i * 1000 / FPS), []);
      pos.push([r3(w.p.x), r3(w.p.y), r3(w.p.z)]);
      const z = qrot(w.q, { x: 0, y: 0, z: 1 }), x = qrot(w.q, { x: 1, y: 0, z: 0 });
      axis.push([r3(z.x), r3(z.y), r3(z.z), r3(x.x), r3(x.y), r3(x.z)]);
    }
    const bursts = anim.keys(p.partEmit).filter(([, v]) => v > 0).map(([t, v]) => [t / 1000, v]);
    const flags = p.flags >>> 0;
    return {
      name: 'p' + pi, texture: tex, blend: mat ? mat.blendMode : 2,
      // Only layers whose UV source is the particle flipbook (6) are cut into frames; others use the whole image.
      cols: flipUv ? (p.numberOfColumns || layerCols || 1) : 1, rows: flipUv ? (p.numberOfRows || layerRows || 1) : 1,
      frames: [p.phase1StartImageIndex, p.phase1EndImageIndex, p.phase2StartImageIndex, p.phase2EndImageIndex, r3(p.relativePhase1Length)],
      randFrame: !!(flags & 0x10000), maxParticles: p.maxParticles, type: p.particleType, lengthWidth: r3(p.lengthWidthRatio),
      pos: collapse(pos), axis: collapse(axis),
      rate: sampled(anim, p.emissionRate, dur, r3), bursts,
      speed: [sampled(anim, p.emissionSpeed1, dur, r3), sampled(anim, p.emissionSpeed2, dur, r3), !!p.randomizeWithEmissionSpeed2],
      angle: [sampled(anim, p.emissionAngleX, dur, r3), sampled(anim, p.emissionAngleY, dur, r3)],
      spread: [sampled(anim, p.emissionSpreadX, dur, r3), sampled(anim, p.emissionSpreadY, dur, r3)],
      life: [sampled(anim, p.lifespan1, dur, r3), sampled(anim, p.lifespan2, dur, r3), !!p.randomizeWithLifespan2],
      emitType: p.emissionType, area: p.emissionAreaType,
      areaSize: sampled(anim, p.emissionAreaSize, dur, v => [r3(v.x), r3(v.y), r3(v.z)]),
      areaRadius: sampled(anim, p.emissionAreaRadius, dur, r3),
      size: sampled(anim, p.particleSizes1, dur, v => [r3(v.x), r3(v.y), r3(v.z)]),
      size2: p.randomizeWithParticleSizes2 ? sampled(anim, p.particleSizes2, dur, v => [r3(v.x), r3(v.y), r3(v.z)]) : null,
      rot: sampled(anim, p.rotationValues1, dur, v => [r3(v.x), r3(v.y), r3(v.z)]),
      rot2: p.randomizeWithRotationValues2 ? sampled(anim, p.rotationValues2, dur, v => [r3(v.x), r3(v.y), r3(v.z)]) : null,
      color: [sampled(anim, p.initialColor1, dur, col), sampled(anim, p.middleColor1, dur, col), sampled(anim, p.finalColor1, dur, col)],
      color2: p.randomizeWithColor2 ? [sampled(anim, p.initialColor2, dur, col), sampled(anim, p.middleColor2, dur, col), sampled(anim, p.finalColor2, dur, col)] : null,
      mid: [r3(p.sizeAnimationMiddle), r3(p.colorAnimationMiddle), r3(p.alphaAnimationMiddle), r3(p.rotationAnimationMiddle)],
      gravity: r3(p.zAcceleration), drag: r3(p.drag), mass: [r3(p.mass), r3(p.mass2), !!p.randomizeWithMass2],
    };
  });
  return { source: m3Path, sequence: seq.name, duration: dur / 1000, sequences: seqs.map(s => s.name), emitters, textures: [...textures] };
}

module.exports = { extractFx, extractFxVariants };

if (require.main === module) {
  const { SC2Assets, findInstall } = require('./sc2');
  ensureStructures().then(() => {
    const sc2 = new SC2Assets(findInstall(process.env.SC2_DIR));
    const fx = extractFx(sc2, process.argv[2], process.argv[3]);
    console.log(JSON.stringify(fx, null, process.env.PRETTY ? 1 : 0));
  }).catch(e => { console.error(e.message); process.exit(1); });
}
