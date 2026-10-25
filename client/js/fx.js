// StarCraft II particle effects in the browser: replays the emitters the companion server extracts from SC2's own
// M3 effect models (/fx/<key>.json, see server/m3fx.js) with flipbooked, camera-facing sprites.
// A straight port of the Unreal build's effect player (Unreal/.../BSFx.cpp). Positions are game units, Z up.
window.BS = window.BS || {};
(function (BS) {
  'use strict';

  const rnd01 = Math.random;
  const lerp = (a, b, t) => a + (b - a) * t;
  const MAX_SPRITES = 5000;

  // A value sampled over the effect's sequence: a number, an array, or {fps, v: [...]}.
  class Curve {
    constructor(j, dim) {
      this.dim = dim; this.fps = 0;
      if (j && typeof j === 'object' && !Array.isArray(j) && j.v) { this.fps = j.fps; this.v = j.v.flat(); }
      else this.v = (Array.isArray(j) ? j : [j == null ? 0 : j]).map(x => typeof x === 'boolean' ? (x ? 1 : 0) : x);
      while (this.v.length < dim) this.v.push(0);
    }
    get(t, k = 0) {
      const d = this.dim, v = this.v;
      if (this.fps <= 0 || v.length <= d) return v[k] || 0;
      const n = v.length / d, f = Math.min(n - 1, Math.max(0, t * this.fps)), i = Math.floor(f), j = Math.min(i + 1, n - 1);
      return lerp(v[i * d + k], v[j * d + k], f - i);
    }
  }

  function parseEmitter(o) {
    const e = {
      texture: o.texture, blend: o.blend, cols: Math.max(1, o.cols), rows: Math.max(1, o.rows),
      frames: o.frames.slice(0, 4), phase1: Math.min(1, Math.max(0.01, o.frames[4] || 1)), randFrame: !!o.randFrame,
      max: Math.max(1, o.maxParticles), type: o.type, lw: o.lengthWidth, emitType: o.emitType, area: o.area,
      gravity: o.gravity, drag: o.drag, mid: o.mid.map(m => Math.min(0.99, Math.max(0.01, m))),
      pos: new Curve(o.pos, 3), axis: new Curve(o.axis, 6), rate: new Curve(o.rate, 1),
      speed1: new Curve(o.speed[0], 1), speed2: new Curve(o.speed[1], 1), randSpeed: !!o.speed[2],
      angX: new Curve(o.angle[0], 1), angY: new Curve(o.angle[1], 1), sprX: new Curve(o.spread[0], 1), sprY: new Curve(o.spread[1], 1),
      life1: new Curve(o.life[0], 1), life2: new Curve(o.life[1], 1), randLife: !!o.life[2],
      areaSize: new Curve(o.areaSize, 3), areaRadius: new Curve(o.areaRadius, 1),
      size: new Curve(o.size, 3), size2: o.size2 ? new Curve(o.size2, 3) : null,
      rot: new Curve(o.rot, 3), rot2: o.rot2 ? new Curve(o.rot2, 3) : null,
      col: o.color.map(c => new Curve(c, 4)), col2: o.color2 ? o.color2.map(c => new Curve(c, 4)) : null,
      bursts: o.bursts || [],
    };
    // SC2 leaves a grid on single-image textures; it only applies when frames animate or are randomised.
    if (!e.randFrame && e.frames[1] <= e.frames[0] && e.frames[3] <= e.frames[0] && e.frames[0] === 0) { e.cols = 1; e.rows = 1; }
    return e;
  }

  // Rotate v about unit axis k by angle a (Rodrigues).
  function rotAxis(v, k, a) {
    const c = Math.cos(a), s = Math.sin(a), d = k[0] * v[0] + k[1] * v[1] + k[2] * v[2];
    const x = [k[1] * v[2] - k[2] * v[1], k[2] * v[0] - k[0] * v[2], k[0] * v[1] - k[1] * v[0]];
    return [v[0] * c + x[0] * s + k[0] * d * (1 - c), v[1] * c + x[1] * s + k[1] * d * (1 - c), v[2] * c + x[2] * s + k[2] * d * (1 - c)];
  }
  const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
  const norm = a => { const l = Math.hypot(a[0], a[1], a[2]) || 1; return [a[0] / l, a[1] / l, a[2] / l]; };
  function randUnit() { const z = rnd01() * 2 - 1, a = rnd01() * Math.PI * 2, r = Math.sqrt(1 - z * z); return [r * Math.cos(a), r * Math.sin(a), z]; }
  const curve3 = (k, t, m) => t < m ? lerp(k[0], k[1], t / m) : lerp(k[1], k[2], (t - m) / (1 - m));

  class Effects {
    constructor(renderer) {
      this.r = renderer; this.gl = renderer.gl;
      this.defs = new Map();          // key -> Promise<def|null>
      this.ready = new Map();         // key -> def (once loaded)
      this.textures = new Map();      // sc2 path -> WebGLTexture (or null while loading)
      this.batches = new Map();       // texture|blend -> {tex, add, list}
      this.instances = new Map();     // handle -> instance
      this.sprites = [];
      this.beams = [];                // laser bolts: depth-tested glowing segments (see drawBeams)
      this.next = 1;
      this.density = BS.perfScale || 1;     // phones emit fewer particles
      this.maxSprites = Math.round(MAX_SPRITES * (BS.perfScale || 1));
      this.initGl();
    }

    // ---- Loading ----------------------------------------------------------------------------------
    load(key) {
      if (this.defs.has(key)) return this.defs.get(key);
      const p = fetch('/fx/' + encodeURIComponent(key) + '.json').then(r => r.ok ? r.json() : null).then(j => {
        if (!j || !j.variants || !j.variants.length) return null;
        const def = { key, loop: !!j.loop, variants: j.variants.map(v => ({ duration: Math.max(0.05, v.duration), emitters: v.emitters.map(parseEmitter) })) };
        for (const v of def.variants) for (const e of v.emitters) this.texture(e.texture);
        this.ready.set(key, def);
        return def;
      }).catch(() => null);
      this.defs.set(key, p);
      return p;
    }
    preload(keys) { return Promise.all(keys.map(k => this.load(k))); }
    texture(path) {
      if (!path || this.textures.has(path)) return;
      this.textures.set(path, null);
      const img = new Image();
      img.onload = () => {
        const gl = this.gl, t = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_2D, t);
        gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, false);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, img);
        const pot = (img.width & (img.width - 1)) === 0 && (img.height & (img.height - 1)) === 0;
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
        if (pot) { gl.generateMipmap(gl.TEXTURE_2D); gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR_MIPMAP_LINEAR); }
        else gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
        gl.bindTexture(gl.TEXTURE_2D, null);
        this.textures.set(path, t);
      };
      img.src = '/fxtex/' + path.replace(/\.dds$/i, '.png');
    }

    // ---- Playback ---------------------------------------------------------------------------------
    // Returns a handle (0 if the effect is unknown or still loading).
    play(key, x, y, z, yaw, scale, delay) {
      const def = this.ready.get(key);
      if (!def) { this.load(key); return 0; }
      const vi = Math.floor(rnd01() * def.variants.length), n = def.variants[vi].emitters.length;
      const h = this.next++;
      this.instances.set(h, { def, var: def.variants[vi], x, y, z: z || 0, yaw: yaw || 0, scale: scale || 1, delay: delay || 0, t: 0,
        loop: def.loop, stopped: false, acc: new Float32Array(n), burst: new Int32Array(n), alive: new Int32Array(n) });
      return h;
    }
    move(h, x, y, z, yaw) { const i = this.instances.get(h); if (i) { i.x = x; i.y = y; i.z = z; i.yaw = yaw; } }
    stop(h) { const i = this.instances.get(h); if (i) i.stopped = true; }
    has(key) { return this.ready.has(key); }

    spawn(h, inst, ei, e, t) {
      if (this.sprites.length >= this.maxSprites || inst.alive[ei] >= e.max) return;
      const az = [e.axis.get(t, 0), e.axis.get(t, 1), e.axis.get(t, 2)], ax = [e.axis.get(t, 3), e.axis.get(t, 4), e.axis.get(t, 5)];
      const ay = cross(az, ax);
      let lx = e.pos.get(t, 0), ly = e.pos.get(t, 1), lz = e.pos.get(t, 2);
      const sx = e.areaSize.get(t, 0), sy = e.areaSize.get(t, 1), sz = e.areaSize.get(t, 2), rad = e.areaRadius.get(t);
      let o = [0, 0, 0];
      switch (e.area) {
        case 1: o = [(rnd01() - 0.5) * sx, (rnd01() - 0.5) * sy, 0]; break;
        case 2: { const u = randUnit(), r = rad * Math.cbrt(rnd01()); o = [u[0] * r, u[1] * r, u[2] * r]; break; }
        case 3: o = [(rnd01() - 0.5) * sx, (rnd01() - 0.5) * sy, (rnd01() - 0.5) * sz]; break;
        case 4: case 5: { const a = rnd01() * Math.PI * 2, r = rad * Math.sqrt(rnd01()); o = [Math.cos(a) * r, Math.sin(a) * r, e.area === 4 ? (rnd01() - 0.5) * sz : 0]; break; }
      }
      lx += ax[0] * o[0] + ay[0] * o[1] + az[0] * o[2]; ly += ax[1] * o[0] + ay[1] * o[1] + az[1] * o[2]; lz += ax[2] * o[0] + ay[2] * o[1] + az[2] * o[2];
      let dir;
      if (e.emitType === 1 || e.emitType === 3) dir = randUnit();
      else if (e.emitType === 2) dir = rnd01() < 0.5 ? az : [-az[0], -az[1], -az[2]];
      else {
        const rx = e.angX.get(t) + (rnd01() * 2 - 1) * e.sprX.get(t), ry = e.angY.get(t) + (rnd01() * 2 - 1) * e.sprY.get(t);
        dir = rotAxis(rotAxis(az, norm(ax), rx), norm(ay), ry);
      }
      const speed = e.randSpeed ? lerp(e.speed1.get(t), e.speed2.get(t), rnd01()) : e.speed1.get(t);
      // Model space (SC2 models face -Y) to game space: rotate by yaw + 90 degrees.
      const phi = inst.yaw + Math.PI / 2, cs = Math.cos(phi), sn = Math.sin(phi), sc = inst.scale;
      const px = inst.x + (lx * cs - ly * sn) * sc, py = inst.y + (lx * sn + ly * cs) * sc, pz = inst.z + lz * sc;
      const vx = (dir[0] * cs - dir[1] * sn) * speed * sc, vy = (dir[0] * sn + dir[1] * cs) * speed * sc, vz = dir[2] * speed * sc;
      const s1 = [e.size.get(t, 0), e.size.get(t, 1), e.size.get(t, 2)], s2 = e.size2 ? [e.size2.get(t, 0), e.size2.get(t, 1), e.size2.get(t, 2)] : s1, ks = rnd01();
      const r1 = [e.rot.get(t, 0), e.rot.get(t, 1), e.rot.get(t, 2)], r2 = e.rot2 ? [e.rot2.get(t, 0), e.rot2.get(t, 1), e.rot2.get(t, 2)] : r1, kr = rnd01();
      const kc = rnd01(), cols = [];
      for (let k = 0; k < 3; k++) {
        const c = [0, 1, 2, 3].map(i => e.col[k].get(t, i));
        cols.push(e.col2 ? c.map((x, i) => lerp(x, e.col2[k].get(t, i), kc)) : c);
      }
      const nf = e.cols * e.rows;
      const frame0 = nf > 1 && e.randFrame ? e.frames[0] + Math.floor(rnd01() * (Math.max(e.frames[0], Math.min(e.frames[1] > 0 ? e.frames[1] : nf - 1, nf - 1)) - e.frames[0] + 1)) : -1;
      const life = Math.max(0.02, e.randLife ? lerp(e.life1.get(t), e.life2.get(t), rnd01()) : e.life1.get(t));
      this.sprites.push({ e, h, ei, x: px, y: py, z: pz, ox: px, oy: py, oz: pz, vx, vy, vz, sp0: Math.max(0.01, Math.hypot(vx, vy, vz)),
        age: 0, life, scale: sc, s: s1.map((v, i) => lerp(v, s2[i], ks)), r: r1.map((v, i) => lerp(v, r2[i], kr)), c: cols, frame0 });
      inst.alive[ei]++;
    }

    update(dt) {
      if (dt <= 0) return;
      for (const [h, inst] of this.instances) {
        if (inst.delay > 0) { inst.delay -= dt; continue; }
        const v = inst.var, t0 = inst.t, t1 = inst.t + dt;
        if (!inst.stopped) {
          for (let i = 0; i < v.emitters.length; i++) {
            const e = v.emitters[i], tc = Math.min(t1, v.duration);
            inst.acc[i] += Math.max(0, e.rate.get(tc)) * dt;
            let n = Math.floor(inst.acc[i]); inst.acc[i] -= n;
            while (inst.burst[i] < e.bursts.length && e.bursts[inst.burst[i]][0] <= t1) n += Math.round(e.bursts[inst.burst[i]++][1]);
            for (let k = 0; k < n; k++) if (this.density >= 1 || rnd01() < this.density) this.spawn(h, inst, i, e, tc);
          }
        }
        inst.t = t1;
        if (inst.t >= v.duration) {
          if (inst.loop && !inst.stopped) { inst.t %= v.duration; inst.burst.fill(0); }
          else if (t0 < v.duration || inst.stopped) this.instances.delete(h);
        } else if (inst.stopped) this.instances.delete(h);
      }
      const S = this.sprites;
      for (let i = S.length - 1; i >= 0; i--) {
        const s = S[i];
        s.age += dt;
        if (s.age >= s.life) {
          const inst = this.instances.get(s.h); if (inst) inst.alive[s.ei]--;
          S[i] = S[S.length - 1]; S.pop(); continue;
        }
        if (s.e.drag > 0) { const k = Math.exp(-s.e.drag * dt); s.vx *= k; s.vy *= k; s.vz *= k; }
        s.vz += s.e.gravity * s.scale * dt;
        s.x += s.vx * dt; s.y += s.vy * dt; s.z += s.vz * dt;
      }
    }

    // A laser bolt from a to b ([x, y, z]) that fades over life seconds; rgb is the glow colour (the core is near white).
    beam(a, b, life, rgb, width) { this.beams.push({ a, b, t: 0, life, rgb: rgb || [1, 0.3, 0.15], w: width || 0.09 }); }

    // ---- Rendering --------------------------------------------------------------------------------
    initGl() {
      const gl = this.gl;
      const vs = 'attribute vec3 a_pos; attribute vec2 a_uv; attribute vec4 a_col; uniform mat4 u_vp; varying vec2 v_uv; varying vec4 v_col;' +
        'void main(){ v_uv = a_uv; v_col = a_col; gl_Position = u_vp * vec4(a_pos, 1.0); }';
      const fs = 'precision mediump float; uniform sampler2D u_tex; uniform float u_add; varying vec2 v_uv; varying vec4 v_col;' +
        'void main(){ vec4 t = texture2D(u_tex, v_uv); float a = t.a * v_col.a;' +
        ' if (u_add > 0.5) gl_FragColor = vec4(t.rgb * v_col.rgb * a * 1.5, 1.0); else gl_FragColor = vec4(t.rgb * v_col.rgb, a); }';
      const sh = (type, src) => { const s = gl.createShader(type); gl.shaderSource(s, src); gl.compileShader(s); if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(s)); return s; };
      const p = gl.createProgram();
      gl.attachShader(p, sh(gl.VERTEX_SHADER, vs)); gl.attachShader(p, sh(gl.FRAGMENT_SHADER, fs)); gl.linkProgram(p);
      this.prog = p;
      this.loc = { pos: gl.getAttribLocation(p, 'a_pos'), uv: gl.getAttribLocation(p, 'a_uv'), col: gl.getAttribLocation(p, 'a_col'),
        vp: gl.getUniformLocation(p, 'u_vp'), tex: gl.getUniformLocation(p, 'u_tex'), add: gl.getUniformLocation(p, 'u_add') };
      this.vbuf = gl.createBuffer(); this.ibuf = gl.createBuffer();
      this.verts = new Float32Array(MAX_SPRITES * 4 * 9);
      const idx = new Uint16Array(Math.min(MAX_SPRITES, 16383) * 6);
      for (let i = 0, v = 0; i < idx.length; i += 6, v += 4) { idx[i] = v; idx[i + 1] = v + 1; idx[i + 2] = v + 2; idx[i + 3] = v; idx[i + 4] = v + 2; idx[i + 5] = v + 3; }
      gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, this.ibuf); gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, idx, gl.STATIC_DRAW);
      gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, null);
      // Beam texture: white, alpha falling off across the bolt (v) for a soft glowing edge.
      const prof = new Uint8Array(4 * 32);
      for (let i = 0; i < 32; i++) { const d = Math.abs(i - 15.5) / 15.5; prof.set([255, 255, 255, Math.round(255 * Math.exp(-d * d * 5))], i * 4); }
      this.beamTex = gl.createTexture();
      gl.bindTexture(gl.TEXTURE_2D, this.beamTex);
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 1, 32, 0, gl.RGBA, gl.UNSIGNED_BYTE, prof);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR); gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE); gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
      gl.bindTexture(gl.TEXTURE_2D, null);
    }

    // Laser bolts as camera-facing ribbons in the 3D pass, so the hull, units and terrain in front of them hide them.
    // Each bolt is a wide coloured glow plus a thin hot core, added on top of the scene.
    drawBeams(camera, dt) {
      const B = this.beams;
      for (let i = B.length - 1; i >= 0; i--) { B[i].t += dt; if (B[i].t > B[i].life) B.splice(i, 1); }
      if (!B.length) return;
      const gl = this.gl, cam = camera.location, V = this.verts;
      let n = 0;
      for (const bm of B) {
        const k = 1 - bm.t / bm.life, [ax, ay, az] = bm.a, [bx, by, bz] = bm.b;
        let dx = bx - ax, dy = by - ay, dz = bz - az;
        const mx = (ax + bx) / 2 - cam[0], my = (ay + by) / 2 - cam[1], mz = (az + bz) / 2 - cam[2];
        // Side vector: perpendicular to the bolt and to the view ray, so the ribbon faces the camera.
        let sx = dy * mz - dz * my, sy = dz * mx - dx * mz, sz = dx * my - dy * mx;
        const sl = Math.hypot(sx, sy, sz) || 1;
        for (const [w, c] of [[bm.w * 2.6, [bm.rgb[0] * 0.55 * k, bm.rgb[1] * 0.55 * k, bm.rgb[2] * 0.55 * k]], [bm.w * 0.7, [k, 0.55 + 0.45 * bm.rgb[1] * k, 0.4 * k + 0.3 * bm.rgb[2] * k]]]) {
          if (n >= 16383) break;
          const ox = sx / sl * w, oy = sy / sl * w, oz = sz / sl * w, o = n * 36;
          const P = [[ax - ox, ay - oy, az - oz, 0, 0], [bx - ox, by - oy, bz - oz, 1, 0], [bx + ox, by + oy, bz + oz, 1, 1], [ax + ox, ay + oy, az + oz, 0, 1]];
          for (let q = 0; q < 4; q++) V.set([P[q][0], P[q][1], P[q][2], P[q][3], P[q][4], c[0], c[1], c[2], 1], o + q * 9);
          n++;
        }
      }
      if (this.maxAttribs === undefined) this.maxAttribs = gl.getParameter(gl.MAX_VERTEX_ATTRIBS);
      for (let i = 0; i < this.maxAttribs; i++) gl.disableVertexAttribArray(i);
      gl.useProgram(this.prog);
      gl.uniformMatrix4fv(this.loc.vp, false, camera.viewProjectionMatrix);
      gl.uniform1i(this.loc.tex, 0); gl.uniform1f(this.loc.add, 1);
      gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, this.beamTex);
      gl.bindBuffer(gl.ARRAY_BUFFER, this.vbuf); gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, this.ibuf);
      gl.enableVertexAttribArray(this.loc.pos); gl.enableVertexAttribArray(this.loc.uv); gl.enableVertexAttribArray(this.loc.col);
      gl.vertexAttribPointer(this.loc.pos, 3, gl.FLOAT, false, 36, 0);
      gl.vertexAttribPointer(this.loc.uv, 2, gl.FLOAT, false, 36, 12);
      gl.vertexAttribPointer(this.loc.col, 4, gl.FLOAT, false, 36, 20);
      gl.enable(gl.DEPTH_TEST); gl.depthMask(false); gl.disable(gl.CULL_FACE); gl.enable(gl.BLEND); gl.blendFunc(gl.ONE, gl.ONE);
      gl.bufferData(gl.ARRAY_BUFFER, V.subarray(0, n * 36), gl.DYNAMIC_DRAW);
      gl.drawElements(gl.TRIANGLES, n * 6, gl.UNSIGNED_SHORT, 0);
      gl.disableVertexAttribArray(this.loc.pos); gl.disableVertexAttribArray(this.loc.uv); gl.disableVertexAttribArray(this.loc.col);
      gl.bindBuffer(gl.ARRAY_BUFFER, null); gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, null);
      gl.bindTexture(gl.TEXTURE_2D, null);
      gl.depthMask(true); gl.disable(gl.BLEND);
      gl.useProgram(null);
      if (this.r.viewer.webgl) this.r.viewer.webgl.currentShader = null;
    }

    draw(camera) {
      const S = this.sprites;
      if (!S.length) return;
      const gl = this.gl;
      const cam = camera.location;
      // Group sprites per texture and blend mode, blended batches first so additive glows sit on top.
      const groups = new Map();
      for (const s of S) {
        const tex = this.textures.get(s.e.texture);
        if (!tex) continue;
        const add = s.e.blend === 2 || s.e.blend === 3;
        const k = (add ? '1' : '0') + s.e.texture;
        let g = groups.get(k); if (!g) { g = { tex, add, list: [] }; groups.set(k, g); }
        g.list.push(s);
      }
      if (!groups.size) return;
      if (this.maxAttribs === undefined) this.maxAttribs = gl.getParameter(gl.MAX_VERTEX_ATTRIBS);
      for (let i = 0; i < this.maxAttribs; i++) gl.disableVertexAttribArray(i);
      gl.useProgram(this.prog);
      gl.uniformMatrix4fv(this.loc.vp, false, camera.viewProjectionMatrix);
      gl.uniform1i(this.loc.tex, 0);
      gl.activeTexture(gl.TEXTURE0);
      gl.bindBuffer(gl.ARRAY_BUFFER, this.vbuf);
      gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, this.ibuf);
      gl.enableVertexAttribArray(this.loc.pos); gl.enableVertexAttribArray(this.loc.uv); gl.enableVertexAttribArray(this.loc.col);
      gl.vertexAttribPointer(this.loc.pos, 3, gl.FLOAT, false, 36, 0);
      gl.vertexAttribPointer(this.loc.uv, 2, gl.FLOAT, false, 36, 12);
      gl.vertexAttribPointer(this.loc.col, 4, gl.FLOAT, false, 36, 20);
      gl.enable(gl.DEPTH_TEST); gl.depthMask(false); gl.disable(gl.CULL_FACE); gl.enable(gl.BLEND);
      const ordered = [...groups.values()].sort((a, b) => a.add - b.add);
      const V = this.verts;
      for (const g of ordered) {
        if (g.add) gl.blendFunc(gl.ONE, gl.ONE); else gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
        gl.bindTexture(gl.TEXTURE_2D, g.tex);
        gl.uniform1f(this.loc.add, g.add ? 1 : 0);
        let n = 0;
        for (const s of g.list) {
          if (n >= 16383) break;
          this.quad(V, n * 36, s, cam);
          n++;
        }
        gl.bufferData(gl.ARRAY_BUFFER, V.subarray(0, n * 36), gl.DYNAMIC_DRAW);
        gl.drawElements(gl.TRIANGLES, n * 6, gl.UNSIGNED_SHORT, 0);
      }
      gl.disableVertexAttribArray(this.loc.pos); gl.disableVertexAttribArray(this.loc.uv); gl.disableVertexAttribArray(this.loc.col);
      gl.bindBuffer(gl.ARRAY_BUFFER, null); gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, null);
      gl.bindTexture(gl.TEXTURE_2D, null);
      gl.depthMask(true); gl.disable(gl.BLEND);
      gl.useProgram(null);
      if (this.r.viewer.webgl) this.r.viewer.webgl.currentShader = null;
    }

    // Writes one sprite's four vertices (pos3, uv2, rgba4) at V[o]. Allocation-free: this runs for thousands of sprites a frame.
    quad(V, o, s, cam) {
      const e = s.e, t = Math.min(1, s.age / s.life), m = e.mid;
      const size = Math.max(0, curve3(s.s, t, m[0])) * s.scale, ang = curve3(s.r, t, m[3]);
      const c0 = s.c[0], c1 = s.c[1], c2 = s.c[2];
      let r, g, b, a;
      if (t < m[1]) { const k = t / m[1]; r = lerp(c0[0], c1[0], k); g = lerp(c0[1], c1[1], k); b = lerp(c0[2], c1[2], k); }
      else { const k = (t - m[1]) / (1 - m[1]); r = lerp(c1[0], c2[0], k); g = lerp(c1[1], c2[1], k); b = lerp(c1[2], c2[2], k); }
      a = t < m[2] ? lerp(c0[3], c1[3], t / m[2]) : lerp(c1[3], c2[3], (t - m[2]) / (1 - m[2]));
      // Flipbook frame -> uv rectangle.
      const nf = e.cols * e.rows;
      let frame = 0;
      if (nf > 1) {
        if (s.frame0 >= 0) frame = s.frame0;
        else {
          const f0 = e.frames[0], f1 = e.frames[1] > f0 ? e.frames[1] : nf - 1, f2 = e.frames[3] > f1 ? e.frames[3] : f1;
          frame = Math.min(Math.floor(t < e.phase1 ? lerp(f0, f1, t / e.phase1) : lerp(f1, f2, (t - e.phase1) / Math.max(0.01, 1 - e.phase1))), nf - 1);
        }
      }
      const fc = frame % e.cols, fr = (frame / e.cols) | 0;
      const u0 = fc / e.cols, u1 = (fc + 1) / e.cols, v0 = fr / e.rows, v1 = (fr + 1) / e.rows;
      let cx = s.x, cy = s.y, cz = s.z, len = size * 0.5, wid = size * 0.5;
      let tx = cam[0] - s.x, ty = cam[1] - s.y, tz = cam[2] - s.z;
      const tl = Math.hypot(tx, ty, tz) || 1; tx /= tl; ty /= tl; tz /= tl;
      let ax, ay, az, sx, sy, sz;
      if (e.type === 5) {
        // Ground splat: flat on the ground.
        ax = Math.cos(ang); ay = Math.sin(ang); az = 0; sx = -ay; sy = ax; sz = 0; cz = Math.max(cz, 0.02);
      } else {
        if (e.type === 1 || e.type === 7 || e.type === 9) {
          // Stretched along the velocity (or from the spawn point for rays), projected onto the screen.
          let vx = s.vx, vy = s.vy, vz = s.vz;
          if (e.type === 9) { vx = s.x - s.ox; vy = s.y - s.oy; vz = s.z - s.oz; }
          const d = vx * tx + vy * ty + vz * tz;
          ax = vx - tx * d; ay = vy - ty * d; az = vz - tz * d;
          let al = Math.hypot(ax, ay, az);
          if (al < 1e-4) { ax = ty; ay = -tx; az = 0; al = Math.hypot(ax, ay) || 1; }
          ax /= al; ay /= al; az /= al;
          if (e.type === 9) { len = Math.max(size, Math.hypot(vx, vy, vz)) * 0.5; cx = (s.x + s.ox) / 2; cy = (s.y + s.oy) / 2; cz = (s.z + s.oz) / 2; }
          else len = size * 0.5 * Math.max(1, e.lw * Math.min(1, Math.max(0.25, Math.hypot(s.vx, s.vy, s.vz) / s.sp0)));
        } else {
          // Camera-facing, rotated about the view axis.
          let rx = ty, ry = -tx, rz = 0, rl = Math.hypot(rx, ry);
          if (rl < 1e-3) { rx = 0; ry = tz; rz = -ty; rl = Math.hypot(ry, rz) || 1; }
          rx /= rl; ry /= rl; rz /= rl;
          const c = Math.cos(ang), sn = Math.sin(ang);
          // Rodrigues with axis toCam (r is perpendicular to it).
          const kx = ty * rz - tz * ry, ky = tz * rx - tx * rz, kz = tx * ry - ty * rx;
          ax = rx * c + kx * sn; ay = ry * c + ky * sn; az = rz * c + kz * sn;
          if (e.type === 6) len = size * 0.5 * e.lw;
        }
        sx = ty * az - tz * ay; sy = tz * ax - tx * az; sz = tx * ay - ty * ax;
        const sl = Math.hypot(sx, sy, sz) || 1; sx /= sl; sy /= sl; sz /= sl;
      }
      const lx = ax * len, ly = ay * len, lz = az * len, wx = sx * wid, wy = sy * wid, wz = sz * wid;
      // Corners: (-1,-1) (1,-1) (1,1) (-1,1).
      V[o] = cx - lx - wx; V[o + 1] = cy - ly - wy; V[o + 2] = cz - lz - wz; V[o + 3] = u0; V[o + 4] = v1;
      V[o + 9] = cx + lx - wx; V[o + 10] = cy + ly - wy; V[o + 11] = cz + lz - wz; V[o + 12] = u1; V[o + 13] = v1;
      V[o + 18] = cx + lx + wx; V[o + 19] = cy + ly + wy; V[o + 20] = cz + lz + wz; V[o + 21] = u1; V[o + 22] = v0;
      V[o + 27] = cx - lx + wx; V[o + 28] = cy - ly + wy; V[o + 29] = cz - lz + wz; V[o + 30] = u0; V[o + 31] = v0;
      for (let k = 5; k < 36; k += 9) { V[o + k] = r; V[o + k + 1] = g; V[o + k + 2] = b; V[o + k + 3] = a; }
    }
  }

  BS.Effects = Effects;
})(window.BS);
