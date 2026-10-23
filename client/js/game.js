// Simulation: bunker, defenders, zerg waves, economy, upgrades and abilities.
// A port of the Unreal build's simulation (Unreal/BunkerSurvivors/Source/BunkerSurvivors/BSGame.cpp); keep the two in step.
window.BS = window.BS || {};
(function (BS) {
  'use strict';

  const rnd = (a, b) => a + Math.random() * (b - a);
  const pick = arr => arr[Math.floor(Math.random() * arr.length)];
  const clamp = (v, a, b) => v < a ? a : v > b ? b : v;
  const dist = (ax, ay, bx, by) => Math.hypot(ax - bx, ay - by);
  const angDelta = (a, b) => { let d = (b - a) % (Math.PI * 2); if (d > Math.PI) d -= Math.PI * 2; if (d < -Math.PI) d += Math.PI * 2; return d; };
  const T = () => BS.TERRAN;

  // The SC2 bunker's four firing slits (Attach_Weapon, 02, 04, 06) in model space.
  const BUNKER_SLITS = [[-0.60, -0.01], [-0.04, 0.58], [0.55, 0.03], [0.01, -0.56]];
  const SLIT_Z = 0.6;

  class Game {
    constructor(renderer, opts) {
      this.r = renderer; this.fx = renderer.fx;
      this.mode = opts.mode || 'lane';
      this.hard = this.mode === 'arena';
      this.time = 0; this.acc = 0; this.step = 1 / 60;
      this.paused = false; this.over = false; this.started = false;
      this.minerals = 150; this.income = 2; this.kills = 0; this.score = 0;
      this.wave = 0; this.waveTimer = 8; this.waveLen = 30; this.spawnQueue = []; this.spawnTimer = 0;
      this.enemies = []; this.corpses = []; this.projectiles = []; this.floaters = []; this.walkers = []; this.drops = []; this.flashes = [];
      this.focus = null; this.focusUntil = 0; this.shake = 0;
      this.toasts = []; this.waveBanner = null; this.events = [];
      this.bunker = { x: 0, y: 0, yaw: Math.PI / 2, hp: T().bunker.hp, maxHp: T().bunker.hp, armor: T().bunker.armor, slots: T().bunker.slots, units: [], handle: null, dead: false, flash: 0 };
      this.tanks = []; this.scvs = []; this.banshees = [];
      this.up = { stim: 0, weapons: 0, vehicle: 0, neosteel: 0, plating: 0, hisec: 0, medivac: 0 };
      this.ab = { stimUntil: 0, stimCd: 0, strikeCd: 0, repairCd: 0, yamatoCd: 0 };
      this.station = null;
      this.layout();
      this.bunker.handle = this.r.spawn('bunker', 0, 0, 0, this.bunker.yaw).setTeam(1);
      this.bunker.handle.play(/^stand$/i, true);
      this.addScv(true);
    }

    layout() {
      if (this.mode === 'lane') {
        this.tankSlots = [[-3.4, -0.4], [3.4, -0.4], [-5.4, -2.8], [5.4, -2.8]];
        this.scvIdle = [[-2.6, -2.9], [2.6, -2.9], [-4.4, -2.4], [4.4, -2.4]];
        this.tankMax = T().tank.maxLane;
      } else {
        this.tankSlots = [[-3.0, -2.6], [3.0, -2.6], [-3.0, 2.6], [3.0, 2.6]];
        this.scvIdle = [[-2.6, -2.6], [2.6, 2.6], [-2.6, 2.6], [2.6, -2.6]];
        this.tankMax = T().tank.maxArena;
      }
    }

    // ---- Opening and reinforcements -------------------------------------------------------------
    // Three marines double-time it into the bunker while the first wave gathers.
    beginOpening() {
      for (let i = 0; i < 3; i++) this.addWalker('marine', -3.3 + i * 0.25, 3.0 + i * 0.9, 0.5 + i * 0.45);
    }
    addWalker(type, x, y, delay) {
      const w = { def: BS.INFANTRY[type], type, x, y, delay: delay || 0, walking: false, handle: this.r.spawn(type, x, y, 0, -Math.PI / 4).setTeam(1) };
      w.handle.play(/^stand$/i, true);
      this.walkers.push(w);
    }
    pendingInfantry() { return this.walkers.length + this.drops.reduce((n, d) => n + d.cargo.length, 0); }

    addInfantry(type, free) {
      const d = BS.INFANTRY[type];
      if (this.bunker.units.length + this.pendingInfantry() >= this.bunker.slots) return this.toast('Bunker is full');
      if (!free) { if (this.minerals < d.cost) return this.toast('Not enough minerals'); this.minerals -= d.cost; }
      // Ride along with a medivac that is still inbound, or call a new one.
      const ride = this.drops.find(dr => dr.state === 'incoming' && dr.cargo.length < 4);
      if (ride) { ride.cargo.push(type); return true; }
      const lx = (Math.random() < 0.5 ? 1 : -1) * rnd(2.2, 2.7), ly = rnd(2.6, 3.2);
      const dr = { state: 'incoming', t: 0, lx, ly, cargo: [type], mx: lx, my: this.mode === 'lane' ? -18 : -30, mz: 6 };
      dr.handle = this.r.spawn('medivac', dr.mx, dr.my, dr.mz, Math.PI / 2).setTeam(1);
      dr.handle.play(/^walk|^fly/i, true);
      this.drops.push(dr);
      this.toast('Reinforcements inbound');
      return true;
    }
    updateReinforcements(dt) {
      for (let i = this.drops.length - 1; i >= 0; i--) {
        const d = this.drops[i];
        d.t += dt;
        if (d.state === 'incoming') {
          const gx = d.lx, gy = d.ly, gz = 1.6, dx = gx - d.mx, dy = gy - d.my, dz = gz - d.mz, dd = Math.hypot(dx, dy, dz);
          if (dd > 0.25 && d.t < 8) { const s = Math.min(dd, 11 * dt) / dd; d.mx += dx * s; d.my += dy * s; d.mz += dz * s; }
          else { d.state = 'unloading'; d.t = 0; d.handle.play(/^stand work start/i, false); BS.audio.play('medivacUnload', { vol: 0.5 }); }
          d.handle.setPos(d.mx, d.my, d.mz).setYaw(Math.PI / 2);
        } else if (d.state === 'unloading') {
          if (d.t > 0.45 && d.cargo.length) { d.t = 0.15; this.addWalker(d.cargo.shift(), d.lx + rnd(-0.3, 0.3), d.ly + rnd(-0.2, 0.2), 0); }
          else if (!d.cargo.length && d.t > 0.5) { d.state = 'leaving'; d.t = 0; d.handle.play(/^stand work end/i, false); }
        } else {
          d.my -= 11 * dt; d.mz += 2.5 * dt;
          if (d.t > 0.4 && !d.flying) { d.flying = true; d.handle.play(/^walk|^fly/i, true); }
          d.handle.setPos(d.mx, d.my, d.mz).setYaw(-Math.PI / 2);
          if (d.t > 2.8) { d.handle.release(); this.drops.splice(i, 1); }
        }
      }
      for (let i = this.walkers.length - 1; i >= 0; i--) {
        const w = this.walkers[i];
        if (w.delay > 0) { w.delay -= dt; continue; }
        // Head for the nearest side hatch.
        const hx = w.x < 0 ? -T().bunker.halfSize - 0.1 : T().bunker.halfSize + 0.1, hy = 0.4;
        const dx = hx - w.x, dy = hy - w.y, dd = Math.hypot(dx, dy);
        if (dd < 0.3 || this.bunker.dead) {
          if (!this.bunker.dead && this.bunker.units.length < this.bunker.slots) {
            this.bunker.units.push({ type: w.type, def: w.def, hp: w.def.hp, maxHp: w.def.hp, cd: Math.random() * 0.3 });
            this.bunker.handle.play(/^stand work/i, true);
            this.floater(0, 0, 3, '+' + w.def.name, '#7fd8ff');
          }
          w.handle.release(); this.walkers.splice(i, 1); continue;
        }
        const s = Math.min(dd, 3.15 * dt);
        w.x += dx / dd * s; w.y += dy / dd * s;
        if (!w.walking) { w.walking = true; w.handle.play(/^walk/i, true, 1.2); }
        w.handle.setPos(w.x, w.y, 0).setYaw(Math.atan2(dy, dx));
      }
    }

    // ---- Purchases --------------------------------------------------------------------------------
    addScv(free) {
      const d = T().scv;
      if (this.scvs.length >= d.max) return this.toast('SCV limit reached');
      if (!free) { if (this.minerals < d.cost) return this.toast('Not enough minerals'); this.minerals -= d.cost; }
      const idle = this.scvIdle[this.scvs.length % this.scvIdle.length];
      const s = { x: idle[0], y: idle[1] - 6, yaw: Math.PI / 2, idle, hp: d.hp, maxHp: d.hp, radius: d.radius, anim: '', dead: false, flash: 0 };
      s.handle = this.r.spawn('scv', s.x, s.y, 0, s.yaw).setTeam(1);
      this.scvs.push(s);
      return true;
    }
    tankCost() { return T().tank.cost + T().tank.costStep * Math.max(0, this.tanks.length - 1); }
    dropTank() {
      const d = T().tank;
      if (this.tanks.length >= this.tankMax) return this.toast('No room for more tanks');
      const cost = this.tankCost();
      if (this.minerals < cost) return this.toast('Not enough minerals');
      this.minerals -= cost;
      const slot = this.tankSlots[this.tanks.length];
      const t = { x: slot[0], y: slot[1], hp: d.hp, maxHp: d.hp, armor: d.armor, cd: 0, state: 'incoming', t: 0, radius: d.radius, handle: null, dead: false, flash: 0,
        mx: slot[0], my: this.mode === 'lane' ? -16 : -28, mz: 5.5 };
      t.medivac = this.r.spawn('medivac', t.mx, t.my, t.mz, Math.PI / 2).setTeam(1);
      t.medivac.play(/^walk|^fly/i, true);
      this.tanks.push(t);
      this.toast('Medivac inbound with a siege tank');
      return true;
    }
    upgradeLevel(id) { return this.up[id] || 0; }
    upgradeCost(id) { const u = BS.UPGRADES[id], l = this.upgradeLevel(id); return u.costs[Math.min(l, u.costs.length - 1)]; }
    upgradeAvailable(id) {
      if (id === 'medivac' && !this.up.stim) return false;
      if (id === 'vehicle' && !this.tanks.length) return false;
      return this.upgradeLevel(id) < BS.UPGRADES[id].costs.length;
    }
    buyUpgrade(id) {
      if (!this.upgradeAvailable(id)) return false;
      const cost = this.upgradeCost(id);
      if (this.minerals < cost) return this.toast('Not enough minerals');
      this.minerals -= cost;
      const up = this.up; up[id]++;
      switch (id) {
        case 'stim': this.toast('Stimpack researched - hit STIM'); break;
        case 'weapons': this.toast('Infantry Weapons level ' + up.weapons); break;
        case 'vehicle': this.toast('Vehicle Weapons level ' + up.vehicle); break;
        case 'neosteel': this.bunker.slots = T().bunker.slotsUpgraded; this.toast('Bunker expanded to 6 slots'); break;
        case 'plating': this.bunker.maxHp += 150; this.bunker.hp += 150; if (up.plating <= 3) this.bunker.armor += 1; this.toast('Building Armor level ' + up.plating); break;
        case 'hisec': this.toast('Hi-Sec Auto Tracking online'); break;
        case 'medivac': this.toast('Medivac on station'); break;
      }
      return true;
    }

    // ---- Abilities ----------------------------------------------------------------------------------
    callAirStrike() {
      const d = T().banshee;
      if (this.ab.strikeCd > 0) return this.toast('Air support is refuelling');
      if (this.minerals < d.cost) return this.toast('Not enough minerals');
      this.minerals -= d.cost; this.ab.strikeCd = d.cooldown;
      const lane = this.mode === 'lane';
      for (let i = 0; i < 2; i++) {
        const b = { x: lane ? (i ? 2.2 : -2.2) : (i ? 3 : -3), y: lane ? -14 : -34, z: d.fly, yaw: Math.PI / 2, cd: 0.3 + i * 0.25, back: false };
        b.handle = this.r.spawn('banshee', b.x, b.y, b.z, b.yaw).setTeam(1);
        b.handle.play(/^walk|^fly/i, true);
        this.banshees.push(b);
      }
      this.toast('Banshees inbound');
      return true;
    }
    useStim() {
      if (!this.up.stim) return this.toast('Research Stimpack first');
      if (this.ab.stimCd > 0) return false;
      let any = false;
      for (const u of this.bunker.units) if (u.hp > u.def.stimHp + 1) { u.hp -= u.def.stimHp; any = true; }
      if (!any) return this.toast('Troops too wounded to stim');
      this.ab.stimUntil = this.time + T().stim.duration; this.ab.stimCd = T().stim.cooldown;
      BS.audio.play('stim', { vol: 0.9 });
      this.floater(0, 0, 3, 'STIM!', '#ffd34d');
      return true;
    }
    emergencyRepair() {
      const b = this.bunker, R = T().repair;
      if (this.ab.repairCd > 0) return false;
      if (b.hp >= b.maxHp) return this.toast('Bunker is intact');
      if (this.minerals < R.cost) return this.toast('Not enough minerals');
      this.minerals -= R.cost; this.ab.repairCd = R.cooldown;
      b.hp = Math.min(b.maxHp, b.hp + R.amount);
      this.floater(0, 0, 3, '+' + R.amount, '#7fe0ff');
      BS.audio.play('scvRepair');
      return true;
    }
    useYamato() {
      const Y = T().yamato;
      if (this.ab.yamatoCd > 0) return this.toast('Yamato cannon recharging');
      if (this.minerals < Y.cost) return this.toast('Not enough minerals');
      // A battlecruiser in orbit fires on the densest cluster of zerg.
      let pickE = null, best = 0;
      for (const e of this.enemies) { if (e.hp <= 0) continue; const n = this.enemiesInRange(e.x, e.y, Y.radius).length; if (n > best) { best = n; pickE = e; } }
      if (!pickE) return this.toast('No targets');
      this.minerals -= Y.cost; this.ab.yamatoCd = Y.cooldown;
      const p = { kind: 'yamato', friendly: true, fx: pickE.x * 0.5, fy: pickE.y - 16, fz: 14, tx: pickE.x, ty: pickE.y, tz: 0.6, t: 0, dur: 0.55, dmg: Y.dmg, target: pickE };
      this.projectileModel(p, 'projYamato', 'yamatoMissile');
      this.projectiles.push(p);
      BS.audio.play('tankFire', { vol: 0.8 });
      this.toast('Yamato cannon fired');
      return true;
    }
    yamatoImpact(x, y) {
      const Y = T().yamato;
      const hit = this.enemiesInRange(x, y, Y.radius);
      if (hit.length) this.hurtEnemy(hit[0], Y.dmg - Y.splash);
      for (const v of hit) if (v.hp > 0) this.hurtEnemy(v, Y.splash);
      this.fx.play('yamatoImpact', x, y, 0.3, 0, 1.2);
      BS.audio.play('tankHit', { vol: 0.9 }); BS.audio.play('explosion', { vol: 0.7 });
      this.shake = 1;
    }

    // ---- Waves ----------------------------------------------------------------------------------------
    startWave() {
      this.wave++;
      const w = this.wave;
      let budget = (20 + 10 * w + 0.45 * w * w) * (this.hard ? 1.15 : 1);
      const types = Object.entries(BS.ZERG).filter(([, d]) => d.budget > 0 && w >= Math.ceil(d.unlock * (this.hard ? 0.85 : 1)));
      const list = [];
      for (let guard = 0; budget > 0 && guard < 400; guard++) {
        const total = types.reduce((s, [, d]) => s + (d.budget <= budget ? d.weight : 0), 0);
        if (total <= 0) break;
        let roll = Math.random() * total;
        for (const [k, d] of types) { if (d.budget > budget) continue; roll -= d.weight; if (roll <= 0) { list.push(k); budget -= d.budget; break; } }
      }
      if (w % 5 === 0) list.push(w >= 10 ? 'ultralisk' : 'roach', 'roach');
      if (this.hard && w % 10 === 0) list.push('ultralisk', 'ultralisk');
      for (let i = list.length - 1; i > 0; i--) { const j = Math.floor(Math.random() * (i + 1)); [list[i], list[j]] = [list[j], list[i]]; }
      // On top of the budget: a zergling swarm that snowballs fast (4 at wave 1, ~50 by wave 8, ~150 by wave 15).
      const swarm = Math.round(2 + 1.2 * w + 0.6 * w * w);
      // Lane packs pick an approach: mostly true north, the rest sweeping in from the north-east and north-west.
      const packAng = flank => this.mode === 'lane' ? (Math.random() < flank ? (Math.random() < 0.5 ? 1 : 2) : 0) : Math.random() * Math.PI * 2;
      const spread = this.waveLen - 8;
      for (const s of this.spawnQueue) s.t = 0;          // anything held back by the alive cap comes first
      const packs = [];
      while (list.length) packs.push(list.splice(0, Math.min(list.length, 2 + Math.floor(Math.random() * 4))));
      packs.forEach((pk, g) => { const t = 0.5 + spread * (g + rnd(0, 0.8)) / packs.length, ang = packAng(0.2); for (const k of pk) this.spawnQueue.push({ t, type: k, ang }); });
      for (let left = swarm; left > 0;) {
        const n = Math.min(left, 6 + Math.floor(Math.random() * 11)), t = 0.5 + rnd(0, spread), ang = packAng(0.55);
        for (let k = 0; k < n; k++) this.spawnQueue.push({ t: t + k * 0.04, type: 'zergling', ang });
        left -= n;
      }
      this.spawnQueue.sort((a, b) => a.t - b.t);
      this.spawnTimer = 0; this.waveTimer = this.waveLen;
      this.minerals += 20 + 8 * w;
      this.waveBanner = { text: 'WAVE ' + w + (w % 5 === 0 ? '  //  HEAVY ASSAULT' : ''), t: this.time };
    }
    maxAlive() { return (this.hard ? 200 : 160) * (BS.perfScale || 1); }
    spawnEnemy(type, ang, at) {
      const d = BS.ZERG[type];
      let x, y;
      if (at) { x = at[0] + rnd(-0.6, 0.6); y = at[1] + rnd(-0.6, 0.6); }
      else if (this.mode === 'lane') {
        if (ang === 1 || ang === 2) { x = (ang === 1 ? 1 : -1) * rnd(10, 13); y = rnd(17, 23); }
        else { x = rnd(-4.6, 4.6); y = rnd(27, 31); }
      } else { const rr = rnd(25, 28); x = Math.cos(ang + rnd(-0.35, 0.35)) * rr; y = Math.sin(ang + rnd(-0.35, 0.35)) * rr; }
      let model = d.model, death = d.death;
      if (d.variants && Math.random() < 0.45) { const v = pick(d.variants); model = v; if (BS.EXTRA_MODELS[v + 'Death']) death = v + 'Death'; }
      const z = d.flying ? d.fly : 0;
      const e = { type, def: d, x, y, z, yaw: Math.atan2(-y, -x), hp: 0, maxHp: 0, radius: d.radius, life: d.life || 0, cd: rnd(0, d.cd), retarget: 0,
        target: null, hitAt: -1, pendingHit: false, slowUntil: 0, burrowed: false, burrowT: 0, attacking: false, anim: '', flash: 0, sx: 0, sy: 0, model, death };
      e.hp = e.maxHp = d.hp * (1 + (this.hard ? 0.025 : 0.02) * Math.max(0, this.wave - 10));   // late waves get tougher
      e.handle = this.r.spawn(model, x, y, z, e.yaw).setTeam(3);
      if (d.scale) e.handle.setScale(d.scale);
      this.playAnim(e, 'walk');
      this.enemies.push(e);
      return e;
    }
    playAnim(e, kind) {
      if (e.anim === kind) return;
      e.anim = kind;
      const d = e.def, h = e.handle;
      if (kind === 'walk') {
        const sp = e.slowUntil > this.time ? d.speed * 0.5 : d.speed;
        h.play(/^walk$|^walk 01$|^fly/i, true, clamp(sp / 2.8, 0.6, 2.2));
      } else if (kind === 'attack') {
        const len = h.play(d.attackSeq || /^attack/i, true, 1);
        if (len > 0) h.setRate(len / this.enemyCd(e));
      } else if (kind === 'stand') h.play(/^stand$/i, true);
      else if (kind === 'burrow') h.play(/^burrow$/i, false);
    }
    enemyCd(e) { return e.def.cd / (this.hard ? 1.05 : 1); }

    // ---- Targets -------------------------------------------------------------------------------------
    targets() {
      const list = [];
      if (!this.bunker.dead) list.push({ kind: 'bunker', obj: this.bunker, x: 0, y: 0, radius: T().bunker.halfSize, structure: true, armored: true });
      for (const t of this.tanks) if (t.state === 'ready' && !t.dead) list.push({ kind: 'tank', obj: t, x: t.x, y: t.y, radius: t.radius, armored: true });
      for (const s of this.scvs) if (!s.dead) list.push({ kind: 'scv', obj: s, x: s.x, y: s.y, radius: s.radius, light: true });
      return list;
    }
    valid(tg) { return tg && !tg.obj.dead && (tg.kind !== 'tank' || tg.obj.state === 'ready'); }
    damageDefender(tg, dmg) {
      if (!this.valid(tg)) return;
      const o = tg.obj;
      if (tg.kind === 'bunker') {
        const real = Math.max(0.5, dmg - this.bunker.armor);
        o.hp -= real; o.flash = 1;
        this.shake = Math.min(1, this.shake + real / 60);
        if (o.hp <= 0) this.gameOver();
      } else if (tg.kind === 'tank') {
        o.hp -= Math.max(0.5, dmg - o.armor); o.flash = 1;
        if (o.hp <= 0 && !o.dead) {
          o.dead = true;
          this.corpse('siegetankDeath', o.x, o.y, 0, Math.PI / 2, 6, 1, true);
          BS.audio.play('tankDeath', { vol: 0.8 });
          o.handle && o.handle.release(); if (o.medivac) o.medivac.release();
          this.tanks.splice(this.tanks.indexOf(o), 1);
          this.toast('Siege tank destroyed');
        }
      } else {
        o.hp -= Math.max(0.5, dmg); o.flash = 1;
        if (o.hp <= 0 && !o.dead) {
          o.dead = true;
          this.corpse('scvDeath', o.x, o.y, 0, o.yaw, 3.5, 1, true);
          o.handle.release();
          this.scvs.splice(this.scvs.indexOf(o), 1);
          this.toast('SCV lost');
        }
      }
    }
    corpse(key, x, y, z, yaw, hold, scale, terran) {
      this.fx.play(key, x, y, Math.max(z, 0), yaw, scale || 1);
      // Corpses render in their own pass behind the living (see Renderer.frame), so they never cover live zerg.
      const h = this.r.spawn(key, x, y, Math.min(z, 0), yaw, { corpse: true }).setTeam(terran ? 1 : 3);
      if (scale) h.setScale(scale);
      const c = { h, t: 0, x, y, z: Math.min(z, 0), yaw, sink: !/^bunker/.test(key), hold: hold || 3.5 };
      const len = h.playRandom(/^death/i, false);
      c.hold = c.sink ? clamp(Math.min(len || 3, 4), 2.5, 4) : Math.max(hold, Math.min(len || 8, 8));
      if (z > 0.5) c.hold = Math.max(c.hold, 2);
      // Baneling chunks fade out in SC2 through visibility tracks the viewer does not apply; drop them after the burst.
      if (/^baneling/.test(key)) { c.hold = 0.45; c.sink = false; }
      this.corpses.push(c);
      return c;
    }

    // ---- Combat helpers --------------------------------------------------------------------------------
    enemiesInRange(x, y, range, pred) {
      const out = [];
      for (const e of this.enemies) {
        if (e.hp <= 0 || (pred && !pred(e))) continue;
        const d = dist(x, y, e.x, e.y) - e.radius;
        if (d <= range) out.push([d, e]);
      }
      out.sort((a, b) => a[0] - b[0]);
      return out.map(p => p[1]);
    }
    hurtEnemy(e, dmg) {
      if (!e || e.hp <= 0) return;
      e.hp -= Math.max(0.5, dmg - e.def.armor);
      e.flash = 1;
      if (e.hp <= 0) this.killEnemy(e, false);
    }
    killEnemy(e, suicide) {
      const d = e.def;
      e.hp = 0;
      this.kills++; this.minerals += d.bounty; this.score += d.bounty * 2;
      if (d.soundDeath) BS.audio.play(d.soundDeath, { vol: 0.5 });
      if (d.suicide && !suicide) this.banelingBlast(e, true);
      const i = this.enemies.indexOf(e); if (i >= 0) this.enemies.splice(i, 1);
      e.handle.release();
      this.corpse(e.death, e.x, e.y, e.z, e.yaw, d.deathHold || 3.5, d.scale || 1);
      if (this.focus === e) this.focus = null;
    }
    banelingBlast(e, fromDeath) {
      const d = e.def;
      for (const tg of this.targets()) {
        if (dist(e.x, e.y, tg.x, tg.y) - tg.radius > d.splash) continue;
        const dmg = tg.structure ? d.dmgStructure : (tg.light ? d.dmg + d.dmgLight : d.dmg);
        this.damageDefender(tg, fromDeath ? dmg * 0.5 : dmg);
      }
      BS.audio.play('banelingDeath', { vol: 0.7 });
    }
    floater(x, y, z, text, color) { this.floaters.push({ x, y, z, text, color, t: 0 }); }
    toast(msg) { this.toasts.push({ msg, t: this.time }); if (this.toasts.length > 4) this.toasts.shift(); return false; }

    // The bunker's gun slit facing a direction (game space).
    bunkerSlit(ang) {
      const phi = this.bunker.yaw + Math.PI / 2, cs = Math.cos(phi), sn = Math.sin(phi);
      let best = null, bestDot = -2;
      for (const [mx, my] of BUNKER_SLITS) {
        const x = mx * cs - my * sn, y = mx * sn + my * cs;
        const dot = (x * Math.cos(ang) + y * Math.sin(ang)) / Math.hypot(x, y);
        if (dot > bestDot) { bestDot = dot; best = [x, y, SLIT_Z]; }
      }
      return best;
    }
    projectileModel(p, model, trail, trailScale) {
      const yaw = Math.atan2(p.ty - p.fy, p.tx - p.fx);
      if (model) { p.model = this.r.spawn(model, p.fx, p.fy, p.fz, yaw).setTeam(1); p.model.play(/^stand/i, true); }
      if (trail) p.trail = this.fx.play(trail, p.fx, p.fy, p.fz, yaw, trailScale || 1);
    }

    // ---- Update -----------------------------------------------------------------------------------------
    update(dt) {
      if (this.paused || this.over || !this.started) return;
      this.acc += Math.min(dt, 0.1);
      let n = 0;
      while (this.acc >= this.step && n++ < 6) { this.tick(this.step); this.acc -= this.step; }
    }
    tick(dt) {
      this.time += dt;
      this.minerals += this.income * dt;
      this.score += dt * 0.5;
      this.shake = Math.max(0, this.shake - dt * 2);
      for (const k of ['stimCd', 'strikeCd', 'repairCd', 'yamatoCd']) this.ab[k] = Math.max(0, this.ab[k] - dt);
      this.waveTimer -= dt;
      if (this.waveTimer <= 0 || (!this.spawnQueue.length && !this.enemies.length && this.wave > 0 && this.waveTimer < this.waveLen - 6)) {
        if (this.wave > 0 && !this.enemies.length) { this.minerals += 10 + 5 * this.wave; this.score += 100 * this.wave; }
        this.startWave();
      }
      this.spawnTimer += dt;
      while (this.spawnQueue.length && this.spawnQueue[0].t <= this.spawnTimer) {
        const s = this.spawnQueue.shift();
        if (this.enemies.length < this.maxAlive()) this.spawnEnemy(s.type, s.ang);
        else this.spawnQueue.push({ t: this.spawnTimer + 2, type: s.type, ang: s.ang });
      }
      this.tgs = this.targets();
      this.updateEnemies(dt);
      this.updateBunker(dt);
      this.updateTanks(dt);
      this.updateScvs(dt);
      this.updateBanshees(dt);
      this.updateProjectiles(dt);
      this.updateReinforcements(dt);
      if (this.focus && (this.focus.hp <= 0 || this.time > this.focusUntil)) this.focus = null;
    }

    updateEnemies(dt) {
      const E = this.enemies, N = E.length;
      for (const e of E) { e.sx = 0; e.sy = 0; }
      for (let i = 0; i < N; i++) {
        const a = E[i];
        for (let j = i + 1; j < N; j++) {
          const b = E[j];
          if (!!a.def.flying !== !!b.def.flying) continue;
          const dx = b.x - a.x, dy = b.y - a.y, min = a.radius + b.radius + 0.1, d2 = dx * dx + dy * dy;
          if (d2 < min * min && d2 > 1e-6) { const d = Math.sqrt(d2), push = (min - d) / d * 0.5; a.sx -= dx * push; a.sy -= dy * push; b.sx += dx * push; b.sy += dy * push; }
        }
      }
      const targets = this.tgs;
      for (const e of E.slice()) {
        if (e.hp <= 0) continue;
        const d = e.def;
        if (e.life > 0) { e.life -= dt; if (e.life <= 0) { this.killEnemy(e, true); this.kills--; continue; } }
        e.retarget -= dt;
        if (!this.valid(e.target) || e.retarget <= 0) {
          let best = null, bd = 1e9;
          for (const t of targets) { if (t.kind === 'scv' && Math.random() < 0.5) continue; const dd = dist(e.x, e.y, t.x, t.y) - t.radius; if (dd < bd) { bd = dd; best = t; } }
          e.target = best; e.retarget = rnd(0.5, 0.8);
        }
        const t = e.target;
        if (!t) continue;
        t.x = t.obj.x !== undefined ? t.obj.x : t.x; t.y = t.obj.y !== undefined ? t.obj.y : t.y;
        const dx = t.x - e.x, dy = t.y - e.y, dd = Math.hypot(dx, dy);
        const reach = dd - t.radius - e.radius, range = d.range + (t.kind === 'bunker' ? 0.2 : 0), slowed = e.slowUntil > this.time;
        if (d.suicide && reach <= range) { this.banelingBlast(e, false); this.killEnemy(e, true); continue; }
        if (d.burrows) {
          if (!e.burrowed && reach <= range - 1) { e.burrowed = true; e.burrowT = 1; this.playAnim(e, 'burrow'); }
          else if (e.burrowed) e.burrowT -= dt;
        }
        const canAttack = reach <= range && !(d.burrows && (!e.burrowed || e.burrowT > 0));
        if (canAttack) {
          e.attacking = true;
          e.yaw = Math.atan2(dy, dx);
          if (d.attackSeq) this.playAnim(e, 'attack'); else if (!d.burrows) this.playAnim(e, 'stand');
          e.cd -= dt * (slowed ? 0.8 : 1);
          if (e.cd <= 0) {
            const cd = this.enemyCd(e);
            e.cd += cd; e.hitAt = this.time + cd * d.hitFrac; e.hitTarget = t;
            if (d.soundAttack) BS.audio.play(d.soundAttack, { vol: 0.35, gap: 0.15 });
            if (d.projectile) this.enemyShot(e, t, cd);
            else e.pendingHit = true;
          }
          if (e.pendingHit && this.time >= e.hitAt) {
            e.pendingHit = false;
            const tg = e.hitTarget;
            if (this.valid(tg)) {
              let dmg = d.dmg + (tg.light && d.dmgLight ? d.dmgLight : 0) + (tg.armored && d.dmgArmored ? d.dmgArmored : 0);
              this.damageDefender(tg, dmg);
              if (d.impactFx) { const a = Math.atan2(e.y - tg.y, e.x - tg.x); this.fx.play(d.impactFx, tg.x + Math.cos(a) * tg.radius, tg.y + Math.sin(a) * tg.radius, 0.5, a); }
              if (d.cleave) for (const o of targets) if (o !== tg && dist(tg.x, tg.y, o.x, o.y) - o.radius < d.cleave) this.damageDefender(o, dmg * 0.5);
            }
          }
        } else if (!(d.burrows && e.burrowed)) {
          e.attacking = false;
          this.playAnim(e, 'walk');
          const sp = d.speed * (slowed ? 0.5 : 1), nx = dx / (dd || 1), ny = dy / (dd || 1);
          e.x += nx * sp * dt + e.sx * 0.8; e.y += ny * sp * dt + e.sy * 0.8;
          if (this.mode === 'lane') { const half = 5.2 + Math.max(0, e.y - 4) * 0.75; e.x = clamp(e.x, -half, half); }   // the lane fans out to the north
          // Face the way it is heading, not the way the crowd shoves it, and turn at a creature's pace.
          const turn = (d.radius >= 0.9 ? 3 : 8) * dt;
          e.yaw += clamp(angDelta(e.yaw, Math.atan2(ny, nx)), -turn, turn);
        }
      }
    }
    enemyShot(e, t, cd) {
      const d = e.def;
      const p = { kind: d.projectile, friendly: false, def: d, target: t, src: e, fx: e.x, fy: e.y, fz: e.z + d.height * 0.6, tx: t.x, ty: t.y, tz: 1.0, t: 0, dur: Math.max(0.15, cd * d.hitFrac), dmg: d.dmg, arc: 0 };
      switch (d.projectile) {
        case 'acid': p.arc = 1.5; p.impact = 'roachImpact'; this.projectileModel(p, null, 'roachMissile', 0.7); break;
        case 'spine': this.projectileModel(p, 'projHydralisk', null); break;
        case 'bullet': p.impact = 'marineImpact'; this.fx.play('marineLaunch', p.fx, p.fy, p.fz, Math.atan2(t.y - e.y, t.x - e.x)); break;
        case 'glaive': p.bounces = d.bounces; p.hit = []; p.impact = 'mutaliskImpact'; this.projectileModel(p, 'projMutalisk', 'mutaliskMissile'); break;
        case 'brood': p.arc = 3; p.model = this.r.spawn('zergling', p.fx, p.fy, p.fz, 0).setScale(0.45).setTeam(3); p.model.play(/^stand/i, true); break;
      }
      this.projectiles.push(p);
    }

    // Bunker occupants fire from the slit facing their target.
    updateBunker(dt) {
      const b = this.bunker;
      if (b.dead) return;
      const stim = this.ab.stimUntil > this.time;
      const bonus = T().bunker.rangeBonus + T().bunker.halfSize + (this.up.hisec ? T().hiSecRange : 0) + (this.hard ? 1 : 0);
      if (this.up.medivac) this.updateStation(dt);
      for (const u of b.units) {
        u.cd -= dt * (stim ? 1.5 : 1);
        if (u.cd > 0) continue;
        const d = u.def, range = d.range + bonus, pred = e => !(d.groundOnly && e.def.flying);
        let target = null;
        if (this.focus && this.focus.hp > 0 && pred(this.focus) && dist(0, 0, this.focus.x, this.focus.y) - this.focus.radius <= range) target = this.focus;
        if (!target) {
          const list = this.enemiesInRange(0, 0, range, pred);
          if (!list.length) { u.cd = 0.05; continue; }
          target = list[0];
          if (u.type === 'marauder') target = list.find(e => e.def.armored) || target;
          if (u.type === 'firebat') target = list.find(e => e.def.light) || target;
        }
        u.cd += d.cd;
        const dmg = d.dmg + this.up.weapons;
        const ang = Math.atan2(target.y, target.x);
        const slit = this.bunkerSlit(ang);
        const hx = target.x, hy = target.y, hz = target.z + target.def.height * 0.5;
        if (u.type === 'marine') {
          this.hurtEnemy(target, dmg);
          // A little spread along the slit; three-round bursts with SC2's muzzle mesh and impact sparks.
          const k = rnd(-0.18, 0.18), sx = slit[0] - Math.sin(ang) * k, sy = slit[1] + Math.cos(ang) * k, aim = Math.atan2(hy - sy, hx - sx);
          for (let r = 0; r < 3; r++) {
            this.fx.play('marineLaunch', sx, sy, slit[2], aim, 1.4, r * 0.08);
            this.fx.play('marineImpact', hx + rnd(-0.2, 0.2), hy + rnd(-0.2, 0.2), hz + rnd(-0.15, 0.25), aim + Math.PI, 1, r * 0.08 + 0.03);
            this.flashes.push({ x: sx, y: sy, z: slit[2], yaw: aim, delay: r * 0.08, life: 0.09 });
          }
          BS.audio.play('marineFire', { vol: 0.3, gap: 0.09 });
        } else if (u.type === 'firebat') {
          for (const v of this.enemiesInRange(hx, hy, d.splash, pred)) this.hurtEnemy(v, dmg + (v.def.light ? d.dmgLight : 0));
          this.fx.play('firebatLaunch', slit[0], slit[1], slit[2], ang, 1.2);
          BS.audio.play('firebatFire', { vol: 0.45, gap: 0.3 });
        } else {
          const p = { kind: 'grenade', friendly: true, fx: slit[0], fy: slit[1], fz: slit[2], tx: hx, ty: hy, tz: hz, t: 0, dur: 0.3, arc: 0.6, target, dmg, def: d, impact: 'marauderImpact' };
          this.fx.play('marauderLaunch', slit[0], slit[1], slit[2], ang);
          this.projectileModel(p, 'projMarauder', 'marauderMissile');
          this.projectiles.push(p);
          BS.audio.play('marauderFire', { vol: 0.4, gap: 0.2 });
        }
      }
    }
    // Medivac on Station: parked at the bunker's shoulder; heals the troops inside and the most hurt SCV nearby.
    updateStation(dt) {
      for (const u of this.bunker.units) u.hp = Math.min(u.maxHp, u.hp + 9 * dt);
      let patient = null;
      for (const s of this.scvs) if (!s.dead && s.hp < s.maxHp - 0.5 && dist(0, 0.5, s.x, s.y) < 6 && (!patient || s.hp / s.maxHp < patient.hp / patient.maxHp)) patient = s;
      if (patient) patient.hp = Math.min(patient.maxHp, patient.hp + 12.6 * dt);
      if (!this.station) { this.station = { yaw: Math.PI / 2, anim: '', animT: 10, h: this.r.spawn('medivac', 1.9, -0.9, 2.7, Math.PI / 2).setTeam(1).setScale(0.75) }; }
      const st = this.station, px = 1.9, py = -0.9;
      const want = patient ? Math.atan2(patient.y - py, patient.x - px) : Math.PI / 2;
      st.yaw += clamp(angDelta(st.yaw, want), -1.5 * dt, 1.5 * dt);
      st.h.setPos(px, py, 2.7 + 0.12 * Math.sin(this.time * 1.7)).setYaw(st.yaw);
      st.animT += dt;
      const wantAnim = patient ? 'work' : 'idle';
      if (st.anim !== wantAnim && st.animT > 1) { st.anim = wantAnim; st.animT = 0; st.h.play(patient ? /^stand work|^attack/i : /^walk|^fly/i, true); }
    }

    updateTanks(dt) {
      const D = T().tank;
      for (const t of this.tanks.slice()) {
        if (t.dead) continue;
        if (t.state === 'incoming') {
          t.t += dt;
          const dx = t.x - t.mx, dy = t.y - t.my, dz = 5.5 - t.mz, dd = Math.hypot(dx, dy, dz);
          if (dd > 0.3 && t.t < 10) { const s = Math.min(dd, 9 * dt) / dd; t.mx += dx * s; t.my += dy * s; t.mz += dz * s; }
          else { t.state = 'unloading'; t.t = 0; t.medivac.play(/^stand work start/i, false); BS.audio.play('medivacUnload', { vol: 0.6 }); }
          t.medivac.setPos(t.mx, t.my, t.mz).setYaw(Math.PI / 2);
        } else if (t.state === 'unloading') {
          t.t += dt;
          if (t.t > 1.6 && !t.handle) { t.handle = this.r.spawn('siegetankMorph', t.x, t.y, 0, Math.PI / 2).setTeam(1); t.handle.play(/^morph start/i, false); this.shake = Math.min(1, this.shake + 0.2); }
          if (t.t > 2.2 && !t.endPlayed) { t.endPlayed = true; t.medivac.play(/^stand work end/i, false); }
          if (t.t > 3.6) { t.state = 'leaving'; t.t = 0; t.medivac.play(/^walk|^fly/i, true); }
        } else if (t.state === 'leaving') {
          t.t += dt; t.my -= 10 * dt; t.mz += 2 * dt;
          t.medivac.setPos(t.mx, t.my, t.mz).setYaw(-Math.PI / 2);
          if (t.t > 2) {
            if (t.handle) t.handle.release();
            t.handle = this.r.spawn('siegetank', t.x, t.y, 0, Math.PI / 2).setTeam(1); t.handle.play(/^stand$/i, true);
            t.state = 'ready'; t.mt = 0;
            this.toast('Siege tank in position');
          }
        } else {
          if (t.medivac) { t.mt += dt; t.my -= 10 * dt; t.mz += 2 * dt; t.medivac.setPos(t.mx, t.my, t.mz); if (t.mt > 3) { t.medivac.release(); t.medivac = null; } }
          t.cd -= dt;
          if (t.cd > 0) continue;
          const range = D.range + (this.up.hisec ? T().hiSecRange : 0);
          const pred = e => !e.def.flying && dist(t.x, t.y, e.x, e.y) > D.minRange;
          let target = null;
          if (this.focus && this.focus.hp > 0 && pred(this.focus) && dist(t.x, t.y, this.focus.x, this.focus.y) <= range) target = this.focus;
          if (!target) {
            const list = this.enemiesInRange(t.x, t.y, range, pred);
            if (!list.length) { t.cd = 0.1; continue; }
            let bestN = -1;
            for (const e of list.slice(0, 8)) { const n = this.enemiesInRange(e.x, e.y, 1.1, pred).length; if (n > bestN) { bestN = n; target = e; } }
          }
          t.cd = D.cd;
          const ang = Math.atan2(target.y - t.y, target.x - t.x);
          t.handle.setYaw(ang); t.handle.play(/^attack/i, false);
          const fx = t.x + Math.cos(ang) * 1.4, fy = t.y + Math.sin(ang) * 1.4;
          // SC2 siege shells are not drawn: launch flash, then the impact.
          this.fx.play('tankLaunch', fx, fy, 1.3, ang);
          this.projectiles.push({ kind: 'shell', friendly: true, fx, fy, fz: 1.3, tx: target.x, ty: target.y, tz: 0.1, t: 0, dur: 0.35, arc: 4,
            dmg: D.dmg + 5 * this.up.vehicle, dmgArmored: D.dmgArmored + 3 * this.up.vehicle, impact: 'tankImpact' });
          BS.audio.play('tankFire', { vol: 0.7, gap: 0.2 });
          this.shake = Math.min(1, this.shake + 0.15);
        }
      }
    }

    // Repair jobs, most urgent first: the bunker gets a head start, and when it is in real danger everyone works on it.
    updateScvs(dt) {
      const D = T().scv, b = this.bunker, frac = b.maxHp > 0 ? b.hp / b.maxHp : 1;
      const jobs = [];
      if (!b.dead && b.hp < b.maxHp - 1) jobs.push({ x: 0, y: 0, r: T().bunker.halfSize, cost: 0.25, obj: b, need: frac - 0.2 });
      if (frac > 0.4) for (const t of this.tanks) if (t.state === 'ready' && !t.dead && t.hp < t.maxHp - 1) jobs.push({ x: t.x, y: t.y, r: t.radius, cost: 0.4, obj: t, need: t.hp / t.maxHp });
      jobs.sort((a, c) => a.need - c.need);
      this.scvs.forEach((s, i) => {
        const job = jobs.length ? jobs[i % jobs.length] : null;
        let gx, gy, stop;
        if (job) { const a = Math.atan2(s.idle[1] - job.y, s.idle[0] - job.x); gx = job.x + Math.cos(a) * (job.r + 0.6); gy = job.y + Math.sin(a) * (job.r + 0.6); stop = 0.25; }
        else { gx = s.idle[0]; gy = s.idle[1]; stop = 0.3; }
        const dx = gx - s.x, dy = gy - s.y, dd = Math.hypot(dx, dy);
        if (dd > stop) {
          const sp = Math.min(dd, D.speed * dt);
          s.x += dx / dd * sp; s.y += dy / dd * sp; s.yaw = Math.atan2(dy, dx);
          if (s.anim !== 'walk') { s.anim = 'walk'; s.handle.play(/^walk/i, true, 1.4); }
        } else if (job) {
          s.yaw = Math.atan2(job.y - s.y, job.x - s.x);
          if (s.anim !== 'work') { s.anim = 'work'; s.handle.play(/^work/i, true); BS.audio.play('scvRepair', { vol: 0.25, gap: 1.5 }); }
          const amount = Math.min(D.repairRate * dt, job.obj.maxHp - job.obj.hp, this.minerals / job.cost);
          if (amount > 0) { job.obj.hp += amount; this.minerals -= amount * job.cost; }
        } else if (s.anim !== 'stand') { s.anim = 'stand'; s.handle.play(/^stand$/i, true); }
        s.handle.setPos(s.x, s.y, 0).setYaw(s.yaw);
      });
    }

    updateBanshees(dt) {
      const D = T().banshee, lane = this.mode === 'lane', farY = lane ? 30 : 32, homeY = lane ? -16 : -36;
      for (let i = this.banshees.length - 1; i >= 0; i--) {
        const b = this.banshees[i];
        if (!b.back) { b.y += D.speed * dt; if (b.y >= farY) { b.back = true; b.yaw = -Math.PI / 2; } }
        else { b.y -= D.speed * dt; if (b.y <= homeY) { b.handle.release(); this.banshees.splice(i, 1); continue; } }
        b.handle.setPos(b.x, b.y, b.z).setYaw(b.yaw);
        b.cd -= dt;
        if (b.cd > 0) continue;
        const list = this.enemiesInRange(b.x, b.y, D.range, e => !e.def.flying);
        if (!list.length) { b.cd = 0.1; continue; }
        b.cd = D.cd;
        for (let k = 0; k < D.shots; k++) {
          const p = { kind: 'rocket', friendly: true, fx: b.x + (k ? 0.4 : -0.4), fy: b.y, fz: b.z - 0.3, tx: list[0].x, ty: list[0].y, tz: 0.3, t: 0, dur: 0.35, dmg: D.dmg, target: list[0], impact: 'bansheeImpact' };
          this.projectileModel(p, 'projBanshee', 'bansheeMissile');
          this.projectiles.push(p);
        }
        b.handle.play(/^attack/i, false);
        BS.audio.play('bansheeFire', { vol: 0.5, gap: 0.2 });
      }
    }

    updateProjectiles(dt) {
      const P = this.projectiles;
      for (let i = P.length - 1; i >= 0; i--) {
        const p = P[i];
        p.t += dt;
        if (p.friendly && p.target && p.target.hp > 0) { p.tx = p.target.x; p.ty = p.target.y; p.tz = p.target.z + p.target.def.height * 0.5; }
        const k = clamp(p.t / p.dur, 0, 1);
        const x = p.fx + (p.tx - p.fx) * k, y = p.fy + (p.ty - p.fy) * k, z = p.fz + (p.tz - p.fz) * k + Math.sin(k * Math.PI) * (p.arc || 0);
        const yaw = Math.atan2(p.ty - p.fy, p.tx - p.fx);
        if (p.model) p.model.setPos(x, y, z).setYaw(yaw);
        if (p.trail) this.fx.move(p.trail, x, y, z, yaw);
        if (p.t < p.dur) continue;
        P.splice(i, 1);
        if (p.model) p.model.release();
        if (p.trail) this.fx.stop(p.trail);
        if (p.impact) this.fx.play(p.impact, p.tx, p.ty, p.tz, Math.atan2(p.fy - p.ty, p.fx - p.tx));
        if (p.friendly) {
          if (p.kind === 'shell') {
            const [r1, r2, r3] = T().tank.splash;
            for (const e of this.enemiesInRange(p.tx, p.ty, r3 + 0.5, e => !e.def.flying)) {
              const dd = dist(p.tx, p.ty, e.x, e.y) - e.radius;
              const mult = dd <= r1 ? 1 : dd <= r2 ? 0.5 : dd <= r3 ? 0.25 : 0;
              if (mult > 0) this.hurtEnemy(e, (p.dmg + (e.def.armored ? p.dmgArmored : 0)) * mult);
            }
            BS.audio.play('tankHit', { vol: 0.6, gap: 0.1 });
            this.shake = Math.min(1, this.shake + 0.1);
          } else if (p.kind === 'grenade') {
            if (p.target && p.target.hp > 0) { p.target.slowUntil = this.time + p.def.slow; p.target.anim = ''; this.hurtEnemy(p.target, p.dmg + (p.target.def.armored ? p.def.dmgArmored : 0)); }
          } else if (p.kind === 'rocket') {
            if (p.target && p.target.hp > 0) this.hurtEnemy(p.target, p.dmg);
            BS.audio.play('bansheeHit', { vol: 0.35, gap: 0.1 });
          } else if (p.kind === 'yamato') this.yamatoImpact(p.tx, p.ty);
          continue;
        }
        const tg = p.target, d = p.def;
        if (!this.valid(tg)) continue;
        let dmg = d.dmg + (tg.light && d.dmgLight ? d.dmgLight : 0) + (tg.armored && d.dmgArmored ? d.dmgArmored : 0);
        if (p.kind === 'spines') {
          const sx = p.src.hp > 0 ? p.src.x : p.fx, sy = p.src.hp > 0 ? p.src.y : p.fy, L = Math.max(0.01, dist(sx, sy, tg.x, tg.y));
          for (const o of this.tgs) {
            const pr = clamp(((o.x - sx) * (tg.x - sx) + (o.y - sy) * (tg.y - sy)) / (L * L), 0, 1);
            if (dist(o.x, o.y, sx + (tg.x - sx) * pr, sy + (tg.y - sy) * pr) - o.radius < 0.6) this.damageDefender(o, dmg);
          }
          this.fx.play('lurkerMissile', sx, sy, 0.1, Math.atan2(tg.y - sy, tg.x - sx));
          continue;
        }
        if (p.kind === 'glaive') dmg = p.dmg;
        this.damageDefender(tg, dmg);
        if (p.kind === 'glaive' && p.bounces > 0) {
          // The glaive ricochets to the nearest defender it has not hit yet, losing a third of its punch each time.
          const hit = p.hit.concat([tg.obj]);
          let next = null, nearest = 4.5;
          for (const o of this.targets()) { if (hit.includes(o.obj)) continue; const dd = dist(tg.x, tg.y, o.x, o.y) - o.radius; if (dd < nearest) { nearest = dd; next = o; } }
          if (next) {
            const q = { kind: 'glaive', friendly: false, def: d, target: next, fx: tg.x, fy: tg.y, fz: 1, tx: next.x, ty: next.y, tz: 1, t: 0, dmg: dmg * 0.67, bounces: p.bounces - 1, hit, impact: 'mutaliskImpact' };
            q.dur = Math.max(0.12, dist(q.fx, q.fy, q.tx, q.ty) / 14);
            this.projectileModel(q, 'projMutalisk', 'mutaliskMissile');
            P.push(q);
          }
        }
        if (d.broodlings && this.enemies.length < 80) for (let k2 = 0; k2 < 2; k2++) this.spawnEnemy('broodling', 0, [tg.x + rnd(-1, 1), tg.y + rnd(-1, 1) + 1.5]);
      }
    }

    // Visual state that follows game speed (called every frame).
    updateVisuals(dt) {
      for (const e of this.enemies) {
        e.flash = Math.max(0, e.flash - dt * 9);
        e.handle.setPos(e.x, e.y, e.z).setYaw(e.yaw);
        // Ultralisk swipe trails only show around the moment a blow lands.
        if (e.def.cleave) e.handle.setSwipe(e.attacking && Math.abs(this.time - e.hitAt) < 0.22);
      }
      for (let i = this.corpses.length - 1; i >= 0; i--) {
        const c = this.corpses[i];
        c.t += dt;
        if (c.t > c.hold) { c.h.release(); this.corpses.splice(i, 1); continue; }
        if (c.sink) { const k = clamp((c.t / c.hold - 0.45) / 0.55, 0, 1); c.h.setPos(c.x, c.y, c.z - 0.9 * k * k); }
      }
      for (let i = this.flashes.length - 1; i >= 0; i--) {
        const f = this.flashes[i];
        if (f.delay > 0) { f.delay -= dt; continue; }
        if (!f.h) { f.h = this.r.spawn('fxMuzzle', f.x, f.y, f.z, f.yaw); f.h.playRandom(/^stand/i, false); }
        f.life -= dt;
        if (f.life <= 0) { f.h.release(); this.flashes.splice(i, 1); }
      }
      for (let i = this.floaters.length - 1; i >= 0; i--) { const f = this.floaters[i]; f.t += dt; if (f.t > 1.3) this.floaters.splice(i, 1); }
      this.bunker.flash = Math.max(0, this.bunker.flash - dt * 4);
      for (const t of this.tanks) t.flash = Math.max(0, t.flash - dt * 4);
      for (const s of this.scvs) s.flash = Math.max(0, s.flash - dt * 4);
    }

    focusAt(wx, wy) {
      let best = null, bd = 4;
      for (const e of this.enemies) { const d = dist(wx, wy, e.x, e.y) - e.radius; if (d < bd) { bd = d; best = e; } }
      if (best) { this.focus = best; this.focusUntil = this.time + 4; return true; }
      return false;
    }

    gameOver() {
      if (this.over) return;
      this.over = true; this.bunker.dead = true; this.bunker.hp = 0;
      this.bunker.handle.release();
      this.corpse('bunkerDeath', 0, 0, 0, this.bunker.yaw, 60, 1, true);
      BS.audio.play('explosion', { vol: 0.9 });
      this.shake = 1;
      this.finalScore = Math.round(this.score + this.kills * 10 + this.wave * 250);
      try {
        const key = 'bs.best.' + this.mode, best = parseInt(localStorage.getItem(key) || '0', 10);
        this.best = Math.max(best, this.finalScore); this.newBest = this.finalScore > best;
        localStorage.setItem(key, String(this.best));
      } catch (e) { this.best = this.finalScore; }
    }
  }

  BS.Game = Game;
})(window.BS);
