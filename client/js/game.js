// Simulation: bunker, defenders, zerg waves, economy, upgrades and abilities.
window.BS = window.BS || {};
(function (BS) {
  'use strict';

  const rnd = (a, b) => a + Math.random() * (b - a);
  const pick = arr => arr[Math.floor(Math.random() * arr.length)];
  const clamp = (v, a, b) => v < a ? a : v > b ? b : v;
  const dist2 = (ax, ay, bx, by) => { const dx = ax - bx, dy = ay - by; return dx * dx + dy * dy; };

  class Game {
    constructor(renderer, opts) {
      this.r = renderer;
      this.mode = opts.mode || 'lane';            // 'lane' (phone) | 'arena' (360 browser mode)
      this.hard = this.mode === 'arena';           // arena = greater challenge
      this.diffMult = this.hard ? 1.35 : 1.0;
      this.time = 0; this.acc = 0; this.step = 1 / 60;
      this.paused = false; this.over = false; this.started = false;
      this.minerals = 150; this.income = 2;
      this.kills = 0; this.score = 0;
      this.wave = 0; this.waveTimer = 8; this.waveLen = this.hard ? 26 : 30; this.spawnQueue = []; this.spawnTimer = 0;
      this.enemies = []; this.dying = []; this.projectiles = []; this.fx = []; this.floaters = [];
      this.focus = null; this.focusUntil = 0;
      this.log = [];
      this.shake = 0;
      const T = BS.TERRAN;
      this.bunker = { x: 0, y: 0, hp: T.bunker.hp, maxHp: T.bunker.hp, armor: T.bunker.armor, slots: T.bunker.slots, units: [], handle: null, dead: false, lastShot: 0 };
      this.tanks = []; this.scvs = []; this.banshees = [];
      this.up = { stim: false, weapons: 0, vehicle: 0, neosteel: false, plating: false, medivac: false };
      this.ab = { stimUntil: 0, stimCd: 0, strikeCd: 0, repairCd: 0 };
      this.layout();
      this.bunker.handle = this.r.spawn('bunker', this.bunker.x, this.bunker.y, 0, 0);
      this.bunker.handle.setTeam(0);
      this.ensureBunkerAnim();
      for (let i = 0; i < 3; i++) this.addInfantry('marine', true);
      this.addScv(true);
      this.renderer = renderer;
    }

    layout() {
      if (this.mode === 'lane') {
        this.tankSlots = [[-3.4, -0.4], [3.4, -0.4]];
        this.scvIdle = [[-2.6, -2.9], [2.6, -2.9], [-4.4, -2.4], [4.4, -2.4]];
        this.tankMax = BS.TERRAN.tank.maxLane;
      } else {
        this.tankSlots = [[-3.8, -3.2], [3.8, -3.2], [-3.8, 3.2], [3.8, 3.2]];
        this.scvIdle = [[-2.6, -2.6], [2.6, 2.6], [-2.6, 2.6], [2.6, -2.6]];
        this.tankMax = BS.TERRAN.tank.maxArena;
      }
    }

    // ---- Defenders ---------------------------------------------------------------------------
    addInfantry(type, free) {
      const d = BS.INFANTRY[type];
      if (this.bunker.units.length >= this.bunker.slots) return this.toast('Bunker is full');
      if (!free) { if (this.minerals < d.cost) return this.toast('Not enough minerals'); this.minerals -= d.cost; }
      this.bunker.units.push({ type, hp: d.hp, maxHp: d.hp, cd: Math.random() * 0.3, def: d });
      this.ensureBunkerAnim();
      return true;
    }
    ensureBunkerAnim() {
      const h = this.bunker.handle; if (!h) return;
      h.play(this.bunker.units.length ? 'Stand Work' : 'Stand', true);
    }
    addScv(free) {
      const d = BS.TERRAN.scv;
      if (this.scvs.length >= d.max) return this.toast('SCV limit reached');
      if (!free) { if (this.minerals < d.cost) return this.toast('Not enough minerals'); this.minerals -= d.cost; }
      const idle = this.scvIdle[this.scvs.length % this.scvIdle.length];
      const s = { x: idle[0], y: idle[1] - 6, idle, hp: d.hp, maxHp: d.hp, state: 'idle', target: null, handle: this.r.spawn('scv', idle[0], idle[1] - 6, 0, Math.PI / 2), radius: d.radius, anim: '' };
      s.handle.setTeam(0);
      this.scvs.push(s);
      return true;
    }
    dropTank() {
      const d = BS.TERRAN.tank;
      if (this.tanks.length >= this.tankMax) return this.toast('No room for more tanks');
      if (this.minerals < d.cost) return this.toast('Not enough minerals');
      this.minerals -= d.cost;
      const slot = this.tankSlots[this.tanks.length];
      const t = { x: slot[0], y: slot[1], hp: d.hp, maxHp: d.hp, armor: d.armor, cd: 0, state: 'incoming', t: 0, radius: d.radius, handle: null, medivac: null, dead: false };
      const startY = this.mode === 'lane' ? -14 : -26;
      t.medivac = this.r.spawn('medivac', slot[0], startY, 5.5, Math.PI / 2);
      t.medivac.setTeam(0);
      t.medivac.play('Walk', true);
      this.tanks.push(t);
      this.toast('Medivac inbound with a siege tank');
      return true;
    }
    callAirStrike() {
      const d = BS.TERRAN.banshee;
      if (this.ab.strikeCd > 0) return this.toast('Air support is refuelling');
      if (this.minerals < d.cost) return this.toast('Not enough minerals');
      this.minerals -= d.cost;
      this.ab.strikeCd = d.cooldown;
      const lane = this.mode === 'lane';
      for (let i = 0; i < 2; i++) {
        const x = lane ? (i ? 2.2 : -2.2) : (i ? 3 : -3);
        const b = { x, y: lane ? -12 : -34, z: d.fly, yaw: Math.PI / 2, t: 0, phase: 'in', cd: 0.3 + i * 0.25, handle: this.r.spawn('banshee', x, lane ? -12 : -34, d.fly, Math.PI / 2) };
        b.handle.setTeam(0); b.handle.play('Walk', true);
        this.banshees.push(b);
      }
      this.toast('Banshees on the way');
      return true;
    }
    useStim() {
      if (!this.up.stim) return this.toast('Research Stimpack first');
      if (this.ab.stimCd > 0) return false;
      let any = false;
      for (const u of this.bunker.units) {
        if (u.hp > u.def.stimHp + 1) { u.hp -= u.def.stimHp; any = true; }
      }
      if (!any) return this.toast('Troops too wounded to stim');
      this.ab.stimUntil = this.time + 11; this.ab.stimCd = 13;
      BS.audio.play('stim', { vol: 0.8 });
      this.floater(this.bunker.x, this.bunker.y, 2.5, 'STIM!', '#ffd34d');
      return true;
    }
    emergencyRepair() {
      const b = this.bunker;
      if (this.ab.repairCd > 0) return false;
      if (b.hp >= b.maxHp) return this.toast('Bunker is intact');
      if (this.minerals < 75) return this.toast('Not enough minerals');
      this.minerals -= 75; this.ab.repairCd = 20;
      b.hp = Math.min(b.maxHp, b.hp + 150);
      this.floater(b.x, b.y, 2.5, '+150', '#7fe0ff');
      BS.audio.play('scvRepair');
      return true;
    }
    buyUpgrade(id) {
      const U = BS.UPGRADES, up = this.up;
      const spend = c => { if (this.minerals < c) { this.toast('Not enough minerals'); return false; } this.minerals -= c; return true; };
      switch (id) {
        case 'stim': if (up.stim || !spend(U.stim.cost)) return false; up.stim = true; this.toast('Stimpack ready: tap STIM'); return true;
        case 'weapons': if (up.weapons >= 3 || !spend(U.weapons.costs[up.weapons])) return false; up.weapons++; this.toast('Infantry Weapons level ' + up.weapons); return true;
        case 'vehicle': if (up.vehicle >= 3 || !spend(U.vehicle.costs[up.vehicle])) return false; up.vehicle++; this.toast('Vehicle Weapons level ' + up.vehicle); return true;
        case 'neosteel': if (up.neosteel || !spend(U.neosteel.cost)) return false; up.neosteel = true; this.bunker.slots = 6; this.toast('Bunker expanded to 6 slots'); return true;
        case 'plating': if (up.plating || !spend(U.plating.cost)) return false; up.plating = true; this.bunker.maxHp += 150; this.bunker.hp += 150; this.bunker.armor += 1; this.toast('Bunker reinforced'); return true;
        case 'medivac': if (up.medivac || !spend(U.medivac.cost)) return false; up.medivac = true; this.toast('Medivac on station'); return true;
      }
      return false;
    }

    // ---- Waves --------------------------------------------------------------------------------
    startWave() {
      this.wave++;
      const w = this.wave;
      let budget = (20 + 8 * w + 0.7 * w * w) * this.diffMult;
      const types = Object.entries(BS.ZERG).filter(([k, d]) => d.budget > 0 && w >= Math.ceil(d.unlock * (this.hard ? 0.75 : 1)));
      const list = [];
      let guard = 0;
      while (budget > 0 && guard++ < 400) {
        const total = types.reduce((s, [, d]) => s + d.weight * (d.budget <= budget ? 1 : 0), 0);
        if (total <= 0) break;
        let roll = Math.random() * total;
        for (const [k, d] of types) {
          if (d.budget > budget) continue;
          roll -= d.weight;
          if (roll <= 0) { list.push(k); budget -= d.budget; break; }
        }
      }
      if (w % 5 === 0) { list.push(w >= 10 ? 'ultralisk' : 'roach', 'roach'); }
      if (this.hard && w % 10 === 0) list.push('ultralisk', 'ultralisk');
      // Group into bursts spread across the wave.
      list.sort(() => Math.random() - 0.5);
      const spread = this.waveLen - 8;
      let t = 0.5;
      while (list.length) {
        const n = Math.min(list.length, 2 + Math.floor(Math.random() * 4));
        const group = list.splice(0, n);
        const ang = Math.random() * Math.PI * 2;
        for (const k of group) this.spawnQueue.push({ t, type: k, ang });
        t += spread / Math.max(1, Math.ceil((list.length + n) / 4)) * (0.6 + Math.random() * 0.8);
      }
      this.spawnQueue.sort((a, b) => a.t - b.t);
      this.spawnTimer = 0;
      this.waveTimer = this.waveLen;
      this.minerals += 20 + 8 * w;
      this.toast('Wave ' + w + (w % 5 === 0 ? ' - heavy assault!' : ''));
    }
    spawnEnemy(type, ang, at) {
      const d = BS.ZERG[type];
      let x, y;
      if (at) { x = at[0] + rnd(-0.6, 0.6); y = at[1] + rnd(-0.6, 0.6); }
      else if (this.mode === 'lane') { x = rnd(-4.6, 4.6); y = rnd(27, 31); }
      else { const rr = rnd(25, 28); x = Math.cos(ang + rnd(-0.35, 0.35)) * rr; y = Math.sin(ang + rnd(-0.35, 0.35)) * rr; }
      const e = {
        id: Math.random(), type, def: d, x, y, z: d.flying ? d.fly : 0, yaw: Math.atan2(-y, -x), hp: d.hp, maxHp: d.hp,
        state: 'move', target: null, retarget: 0, cd: rnd(0, d.cd), hitAt: -1, slowUntil: 0, radius: d.radius, life: d.life || 0,
        burrowed: false, burrowT: 0, anim: '', handle: this.r.spawn(d.model, x, y, d.flying ? d.fly : 0, Math.atan2(-y, -x)),
      };
      if (d.scale) e.handle.setScale(d.scale);
      e.handle.setTeam(3);
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
        h.play(/^walk$|^walk 01$|^fly/i, true, 1);
        const seq = h.model ? h.model.sequences[h.seqIndex] : null;
        const mv = seq && seq.movementSpeed ? seq.movementSpeed / 100 : d.speed / 1.4;
        h.timeScale = Math.max(0.5, (sp / 1.4) / mv); if (h.inst) h.inst.timeScale = h.timeScale;
      } else if (kind === 'attack') {
        const len = h.play(d.attackSeq || /^attack/i, true, 1);
        const ts = len > 0 ? len / this.enemyCd(e) : 1;
        h.timeScale = ts; if (h.inst) h.inst.timeScale = ts;
      } else if (kind === 'stand') h.play(/^stand$/i, true, 1);
      else if (kind === 'burrow') h.play(/^burrow$/i, false, 1);
    }
    enemyCd(e) { return e.def.cd / (this.hard ? 1.05 : 1); }

    // ---- Targets for zerg ---------------------------------------------------------------------
    // Returns {kind, obj, x, y, radius, armored, light}
    defenderTargets() {
      const list = [{ kind: 'bunker', obj: this.bunker, x: this.bunker.x, y: this.bunker.y, radius: BS.TERRAN.bunker.halfSize, structure: true, armored: true }];
      for (const t of this.tanks) if (t.state === 'ready' && !t.dead) list.push({ kind: 'tank', obj: t, x: t.x, y: t.y, radius: t.radius, armored: true });
      for (const s of this.scvs) list.push({ kind: 'scv', obj: s, x: s.x, y: s.y, radius: s.radius, light: true });
      return list;
    }
    damageDefender(tg, dmg, armorPierce) {
      const o = tg.obj;
      const armor = tg.kind === 'bunker' ? this.bunker.armor : (tg.kind === 'tank' ? o.armor : 0);
      const real = Math.max(0.5, dmg - (armorPierce ? 0 : armor));
      o.hp -= real;
      if (tg.kind === 'bunker') { this.shake = Math.min(1, this.shake + real / 60); if (o.hp <= 0) this.gameOver(); }
      else if (o.hp <= 0 && !o.dead) {
        o.dead = true;
        if (tg.kind === 'tank') { this.tanks.splice(this.tanks.indexOf(o), 1); this.corpse('siegetankDeath', o.x, o.y, 0, 0, 5); BS.audio.play('tankDeath'); this.toast('Siege tank destroyed'); }
        else { this.scvs.splice(this.scvs.indexOf(o), 1); this.corpse('scvDeath', o.x, o.y, 0, o.handle.yaw, 3); this.toast('SCV lost'); }
        o.handle.release();
      }
    }
    corpse(key, x, y, z, yaw, hold) {
      const h = this.r.spawn(key, x, y, z, yaw);
      h.setTeam(/^(scv|siegetank|bunker)/.test(key) ? 0 : 3);
      const c = { h, t: 0, hold: hold || 3.5 };
      this.r.ensureModel(key).then(() => { const names = h.model.sequences.filter(s => /^death/i.test(s.name)); const n = names.length ? pick(names).name : 0; h.play(n, false, 1); }).catch(() => {});
      this.dying.push(c);
    }

    // ---- Combat helpers -------------------------------------------------------------------------
    enemiesInRange(x, y, range, pred) {
      const out = [];
      for (const e of this.enemies) {
        if (e.hp <= 0) continue;
        if (pred && !pred(e)) continue;
        const d = Math.sqrt(dist2(x, y, e.x, e.y)) - e.radius;
        if (d <= range) out.push([d, e]);
      }
      out.sort((a, b) => a[0] - b[0]);
      return out.map(p => p[1]);
    }
    hurtEnemy(e, dmg, src) {
      if (e.hp <= 0) return;
      const real = Math.max(0.5, dmg - e.def.armor);
      e.hp -= real;
      if (e.hp <= 0) this.killEnemy(e, src);
    }
    killEnemy(e, src) {
      e.hp = 0;
      const d = e.def;
      this.kills++; this.minerals += d.bounty; this.score += d.bounty * 2;
      if (d.soundDeath) BS.audio.play(d.soundDeath, { vol: 0.5 });
      if (d.suicide && src !== 'suicide') this.banelingBlast(e, true);
      const idx = this.enemies.indexOf(e); if (idx >= 0) this.enemies.splice(idx, 1);
      e.handle.release();
      const c = this.corpse(d.death, e.x, e.y, 0, e.yaw, d.deathHold || 3.5);
      if (d.scale) c.h.setScale(d.scale);
      if (d.flying) c.h.setPos(e.x, e.y, e.z);
      if (this.focus === e) this.focus = null;
    }
    banelingBlast(e, fromDeath) {
      const d = e.def;
      this.fx.push({ kind: 'blast', x: e.x, y: e.y, t: 0, dur: 0.5, r: d.splash, color: '#9dff4a' });
      for (const tg of this.defenderTargets()) {
        const dd = Math.sqrt(dist2(e.x, e.y, tg.x, tg.y)) - tg.radius;
        if (dd > d.splash) continue;
        const dmg = tg.structure ? d.dmgStructure : (tg.light ? d.dmg + d.dmgLight : d.dmg);
        this.damageDefender(tg, fromDeath ? dmg * 0.5 : dmg);
      }
      BS.audio.play('banelingDeath', { vol: 0.7 });
    }
    floater(x, y, z, text, color) { this.floaters.push({ x, y, z, text, color, t: 0 }); }
    toast(msg) { this.log.push({ msg, t: this.time }); if (this.log.length > 6) this.log.shift(); this.lastToast = { msg, t: this.time }; return false; }

    // ---- Update ---------------------------------------------------------------------------------
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
      for (const k of ['stimCd', 'strikeCd', 'repairCd']) this.ab[k] = Math.max(0, this.ab[k] - dt);
      // Waves
      this.waveTimer -= dt;
      if (this.waveTimer <= 0 || (this.spawnQueue.length === 0 && this.enemies.length === 0 && this.wave > 0 && this.waveTimer < this.waveLen - 6)) {
        if (this.wave > 0 && this.enemies.length === 0) { this.minerals += 10 + 5 * this.wave; this.score += 100 * this.wave; }
        this.startWave();
      }
      this.spawnTimer += dt;
      while (this.spawnQueue.length && this.spawnQueue[0].t <= this.spawnTimer) {
        const s = this.spawnQueue.shift();
        if (this.enemies.length < (this.hard ? 90 : 60)) this.spawnEnemy(s.type, s.ang); else this.spawnQueue.push({ t: this.spawnTimer + 2, type: s.type, ang: s.ang });
      }
      this.updateEnemies(dt);
      this.updateBunker(dt);
      this.updateTanks(dt);
      this.updateScvs(dt);
      this.updateBanshees(dt);
      this.updateProjectiles(dt);
      for (let i = this.dying.length - 1; i >= 0; i--) { const c = this.dying[i]; c.t += dt; if (c.t > c.hold) { c.h.release(); this.dying.splice(i, 1); } }
      for (let i = this.fx.length - 1; i >= 0; i--) { const f = this.fx[i]; f.t += dt; if (f.t > f.dur) this.fx.splice(i, 1); }
      for (let i = this.floaters.length - 1; i >= 0; i--) { const f = this.floaters[i]; f.t += dt; if (f.t > 1.2) this.floaters.splice(i, 1); }
      if (this.focus && (this.focus.hp <= 0 || this.time > this.focusUntil)) this.focus = null;
    }

    updateEnemies(dt) {
      const targets = this.defenderTargets();
      const E = this.enemies;
      // Separation (ground vs ground, air vs air)
      for (let i = 0; i < E.length; i++) {
        const a = E[i]; a.sx = 0; a.sy = 0;
        for (let j = i + 1; j < E.length; j++) {
          const b = E[j];
          if (!!a.def.flying !== !!b.def.flying) continue;
          const dx = b.x - a.x, dy = b.y - a.y, min = a.radius + b.radius + 0.1;
          const d2 = dx * dx + dy * dy;
          if (d2 < min * min && d2 > 1e-6) {
            const d = Math.sqrt(d2), push = (min - d) / d * 0.5;
            a.sx -= dx * push; a.sy -= dy * push; b.sx = (b.sx || 0) + dx * push; b.sy = (b.sy || 0) + dy * push;
          }
        }
      }
      for (let i = E.length - 1; i >= 0; i--) {
        const e = E[i], d = e.def;
        if (e.life) { e.life -= dt; if (e.life <= 0) { this.killEnemy(e, 'timeout'); continue; } }
        // Retarget
        e.retarget -= dt;
        if (!e.target || e.retarget <= 0 || e.target.obj.dead || (e.target.kind !== 'bunker' && !targets.some(t => t.obj === e.target.obj))) {
          let best = null, bd = 1e9;
          for (const t of targets) {
            if (t.kind === 'scv' && Math.random() < 0.5) continue;        // SCVs are not always noticed
            const dd = Math.sqrt(dist2(e.x, e.y, t.x, t.y)) - t.radius;
            if (dd < bd) { bd = dd; best = t; }
          }
          e.target = best; e.retarget = 0.5 + Math.random() * 0.3;
        }
        const t = e.target;
        if (!t) continue;
        const dx = t.x - e.x, dy = t.y - e.y, dd = Math.sqrt(dx * dx + dy * dy);
        const reach = dd - t.radius - e.radius;
        const range = d.range + (t.kind === 'bunker' ? 0.2 : 0);
        const slowed = e.slowUntil > this.time;
        if (d.suicide) {
          if (reach <= range) { this.banelingBlast(e, false); this.killEnemy(e, 'suicide'); continue; }
        }
        if (d.burrows) {
          if (!e.burrowed) {
            if (reach <= range - 1) { e.burrowed = true; e.burrowT = 1.0; this.playAnim(e, 'burrow'); e.state = 'burrowing'; }
          } else { e.burrowT -= dt; }
        }
        const canAttack = reach <= range && !(d.burrows && (!e.burrowed || e.burrowT > 0));
        if (canAttack) {
          e.state = 'attack';
          e.yaw = Math.atan2(dy, dx); e.handle.setYaw(e.yaw);
          if (d.attackSeq) this.playAnim(e, 'attack'); else if (!d.burrows) this.playAnim(e, 'stand');
          e.cd -= dt * (slowed ? 0.8 : 1);
          if (e.cd <= 0) {
            const cd = this.enemyCd(e);
            e.cd += cd;
            e.hitAt = this.time + cd * (d.hitFrac || 0.4);
            e.hitTarget = t;
            if (d.soundAttack) BS.audio.play(d.soundAttack, { vol: 0.35, gap: 0.15 });
            if (d.projectile) this.projectiles.push({ kind: d.projectile, from: e, x: e.x, y: e.y, z: e.z + d.height * 0.6, tx: t.x, ty: t.y, tz: 1.0, t: 0, dur: Math.max(0.15, cd * (d.hitFrac || 0.4)), dmg: d.dmg, def: d, target: t, enemy: e });
            else e.pendingHit = true;
          }
          if (e.pendingHit && this.time >= e.hitAt) {
            e.pendingHit = false;
            const tg = e.hitTarget;
            if (tg && !tg.obj.dead) {
              let dmg = d.dmg;
              if (tg.light && d.dmgLight) dmg += d.dmgLight;
              if (tg.armored && d.dmgArmored) dmg += d.dmgArmored;
              this.damageDefender(tg, dmg);
              if (d.cleave) for (const o of targets) if (o !== tg && Math.sqrt(dist2(tg.x, tg.y, o.x, o.y)) - o.radius < d.cleave) this.damageDefender(o, dmg * 0.5);
            }
          }
        } else if (e.state !== 'burrowing' || !e.burrowed || e.burrowT <= 0 && !d.burrows) {
          if (d.burrows && e.burrowed) { /* wait until burrowed */ }
          else {
            e.state = 'move';
            this.playAnim(e, 'walk');
            const sp = d.speed * (slowed ? 0.5 : 1);
            const nx = dx / (dd || 1), ny = dy / (dd || 1);
            let mx = nx * sp * dt + (e.sx || 0) * 0.8, my = ny * sp * dt + (e.sy || 0) * 0.8;
            e.x += mx; e.y += my;
            if (this.mode === 'lane') e.x = clamp(e.x, -5.2, 5.2);
            e.yaw = Math.atan2(my, mx); e.handle.setYaw(e.yaw);
          }
        }
        if (d.burrows && e.burrowed && e.burrowT > 0) { e.burrowT -= 0; }
        e.handle.setPos(e.x, e.y, e.z);
        e.slowUntil = e.slowUntil;
      }
    }

    // Bunker occupants fire.
    updateBunker(dt) {
      const b = this.bunker;
      if (b.dead) return;
      const stim = this.ab.stimUntil > this.time;
      const bonus = BS.TERRAN.bunker.rangeBonus + BS.TERRAN.bunker.halfSize;
      if (this.up.medivac) for (const u of b.units) u.hp = Math.min(u.maxHp, u.hp + 9 * dt);
      for (const u of b.units) {
        u.cd -= dt * (stim ? 1.5 : 1);
        if (u.cd > 0) continue;
        const d = u.def;
        const range = d.range + bonus;
        const pred = e => !(d.groundOnly && e.def.flying);
        let target = null;
        if (this.focus && pred(this.focus) && this.focus.hp > 0 && Math.sqrt(dist2(b.x, b.y, this.focus.x, this.focus.y)) - this.focus.radius <= range) target = this.focus;
        if (!target) {
          const list = this.enemiesInRange(b.x, b.y, range, pred);
          if (!list.length) { u.cd = 0.05; continue; }
          target = list[0];
          if (u.type === 'marauder') { const arm = list.find(e => e.def.armored); if (arm) target = arm; }
          if (u.type === 'firebat') { const lt = list.find(e => e.def.light); if (lt) target = lt; }
        }
        u.cd += d.cd;
        const dmgBase = d.dmg + this.up.weapons;
        const ang = Math.atan2(target.y - b.y, target.x - b.x);
        const mx = b.x + Math.cos(ang) * 1.5, my = b.y + Math.sin(ang) * 1.5, mz = 1.3;
        if (u.type === 'marine') {
          let dmg = dmgBase;
          this.hurtEnemy(target, dmg, 'marine');
          this.fx.push({ kind: 'tracer', x: mx, y: my, z: mz, tx: target.x, ty: target.y, tz: target.z + target.def.height * 0.5, t: 0, dur: 0.08, color: '#ffe9a8' });
          this.fx.push({ kind: 'flash', x: mx, y: my, z: mz, t: 0, dur: 0.06, r: 0.35, color: '#fff3c0' });
          if (this.time - b.lastShot > 0.12) { this.r.effect('fxMuzzle', mx, my, mz, ang, /^stand/i, 0.45, 0.4); b.lastShot = this.time; }
          BS.audio.play('marineFire', { vol: 0.3, gap: 0.09 });
        } else if (u.type === 'firebat') {
          const victims = this.enemiesInRange(target.x, target.y, d.splash, pred);
          for (const v of victims) this.hurtEnemy(v, dmgBase + (v.def.light ? d.dmgLight : 0), 'firebat');
          this.fx.push({ kind: 'flame', x: mx, y: my, z: 0.8, tx: target.x, ty: target.y, t: 0, dur: 0.45, color: '#ff8a2a' });
          BS.audio.play('firebatFire', { vol: 0.45, gap: 0.3 });
        } else if (u.type === 'marauder') {
          this.projectiles.push({ kind: 'grenade', x: mx, y: my, z: mz, target, t: 0, dur: 0.25, dmg: dmgBase, def: d, friendly: true });
          BS.audio.play('marauderFire', { vol: 0.4, gap: 0.2 });
        }
      }
    }

    updateTanks(dt) {
      const D = BS.TERRAN.tank;
      for (const t of this.tanks) {
        if (t.state === 'incoming') {
          t.t += dt;
          const m = t.medivac;
          const dy = t.y - m.y, dx = t.x - m.x, dd = Math.hypot(dx, dy);
          if (dd > 0.3 && t.t < 10) { const sp = 9 * dt; m.setPos(m.x + dx / dd * sp, m.y + dy / dd * sp, 5.5); m.setYaw(Math.atan2(dy, dx)); }
          else {
            t.state = 'unloading'; t.t = 0;
            m.play('Stand Work Start', false, 1);
            BS.audio.play('medivacUnload', { vol: 0.6 });
          }
        } else if (t.state === 'unloading') {
          t.t += dt;
          if (t.t > 1.6 && !t.handle) {
            t.handle = this.r.spawn('siegetankMorph', t.x, t.y, 0, Math.PI / 2);
            t.handle.setTeam(0); t.handle.play('Morph Start', false, 1);
          }
          if (t.t > 2.2) t.medivac.play('Stand Work End', false, 1);
          if (t.t > 3.6) { t.state = 'leaving'; t.t = 0; t.medivac.play('Walk', true, 1); }
        } else if (t.state === 'leaving') {
          t.t += dt;
          const m = t.medivac;
          m.setPos(m.x, m.y - 10 * dt, 5.5 + t.t * 2); m.setYaw(-Math.PI / 2);
          if (t.t > 3.5) { m.release(); t.medivac = null; }
          if (t.t > 2.0 && t.handle.key === 'siegetankMorph') {
            t.handle.release();
            t.handle = this.r.spawn('siegetank', t.x, t.y, 0, Math.PI / 2);
            t.handle.setTeam(0); t.handle.play('Stand', true, 1);
            t.state = 'ready';
            this.toast('Siege tank in position');
          }
        } else if (t.state === 'ready') {
          if (t.medivac) { t.t += dt; t.medivac.setPos(t.medivac.x, t.medivac.y - 10 * dt, 6 + t.t); if (t.t > 4) { t.medivac.release(); t.medivac = null; } }
          t.cd -= dt;
          if (t.cd > 0) continue;
          const pred = e => !e.def.flying && Math.sqrt(dist2(t.x, t.y, e.x, e.y)) > D.minRange;
          let target = null;
          if (this.focus && pred(this.focus) && this.focus.hp > 0 && Math.sqrt(dist2(t.x, t.y, this.focus.x, this.focus.y)) <= D.range) target = this.focus;
          if (!target) {
            const list = this.enemiesInRange(t.x, t.y, D.range, pred);
            if (!list.length) { t.cd = 0.1; continue; }
            // Prefer the densest cluster.
            let best = list[0], bestN = -1;
            for (const e of list.slice(0, 8)) { const n = this.enemiesInRange(e.x, e.y, 1.1, pred).length; if (n > bestN) { bestN = n; best = e; } }
            target = best;
          }
          t.cd = D.cd;
          const ang = Math.atan2(target.y - t.y, target.x - t.x);
          t.handle.setYaw(ang);
          t.handle.play('Attack', false, 1);
          this.projectiles.push({ kind: 'shell', x: t.x, y: t.y, z: 1.2, tx: target.x, ty: target.y, t: 0, dur: 0.55, dmg: D.dmg + 5 * this.up.vehicle, dmgArmored: D.dmgArmored + 3 * this.up.vehicle, splash: D.splash, friendly: true });
          this.fx.push({ kind: 'flash', x: t.x + Math.cos(ang) * 1.4, y: t.y + Math.sin(ang) * 1.4, z: 1.3, t: 0, dur: 0.12, r: 0.6, color: '#ffe0a0' });
          BS.audio.play('tankFire', { vol: 0.7, gap: 0.2 });
          this.shake = Math.min(1, this.shake + 0.15);
        }
      }
    }

    updateScvs(dt) {
      const D = BS.TERRAN.scv;
      const b = this.bunker;
      for (const s of this.scvs) {
        // Pick something to repair: the bunker if damaged, else a damaged tank.
        let job = null;
        if (b.hp < b.maxHp - 1) job = { x: b.x, y: b.y, r: BS.TERRAN.bunker.halfSize, obj: b, cost: 0.25 };
        else { const t = this.tanks.find(t => t.state === 'ready' && t.hp < t.maxHp - 1); if (t) job = { x: t.x, y: t.y, r: t.radius, obj: t, cost: 0.4 }; }
        let gx, gy, stop;
        if (job) { const ang = Math.atan2(s.idle[1] - job.y, s.idle[0] - job.x); gx = job.x + Math.cos(ang) * (job.r + 0.6); gy = job.y + Math.sin(ang) * (job.r + 0.6); stop = 0.25; }
        else { gx = s.idle[0]; gy = s.idle[1]; stop = 0.3; }
        const dx = gx - s.x, dy = gy - s.y, dd = Math.hypot(dx, dy);
        if (dd > stop) {
          const sp = Math.min(dd, D.speed * dt);
          s.x += dx / dd * sp; s.y += dy / dd * sp;
          s.handle.setYaw(Math.atan2(dy, dx));
          if (s.anim !== 'walk') { s.anim = 'walk'; s.handle.play('Walk', true, 1.4); }
        } else if (job) {
          s.handle.setYaw(Math.atan2(job.y - s.y, job.x - s.x));
          if (s.anim !== 'work') { s.anim = 'work'; s.handle.play('Work', true, 1); BS.audio.play('scvRepair', { vol: 0.25, gap: 1.5 }); }
          const amount = Math.min(D.repairRate * dt, job.obj.maxHp - job.obj.hp, this.minerals / job.cost);
          if (amount > 0) { job.obj.hp += amount; this.minerals -= amount * job.cost; }
        } else if (s.anim !== 'stand') { s.anim = 'stand'; s.handle.play(/^stand$/i, true, 1); }
        s.handle.setPos(s.x, s.y, 0);
      }
    }

    updateBanshees(dt) {
      const D = BS.TERRAN.banshee;
      const lane = this.mode === 'lane';
      const farY = lane ? 30 : 32, homeY = lane ? -14 : -36;
      for (let i = this.banshees.length - 1; i >= 0; i--) {
        const b = this.banshees[i];
        b.t += dt;
        if (b.phase === 'in') { b.y += D.speed * dt; if (b.y >= farY) { b.phase = 'back'; b.yaw = -Math.PI / 2; } }
        else { b.y -= D.speed * dt; if (b.y <= homeY) { b.handle.release(); this.banshees.splice(i, 1); continue; } }
        b.handle.setPos(b.x, b.y, b.z); b.handle.setYaw(b.yaw);
        b.cd -= dt;
        if (b.cd <= 0) {
          const list = this.enemiesInRange(b.x, b.y, D.range, e => !e.def.flying);
          if (list.length) {
            b.cd = D.cd;
            const tg = list[0];
            for (let k = 0; k < D.shots; k++) this.projectiles.push({ kind: 'rocket', x: b.x + (k ? 0.4 : -0.4), y: b.y, z: b.z - 0.3, target: tg, t: 0, dur: 0.3, dmg: D.dmg, friendly: true });
            b.handle.play('Attack', false, 1);
            BS.audio.play('bansheeFire', { vol: 0.5, gap: 0.2 });
          } else b.cd = 0.1;
        }
      }
    }

    updateProjectiles(dt) {
      const P = this.projectiles;
      for (let i = P.length - 1; i >= 0; i--) {
        const p = P[i];
        p.t += dt;
        if (p.target) { p.tx = p.target.x; p.ty = p.target.y; p.tz = p.target.z + p.target.def.height * 0.5; }
        if (p.t < p.dur) continue;
        P.splice(i, 1);
        if (p.friendly) {
          if (p.kind === 'shell') {
            const [r1, r2, r3] = p.splash;
            for (const e of this.enemiesInRange(p.tx, p.ty, r3 + 0.5, e => !e.def.flying)) {
              const dd = Math.sqrt(dist2(p.tx, p.ty, e.x, e.y)) - e.radius;
              const mult = dd <= r1 ? 1 : dd <= r2 ? 0.5 : dd <= r3 ? 0.25 : 0;
              if (mult > 0) this.hurtEnemy(e, (p.dmg + (e.def.armored ? p.dmgArmored : 0)) * mult, 'tank');
            }
            this.fx.push({ kind: 'blast', x: p.tx, y: p.ty, t: 0, dur: 0.6, r: 1.6, color: '#ffb347' });
            this.fx.push({ kind: 'blast', x: p.tx, y: p.ty, t: 0, dur: 0.25, r: 0.9, color: '#ffffff' });
            BS.audio.play('tankHit', { vol: 0.6, gap: 0.1 });
            this.shake = Math.min(1, this.shake + 0.1);
          } else if (p.kind === 'grenade') {
            if (p.target.hp > 0) { this.hurtEnemy(p.target, p.dmg + (p.target.def.armored ? p.def.dmgArmored : 0), 'marauder'); p.target.slowUntil = this.time + p.def.slow; }
            this.fx.push({ kind: 'blast', x: p.tx, y: p.ty, t: 0, dur: 0.3, r: 0.6, color: '#8fd3ff' });
          } else if (p.kind === 'rocket') {
            if (p.target.hp > 0) this.hurtEnemy(p.target, p.dmg, 'banshee');
            this.fx.push({ kind: 'blast', x: p.tx, y: p.ty, t: 0, dur: 0.35, r: 0.7, color: '#ffd080' });
            BS.audio.play('bansheeHit', { vol: 0.35, gap: 0.1 });
          }
        } else {
          const tg = p.target; const d = p.def;
          if (tg && !tg.obj.dead) {
            let dmg = d.dmg;
            if (tg.light && d.dmgLight) dmg += d.dmgLight;
            if (tg.armored && d.dmgArmored) dmg += d.dmgArmored;
            if (p.kind === 'spines') {
              // Line damage from the lurker to the target.
              const fx = p.enemy.x, fy = p.enemy.y, ex = tg.x, ey = tg.y, L = Math.hypot(ex - fx, ey - fy) || 1;
              for (const o of this.defenderTargets()) {
                const px = o.x - fx, py = o.y - fy, proj = clamp((px * (ex - fx) + py * (ey - fy)) / (L * L), 0, 1);
                const cx = fx + (ex - fx) * proj, cy = fy + (ey - fy) * proj;
                if (Math.hypot(o.x - cx, o.y - cy) - o.radius < 0.6) this.damageDefender(o, dmg);
              }
              this.fx.push({ kind: 'spines', x: fx, y: fy, tx: ex, ty: ey, t: 0, dur: 0.5, color: '#c9ff7a' });
            } else {
              this.damageDefender(tg, dmg);
              if (d.bounce) { const o = this.defenderTargets().find(o => o !== tg && Math.sqrt(dist2(tg.x, tg.y, o.x, o.y)) < 3.5); if (o) this.damageDefender(o, d.bounce); }
              if (d.broodlings && this.enemies.length < 80) for (let k = 0; k < 2; k++) this.spawnEnemy('broodling', 0, [tg.x + rnd(-1, 1), tg.y + rnd(-1, 1) + 1.5]);
              this.fx.push({ kind: 'blast', x: tg.x, y: tg.y, t: 0, dur: 0.25, r: 0.5, color: p.kind === 'acid' ? '#b7ff5a' : '#ffd0a0' });
            }
          }
        }
      }
    }

    focusAt(wx, wy) {
      let best = null, bd = 4;
      for (const e of this.enemies) { const d = Math.sqrt(dist2(wx, wy, e.x, e.y)) - e.radius; if (d < bd) { bd = d; best = e; } }
      if (best) { this.focus = best; this.focusUntil = this.time + 4; return true; }
      return false;
    }

    gameOver() {
      if (this.over) return;
      this.over = true; this.bunker.dead = true; this.bunker.hp = 0;
      this.bunker.handle.release();
      this.corpse('bunkerDeath', this.bunker.x, this.bunker.y, 0, 0, 30);
      BS.audio.play('explosion', { vol: 0.9 });
      this.shake = 1;
      this.finalScore = Math.round(this.score + this.kills * 10 + this.wave * 250);
      try {
        const key = 'bs.best.' + this.mode;
        const best = parseInt(localStorage.getItem(key) || '0', 10);
        this.best = Math.max(best, this.finalScore);
        localStorage.setItem(key, String(this.best));
        this.newBest = this.finalScore > best;
      } catch (e) { this.best = this.finalScore; }
    }
  }

  BS.Game = Game;
})(window.BS);
