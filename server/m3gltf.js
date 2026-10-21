// Converts StarCraft II M3 models (read live from the local install) into binary glTF (.glb):
// skinned meshes, skeleton, every animation sequence (sampled), and textures as PNG.
// The output is meant for importing into a game engine on the same machine; it is a derived copy
// of the player's own game files and must not be redistributed.
'use strict';
const path = require('path');
const fs = require('fs');
const { mat4, quat, vec3 } = require('gl-matrix');
const M3Parser = require('mdx-m3-viewer/dist/cjs/parsers/m3/model').default;
const M3Bone = require('mdx-m3-viewer/dist/cjs/viewer/handlers/m3/bone').default;
const M3Sts = require('mdx-m3-viewer/dist/cjs/viewer/handlers/m3/sts').default;
const M3Stc = require('mdx-m3-viewer/dist/cjs/viewer/handlers/m3/stc').default;
const M3Stg = require('mdx-m3-viewer/dist/cjs/viewer/handlers/m3/stg').default;
const { decodeDDS, encodePNG } = require('./dds');

const FPS = 30;
const MAT_TWO_SIDED = 0x8;
const BLEND_MODES = ['OPAQUE', 'BLEND', 'ADD', 'ADD_ALPHA', 'MOD', 'MOD2X'];

function refAll(ref) {
  if (!ref) return [];
  const v = ref.get();
  if (v === undefined || v === null) return [];
  return Array.isArray(v) ? v : (ArrayBuffer.isView(v) ? Array.from(v) : [v]);
}
function refFirst(ref) { try { return ref && ref.entries !== undefined ? ref.first() : (ref ? ref.first() : null); } catch (e) { return null; } }

// ---- GLB builder --------------------------------------------------------------------------------
class GlbBuilder {
  constructor() {
    this.json = { asset: { version: '2.0', generator: 'bunker-survivors m3gltf' }, scene: 0, scenes: [{ nodes: [] }], nodes: [], meshes: [], materials: [], textures: [], images: [], samplers: [{ magFilter: 9729, minFilter: 9987, wrapS: 10497, wrapT: 10497 }], accessors: [], bufferViews: [], buffers: [{ byteLength: 0 }] };
    this.chunks = []; this.length = 0;
  }
  view(buf, target) {
    const pad = (4 - (this.length % 4)) % 4;
    if (pad) { this.chunks.push(Buffer.alloc(pad)); this.length += pad; }
    const b = Buffer.from(buf.buffer, buf.byteOffset, buf.byteLength);
    const idx = this.json.bufferViews.length;
    const v = { buffer: 0, byteOffset: this.length, byteLength: b.length };
    if (target) v.target = target;
    this.json.bufferViews.push(v);
    this.chunks.push(b); this.length += b.length;
    return idx;
  }
  accessor(typed, type, componentType, opts) {
    const o = opts || {};
    const bv = this.view(typed, o.target);
    const comps = { SCALAR: 1, VEC2: 2, VEC3: 3, VEC4: 4, MAT4: 16 }[type];
    const a = { bufferView: bv, componentType, count: typed.length / comps, type };
    if (o.normalized) a.normalized = true;
    if (o.minmax) {
      const min = new Array(comps).fill(Infinity), max = new Array(comps).fill(-Infinity);
      for (let i = 0; i < typed.length; i++) { const c = i % comps; if (typed[i] < min[c]) min[c] = typed[i]; if (typed[i] > max[c]) max[c] = typed[i]; }
      a.min = min; a.max = max;
    }
    this.json.accessors.push(a);
    return this.json.accessors.length - 1;
  }
  image(png, name) {
    const bv = this.view(png);
    this.json.images.push({ bufferView: bv, mimeType: 'image/png', name });
    this.json.textures.push({ sampler: 0, source: this.json.images.length - 1, name });
    return this.json.textures.length - 1;
  }
  build() {
    const pad = (4 - (this.length % 4)) % 4;
    if (pad) { this.chunks.push(Buffer.alloc(pad)); this.length += pad; }
    this.json.buffers[0].byteLength = this.length;
    for (const k of ['meshes', 'materials', 'textures', 'images', 'accessors', 'bufferViews']) if (!this.json[k].length) delete this.json[k];
    if (!this.json.textures) delete this.json.samplers;
    let jsonBuf = Buffer.from(JSON.stringify(this.json), 'utf8');
    const jpad = (4 - (jsonBuf.length % 4)) % 4;
    jsonBuf = Buffer.concat([jsonBuf, Buffer.alloc(jpad, 0x20)]);
    const bin = Buffer.concat(this.chunks);
    const header = Buffer.alloc(12);
    header.writeUInt32LE(0x46546C67, 0); header.writeUInt32LE(2, 4); header.writeUInt32LE(12 + 8 + jsonBuf.length + 8 + bin.length, 8);
    const jh = Buffer.alloc(8); jh.writeUInt32LE(jsonBuf.length, 0); jh.writeUInt32LE(0x4E4F534A, 4);
    const bh = Buffer.alloc(8); bh.writeUInt32LE(bin.length, 0); bh.writeUInt32LE(0x004E4942, 4);
    return Buffer.concat([header, jh, jsonBuf, bh, bin]);
  }
}

