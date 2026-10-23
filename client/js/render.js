// Rendering: wraps mdx-m3-viewer (WebGL) for StarCraft II M3 models, adds a textured ground
// plane, a controllable perspective camera (pinch/drag/wheel), instance pooling and a 2D overlay.
window.BS = window.BS || {};
(function (BS) {
  'use strict';

  const MV = window.ModelViewer;
  const DEG = Math.PI / 180;

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
    release() {
      if (this.released) return;
      this.released = true;
      if (this.inst) { if (this.corpse) this.inst.vertexColor[0] = this.inst.vertexColor[1] = this.inst.vertexColor[2] = 1; this.inst.bsHide = null; this.r.releaseInstance(this.model, this.inst, this.corpse); }
      this.inst = null;
    }
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
        Object.assign(c, { tx: 0, ty: this.height > this.width ? 2.4 : 4.5, dist: 22, pitch: 60 * DEG, yaw: 0, minDist: 10, maxDist: 36, bounds: { x0: -8, x1: 8, y0: -4, y1: 20 } });
        c.fov = (this.height > this.width ? 55 : 42) * DEG;
      } else {
        Object.assign(c, { tx: 0, ty: 0, dist: 30, pitch: 56 * DEG, yaw: 0, minDist: 10, maxDist: 60, bounds: { x0: -18, x1: 18, y0: -18, y1: 18 } });
        c.fov = (this.height > this.width ? 60 : 45) * DEG;
      }
      this.updateCamera();
    }
    updateCamera() {
      const c = this.cam, cam = this.scene.camera;
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

    // ---- Ground -------------------------------------------------------------------------------
    initGround() {
      const gl = this.gl;
      const vs = 'attribute vec3 a_pos; attribute vec2 a_uv; uniform mat4 u_vp; varying vec2 v_uv; varying vec3 v_pos;' +
        'void main(){ v_uv = a_uv; v_pos = a_pos; gl_Position = u_vp * vec4(a_pos, 1.0); }';
      const fs = 'precision mediump float; uniform sampler2D u_tex; uniform vec2 u_center; uniform vec2 u_scale; uniform float u_radius; uniform float u_creep;' +
        'varying vec2 v_uv; varying vec3 v_pos;' +
        'void main(){ vec3 c = texture2D(u_tex, v_uv).rgb;' +
        ' vec2 d2 = (v_pos.xy - u_center) * u_scale; float d = length(d2);' +
        ' float fog = smoothstep(u_radius * 0.6, u_radius, d);' +
        ' float v = 0.92 + 0.08 * sin(v_pos.x * 1.3 + 0.5) * sin(v_pos.y * 1.1);' +
        ' vec3 creep = vec3(0.45, 0.2, 0.5);' +
        ' float cr = smoothstep(u_creep - 6.0, u_creep + 6.0, v_pos.y) * 0.45;' +
        ' c = mix(c * v, c * creep * 1.6, cr);' +
        ' c *= (1.0 - 0.92 * fog);' +
        ' gl_FragColor = vec4(c, 1.0); }';
      const compile = (type, src) => { const s = gl.createShader(type); gl.shaderSource(s, src); gl.compileShader(s); if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(s)); return s; };
      const prog = gl.createProgram();
      gl.attachShader(prog, compile(gl.VERTEX_SHADER, vs)); gl.attachShader(prog, compile(gl.FRAGMENT_SHADER, fs));
      gl.linkProgram(prog);
      if (!gl.getProgramParameter(prog, gl.LINK_STATUS)) throw new Error(gl.getProgramInfoLog(prog));
      this.ground = {
        prog, buf: gl.createBuffer(),
        a_pos: gl.getAttribLocation(prog, 'a_pos'), a_uv: gl.getAttribLocation(prog, 'a_uv'),
        u_vp: gl.getUniformLocation(prog, 'u_vp'), u_tex: gl.getUniformLocation(prog, 'u_tex'),
        u_center: gl.getUniformLocation(prog, 'u_center'), u_scale: gl.getUniformLocation(prog, 'u_scale'),
        u_radius: gl.getUniformLocation(prog, 'u_radius'), u_creep: gl.getUniformLocation(prog, 'u_creep'),
        count: 0, center: [0, 8], scale: [1, 0.7], radius: 30, creep: 24,
      };
      this.setGroundShape('lane');
      this.viewer.load(BS.GROUND_TEXTURE, this.solver).then(tex => {
        if (!tex || !tex.webglResource) return;
        gl.bindTexture(gl.TEXTURE_2D, tex.webglResource);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.REPEAT);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.REPEAT);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
        gl.bindTexture(gl.TEXTURE_2D, null);
        this.groundTex = tex.webglResource;
      }).catch(e => console.warn('ground texture failed', e));
    }
    setGroundShape(mode) {
      const gl = this.gl, g = this.ground;
      const tile = 6;
      let x0, x1, y0, y1;
      if (mode === 'lane') { x0 = -22; x1 = 22; y0 = -16; y1 = 48; g.center = [0, 9]; g.scale = [1.0, 0.62]; g.radius = 32; g.creep = 26; }
      else { x0 = -50; x1 = 50; y0 = -50; y1 = 50; g.center = [0, 0]; g.scale = [1, 1]; g.radius = 40; g.creep = 1000; }
      const v = new Float32Array([
        x0, y0, 0, x0 / tile, y0 / tile, x1, y0, 0, x1 / tile, y0 / tile, x1, y1, 0, x1 / tile, y1 / tile,
        x0, y0, 0, x0 / tile, y0 / tile, x1, y1, 0, x1 / tile, y1 / tile, x0, y1, 0, x0 / tile, y1 / tile,
      ]);
      gl.bindBuffer(gl.ARRAY_BUFFER, g.buf);
      gl.bufferData(gl.ARRAY_BUFFER, v, gl.STATIC_DRAW);
      g.count = 6;
    }
    drawGround() {
      const gl = this.gl, g = this.ground;
      if (!g || !this.groundTex || this.noGround) return;
      if (this.maxAttribs === undefined) this.maxAttribs = gl.getParameter(gl.MAX_VERTEX_ATTRIBS);
      // The viewer leaves attribute arrays enabled (some pointing at buffers it has since replaced);
      // isolate our pass completely and let the viewer re-enable what it needs afterwards.
      for (let i = 0; i < this.maxAttribs; i++) gl.disableVertexAttribArray(i);
      gl.useProgram(g.prog);
      gl.bindBuffer(gl.ARRAY_BUFFER, g.buf);
      gl.enableVertexAttribArray(g.a_pos); gl.vertexAttribPointer(g.a_pos, 3, gl.FLOAT, false, 20, 0);
      gl.enableVertexAttribArray(g.a_uv); gl.vertexAttribPointer(g.a_uv, 2, gl.FLOAT, false, 20, 12);
      gl.uniformMatrix4fv(g.u_vp, false, this.scene.camera.viewProjectionMatrix);
      gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, this.groundTex); gl.uniform1i(g.u_tex, 0);
      gl.uniform2f(g.u_center, g.center[0], g.center[1]); gl.uniform2f(g.u_scale, g.scale[0], g.scale[1]);
      gl.uniform1f(g.u_radius, g.radius); gl.uniform1f(g.u_creep, g.creep);
      gl.disable(gl.BLEND); gl.enable(gl.DEPTH_TEST); gl.depthMask(true); gl.disable(gl.CULL_FACE);
      gl.drawArrays(gl.TRIANGLES, 0, g.count);
      // Let the viewer rebind its own shader/texture/attribute state.
      gl.disableVertexAttribArray(g.a_pos); gl.disableVertexAttribArray(g.a_uv);
      gl.bindBuffer(gl.ARRAY_BUFFER, null);
      gl.bindTexture(gl.TEXTURE_2D, null);
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
      this.scene.startFrame();
      this.scene.renderOpaque();
      this.scene.renderTranslucent();
      if (this.fx) this.fx.draw(this.scene.camera);
    }
  }

  BS.Renderer = Renderer;
  BS.quatZ = quatZ;
})(window.BS);
