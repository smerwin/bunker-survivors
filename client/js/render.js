// Rendering: wraps mdx-m3-viewer (WebGL) for StarCraft II M3 models, adds a textured ground
// plane, a controllable perspective camera (pinch/drag/wheel), instance pooling and a 2D overlay.
window.BS = window.BS || {};
(function (BS) {
  'use strict';

  const MV = window.ModelViewer;
  const DEG = Math.PI / 180;
  const BUNKER_EYE = 1.35;                     // bunker view eye height (just over the firing slits)

  // SC2 models face -Y at zero rotation; game yaw is measured from +X, so offset by 90 degrees.
  const FACING_OFFSET = Math.PI / 2;
  function quatZ(yaw) { const h = (yaw + FACING_OFFSET) * 0.5; return [0, 0, Math.sin(h), Math.cos(h)]; }

  class Handle {
    constructor(renderer, key) {
      this.r = renderer; this.key = key;
      this.inst = null; this.model = null;
      this.x = 0; this.y = 0; this.z = 0; this.yaw = 0; this.scale = 1;
      this.team = 0; this.pending = null; this.released = false;
      this.seqName = null; this.seqIndex = -1; this.loop = true; this.seqStart = 0; this.seqLen = 0; this.timeScale = 1;
    }
    attach(model) {
      if (this.released) return;
      this.model = model;
      const inst = this.r.acquireInstance(model, this.corpse);
      this.inst = inst;
      inst.setLocation([this.x, this.y, this.z]);
      inst.setRotation(quatZ(this.yaw));
      inst.setUniformScale(this.scale);
      inst.setTeamColor(this.team);
      inst.bsHide = this.swipe === false ? HIDE_ADDITIVE : null;
      inst.bsTurret = this.turret || 0;
      if (this.turretBone) hookTurret(inst, this.turretBone);
      if (this.corpse) { inst.vertexColor[0] = inst.vertexColor[1] = inst.vertexColor[2] = 0.42; }
      if (this.pending) { const p = this.pending; this.pending = null; if (p.random) this.playRandom(p.name, p.loop); else this.play(p.name, p.loop, p.timeScale); }
      inst.show();
    }
    setPos(x, y, z) {
      this.x = x; this.y = y; this.z = z || 0;
      if (this.inst) this.inst.setLocation([this.x, this.y, this.z]);
      return this;
    }
    setYaw(yaw) {
      this.yaw = yaw;
      if (this.inst) this.inst.setRotation(quatZ(yaw));
      return this;
    }
    setScale(s) { this.scale = s; if (this.inst) this.inst.setUniformScale(s); return this; }
    setTeam(t) { this.team = t; if (this.inst) this.inst.setTeamColor(t); return this; }
    setRate(r) { this.timeScale = r; if (this.inst) this.inst.timeScale = r; return this; }
    // Weapon swipe trails are additive layers SC2 only shows mid-swing; hide them otherwise.
    setSwipe(show) { this.swipe = show; if (this.inst) this.inst.bsHide = show ? null : HIDE_ADDITIVE; return this; }
    // Plays a random sequence whose name matches (e.g. one of several death animations). Returns its length.
    playRandom(re, loop) {
      if (!this.model) { this.pending = { name: re, loop, timeScale: 1, random: true }; return 3; }
      const seqs = this.model.sequences.map((q, i) => [q, i]).filter(([q]) => re.test(q.name));
      if (!seqs.length) return 0;
      return this.play(seqs[Math.floor(Math.random() * seqs.length)][1], loop);
    }
    // Finds a sequence by name (string or RegExp) and plays it. Returns its length in seconds (0 if unknown).
    play(name, loop, timeScale) {
      this.loop = loop !== false; this.timeScale = timeScale || 1;
      if (!this.inst) { this.pending = { name, loop: this.loop, timeScale: this.timeScale }; return 0; }
      const idx = this.r.findSequence(this.model, name);
      if (idx < 0) { this.seqIndex = -1; this.seqLen = 0; return 0; }
      const seq = this.model.sequences[idx];
      this.seqIndex = idx; this.seqName = seq.name;
      this.seqLen = (seq.interval[1] - seq.interval[0]) / 1000;
      this.inst.setSequence(idx);
      this.inst.setSequenceLoopMode(this.loop ? 2 : 1);
      this.inst.timeScale = this.timeScale;
      this.seqStart = this.r.time;
      return this.seqLen;
    }
    // Length (seconds) of a sequence by name without playing it.
    seqLength(name) {
      if (!this.model) return 0;
      const idx = this.r.findSequence(this.model, name);
      if (idx < 0) return 0;
      const seq = this.model.sequences[idx];
      return (seq.interval[1] - seq.interval[0]) / 1000;
    }
    hasSeq(name) { return this.model ? this.r.findSequence(this.model, name) >= 0 : false; }
    get elapsed() { return (this.r.time - this.seqStart) * this.timeScale; }
    get ended() { return !this.loop && this.seqLen > 0 && this.elapsed >= this.seqLen; }
    // Turn one bone (a siege tank's turret) about the vertical by an angle relative to the hull, on top of the
    // animation, as the Unreal build does with a poseable mesh.
    setTurret(bone, rel) {
      this.turretBone = bone; this.turret = rel;
      if (this.inst) { hookTurret(this.inst, bone); this.inst.bsTurret = rel; }
      return this;
    }
    release() {
      if (this.released) return;
      this.released = true;
      if (this.inst) this.inst.bsTurret = 0;
      if (this.inst) { if (this.corpse) this.inst.vertexColor[0] = this.inst.vertexColor[1] = this.inst.vertexColor[2] = 1; this.inst.bsHide = null; this.r.releaseInstance(this.model, this.inst, this.corpse); }
      this.inst = null;
    }
  }

  // Replaces an M3 instance's skeleton update with the viewer's own loop plus one extra step: right after the named
  // bone takes its animated rotation, twist it about the model's up axis by inst.bsTurret radians. With P the parent's
  // world rotation, local' = P^-1 * Rz(a) * P * local, which turns the bone (and everything parented to it) in place.
  function hookTurret(inst, boneName) {
    const sk = inst.skeleton;
    if (!sk || inst.bsTurretHooked) return;
    const re = new RegExp('^' + boneName + '$', 'i'), idx = inst.model.bones.findIndex(b => re.test(b.name));
    if (idx < 0) return;
    inst.bsTurretHooked = true;
    const nodes = sk.nodes, modelNodes = sk.modelNodes, q = [0, 0, 0, 1], t = [0, 0, 0, 1];
    const mul = (o, a, b) => {
      const ax = a[0], ay = a[1], az = a[2], aw = a[3], bx = b[0], by = b[1], bz = b[2], bw = b[3];
      o[0] = aw * bx + ax * bw + ay * bz - az * by; o[1] = aw * by - ax * bz + ay * bw + az * bx;
      o[2] = aw * bz + ax * by - ay * bx + az * bw; o[3] = aw * bw - ax * bx - ay * by - az * bz; return o;
    };
    sk.update = function (dt) {
      for (let i = 0, l = nodes.length; i < l; i++) {
        const node = nodes[i], mn = modelNodes[i];
        this.getValue4(node.localRotation, mn.rotation, inst);
        this.getValue3(node.localLocation, mn.location, inst);
        this.getValue3(node.localScale, mn.scale, inst);
        if (i === idx && inst.bsTurret) {
          const p = node.parent.worldRotation, a = inst.bsTurret * 0.5;
          q[0] = 0; q[1] = 0; q[2] = Math.sin(a); q[3] = Math.cos(a);
          mul(t, q, p);                                   // Rz * P
          mul(t, [-p[0], -p[1], -p[2], p[3]], t);         // P^-1 * Rz * P
          mul(node.localRotation, t, node.localRotation);
        }
        node.recalculateTransformation(inst);
        for (const child of node.children) { child.recalculateTransformation(); child.update(dt); }
      }
    };
  }

  const HIDE_ADDITIVE = b => b.material && b.material.blendMode === 2;
  // Lets an instance skip some of its batches (see Handle.setSwipe) by wrapping the M3 instance's render.
  function patchM3(inst) {
    const proto = Object.getPrototypeOf(inst);
    if (proto.__bsPatched || !proto.renderOpaque) return;
    proto.__bsPatched = true;
    const orig = proto.renderOpaque;
    proto.renderOpaque = function () {
      if (!this.bsHide || !this.model) return orig.call(this);
      const model = this.model, all = model.batches;
      model.batches = all.filter(b => !this.bsHide(b));
      try { return orig.call(this); } finally { model.batches = all; }
    };
  }

  class Renderer {
    constructor(canvas, overlay, opts) {
      this.canvas = canvas; this.overlay = overlay;
      this.maxDpr = (opts && opts.maxDpr) || 2;
      this.animEvery = (opts && opts.animEvery) || 1;
      this.shake = 0;
      this.ctx2d = overlay.getContext('2d');
      this.dpr = Math.min(window.devicePixelRatio || 1, this.maxDpr || 2);
      this.viewer = new MV.viewer.ModelViewer(canvas, { alpha: false, antialias: true, powerPreference: 'high-performance' });
      this.gl = this.viewer.gl;
      this.viewer.on('error', e => console.warn('viewer error', e.error, e.reason || '', e.src || ''));
      this.viewer.addHandler(MV.viewer.handlers.m3);
      this.viewer.addHandler(MV.viewer.handlers.dds);
      this.scene = this.viewer.addScene();
      this.scene.alpha = true;              // we clear ourselves (ground is drawn first)
      this.scene.lightPosition = [40, -60, 90];
      // Corpses live in their own scene, drawn first; the depth buffer is cleared before the living are drawn,
      // so a corpse can never cover a live unit.
      this.corpseScene = this.viewer.addScene();
      this.corpseScene.alpha = true;
      this.corpseScene.lightPosition = this.scene.lightPosition;
      this.solver = (src) => '/asset/' + String(src).replace(/\\/g, '/');
      this.models = new Map();            // key -> Promise<model>
      this.pools = new Map();             // model -> instance[]
      this.handles = new Set();
      this.time = 0;
      this.cam = { tx: 0, ty: 6, dist: 20, pitch: 60 * DEG, yaw: 0, fov: 55 * DEG, minDist: 11, maxDist: 36, bounds: { x0: -5, x1: 5, y0: -3, y1: 18 } };
      this.ground = null;
      this.groundTex = null;
      this.effects = [];                  // one-shot model effects
      this.noGround = /noground=1/.test(location.search);
      this.resize();
      this.initGround();
      this.fx = new BS.Effects(this);
      window.addEventListener('resize', () => this.resize());
    }

    resize() {
      const w = this.canvas.clientWidth || window.innerWidth, h = this.canvas.clientHeight || window.innerHeight;
      this.dpr = Math.min(window.devicePixelRatio || 1, this.maxDpr || 2);
      this.width = w; this.height = h;
      this.canvas.width = Math.round(w * this.dpr); this.canvas.height = Math.round(h * this.dpr);
      this.overlay.width = this.canvas.width; this.overlay.height = this.canvas.height;
      this.scene.viewport = [0, 0, this.canvas.width, this.canvas.height];
      this.corpseScene.viewport = this.scene.viewport;
      this.updateCamera();
    }

    // ---- Models and instances --------------------------------------------------------------
    ensureModel(key) {
      if (this.models.has(key)) return this.models.get(key);
      const path = BS.MODELS[key] || BS.EXTRA_MODELS[key];
      if (!path) return Promise.reject(new Error('unknown model ' + key));
      const p = this.viewer.load(path, this.solver).then(model => {
        if (!model) throw new Error('model failed: ' + key);
        // Particle-only effect models have no mesh; the viewer cannot draw them (and errors trying).
        if (!model.batches || !model.batches.length || !model.arrayBuffer) throw new Error('model has no drawable geometry: ' + key);
        return model;
      });
      this.models.set(key, p);
      return p;
    }
    async preload(keys, onProgress) {
      let done = 0;
      await Promise.all(keys.map(k => this.ensureModel(k).catch(e => console.warn(e)).then(() => { done++; if (onProgress) onProgress(done, keys.length); })));
    }
    findSequence(model, name) {
      const seqs = model.sequences;
      if (typeof name === 'number') return name < seqs.length ? name : -1;
      if (name instanceof RegExp) {
        for (let i = 0; i < seqs.length; i++) if (name.test(seqs[i].name)) return i;
        return -1;
      }
      const lower = String(name).toLowerCase();
      for (let i = 0; i < seqs.length; i++) if (seqs[i].name.toLowerCase() === lower) return i;
      for (let i = 0; i < seqs.length; i++) if (seqs[i].name.toLowerCase().startsWith(lower)) return i;
      return -1;
    }
    acquireInstance(model, corpse) {
      const key = corpse ? 'c' : 'l';
      let pools = this.pools.get(model);
      if (!pools) { pools = { c: [], l: [] }; this.pools.set(model, pools); }
      let inst = pools[key].pop();
      if (!inst) { inst = model.addInstance(); inst.setScene(corpse ? this.corpseScene : this.scene); patchM3(inst); }
      inst.setSequence(-1);
      inst.timeScale = 1;
      return inst;
    }
    releaseInstance(model, inst, corpse) {
      inst.hide();
      inst.setSequence(-1);
      const pools = this.pools.get(model);
      if (pools && pools[corpse ? 'c' : 'l'].length < 96) pools[corpse ? 'c' : 'l'].push(inst);
    }
    // Spawns a model; the handle works immediately and attaches once the model has streamed in.
    spawn(key, x, y, z, yaw, opts) {
      const h = new Handle(this, key);
      h.corpse = !!(opts && opts.corpse);
      h.x = x || 0; h.y = y || 0; h.z = z || 0; h.yaw = yaw || 0;
      this.handles.add(h);
      this.ensureModel(key).then(m => h.attach(m)).catch(e => console.warn('spawn failed', key, e));
      return h;
    }
    // One-shot effect model: plays the first matching sequence once and frees itself.
    effect(key, x, y, z, yaw, seq, scale, maxLife) {
      const h = this.spawn(key, x, y, z, yaw);
      if (scale) h.setScale(scale);
      const life = { h, t: 0, max: maxLife || 3 };
      this.ensureModel(key).then(() => { const len = h.play(seq || 0, false); if (len > 0) life.max = Math.min(life.max, len); }).catch(() => {});
      this.effects.push(life);
      return h;
    }

    // ---- Camera ------------------------------------------------------------------------------
    setCameraMode(mode) {
      const c = this.cam;
      if (mode === 'lane') {
        Object.assign(c, { tx: 0, ty: this.height > this.width ? 3.6 : 4.5, dist: this.height > this.width ? 28 : 22, pitch: 60 * DEG, yaw: 0, minDist: 10, maxDist: 36, bounds: { x0: -8, x1: 8, y0: -4, y1: 20 } });
        c.fov = (this.height > this.width ? 55 : 42) * DEG;
      } else {
        Object.assign(c, { tx: 0, ty: 0, dist: 30, pitch: 56 * DEG, yaw: 0, minDist: 10, maxDist: 60, bounds: { x0: -18, x1: 18, y0: -18, y1: 18 } });
        c.fov = (this.height > this.width ? 60 : 45) * DEG;
      }
      this.updateCamera();
    }
    // Bunker view: a first-person eye inside the bunker, dragged to look all the way round (sky view otherwise).
    setBunkerView(on) {
      const c = this.cam;
      c.fp = !!on;
      if (c.fp) { c.fpYaw = 0; c.fpPitch = 10 * DEG; c.fpFov = (this.height > this.width ? 70 : 60) * DEG; }
      this.updateCamera();
    }
    // Is a world point in front of the camera (worldToScreen mirrors points behind it)?
    inFront(x, y, z) {
      const c = this.cam;
      if (!c.fp) return true;
      const cp = Math.cos(c.fpPitch);
      return x * Math.sin(c.fpYaw) * cp + y * Math.cos(c.fpYaw) * cp - ((z || 0) - BUNKER_EYE) * Math.sin(c.fpPitch) > 0.3;
    }
    updateCamera() {
      const c = this.cam, cam = this.scene.camera;
      if (c.fp) {
        const j = this.shake > 0.01 ? this.shake * 0.05 : 0;
        const eye = [(Math.random() - 0.5) * j, (Math.random() - 0.5) * j, BUNKER_EYE + (Math.random() - 0.5) * j];
        const cp = Math.cos(c.fpPitch), dir = [Math.sin(c.fpYaw) * cp, Math.cos(c.fpYaw) * cp, -Math.sin(c.fpPitch)];
        for (const sc of [this.scene, this.corpseScene]) {
          sc.camera.perspective(c.fpFov, this.canvas.width / this.canvas.height, 0.15, 400);
          sc.camera.moveToAndFace(eye, [eye[0] + dir[0], eye[1] + dir[1], eye[2] + dir[2]], [0, 0, 1]);
        }
        return;
      }
      c.tx = Math.min(c.bounds.x1, Math.max(c.bounds.x0, c.tx));
      c.ty = Math.min(c.bounds.y1, Math.max(c.bounds.y0, c.ty));
      c.dist = Math.min(c.maxDist, Math.max(c.minDist, c.dist));
      const cp = Math.cos(c.pitch), sp = Math.sin(c.pitch);
      // Screen shake nudges the look-at point (big hits and the bunker taking damage).
      const j = this.shake > 0.01 ? this.shake * 0.18 : 0, jx = (Math.random() - 0.5) * j, jy = (Math.random() - 0.5) * j;
      const loc = [c.tx + jx - c.dist * cp * Math.sin(c.yaw), c.ty + jy - c.dist * cp * Math.cos(c.yaw), c.dist * sp];
      for (const sc of [this.scene, this.corpseScene]) {
        sc.camera.perspective(c.fov, this.canvas.width / this.canvas.height, 0.5, 400);
        sc.camera.moveToAndFace(loc, [c.tx + jx, c.ty + jy, 0], [0, 0, 1]);
      }
    }
    zoomBy(factor, sx, sy) {
      const c = this.cam;
      if (c.fp) { c.fpFov = Math.min(85 * DEG, Math.max(30 * DEG, c.fpFov * factor)); this.updateCamera(); return; }
      const before = (sx !== undefined) ? this.screenToGround(sx, sy) : null;
      c.dist = Math.min(c.maxDist, Math.max(c.minDist, c.dist * factor));
      this.updateCamera();
      if (before) {   // keep the ground point under the cursor fixed
        const after = this.screenToGround(sx, sy);
        if (after) { c.tx += before[0] - after[0]; c.ty += before[1] - after[1]; this.updateCamera(); }
      }
    }
    panBy(dx, dy) {
      const c = this.cam;
      if (c.fp) {
        // Grab the world: drag right to look left, all the way round; up and down within limits.
        const k = c.fpFov / this.height;
        c.fpYaw = (c.fpYaw - dx * k) % (Math.PI * 2);
        c.fpPitch = Math.min(50 * DEG, Math.max(-12 * DEG, c.fpPitch - dy * k));
        this.updateCamera();
        return;
      }
      const k = c.dist / this.height * 1.6;
      const cy = Math.cos(c.yaw), sy = Math.sin(c.yaw);
      const mx = -dx * k, my = dy * k;
      c.tx += mx * cy - my * sy;
      c.ty += mx * sy + my * cy;
      this.updateCamera();
    }
    worldToScreen(x, y, z) {
      const cam = this.scene.camera;
      const out = cam.worldToScreen(this._w2s || (this._w2s = new Float32Array(2)), [x, y, z || 0], this.scene.viewport);
      return [out[0] / this.dpr, (this.canvas.height - out[1]) / this.dpr];
    }
    screenToGround(sx, sy) {
      const cam = this.scene.camera;
      const ray = cam.screenToWorldRay(this._ray || (this._ray = new Float32Array(6)), [sx * this.dpr, this.canvas.height - sy * this.dpr], this.scene.viewport);
      const az = ray[2], bz = ray[5];
      if (Math.abs(bz - az) < 1e-6) return null;
      const t = -az / (bz - az);
      if (t < 0 || t > 1) return null;
      return [ray[0] + (ray[3] - ray[0]) * t, ray[1] + (ray[4] - ray[1]) * t];
    }

    // ---- Ground ------------------------------------------------------------------------------------
    // A height grid (flat in the arena; the lane's Y-shaped chokepoint from terrain.js) textured with SC2's Char
    // ground on the flats and Char cliff rock on the faces (projected from the side, dark rock grading to a glowing base).
    initGround() {
      const gl = this.gl;
      this.gl.getExtension('OES_element_index_uint');
      const vs = 'attribute vec3 a_pos; attribute vec3 a_nrm; attribute float a_ao; uniform mat4 u_vp; varying vec3 v_pos; varying vec3 v_nrm; varying float v_ao;' +
        'void main(){ v_pos = a_pos; v_nrm = a_nrm; v_ao = a_ao; gl_Position = u_vp * vec4(a_pos, 1.0); }';
      const fs = 'precision mediump float; uniform sampler2D u_tex; uniform sampler2D u_cliff; uniform float u_hasCliff; uniform float u_h;' +
        ' uniform vec2 u_center; uniform vec2 u_scale; uniform float u_radius; uniform float u_creep;' +
        ' varying vec3 v_pos; varying vec3 v_nrm; varying float v_ao;' +
        'void main(){ vec3 n = normalize(v_nrm);' +
        ' vec3 g = texture2D(u_tex, v_pos.xy / 6.0).rgb;' +
        ' float v = 0.92 + 0.08 * sin(v_pos.x * 1.3 + 0.5) * sin(v_pos.y * 1.1);' +
        ' float cr = smoothstep(u_creep - 6.0, u_creep + 6.0, v_pos.y) * 0.45;' +
        ' g = mix(g * v, g * vec3(0.45, 0.2, 0.5) * 1.6, cr);' +
        ' float cliff = smoothstep(0.12, 0.38, 1.0 - n.z) * u_hasCliff;' +
        ' float vv = clamp(1.0 - v_pos.z / u_h, 0.03, 0.97) * 0.92;' +
        ' vec3 cx = texture2D(u_cliff, vec2(v_pos.y * 0.22, vv)).rgb, cy = texture2D(u_cliff, vec2(v_pos.x * 0.22, vv)).rgb;' +
        ' float wx = abs(n.x) / (abs(n.x) + abs(n.y) + 1e-4);' +
        ' vec3 c = mix(g, mix(cy, cx, wx) * 1.25, cliff);' +
        // Lit from the upper left; low ground at the foot of a cliff sits a little in shadow.
        ' float lit = 0.62 + 0.5 * max(dot(n, normalize(vec3(-0.45, -0.35, 0.82))), 0.0);' +
        ' c *= lit * (1.0 + 0.16 * smoothstep(u_h * 0.7, u_h, v_pos.z)) * v_ao;' +
        ' vec2 d2 = (v_pos.xy - u_center) * u_scale; float fog = smoothstep(u_radius * 0.6, u_radius, length(d2));' +
        ' c *= (1.0 - 0.92 * fog);' +
        ' gl_FragColor = vec4(c, 1.0); }';
      const compile = (type, src) => { const sh = gl.createShader(type); gl.shaderSource(sh, src); gl.compileShader(sh); if (!gl.getShaderParameter(sh, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(sh)); return sh; };
      const prog = gl.createProgram();
      gl.attachShader(prog, compile(gl.VERTEX_SHADER, vs)); gl.attachShader(prog, compile(gl.FRAGMENT_SHADER, fs));
      gl.linkProgram(prog);
      if (!gl.getProgramParameter(prog, gl.LINK_STATUS)) throw new Error(gl.getProgramInfoLog(prog));
      const U = name => gl.getUniformLocation(prog, name);
      this.ground = {
        prog, vbuf: gl.createBuffer(), ibuf: gl.createBuffer(), count: 0,
        a_pos: gl.getAttribLocation(prog, 'a_pos'), a_nrm: gl.getAttribLocation(prog, 'a_nrm'), a_ao: gl.getAttribLocation(prog, 'a_ao'),
        u_vp: U('u_vp'), u_tex: U('u_tex'), u_cliff: U('u_cliff'), u_hasCliff: U('u_hasCliff'), u_h: U('u_h'),
        u_center: U('u_center'), u_scale: U('u_scale'), u_radius: U('u_radius'), u_creep: U('u_creep'),
        center: [0, 8], scale: [1, 0.7], radius: 30, creep: 24, cliffs: false,
      };
      this.setGroundShape('lane');
      const repeat = t => { gl.bindTexture(gl.TEXTURE_2D, t); gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.REPEAT); gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.REPEAT); };
      this.viewer.load(BS.GROUND_TEXTURE, this.solver).then(tex => {
        if (!tex || !tex.webglResource) return;
        repeat(tex.webglResource);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
        gl.bindTexture(gl.TEXTURE_2D, null);
        this.groundTex = tex.webglResource;
      }).catch(e => console.warn('ground texture failed', e));
      const img = new Image();
      img.onload = () => {
        const t = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_2D, t);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, img);
        gl.generateMipmap(gl.TEXTURE_2D);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR_MIPMAP_LINEAR);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.REPEAT);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
        gl.bindTexture(gl.TEXTURE_2D, null);
        this.cliffTex = t;
      };
      img.src = '/fxtex/' + BS.CLIFF_TEXTURE;
    }
    setGroundShape(mode) {
      const gl = this.gl, g = this.ground;
      BS.Terrain.setMode(mode);
      let x0, x1, y0, y1;
      if (mode === 'lane') { x0 = -24; x1 = 24; y0 = -18; y1 = 50; g.center = [0, 9]; g.scale = [1.0, 0.62]; g.radius = 34; g.creep = 28; }
      else { x0 = -50; x1 = 50; y0 = -50; y1 = 50; g.center = [0, 0]; g.scale = [1, 1]; g.radius = 40; g.creep = 1000; }
      g.cliffs = BS.Terrain.active;
      // Fine grid where the cliffs are, a single quad otherwise.
      const step = g.cliffs ? 0.3 : (x1 - x0);
      const nx = Math.round((x1 - x0) / step) + 1, ny = Math.round((y1 - y0) / step) + 1;
      const hgt = new Float32Array(nx * ny);
      for (let j = 0; j < ny; j++) for (let i = 0; i < nx; i++) hgt[j * nx + i] = BS.Terrain.height(x0 + i * step, y0 + j * step);
      const v = new Float32Array(nx * ny * 7);
      for (let j = 0; j < ny; j++) for (let i = 0; i < nx; i++) {
        const k = j * nx + i, hx = (hgt[j * nx + Math.min(i + 1, nx - 1)] - hgt[j * nx + Math.max(i - 1, 0)]) / (2 * step),
          hy = (hgt[Math.min(j + 1, ny - 1) * nx + i] - hgt[Math.max(j - 1, 0) * nx + i]) / (2 * step);
        const l = Math.hypot(hx, hy, 1);
        // Low ground at the foot of a cliff sits in shadow, which outlines the channel.
        const sdf = g.cliffs ? BS.Terrain.lowSdf(x0 + i * step, y0 + j * step) : -9;
        const ao = sdf < 0 ? 1 - 0.38 * Math.max(0, 1 + sdf / 1.6) : 1;
        v.set([x0 + i * step, y0 + j * step, hgt[k], -hx / l, -hy / l, 1 / l, ao], k * 7);
      }
      const big = nx * ny > 65535, idx = big ? new Uint32Array((nx - 1) * (ny - 1) * 6) : new Uint16Array((nx - 1) * (ny - 1) * 6);
      let o = 0;
      for (let j = 0; j < ny - 1; j++) for (let i = 0; i < nx - 1; i++) {
        const a = j * nx + i, b = a + 1, c = a + nx, d = c + 1;
        idx[o++] = a; idx[o++] = b; idx[o++] = d; idx[o++] = a; idx[o++] = d; idx[o++] = c;
      }
      gl.bindBuffer(gl.ARRAY_BUFFER, g.vbuf); gl.bufferData(gl.ARRAY_BUFFER, v, gl.STATIC_DRAW);
      gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, g.ibuf); gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, idx, gl.STATIC_DRAW);
      gl.bindBuffer(gl.ARRAY_BUFFER, null); gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, null);
      g.count = idx.length; g.indexType = big ? gl.UNSIGNED_INT : gl.UNSIGNED_SHORT;
    }
    drawGround() {
      const gl = this.gl, g = this.ground;
      if (!g || !this.groundTex || this.noGround) return;
      if (this.maxAttribs === undefined) this.maxAttribs = gl.getParameter(gl.MAX_VERTEX_ATTRIBS);
      // The viewer leaves attribute arrays enabled (some pointing at buffers it has since replaced);
      // isolate our pass completely and let the viewer re-enable what it needs afterwards.
      for (let i = 0; i < this.maxAttribs; i++) gl.disableVertexAttribArray(i);
      gl.useProgram(g.prog);
      gl.bindBuffer(gl.ARRAY_BUFFER, g.vbuf);
      gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, g.ibuf);
      gl.enableVertexAttribArray(g.a_pos); gl.vertexAttribPointer(g.a_pos, 3, gl.FLOAT, false, 28, 0);
      gl.enableVertexAttribArray(g.a_nrm); gl.vertexAttribPointer(g.a_nrm, 3, gl.FLOAT, false, 28, 12);
      gl.enableVertexAttribArray(g.a_ao); gl.vertexAttribPointer(g.a_ao, 1, gl.FLOAT, false, 28, 24);
      gl.uniformMatrix4fv(g.u_vp, false, this.scene.camera.viewProjectionMatrix);
      gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, this.groundTex); gl.uniform1i(g.u_tex, 0);
      gl.activeTexture(gl.TEXTURE1); gl.bindTexture(gl.TEXTURE_2D, this.cliffTex || this.groundTex); gl.uniform1i(g.u_cliff, 1);
      gl.uniform1f(g.u_hasCliff, g.cliffs && this.cliffTex ? 1 : 0); gl.uniform1f(g.u_h, BS.Terrain.H);
      gl.uniform2f(g.u_center, g.center[0], g.center[1]); gl.uniform2f(g.u_scale, g.scale[0], g.scale[1]);
      gl.uniform1f(g.u_radius, g.radius); gl.uniform1f(g.u_creep, g.creep);
      gl.disable(gl.BLEND); gl.enable(gl.DEPTH_TEST); gl.depthMask(true); gl.disable(gl.CULL_FACE);
      gl.drawElements(gl.TRIANGLES, g.count, g.indexType, 0);
      gl.disableVertexAttribArray(g.a_pos); gl.disableVertexAttribArray(g.a_nrm); gl.disableVertexAttribArray(g.a_ao);
      gl.bindBuffer(gl.ARRAY_BUFFER, null); gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, null);
      gl.activeTexture(gl.TEXTURE1); gl.bindTexture(gl.TEXTURE_2D, null);
      gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, null);
      gl.useProgram(null);
      this.viewer.webgl.currentShader = null;
    }

    // ---- Frame --------------------------------------------------------------------------------
    frame(dt) {
      this.time += dt;
      for (let i = this.effects.length - 1; i >= 0; i--) {
        const e = this.effects[i]; e.t += dt;
        if (e.t >= e.max) { e.h.release(); this.effects.splice(i, 1); }
      }
      const v = this.viewer;
      // Skeletal animation is the expensive part on phones: there it is advanced every other frame.
      this.animAcc = (this.animAcc || 0) + dt; this.animFrame = (this.animFrame || 0) + 1;
      if (this.animEvery <= 1 || this.animFrame % (this.animEvery || 1) === 0 || dt === 0) { v.update(this.animAcc * 1000); this.animAcc = 0; }
      if (this.fx) this.fx.update(dt);
      if (this.shake > 0.01) this.updateCamera();
      v.startFrame();
      this.scene.startFrame();
      this.drawGround();
      this.corpseScene.startFrame();
      this.corpseScene.renderOpaque();
      this.corpseScene.renderTranslucent();
      this.gl.clear(this.gl.DEPTH_BUFFER_BIT);
      if (this.ground && this.ground.cliffs) { this.gl.colorMask(false, false, false, false); this.drawGround(); this.gl.colorMask(true, true, true, true); }
      this.scene.startFrame();
      this.scene.renderOpaque();
      this.scene.renderTranslucent();
      if (this.fx) this.fx.draw(this.scene.camera);
    }
  }

  BS.Renderer = Renderer;
  BS.quatZ = quatZ;
})(window.BS);