// ---- Textures -----------------------------------------------------------------------------------
// SC2 normal maps store X in alpha and Y in green (DXT5nm); rebuild a standard RGB tangent-space map.
function normalMapToRgb(img) {
  const d = img.data;
  for (let i = 0; i < d.length; i += 4) {
    const x = d[i + 3] / 127.5 - 1, y = d[i + 1] / 127.5 - 1;
    const z = Math.sqrt(Math.max(0, 1 - x * x - y * y));
    d[i] = Math.round((x + 1) * 127.5); d[i + 1] = Math.round((y + 1) * 127.5); d[i + 2] = Math.round((z + 1) * 127.5); d[i + 3] = 255;
  }
  return img;
}

function loadTexture(sc2, imagePath, kind, cache, report) {
  if (!imagePath) return null;
  const key = kind + ':' + imagePath.toLowerCase();
  if (cache.has(key)) return cache.get(key);
  let result = null;
  const buf = sc2.read(imagePath);
  if (!buf) report.missing.push(imagePath);
  else {
    try {
      let img = decodeDDS(buf);
      if (kind === 'normal') img = normalMapToRgb(img);
      result = encodePNG(img);
    } catch (e) { report.badTextures.push(imagePath + ' (' + e.message + ')'); }
  }
  cache.set(key, result);
  return result;
}

// ---- Conversion ---------------------------------------------------------------------------------
function convertM3(sc2, m3Path, options) {
  const opts = Object.assign({ animations: true, textures: true, fps: FPS }, options || {});
  const report = { model: m3Path, missing: [], badTextures: [], warnings: [] };
  const buf = sc2.read(m3Path);
  if (!buf) throw new Error('model not found: ' + m3Path);
  const parser = new M3Parser();
  parser.load(new Uint8Array(buf.buffer, buf.byteOffset, buf.length));
  const m = parser.model;
  const div = m.divisions.first();
  const regions = refAll(div.regions);
  const triangles = div.triangles.get();
  const batches = refAll(div.batches);
  const vertices = m.vertices.get();
  const boneLookup = m.boneLookup.get() || new Uint16Array(0);
  const iref = refAll(m.absoluteInverseBoneRestPositions);
  const bones = refAll(m.bones).map(b => new M3Bone(b));

  let uvSetCount = 1;
  if (m.vertexFlags & 0x40000) uvSetCount = 2; else if (m.vertexFlags & 0x80000) uvSetCount = 3; else if (m.vertexFlags & 0x100000) uvSetCount = 4;
  const vSize = (7 + uvSetCount) * 4;
  if (vertices.length % vSize !== 0) report.warnings.push('vertex buffer size ' + vertices.length + ' is not a multiple of ' + vSize);
  const dv = new DataView(vertices.buffer, vertices.byteOffset, vertices.byteLength);

  const g = new GlbBuilder();
  const J = g.json;
  // SC2 is Z-up, glTF is Y-up. The SC2Root joint carries the rotation (-90 degrees about X) and is the
  // skeleton's root bone, so every engine sees one consistent skeleton. Vertices are converted to glTF
  // space and inverse bind matrices are IREF * R^-1, which keeps bind pose and rest pose identical.
  const R = mat4.fromXRotation(mat4.create(), -Math.PI / 2);
  const Rinv = mat4.invert(mat4.create(), R);
  const rootIdx = J.nodes.length;
  J.nodes.push({ name: 'SC2Root', rotation: [-Math.SQRT1_2, 0, 0, Math.SQRT1_2], children: [] });
  J.scenes[0].nodes.push(rootIdx);
  const toGltf = (arr, i) => { const y = arr[i + 1], z = arr[i + 2]; arr[i + 1] = z; arr[i + 2] = -y; };

  // Skeleton: one node per bone, with the bone's rest (init) local transform.
  const boneNode = [];
  const usedNames = new Set();
  bones.forEach((b, i) => {
    let name = b.name || ('Bone' + i);
    while (usedNames.has(name)) name += '_';
    usedNames.add(name);
    const t = Array.from(b.location.initValue), r = Array.from(b.rotation.initValue), s = Array.from(b.scale.initValue);
    const node = { name };
    if (t.some(v => v !== 0)) node.translation = t;
    if (!(r[0] === 0 && r[1] === 0 && r[2] === 0 && r[3] === 1)) node.rotation = r;
    if (s.some(v => v !== 1)) node.scale = s;
    boneNode[i] = J.nodes.length;
    J.nodes.push(node);
  });
  bones.forEach((b, i) => {
    const parent = b.parent >= 0 && b.parent < bones.length ? boneNode[b.parent] : rootIdx;
    (J.nodes[parent].children = J.nodes[parent].children || []).push(boneNode[i]);
  });

  let skinIdx = -1;
  if (bones.length) {
    // Joint 0 is SC2Root; joint k+1 is bone k. Vertex joint indices are offset by one below.
    const ibm = new Float32Array((bones.length + 1) * 16);
    ibm.set(Rinv, 0);
    const tmp = mat4.create();
    for (let i = 0; i < bones.length; i++) ibm.set(mat4.multiply(tmp, iref[i] || mat4.create(), Rinv), (i + 1) * 16);
    J.skins = [{ name: 'Skeleton', skeleton: rootIdx, joints: [rootIdx].concat(boneNode), inverseBindMatrices: g.accessor(ibm, 'MAT4', 5126) }];
    skinIdx = 0;
  }

  // Materials.
  const materialRefs = refAll(m.materialReferences);
  const standardMaterials = refAll(m.materials[0]);
  const texCache = new Map();
  const matIndex = new Map();
  function materialFor(refIndex) {
    if (matIndex.has(refIndex)) return matIndex.get(refIndex);
    const mr = materialRefs[refIndex];
    let idx = -1;
    if (mr && mr.materialType === 1 && standardMaterials[mr.materialIndex]) {
      const sm = standardMaterials[mr.materialIndex];
      const layerPath = ref => { const l = refFirst(ref); if (!l) return null; const p = l.imagePath.get(); return p && p.length ? p.replace(/\\/g, '/') : null; };
      const diffuse = layerPath(sm.diffuseLayer), normal = layerPath(sm.normalLayer), emissive = layerPath(sm.emissiveLayer), specular = layerPath(sm.specularLayer);
      const mat = { name: sm.name.get() || ('Material' + mr.materialIndex), pbrMetallicRoughness: { metallicFactor: 0, roughnessFactor: 0.6 }, extras: { sc2BlendMode: BLEND_MODES[sm.blendMode] || String(sm.blendMode), teamColorMask: 'baseColorTexture.alpha', diffuse, normal, emissive, specular } };
      const blend = sm.blendMode;
      if (blend === 0) mat.alphaMode = 'OPAQUE';
      else if (blend === 1) mat.alphaMode = 'BLEND';
      else { mat.alphaMode = 'BLEND'; mat.extras.additive = true; }
      if (sm.flags & MAT_TWO_SIDED) mat.doubleSided = true;
      if (opts.textures) {
        const d = loadTexture(sc2, diffuse, 'diffuse', texCache, report);
        if (d) mat.pbrMetallicRoughness.baseColorTexture = { index: g.image(d, path.basename(diffuse, '.dds')) };
        const n = loadTexture(sc2, normal, 'normal', texCache, report);
        if (n) mat.normalTexture = { index: g.image(n, path.basename(normal, '.dds')) };
        const e = loadTexture(sc2, emissive, 'emissive', texCache, report);
        if (e) { mat.emissiveTexture = { index: g.image(e, path.basename(emissive, '.dds')) }; mat.emissiveFactor = [1, 1, 1]; }
      }
      J.materials.push(mat);
      idx = J.materials.length - 1;
    }
    matIndex.set(refIndex, idx);
    return idx;
  }

  // Geometry: one primitive per batch (region + material). Skinned and unskinned regions are split.
  const regionCache = new Map();
  function regionAccessors(ri) {
    if (regionCache.has(ri)) return regionCache.get(ri);
    const r = regions[ri];
    const n = r.verticesCount, first = r.firstVertexIndex;
    const pos = new Float32Array(n * 3), nrm = new Float32Array(n * 3), tan = new Float32Array(n * 4);
    const uv = []; for (let u = 0; u < uvSetCount; u++) uv.push(new Float32Array(n * 2));
    const joints = new Uint16Array(n * 4), weights = new Float32Array(n * 4);
    const skinned = r.boneWeightPairsCount > 0 && bones.length > 0;
    for (let i = 0; i < n; i++) {
      const o = (first + i) * vSize;
      pos[i * 3] = dv.getFloat32(o, true); pos[i * 3 + 1] = dv.getFloat32(o + 4, true); pos[i * 3 + 2] = dv.getFloat32(o + 8, true);
      const nx = vertices[o + 20] / 255 * 2 - 1, ny = vertices[o + 21] / 255 * 2 - 1, nz = vertices[o + 22] / 255 * 2 - 1, nw = vertices[o + 23] / 255 * 2 - 1;
      const nl = Math.hypot(nx, ny, nz) || 1;
      nrm[i * 3] = nx / nl; nrm[i * 3 + 1] = ny / nl; nrm[i * 3 + 2] = nz / nl;
      for (let u = 0; u < uvSetCount; u++) { uv[u][i * 2] = dv.getInt16(o + 24 + u * 4, true) / 2048; uv[u][i * 2 + 1] = dv.getInt16(o + 26 + u * 4, true) / 2048; }
      const to = o + 24 + uvSetCount * 4;
      const tx = vertices[to] / 255 * 2 - 1, ty = vertices[to + 1] / 255 * 2 - 1, tz = vertices[to + 2] / 255 * 2 - 1;
      const tl = Math.hypot(tx, ty, tz) || 1;
      tan[i * 4] = tx / tl; tan[i * 4 + 1] = ty / tl; tan[i * 4 + 2] = tz / tl; tan[i * 4 + 3] = nw < 0 ? -1 : 1;
      toGltf(pos, i * 3); toGltf(nrm, i * 3); toGltf(tan, i * 4);
      if (skinned) {
        const look = k => { const li = r.firstBoneLookupIndex + vertices[o + 16 + k]; return (li < boneLookup.length ? boneLookup[li] : 0) + 1; };
        if (r.boneWeightPairsCount === 1) { joints[i * 4] = look(0); weights[i * 4] = 1; }
        else {
          let sum = 0;
          for (let k = 0; k < 4; k++) { const w = vertices[o + 12 + k]; joints[i * 4 + k] = w > 0 ? look(k) : 0; weights[i * 4 + k] = w; sum += w; }
          if (sum <= 0) { joints[i * 4] = look(0); weights[i * 4] = 1; sum = 1; }
          for (let k = 0; k < 4; k++) weights[i * 4 + k] /= sum;
        }
      }
    }
    const idx = new Uint16Array(r.triangleIndicesCount);
    for (let i = 0; i < idx.length; i++) idx[i] = triangles[r.firstTriangleIndex + i];
    const attrs = {
      POSITION: g.accessor(pos, 'VEC3', 5126, { minmax: true, target: 34962 }),
      NORMAL: g.accessor(nrm, 'VEC3', 5126, { target: 34962 }),
      TANGENT: g.accessor(tan, 'VEC4', 5126, { target: 34962 }),
    };
    uv.forEach((u, k) => { attrs['TEXCOORD_' + k] = g.accessor(u, 'VEC2', 5126, { target: 34962 }); });
    if (skinned) { attrs.JOINTS_0 = g.accessor(joints, 'VEC4', 5123, { target: 34962 }); attrs.WEIGHTS_0 = g.accessor(weights, 'VEC4', 5126, { target: 34962 }); }
    const res = { attrs, indices: g.accessor(idx, 'SCALAR', 5123, { target: 34963 }), skinned };
    regionCache.set(ri, res);
    return res;
  }

  const skinnedPrims = [], staticPrims = [];
  for (const b of batches) {
    const mi = materialFor(b.materialReferenceIndex);
    if (mi < 0) continue;               // only standard materials are drawable
    if (!regions[b.regionIndex]) continue;
    const ra = regionAccessors(b.regionIndex);
    const prim = { attributes: ra.attrs, indices: ra.indices, material: mi, mode: 4 };
    (ra.skinned ? skinnedPrims : staticPrims).push(prim);
  }
  const modelName = (opts.name || path.basename(m3Path, '.m3'));
  // Skinned meshes sit at the scene root (glTF ignores their node transform). Unskinned parts are
  // rigidly bound to SC2Root so one skeletal mesh holds everything.
  if (staticPrims.length && bones.length) {
    for (const p of staticPrims) {
      const acc = J.accessors[p.attributes.POSITION];
      const n = acc.count;
      p.attributes = Object.assign({}, p.attributes, {
        JOINTS_0: g.accessor(new Uint16Array(n * 4), 'VEC4', 5123, { target: 34962 }),
        WEIGHTS_0: g.accessor(new Float32Array(n * 4).map((v, i) => (i % 4 === 0 ? 1 : 0)), 'VEC4', 5126, { target: 34962 }),
      });
      skinnedPrims.push(p);
    }
    staticPrims.length = 0;
  }
  if (skinnedPrims.length) {
    J.meshes.push({ name: modelName, primitives: skinnedPrims });
    J.nodes.push({ name: modelName + '_Mesh', mesh: J.meshes.length - 1, skin: skinIdx });
    J.scenes[0].nodes.push(J.nodes.length - 1);
  }
  if (staticPrims.length) {
    J.meshes.push({ name: modelName + '_Static', primitives: staticPrims });
    J.nodes.push({ name: modelName + '_StaticMesh', mesh: J.meshes.length - 1 });
    J.scenes[0].nodes.push(J.nodes.length - 1);
  }
  if (!skinnedPrims.length && !staticPrims.length) report.warnings.push('no drawable geometry (particle-only model?)');

  // Animations: sample every sequence's local bone transforms.
  const seqs = refAll(m.sequences);
  if (opts.animations && seqs.length && bones.length) {
    const sts = refAll(m.sts).map(s => new M3Sts(s));
    const stc = refAll(m.stc).map(s => new M3Stc(s));
    const stg = refAll(m.stg).map(s => new M3Stg(s, sts, stc));
    J.animations = [];
    const usedAnim = new Set();
    seqs.forEach((seq, si) => {
      if (!stg[si]) return;
      const [t0, t1] = seq.interval;
      const len = Math.max(1, t1 - t0);
      const frames = Math.max(2, Math.round(len / 1000 * opts.fps) + 1);
      const times = new Float32Array(frames);
      for (let f = 0; f < frames; f++) times[f] = Math.min(len, f * 1000 / opts.fps) / 1000;
      times[frames - 1] = len / 1000;
      const inst = { frame: 0 };
      const T = bones.map(() => new Float32Array(frames * 3)), R = bones.map(() => new Float32Array(frames * 4)), S = bones.map(() => new Float32Array(frames * 3));
      for (let f = 0; f < frames; f++) {
        inst.frame = t0 + times[f] * 1000;
        for (let bi = 0; bi < bones.length; bi++) {
          const b = bones[bi];
          const t = stg[si].getValueUnsafe(b.location, inst); T[bi][f * 3] = t[0]; T[bi][f * 3 + 1] = t[1]; T[bi][f * 3 + 2] = t[2];
          const r = stg[si].getValueUnsafe(b.rotation, inst);
          // Keep quaternions in one hemisphere so interpolation does not take the long way round.
          let rx = r[0], ry = r[1], rz = r[2], rw = r[3];
          if (f > 0) { const p = f * 4 - 4; if (R[bi][p] * rx + R[bi][p + 1] * ry + R[bi][p + 2] * rz + R[bi][p + 3] * rw < 0) { rx = -rx; ry = -ry; rz = -rz; rw = -rw; } }
          const ql = Math.hypot(rx, ry, rz, rw) || 1;
          R[bi][f * 4] = rx / ql; R[bi][f * 4 + 1] = ry / ql; R[bi][f * 4 + 2] = rz / ql; R[bi][f * 4 + 3] = rw / ql;
          const s = stg[si].getValueUnsafe(b.scale, inst); S[bi][f * 3] = s[0]; S[bi][f * 3 + 1] = s[1]; S[bi][f * 3 + 2] = s[2];
        }
      }
      const input = g.accessor(times, 'SCALAR', 5126, { minmax: true });
      const samplers = [], channels = [];
      const varies = (arr, init) => { for (let i = 0; i < arr.length; i++) if (Math.abs(arr[i] - init[i % init.length]) > 1e-5) return true; return false; };
      for (let bi = 0; bi < bones.length; bi++) {
        const b = bones[bi];
        const add = (data, type, pathName) => { samplers.push({ input, output: g.accessor(data, type, 5126), interpolation: 'LINEAR' }); channels.push({ sampler: samplers.length - 1, target: { node: boneNode[bi], path: pathName } }); };
        if (varies(T[bi], b.location.initValue)) add(T[bi], 'VEC3', 'translation');
        if (varies(R[bi], b.rotation.initValue)) add(R[bi], 'VEC4', 'rotation');
        if (varies(S[bi], b.scale.initValue)) add(S[bi], 'VEC3', 'scale');
      }
      let name = seq.name.get();
      while (usedAnim.has(name)) name += '_';
      usedAnim.add(name);
      if (!channels.length) {
        // glTF requires at least one channel; keep the clip so engines still see the sequence.
        add0();
      }
      function add0() {
        const r = new Float32Array(frames * 4);
        for (let f = 0; f < frames; f++) r.set(bones[0].rotation.initValue, f * 4);
        samplers.push({ input, output: g.accessor(r, 'VEC4', 5126), interpolation: 'LINEAR' });
        channels.push({ sampler: samplers.length - 1, target: { node: boneNode[0], path: 'rotation' } });
      }
      J.animations.push({ name, samplers, channels, extras: { sc2Interval: [t0, t1], movementSpeed: seq.movementSpeed, looping: !(seq.flags & 1) } });
    });
    if (!J.animations.length) delete J.animations;
  }

  // Attachment points (weapon muzzles, origin, overhead) as named empty nodes under their bones.
  for (const ap of refAll(m.attachmentPoints)) {
    const bi = ap.bone;
    if (bi < 0 || bi >= bones.length) continue;
    J.nodes.push({ name: 'Attach_' + String(ap.name.get()).replace(/^Ref_/, '') });
    const parent = J.nodes[boneNode[bi]];
    (parent.children = parent.children || []).push(J.nodes.length - 1);
  }

  const b0 = m.boundings;
  J.scenes[0].extras = { source: m3Path, sc2Bounds: { min: Array.from(b0.min), max: Array.from(b0.max), radius: b0.radius }, facing: '-Y (SC2 model space)' };
  report.stats = { bones: bones.length, regions: regions.length, primitives: skinnedPrims.length + staticPrims.length, animations: J.animations ? J.animations.length : 0, materials: J.materials.length, textures: J.textures.length };
  return { glb: g.build(), report };
}

module.exports = { convertM3 };

if (require.main === module) {
  const { SC2Assets, findInstall } = require('./sc2');
  const [m3, out] = process.argv.slice(2);
  if (!m3 || !out) { console.error('usage: node m3gltf.js <assets/...m3> <out.glb>'); process.exit(1); }
  const sc2 = new SC2Assets(findInstall(process.env.SC2_DIR));
  const t0 = Date.now();
  const { glb, report } = convertM3(sc2, m3);
  fs.mkdirSync(path.dirname(path.resolve(out)), { recursive: true });
  fs.writeFileSync(out, glb);
  console.log(JSON.stringify(Object.assign(report, { bytes: glb.length, ms: Date.now() - t0 })));
}
