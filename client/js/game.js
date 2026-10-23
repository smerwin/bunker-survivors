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
      this.tanks = []; this.scvs = []; this.banshees = []; this.squad = []; this.structures = []; this.squadDeaths = []; this.boundTeam = 0; this.boundT = 0; this.nextTeam = 0;
      this.up = { stim: 0, weapons: 0, vehicle: 0, neosteel: 0, plating: 0, hisec: 0, medivac: 0, rails: 0 };
      this.ab = { stimUntil: 0, stimCd: 0, strikeCd: 0, repairCd: 0, yamatoCd: 0 };
      this.techUnlocked = {}; this.techCd = { nuke: 0, vikings: 0 };
      this.mechs = []; this.bc = null; this.vikingWing = []; this.nukes = []; this.beams = [];
      this.station = null;
      BS.Terrain.setMode(this.mode);
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
        this.samSlots = [[-5.6, 1.2], [5.6, 1.2], [-3.6, -4.6]];
        this.sensorSlot = [3.6, -4.6];
        this.strikePosts = [[-3.4, 3.6], [3.4, 3.6], [-1.4, 6.4], [1.4, 6.4]];   // in view: covering both ledges and the stem
        // High-tech walkers: goliaths at the bunker's shoulders; the thors and the odin are too big to stand in the lane
        // without walling off the view, so they fire long-range from behind the bunker.
        this.mechSlots = { goliath: [[-3.9, 1.6], [3.9, 1.6], [-2.3, 2.3], [2.3, 2.3]], thor: [[-5.0, -5.8], [5.0, -5.8]], odin: [[0, -10.5]] };
      } else {
        this.tankSlots = [[-3.0, -2.6], [3.0, -2.6], [-3.0, 2.6], [3.0, 2.6]];
        this.scvIdle = [[-2.6, -2.6], [2.6, 2.6], [-2.6, 2.6], [2.6, -2.6]];
        this.tankMax = T().tank.maxArena;
        this.samSlots = [[0, 4.0], [-4.0, 0], [4.0, 0]];
        this.sensorSlot = [-4.2, -4.2];
        this.strikePosts = [45, 135, 225, 315].map(a => [Math.cos(a * Math.PI / 180) * 7, Math.sin(a * Math.PI / 180) * 7]);
        this.mechSlots = { goliath: [[0, 2.7], [2.7, 0], [-2.7, 0], [0, -2.7]], thor: [[-4.6, -3.6], [4.6, 3.6]], odin: [[-5.2, 3.2]] };
      }
      // Ranks of eight behind the bunker (the side away from the lane), stepping round the tanks, SCVs, turrets and
      // sensor tower, as deep as it takes to fit the whole squad.
      const taken = this.tankSlots.concat(this.scvIdle, this.samSlots, [this.sensorSlot], ...Object.values(this.mechSlots));
      this.squadSpots = [];
      for (let y = -2.3; this.squadSpots.length < T().squad.max; y -= 0.7)
        for (let i = 0; i < 8 && this.squadSpots.length < T().squad.max; i++) {
          const x = -2.625 + i * 0.75;
          if (!taken.some(([tx, ty]) => Math.hypot(tx - x, ty - y) < 1.0)) this.squadSpots.push([x, y]);
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
    troopsFull() { return this.bunker.units.length + this.squad.length + this.pendingInfantry() >= this.bunker.slots + T().squad.max; }

    addInfantry(type, free) {
      const d = BS.INFANTRY[type];
      if (this.troopsFull()) return this.toast('No room for more troops');
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
        // Bunker full: fall in behind it instead.
        if (!this.bunker.dead && this.bunker.units.length + this.walkers.filter(o => o !== w && o.delay <= 0 && !o.outside).length >= this.bunker.slots && this.squad.length < T().squad.max) {
          w.outside = true;
          this.joinSquad(w.type, w.def.hp, w.x, w.y, w.handle);
          this.walkers.splice(i, 1); continue;
        }
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

    // One step for a ground unit: straight on if it can, otherwise the nearest heading off the cliff face, so nothing
    // hangs on a corner. Returns false when boxed in.
    stepGround(o, mx, my) {
      const open = (x, y) => BS.Terrain.walkable(x, y);
      if (open(o.x + mx, o.y + my)) { o.x += mx; o.y += my; return true; }
      for (const a of [0.5, -0.5, 1.0, -1.0, Math.PI / 2, -Math.PI / 2, 2.1, -2.1]) {
        const c = Math.cos(a), sn = Math.sin(a), k = Math.max(0.35, Math.cos(a)), rx = (mx * c - my * sn) * k, ry = (mx * sn + my * c) * k;
        if (open(o.x + rx, o.y + ry)) { o.x += rx; o.y += ry; return true; }
      }
      // Wedged on a slope: step down it, back towards the low ground.
      const e = 0.2, gx = BS.Terrain.height(o.x + e, o.y) - BS.Terrain.height(o.x - e, o.y), gy = BS.Terrain.height(o.x, o.y + e) - BS.Terrain.height(o.x, o.y - e), gl = Math.hypot(gx, gy);
      if (gl > 1e-4) { const L = Math.hypot(mx, my); o.x -= gx / gl * L; o.y -= gy / gl * L; return true; }
      return false;
    }

    // Troopers path round the bunker: straight to the goal if the way is clear, else via the corner that makes the
    // shortest clear route.
    roundBunker(m, gx, gy) {
      const B = T().bunker.halfSize + 0.25, C = B + 0.3;
      const hits = (ax, ay, bx, by) => {   // segment against the box (slab test)
        let t0 = 0, t1 = 1;
        const dx = bx - ax, dy = by - ay;
        for (const [p, q] of [[-dx, ax + B], [dx, B - ax], [-dy, ay + B], [dy, B - ay]]) {
          if (Math.abs(p) < 1e-9) { if (q < 0) return false; continue; }
          const r = q / p;
          if (p < 0) { if (r > t1) return false; if (r > t0) t0 = r; } else { if (r < t0) return false; if (r < t1) t1 = r; }
        }
        return t0 <= t1;
      };
      if ((Math.abs(m.x) < B && Math.abs(m.y) < B) || !hits(m.x, m.y, gx, gy)) return [gx, gy];
      let best = null, bl = 1e9;
      for (const [cx, cy] of [[C, C], [-C, C], [C, -C], [-C, -C]]) {
        if (hits(m.x, m.y, cx, cy)) continue;
        const l = dist(m.x, m.y, cx, cy) + dist(cx, cy, gx, gy) + (hits(cx, cy, gx, gy) ? 2 * C : 0);
        if (l < bl) { bl = l; best = [cx, cy]; }
      }
      return best || [gx, gy];
    }

    // ---- Squad behind the bunker ----------------------------------------------------------------------
    freeSquadSpot() { return this.squadSpots.find(sp => !this.squad.some(m => m.spot === sp)); }
    joinSquad(type, hp, x, y, handle) {
      const def = BS.INFANTRY[type], spot = this.freeSquadSpot();
      if (!spot) { if (handle) handle.release(); return null; }
      const m = { type, def, hp, maxHp: def.hp, cd: Math.random() * 0.3, x, y, yaw: -Math.PI / 2, spot, boarding: false, anim: '', dead: false, flash: 0, radius: 0.375,
        team: this.nextTeam, retreatUntil: 0 };
      this.nextTeam ^= 1;                       // alternate fire teams
      m.handle = handle || this.r.spawn(type, x, y, 0, m.yaw).setTeam(1);
      this.squad.push(m);
      return m;
    }
    // Send a trooper out of the bunker's side hatch to stand behind it.
    eject(i) {
      const b = this.bunker, u = b.units[i];
      if (!u || b.dead) return false;
      if (this.squad.length >= T().squad.max) return this.toast('No room behind the bunker');
      b.units.splice(i, 1);
      const side = this.squad.filter(m => m.x < 0).length <= this.squad.filter(m => m.x >= 0).length ? -1 : 1;
      this.joinSquad(u.type, u.hp, side * (T().bunker.halfSize + 0.3), 0.4);
      if (!b.units.length) b.handle.play(/^stand$/i, true);
      this.floater(0, 0, 3, u.def.name + ' out', '#7fd8ff');
      return true;
    }
    board(i) {
      const m = this.squad[i];
      if (!m || m.dead) return false;
      if (this.bunker.units.length >= this.bunker.slots) return this.toast('Bunker is full');
      m.boarding = !m.boarding;
      return true;
    }
    updateSquad(dt) {
      const hs = T().bunker.halfSize, stim = this.ab.stimUntil > this.time, Q = this.squad;
      // Keep a little elbow room so troopers converging on one spot do not stack. Anyone standing on their own spot is
      // already spaced out and lets others through the ranks.
      const home = m => Math.abs(m.x - m.spot[0]) + Math.abs(m.y - m.spot[1]) < 0.3;
      for (let i = 0; i < Q.length; i++) for (let j = i + 1; j < Q.length; j++) {
        const p = Q[i], q = Q[j], dx = q.x - p.x, dy = q.y - p.y, d2 = dx * dx + dy * dy;
        if (d2 >= 0.49 || home(p) || home(q)) continue;
        if (d2 < 0.49 && d2 > 1e-6) { const d = Math.sqrt(d2), push = (0.7 - d) / d * 0.25; p.x -= dx * push; p.y -= dy * push; q.x += dx * push; q.y += dy * push; }
      }
      // Fire and movement: when the squad advances, its two fire teams take turns - one bounds forward while the
      // other holds and gives covering fire.
      this.boundT += dt;
      if (this.boundT > 1.4) { this.boundT = 0; this.boundTeam ^= 1; }
      const bothTeams = Q.some(m => m.team === 0) && Q.some(m => m.team === 1);
      this.squadDeaths = this.squadDeaths.filter(k => this.time - k.t < 6);
      const lane = this.mode === 'lane';
      const forward = m => lane ? m.y > 0.6 : Math.hypot(m.x, m.y) > 3.2;     // ahead of the bunker's line
      for (const m of Q.slice()) {
        if (m.dead) continue;
        const d = m.def, pred = e => !(d.groundOnly && e.def.flying);
        // Morale: when too many of its neighbours have just been killed, the group falls back level with the bunker.
        if (!m.boarding && m.retreatUntil <= this.time && forward(m)) {
          const lost = this.squadDeaths.filter(k => dist(k.x, k.y, m.x, m.y) < 3.5).length;
          const near = Q.filter(o => !o.dead && dist(o.x, o.y, m.x, m.y) < 3.5);
          if (lost >= 2 && lost >= (lost + near.length) / 3) {      // a third of the group gone
            for (const o of near) o.retreatUntil = this.time + 8;
            this.toast('Squad falling back');
          }
        }
        let gx = m.spot[0], gy = m.spot[1];
        if (m.boarding) { gx = (m.x < 0 ? -1 : 1) * (hs + 0.3); gy = 0.4; }
        else if (m.retreatUntil > this.time) {
          if (!forward(m)) { gx = m.x; gy = m.y; }                                    // back on the line: hold and fire
          else if (lane) { gx = clamp(m.x, -6, 6); gx = (gx < 0 ? -1 : 1) * Math.max(Math.abs(gx), hs + 0.8); gy = 0.4; }
          else { const a = Math.atan2(m.y, m.x); gx = Math.cos(a) * 2.8; gy = Math.sin(a) * 2.8; }
        }
        else if (this.enemiesInRange(m.x, m.y, d.range, pred).length) { gx = m.x; gy = m.y; }   // hold and fire
        else {
          // Advance on the nearest zerg that is attacking our position from beyond our reach.
          let th = null, best = 1e9;
          for (const e of this.enemies) {
            if (e.hp <= 0 || !e.attacking || !pred(e) || dist(0, 0, e.x, e.y) > 14) continue;
            const dd = dist(m.x, m.y, e.x, e.y);
            if (dd < best) { best = dd; th = e; }
          }
          if (th && bothTeams && m.team !== this.boundTeam) { gx = m.x; gy = m.y; }   // covering while the other team bounds
          else if (th) { const a = Math.atan2(m.y - th.y, m.x - th.x), r = d.range * 0.8 + th.radius; gx = th.x + Math.cos(a) * r; gy = th.y + Math.sin(a) * r; }
        }
        const far = Math.hypot(gx - m.x, gy - m.y);
        [gx, gy] = this.roundBunker(m, gx, gy);
        const dx = gx - m.x, dy = gy - m.y, dd = Math.hypot(dx, dy) || 1;
        if (far > 0.12) {
          const sp = Math.min(dd, 3.15 * dt), ox = m.x, oy = m.y;
          this.stepGround(m, dx / dd * sp, dy / dd * sp);
          if (m.x !== ox || m.y !== oy) m.yaw = Math.atan2(m.y - oy, m.x - ox);
          if (m.anim !== 'walk') { m.anim = 'walk'; m.handle.play(/^walk/i, true, 1.2); }
          m.handle.setPos(m.x, m.y, BS.Terrain.height(m.x, m.y)).setYaw(m.yaw);
          continue;
        }
        if (m.boarding) {
          m.boarding = false;
          if (this.bunker.units.length < this.bunker.slots && !this.bunker.dead) {
            this.bunker.units.push({ type: m.type, def: m.def, hp: m.hp, maxHp: m.maxHp, cd: m.cd });
            this.bunker.handle.play(/^stand work/i, true);
            m.handle.release(); this.squad.splice(this.squad.indexOf(m), 1);
            this.floater(0, 0, 3, '+' + m.def.name, '#7fd8ff');
          } else this.toast('Bunker is full');
          continue;
        }
        // Holding position: fire at whatever comes into (unboosted) range.
        m.cd -= dt * (stim ? 1.5 : 1);
        if (m.cd <= 0) {
          let target = null;
          if (this.focus && this.focus.hp > 0 && pred(this.focus) && dist(m.x, m.y, this.focus.x, this.focus.y) - this.focus.radius <= d.range) target = this.focus;
          if (!target) target = this.enemiesInRange(m.x, m.y, d.range, pred)[0];
          if (target) {
            m.cd += d.cd;
            m.yaw = Math.atan2(target.y - m.y, target.x - m.x);
            const cs = Math.cos(m.yaw), sn = Math.sin(m.yaw);
            this.fireInfantry(m, m.x + cs * 0.45, m.y + sn * 0.45, BS.Terrain.height(m.x, m.y) + 0.85, target);
            m.anim = 'attack'; m.standAt = this.time + Math.min(m.def.cd / (stim ? 1.5 : 1), m.handle.play(/^attack/i, false, stim ? 2.1 : 1.4) || 0.5);
          } else m.cd = 0.1;
        }
        if (m.anim === 'walk' || (m.anim === 'attack' && this.time > m.standAt)) { m.anim = 'stand'; m.handle.play(/^stand$/i, true); }
        m.handle.setPos(m.x, m.y, BS.Terrain.height(m.x, m.y)).setYaw(m.yaw);
      }
    }
    // A trooper dies: behind the bunker it falls with SC2's death animation; inside, it is carried out.
    killTrooper(u, why) {
      const b = this.bunker;
      if (b.units.includes(u)) b.units.splice(b.units.indexOf(u), 1);
      else {
        u.dead = true;
        this.squad.splice(this.squad.indexOf(u), 1);
        this.squadDeaths.push({ x: u.x, y: u.y, t: this.time });
        u.handle.release();
        this.corpse(u.def.death, u.x, u.y, BS.Terrain.height(u.x, u.y), u.yaw, 3.5, 1, true);
      }
      this.toast(u.def.name + (why === 'stim' ? ' overdosed on stim' : ' down'));
      this.floater(u.x || 0, u.y || 0, 2.5, why === 'stim' ? 'OVERDOSE' : 'KIA', '#ff6a5a');
    }

    // ---- Structures: missile turrets and the sensor tower -----------------------------------------------
    samCount() { return this.structures.filter(s => s.kind === 'sam').length; }
    sensorUp() { return this.structures.some(s => s.kind === 'sensor' && s.state === 'ready'); }
    buildStructure(kind) {
      const D = T()[kind];
      let slot;
      if (kind === 'sam') {
        if (this.samCount() >= D.max) return this.toast('Turret limit reached');
        slot = this.samSlots.find(sl => !this.structures.some(s => s.x === sl[0] && s.y === sl[1]));
      } else {
        if (this.structures.some(s => s.kind === 'sensor')) return this.toast('Sensor tower already built');
        slot = this.sensorSlot;
      }
      if (!slot) return this.toast('No room');
      if (this.minerals < D.cost) return this.toast('Not enough minerals');
      this.minerals -= D.cost;
      const st = { kind, x: slot[0], y: slot[1], hp: D.hp * 0.1, maxHp: D.hp, armor: D.armor, radius: D.radius, cd: 0, state: 'building', t: 0, dead: false, flash: 0, yaw: Math.PI / 2 };
      st.handle = this.r.spawn(kind === 'sam' ? 'missileturret' : 'sensortower', st.x, st.y, BS.Terrain.height(st.x, st.y), st.yaw).setTeam(1);
      st.handle.play(/^birth/i, false);
      this.structures.push(st);
      this.toast(kind === 'sam' ? 'Building missile turret' : 'Building sensor tower');
      return true;
    }
    updateStructures(dt) {
      const D = T().sam;
      for (const st of this.structures) {
        if (st.dead) continue;
        if (st.state === 'building') {
          const B = T()[st.kind];
          st.t += dt; st.hp = Math.min(st.maxHp, st.hp + st.maxHp * 0.9 / B.build * dt);
          if (st.t >= B.build) { st.state = 'ready'; st.handle.play(/^stand$/i, true); this.toast(st.kind === 'sam' ? 'Missile turret online' : 'Sensor tower online: radar active'); }
          continue;
        }
        if (st.kind !== 'sam') continue;
        st.cd -= dt;
        if (st.cd > 0) continue;
        const target = this.enemiesInRange(st.x, st.y, D.range, e => e.def.flying)[0];
        if (!target) { st.cd = 0.1; continue; }
        st.cd = D.cd;
        st.yaw = Math.atan2(target.y - st.y, target.x - st.x);
        st.handle.setYaw(st.yaw); st.handle.play(/^attack/i, false);
        for (let k = 0; k < D.shots; k++) {
          const ox = -Math.sin(st.yaw) * (k ? 0.35 : -0.35), oy = Math.cos(st.yaw) * (k ? 0.35 : -0.35);
          const p = { kind: 'sam', friendly: true, fx: st.x + ox, fy: st.y + oy, fz: 1.6, tx: target.x, ty: target.y, tz: target.z, t: -k * 0.12, dur: 0.4, dmg: D.dmg, target, impact: 'samImpact' };
          this.projectileModel(p, 'projSam', 'samMissile');
          this.projectiles.push(p);
        }
        BS.audio.play('samFire', { vol: 0.45, gap: 0.2 });
      }
    }
    destroyStructure(st) {
      st.dead = true;
      st.handle.release();
      this.structures.splice(this.structures.indexOf(st), 1);
      this.corpse(st.kind === 'sam' ? 'missileturretDeath' : 'sensortowerDeath', st.x, st.y, 0, st.yaw, 4, 1, true);
      BS.audio.play('explosion', { vol: 0.6 });
      this.toast(st.kind === 'sam' ? 'Missile turret destroyed' : 'Sensor tower destroyed: radar offline');
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
      // First slot no living tank is using (a replacement for a destroyed tank goes where the old one stood).
      const slot = this.tankSlots.find(sl => !this.tanks.some(t => !t.dead && Math.hypot(t.x - sl[0], t.y - sl[1]) < 0.5));
      if (!slot) return this.toast('No room for more tanks');
      this.minerals -= cost;
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
      if (id === 'rails' && !this.up.hisec) return false;
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
        case 'rails': this.toast('Magnetic rails fitted: +2 bunker range'); break;
      }
      this.checkTech();
      return true;
    }

    // ---- High-tech reinforcements -------------------------------------------------------------------------
    // Surprise cards: each appears once its tech tree is maxed out (see BS.TECH).
    treeMaxed(tree) {
      const maxed = id => this.upgradeLevel(id) >= BS.UPGRADES[id].costs.length;
      if (tree === 'systems') return ['stim', 'neosteel', 'hisec', 'rails', 'medivac'].every(maxed);
      if (tree === 'all') return ['weapons', 'vehicle', 'plating', 'systems'].every(t => this.treeMaxed(t));
      if (tree === 'bc') return !!this.bc;
      return maxed(tree);
    }
    checkTech() {
      for (const c of BS.TECH) {
        if (this.techUnlocked[c.id] || !this.treeMaxed(c.unlock)) continue;
        this.techUnlocked[c.id] = true;
        this.waveBanner = { text: 'NEW TECH  //  ' + c.banner, t: this.time };
        this.toast(c.name + ' unlocked');
      }
    }
    // For the armoury: '' (buyable), 'max' (all out) or 'wait' (cooling down), plus the price.
    techCount(kind) { return this.mechs.filter(m => m.kind === kind).length; }
    techState(id) {
      if (id === 'nuke' || id === 'vikings') return { state: this.techCd[id] > 0 ? 'wait' : '', cost: T()[id].cost, wait: this.techCd[id] };
      if (id === 'bc') return { state: this.bc ? 'max' : '', cost: T().bc.cost };
      return { state: this.techCount(id) >= T()[id].max ? 'max' : '', cost: T()[id].cost };
    }
    buyTech(id) {
      if (!this.techUnlocked[id]) return false;
      if (id === 'nuke') return this.launchNuke();
      if (id === 'vikings') return this.callVikings();
      if (id === 'bc') return this.callBattlecruiser();
      return this.addMech(id);
    }
    // Test helper (?tech=max): research every upgrade (with a tank on the field so Vehicle Weapons can be bought).
    grantAllTech() {
      this.minerals += 1e6;
      if (!this.tanks.length) this.dropTank();
      for (const id of Object.keys(BS.UPGRADES)) for (let guard = 0; guard < 20 && this.upgradeAvailable(id); guard++) this.buyUpgrade(id);
      this.minerals += 20000;
    }

    // Goliaths, thors and the odin walk in from the south to their posts, then fight with separate anti-ground and
    // anti-air weapons.
    addMech(kind) {
      const D = T()[kind];
      if (this.techCount(kind) >= D.max) return this.toast('No room for more');
      const slot = this.mechSlots[kind].find(sl => !this.mechs.some(m => m.slot === sl));
      if (!slot) return this.toast('No room for more');
      if (this.minerals < D.cost) return this.toast('Not enough minerals');
      this.minerals -= D.cost;
      const m = { kind, def: D, slot, x: slot[0], y: this.mode === 'lane' ? -9 : -14, yaw: Math.PI / 2, hp: D.hp, maxHp: D.hp, armor: D.armor, radius: D.radius,
        state: 'walking', cdG: 0.5, cdA: 0.5, dead: false, flash: 0, anim: '', aim: Math.PI / 2 };
      m.handle = this.r.spawn(kind, m.x, m.y, BS.Terrain.height(m.x, m.y), m.yaw).setTeam(1);
      this.mechs.push(m);
      this.toast(BS.TECH.find(c => c.id === kind).name + ' inbound');
      return true;
    }
    updateMechs(dt) {
      for (const m of this.mechs.slice()) {
        if (m.dead) continue;
        const D = m.def;
        if (m.state === 'walking') {
          const [gx, gy] = this.roundBunker(m, m.slot[0], m.slot[1]);
          const far = Math.hypot(m.slot[0] - m.x, m.slot[1] - m.y), dx = gx - m.x, dy = gy - m.y, dd = Math.hypot(dx, dy) || 1;
          if (far > 0.15) {
            const sp = Math.min(dd, D.speed * dt);
            this.stepGround(m, dx / dd * sp, dy / dd * sp);
            m.yaw += clamp(angDelta(m.yaw, Math.atan2(dy, dx)), -3 * dt, 3 * dt);
            if (m.anim !== 'walk') { m.anim = 'walk'; m.handle.play(/^walk/i, true); }
          } else { m.state = 'ready'; m.anim = 'stand'; m.handle.play(/^stand$/i, true); this.toast(BS.TECH.find(c => c.id === m.kind).name + ' in position'); }
          m.handle.setPos(m.x, m.y, BS.Terrain.height(m.x, m.y)).setYaw(m.yaw);
          continue;
        }
        const bonus = this.up.vehicle * 2;
        m.cdG -= dt; m.cdA -= dt;
        for (const air of [false, true]) {
          if ((air ? m.cdA : m.cdG) > 0) continue;
          const range = air ? D.aRange : D.gRange, pred = e => !!e.def.flying === air;
          let target = this.focus && this.focus.hp > 0 && pred(this.focus) && dist(m.x, m.y, this.focus.x, this.focus.y) - this.focus.radius <= range ? this.focus : null;
          if (!target) target = this.enemiesInRange(m.x, m.y, range, pred)[0];
          if (!target) { if (air) m.cdA = 0.15; else m.cdG = 0.15; continue; }
          if (air) m.cdA = D.aCd; else m.cdG = D.gCd;
          m.aim = Math.atan2(target.y - m.y, target.x - m.x);
          this.mechFire(m, target, air, (air ? D.aDmg : D.gDmg) + bonus);
        }
        m.yaw += clamp(angDelta(m.yaw, m.aim), -4 * dt, 4 * dt);
        m.handle.setPos(m.x, m.y, BS.Terrain.height(m.x, m.y)).setYaw(m.yaw);
      }
    }
    // One volley: goliath autocannons and hellfire missiles, thor arm cannons and javelin barrage, odin heavy cannons
    // and missile salvos.
    mechFire(m, target, air, dmg) {
      const D = m.def, h = BS.Terrain.height(m.x, m.y), ang = m.aim, cs = Math.cos(ang), sn = Math.sin(ang);
      const mz = h + (m.kind === 'odin' ? 4 : m.kind === 'thor' ? 3 : 1.4), shots = air ? D.aShots : D.gShots;
      const hx = target.x, hy = target.y, hz = target.z + target.def.height * 0.5;
      m.handle.play(/^attack/i, false); m.anim = 'attack';
      if (m.kind === 'goliath' && !air) {
        for (let k = 0; k < shots; k++) {
          const off = k ? 0.35 : -0.35, sx = m.x + cs * 0.7 - sn * off, sy = m.y + sn * 0.7 + cs * off;
          this.fx.play('marineLaunch', sx, sy, mz, ang, 2, k * 0.12);
          this.fx.play('marineImpact', hx + rnd(-0.2, 0.2), hy + rnd(-0.2, 0.2), hz, ang + Math.PI, 1.6, k * 0.12 + 0.04);
          this.hurtEnemy(target, dmg);
        }
        BS.audio.play('goliathFire', { vol: 0.45, gap: 0.15 });
        return;
      }
      const look = {
        goliath: { trail: 'goliathAirMissile', impact: 'goliathAirImpact', launch: null, splash: 0 },
        thor: air ? { trail: 'thorShoulderMissile', impact: 'thorShoulderImpact', launch: 'thorShoulderLaunch', splash: D.aSplash }
                  : { trail: 'thorArmMissile', impact: 'thorArmImpact', launch: 'thorArmLaunch', splash: 0 },
        odin: air ? { trail: 'thorShoulderMissile', impact: 'odinShoulderImpact', launch: 'thorShoulderLaunch', splash: D.aSplash }
                  : { trail: null, impact: 'tankImpact', launch: 'tankLaunch', splash: D.gSplash, scale: 1.6, arc: 3 },
      }[m.kind];
      const pred = e => !!e.def.flying === air;
      for (let k = 0; k < shots; k++) {
        const off = (k - (shots - 1) / 2) * 0.45, sx = m.x + cs * 0.6 - sn * off, sy = m.y + sn * 0.6 + cs * off;
        if (look.launch) this.fx.play(look.launch, sx, sy, mz, ang, look.scale || 1, k * 0.08);
        const p = { kind: 'missile', friendly: true, fx: sx, fy: sy, fz: mz, tx: hx, ty: hy, tz: hz, t: -k * 0.08, dur: 0.45 + (look.arc ? 0.15 : 0), arc: look.arc || (air ? 0.8 : 0.4),
          dmg, target, impact: look.impact, impactScale: look.scale || 1, splash: look.splash, pred };
        if (look.trail) this.projectileModel(p, null, look.trail);
        this.projectiles.push(p);
      }
      BS.audio.play(m.kind === 'goliath' ? 'goliathFire' : 'thorFire', { vol: m.kind === 'odin' ? 0.8 : 0.5, gap: 0.15 });
      if (m.kind === 'odin' && !air) this.shake = Math.min(1, this.shake + 0.2);
    }
    killMech(m) {
      m.dead = true;
      this.mechs.splice(this.mechs.indexOf(m), 1);
      m.handle.release();
      this.corpse(m.kind + 'Death', m.x, m.y, BS.Terrain.height(m.x, m.y), m.yaw, 6, 1, true);
      BS.audio.play('explosion', { vol: 0.8 });
      this.toast(BS.TECH.find(c => c.id === m.kind).name + ' destroyed');
    }

    // Battlecruiser on station: flies in over the lane and stays, out of reach.
    callBattlecruiser() {
      const D = T().bc;
      if (this.bc) return this.toast('The battlecruiser is already on station');
      if (this.minerals < D.cost) return this.toast('Not enough minerals');
      this.minerals -= D.cost;
      const lane = this.mode === 'lane';
      // On station over the tip of the mesa where the lane forks (the bottom of the V), nose to the south, a little scaled
      // down: it overlooks both arms and the stem without hiding the lane.
      this.bc = { x: 0, y: lane ? -24 : -34, z: 10, sx: 0, sy: lane ? 13 : 0, sz: lane ? 6.5 : 8, yaw: Math.PI / 2, state: 'in', cdM: 3, cdY: 5, t: 0,
        turrets: D.turrets.map((bone, i) => ({ bone, cd: 0.3 + i * 0.2, shots: 0, target: null })) };
      this.bc.handle = this.r.spawn('battlecruiser', this.bc.x, this.bc.y, this.bc.z, this.bc.yaw).setTeam(1).setScale(0.75);
      this.bc.handle.play(/^walk|^fly|^stand/i, true);
      this.toast('Battlecruiser inbound');
      this.checkTech();
      return true;
    }
    updateBc(dt) {
      const b = this.bc, D = T().bc;
      if (!b) return;
      b.t += dt;
      if (b.state === 'in') {
        const dx = b.sx - b.x, dy = b.sy - b.y, dz = b.sz - b.z, dd = Math.hypot(dx, dy, dz);
        if (dd > 0.2) { const s = Math.min(dd, 7 * dt) / dd; b.x += dx * s; b.y += dy * s; b.z += dz * s; }
        else { b.state = 'station'; b.handle.play(/^stand$/i, true); this.toast('Battlecruiser on station'); }
        b.handle.setPos(b.x, b.y, b.z).setYaw(b.yaw);
        return;
      }
      b.yaw += clamp(angDelta(b.yaw, -Math.PI / 2), -0.6 * dt, 0.6 * dt);     // swing round to face south
      b.handle.setPos(b.x, b.y, b.sz + 0.25 * Math.sin(b.t * 0.7)).setYaw(b.yaw);
      // Six laser turrets on the hull's hardpoints, each with its own target, firing two-shot bursts.
      b.turrets.forEach((tu, i) => {
        tu.cd -= dt;
        if (tu.cd > 0) return;
        const at = b.handle.boneWorld(tu.bone) || [b.x + (i % 2 ? 1.6 : -1.6), b.y + (1 - Math.floor(i / 2)) * 1.8, b.sz - 0.3];
        const t = tu.target;
        if (!t || t.hp <= 0 || dist(at[0], at[1], t.x, t.y) - t.radius > D.range) {
          const near = this.enemiesInRange(at[0], at[1], D.range);
          tu.target = near.length ? near[Math.floor(Math.random() * Math.min(4, near.length))] : null;
          tu.shots = 0;
        }
        const e = tu.target;
        if (!e) { tu.cd = 0.2; return; }
        this.hurtEnemy(e, e.def.flying ? D.laserA : D.laserG);
        const hit = [e.x + rnd(-0.15, 0.15), e.y + rnd(-0.15, 0.15), e.z + e.def.height * 0.5];
        this.beams.push({ a: at, b: hit, t: 0, life: 0.14 });
        this.fx.play('laserImpact', hit[0], hit[1], hit[2], Math.atan2(at[1] - hit[1], at[0] - hit[0]), 1.4);
        BS.audio.play('laserHit', { vol: 0.3, gap: 0.1 });
        BS.audio.play('bcFire', { vol: 0.3, gap: 0.3 });
        tu.shots++;
        if (tu.shots >= D.burst) { tu.shots = 0; tu.cd = D.turretCd * rnd(0.85, 1.15); } else tu.cd = D.burstGap;
      });
      // Missile pods every few seconds at a spread of targets.
      b.cdM -= dt;
      if (b.cdM <= 0) {
        const list = this.enemiesInRange(b.x, b.y, D.range + 2);
        b.cdM = list.length ? D.missileCd : 0.5;
        list.slice(0, D.missiles).forEach((e, k) => {
          const p = { kind: 'missile', friendly: true, fx: b.x + rnd(-2, 2), fy: b.y + rnd(-1, 1), fz: b.sz - 0.4, tx: e.x, ty: e.y, tz: e.z + 0.4, t: -k * 0.1, dur: 0.6, arc: 1.2,
            dmg: D.missileDmg, target: e, impact: 'bcMissileImpact' };
          this.projectileModel(p, null, 'bcMissile');
          this.projectiles.push(p);
        });
        if (list.length) BS.audio.play('bcMissiles', { vol: 0.5 });
      }
      // Yamato cannon on its own, at the toughest zerg in reach.
      b.cdY -= dt;
      if (b.cdY <= 0) {
        let pickE = null;
        for (const e of this.enemiesInRange(b.x, b.y, D.range + 4)) if (!pickE || e.hp > pickE.hp) pickE = e;
        b.cdY = pickE ? D.yamatoCd : 1;
        if (pickE) {
          const p = { kind: 'yamato', friendly: true, fx: b.x, fy: b.y + 2.5, fz: b.sz, tx: pickE.x, ty: pickE.y, tz: 0.6, t: 0, dur: 0.5, dmg: D.yamato, target: pickE };
          this.projectileModel(p, 'projYamato', 'yamatoMissile');
          this.projectiles.push(p);
          BS.audio.play('bcYamato', { vol: 0.8 });
          this.floater(b.x, b.y, b.sz + 1, 'YAMATO', '#bd6bff');
        }
      }
    }

    // Viking wing: four fighters take up posts high over the lane and shoot down air for a while.
    callVikings() {
      const D = T().vikings;
      if (this.techCd.vikings > 0) return this.toast('Vikings are rearming');
      if (this.minerals < D.cost) return this.toast('Not enough minerals');
      this.minerals -= D.cost; this.techCd.vikings = D.cooldown;
      for (let i = 0; i < D.count; i++) {
        const post = this.strikePosts[i % this.strikePosts.length];
        const v = { post: [post[0] * 1.2, post[1] + 2], x: post[0] * 0.6, y: (this.mode === 'lane' ? -18 : -34) - i * 1.2, z: D.fly + 1.5, yaw: Math.PI / 2, cd: 0.4 + i * 0.2, state: 'in', t: 0 };
        v.handle = this.r.spawn('vikingfighter', v.x, v.y, v.z, v.yaw).setTeam(1);
        v.handle.play(/^walk|^fly|^stand/i, true);
        this.vikingWing.push(v);
      }
      this.toast('Viking wing inbound');
      return true;
    }
    updateVikings(dt) {
      const D = T().vikings, homeY = this.mode === 'lane' ? -20 : -36;
      for (let i = this.vikingWing.length - 1; i >= 0; i--) {
        const v = this.vikingWing[i];
        v.t += dt;
        if (v.state === 'in') {
          const dx = v.post[0] - v.x, dy = v.post[1] - v.y, dz = D.fly - v.z, dd = Math.hypot(dx, dy, dz);
          if (dd > 0.2) { const s = Math.min(dd, D.speed * dt) / dd; v.x += dx * s; v.y += dy * s; v.z += dz * s; v.yaw = Math.atan2(dy, dx); }
          else { v.state = 'patrol'; v.t = 0; }
        } else if (v.state === 'patrol') {
          // A lazy figure-of-eight round the post, turning to engage.
          v.x = v.post[0] + Math.sin(v.t * 0.8 + i) * 1.2; v.y = v.post[1] + Math.sin(v.t * 1.6 + i) * 0.6; v.z = D.fly + 0.2 * Math.sin(v.t * 2 + i);
          const list = this.enemiesInRange(v.x, v.y, D.range, e => e.def.flying);
          if (list.length) v.yaw += clamp(angDelta(v.yaw, Math.atan2(list[0].y - v.y, list[0].x - v.x)), -4 * dt, 4 * dt);
          v.cd -= dt;
          if (v.cd <= 0 && list.length) {
            v.cd = D.cd;
            for (let k = 0; k < D.shots; k++) {
              const off = k ? 0.4 : -0.4;
              const p = { kind: 'missile', friendly: true, fx: v.x - Math.sin(v.yaw) * off, fy: v.y + Math.cos(v.yaw) * off, fz: v.z - 0.2, tx: list[0].x, ty: list[0].y, tz: list[0].z,
                t: -k * 0.1, dur: 0.4, dmg: D.dmg, target: list[0], impact: 'vikingImpact' };
              this.projectileModel(p, null, 'vikingMissile');
              this.projectiles.push(p);
            }
            this.fx.play('vikingLaunch', v.x, v.y, v.z, v.yaw);
            BS.audio.play('vikingFire', { vol: 0.45, gap: 0.2 });
          } else if (v.cd <= 0) v.cd = 0.1;
          if (v.t > D.hover) { v.state = 'out'; v.t = 0; }
        } else {
          v.y -= D.speed * dt; v.z += 2 * dt;
          v.yaw += clamp(angDelta(v.yaw, -Math.PI / 2), -3 * dt, 3 * dt);
          if (v.y <= homeY) { v.handle.release(); this.vikingWing.splice(i, 1); continue; }
        }
        v.handle.setPos(v.x, v.y, v.z).setYaw(v.yaw);
      }
    }

    // Tactical nuke: a ghost paints the densest pack well away from the bunker; the warhead falls, then everything
    // in the blast takes 300 (half at the edge).
    launchNuke() {
      const D = T().nuke;
      if (this.techCd.nuke > 0) return this.toast('Nuke silo reloading');
      if (this.minerals < D.cost) return this.toast('Not enough minerals');
      let best = null, bestN = 0;
      for (const e of this.enemies) {
        if (e.hp <= 0 || Math.hypot(e.x, e.y) < D.minDist) continue;
        const n = this.enemiesInRange(e.x, e.y, D.radius * 0.6).length;
        if (n > bestN) { bestN = n; best = e; }
      }
      if (!best) return this.toast('No target clear of the bunker');
      this.minerals -= D.cost; this.techCd.nuke = D.cooldown;
      const n = { x: best.x, y: best.y, t: 0 };
      n.fall = this.fx.play('nukeFalling', n.x, n.y, 14, 0, 1.4);
      this.nukes.push(n);
      BS.audio.play('nukeLaunch', { vol: 0.9 });
      this.toast('Nuclear launch detected');
      this.floater(n.x, n.y, 1, 'NUKE', '#ff6a5a');
      return true;
    }
    updateNukes(dt) {
      const D = T().nuke;
      for (let i = this.nukes.length - 1; i >= 0; i--) {
        const n = this.nukes[i];
        n.t += dt;
        if (n.t < D.delay) { this.fx.move(n.fall, n.x, n.y, 14 * (1 - n.t / D.delay), 0); continue; }
        this.nukes.splice(i, 1);
        this.fx.stop(n.fall);
        this.fx.play('nuke', n.x, n.y, BS.Terrain.height(n.x, n.y), 0, 1.6);
        for (const e of this.enemiesInRange(n.x, n.y, D.radius)) this.hurtEnemy(e, dist(n.x, n.y, e.x, e.y) < D.radius * 0.5 ? D.dmg : D.dmg * 0.5);
        BS.audio.play('explosion', { vol: 1 }); BS.audio.play('tankHit', { vol: 0.9 });
        this.shake = 1;
      }
    }

    // ---- Abilities ----------------------------------------------------------------------------------
    callAirStrike() {
      const d = T().banshee;
      if (this.ab.strikeCd > 0) return this.toast('Air support is refuelling');
      if (this.minerals < d.cost) return this.toast('Not enough minerals');
      this.minerals -= d.cost; this.ab.strikeCd = d.cooldown;
      const lane = this.mode === 'lane';
      for (let i = 0; i < d.count; i++) {
        const post = this.strikePosts[i % this.strikePosts.length];
        const b = { post, x: post[0] * 0.5, y: (lane ? -16 : -34) - i * 1.2, z: d.fly + 1.5, yaw: Math.PI / 2, cd: 0.3 + i * 0.2, state: 'in', t: 0, anim: '' };
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
      const troops = this.bunker.units.concat(this.squad.filter(m => !m.dead));
      if (!troops.length) return this.toast('No troops to stim');
      for (const u of troops) { if (u.hp <= u.def.stimHp) this.killTrooper(u, 'stim'); else u.hp -= u.def.stimHp; }
      this.ab.stimUntil = this.time + T().stim.duration; this.ab.stimCd = T().stim.cooldown;
      BS.audio.play('stim', { vol: 0.9 });
      this.floater(0, 0, 3, 'STIM!', '#ffd34d');
      const out = this.squad.filter(m => !m.dead);
      if (out.length) this.floater(out.reduce((s, m) => s + m.x, 0) / out.length, out.reduce((s, m) => s + m.y, 0) / out.length, 2.4, 'STIM!', '#ffd34d');
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
        if (ang === 1 || ang === 2) {
          // Somewhere down the arm (see terrain.js), not on the cliffs beside it.
          const s = ang === 1 ? 1 : -1, t = rnd(0.55, 0.85), off = rnd(-2.2, 2.2);
          const A = BS.Terrain.arm, dx = A.bx - A.ax, dy = A.by - A.ay, L = Math.hypot(dx, dy);
          x = s * (A.ax + dx * t + off * dy / L); y = A.ay + dy * t - off * dx / L;
        }
        else { x = rnd(-4.6, 4.6); y = rnd(27, 31); }
      } else { const rr = rnd(25, 28); x = Math.cos(ang + rnd(-0.35, 0.35)) * rr; y = Math.sin(ang + rnd(-0.35, 0.35)) * rr; }
      let model = d.model, death = d.death;
      if (d.variants && Math.random() < 0.45) { const v = pick(d.variants); model = v; if (BS.EXTRA_MODELS[v + 'Death']) death = v + 'Death'; }
      const z = d.flying ? d.fly : BS.Terrain.height(x, y);
      const e = { type, def: d, x, y, z, yaw: Math.atan2(-y, -x), hp: 0, maxHp: 0, radius: d.radius, life: d.life || 0, cd: rnd(0, d.cd), retarget: 0,
        target: null, hitAt: -1, pendingHit: false, slowUntil: 0, burrowed: false, burrowT: 0, attacking: false, anim: '', flash: 0, sx: 0, sy: 0, model, death };
      // Lane terrain: ground zerg follow the Y; some ranged ones climb a ramp to a ledge over the bunker.
      if (!d.flying && !at) {
        const high = this.mode === 'lane' && ['acid', 'spine', 'bullet'].includes(d.projectile) && Math.random() < 0.4;
        e.route = BS.Terrain.route(x, y, high); e.highRoute = high;
      }
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
      for (const m of this.squad) if (!m.dead) list.push({ kind: 'trooper', obj: m, x: m.x, y: m.y, radius: m.radius, light: m.type !== 'marauder', armored: m.type === 'marauder' });
      for (const st of this.structures) if (!st.dead) list.push({ kind: 'structure', obj: st, x: st.x, y: st.y, radius: st.radius, structure: true, armored: true });
      for (const m of this.mechs) if (!m.dead) list.push({ kind: 'mech', obj: m, x: m.x, y: m.y, radius: m.radius, armored: true });
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
      } else if (tg.kind === 'trooper') {
        o.hp -= Math.max(0.5, dmg - (o.type === 'marauder' ? 1 : 0)); o.flash = 1;
        if (o.hp <= 0 && !o.dead) this.killTrooper(o);
      } else if (tg.kind === 'mech') {
        o.hp -= Math.max(0.5, dmg - o.armor); o.flash = 1;
        if (o.hp <= 0 && !o.dead) this.killMech(o);
      } else if (tg.kind === 'structure') {
        o.hp -= Math.max(0.5, dmg - o.armor); o.flash = 1;
        if (o.hp <= 0 && !o.dead) this.destroyStructure(o);
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
      if (this.r.nearEye(x, y, z) && !/^bunker/.test(key)) return null;   // bunker view: no corpse (or gore burst) against the eye
      this.fx.play(key, x, y, Math.max(z, BS.Terrain.height(x, y)), yaw, scale || 1);
      // Corpses render in their own pass behind the living (see Renderer.frame), so they never cover live zerg.
      const gz = BS.Terrain.height(x, y);
      z = Math.min(z, gz);
      const h = this.r.spawn(key, x, y, z, yaw, { corpse: true }).setTeam(terran ? 1 : 3);
      if (scale) h.setScale(scale);
      const c = { h, t: 0, x, y, z, yaw, sink: !/^bunker/.test(key), hold: hold || 3.5 };
      const len = h.playDeath(false);
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
      for (const k of ['nuke', 'vikings']) this.techCd[k] = Math.max(0, this.techCd[k] - dt);
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
      this.updateSquad(dt);
      this.updateStructures(dt);
      this.updateBanshees(dt);
      this.updateMechs(dt);
      this.updateBc(dt);
      this.updateVikings(dt);
      this.updateNukes(dt);
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
          if (!!a.def.flying !== !!b.def.flying || a.burrowed || b.burrowed) continue;   // burrowed lurkers are underground: walk over them
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
        const high = BS.Terrain.onHigh(e.x, e.y);
        const canAttack = reach <= range + (high ? 1 : 0) && !(d.burrows && (!e.burrowed || e.burrowT > 0)) && !(e.route && e.route.length && e.highRoute);
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
        } else if (e.perched) {
          e.attacking = false;
          this.playAnim(e, 'stand');           // holding the ledge, waiting for something in range
        } else if (!(d.burrows && e.burrowed)) {
          e.attacking = false;
          this.playAnim(e, 'walk');
          // Head for the next waypoint of the route through the Y, then straight for the target.
          let gx = t.x, gy = t.y;
          if (e.route && e.route.length) {
            [gx, gy] = e.route[0];
            if (Math.hypot(gx - e.x, gy - e.y) < 0.9) { e.route.shift(); if (!e.route.length && e.highRoute) e.perched = true; }
          }
          const gd = Math.hypot(gx - e.x, gy - e.y) || 1;
          const sp = d.speed * (slowed ? 0.5 : 1), nx = (gx - e.x) / gd, ny = (gy - e.y) / gd;
          const mx = nx * sp * dt + e.sx * 0.8, my = ny * sp * dt + e.sy * 0.8;
          // Cliffs: ground units steer around them; flyers ignore them.
          if (d.flying) { e.x += mx; e.y += my; } else this.stepGround(e, mx, my);
          if (this.mode === 'lane') e.x = clamp(e.x, -21, 21);
          if (!d.flying) e.z = BS.Terrain.height(e.x, e.y);
          // Face the way it is heading, not the way the crowd shoves it, and turn at a creature's pace.
          const turn = (d.radius >= 0.9 ? 3 : 8) * dt;
          e.yaw += clamp(angDelta(e.yaw, Math.atan2(ny, nx)), -turn, turn);
          if (e.perched) this.playAnim(e, 'stand');
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
      const bonus = T().bunker.rangeBonus + T().bunker.halfSize + (this.up.hisec ? T().hiSecRange : 0) + (this.up.rails ? T().railsRange : 0) + (this.hard ? 1 : 0);
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
        const slit = this.bunkerSlit(Math.atan2(target.y, target.x));
        // A little spread along the slit.
        const ang = Math.atan2(target.y, target.x), k = rnd(-0.18, 0.18);
        this.fireInfantry(u, slit[0] - Math.sin(ang) * k, slit[1] + Math.cos(ang) * k, slit[2], target);
      }
    }
    // One shot from a trooper at a muzzle position (a bunker slit, or the rifle of a soldier standing outside).
    fireInfantry(u, sx, sy, sz, target) {
        const d = u.def, pred = e => !(d.groundOnly && e.def.flying);
        const miss = BS.Terrain.onHigh(target.x, target.y) && Math.random() < 0.3;   // firing up at the high ground
        const dmg = miss ? 0 : d.dmg + this.up.weapons;
        const hx = target.x, hy = target.y, hz = target.z + target.def.height * 0.5, ang = Math.atan2(hy - sy, hx - sx);
        if (u.type === 'marine') {
          if (dmg) this.hurtEnemy(target, dmg);
          // Three-round bursts with SC2's muzzle mesh and impact sparks.
          const aim = ang;
          for (let r = 0; r < 3; r++) {
            this.fx.play('marineLaunch', sx, sy, sz, aim, 1.4, r * 0.08);
            this.fx.play('marineImpact', hx + rnd(-0.2, 0.2), hy + rnd(-0.2, 0.2), hz + rnd(-0.15, 0.25), aim + Math.PI, 1, r * 0.08 + 0.03);
            this.flashes.push({ x: sx, y: sy, z: sz, yaw: aim, delay: r * 0.08, life: 0.09 });
          }
          BS.audio.play('marineFire', { vol: 0.3, gap: 0.09 });
        } else if (u.type === 'firebat') {
          if (dmg) for (const v of this.enemiesInRange(hx, hy, d.splash, pred)) this.hurtEnemy(v, dmg + (v.def.light ? d.dmgLight : 0));
          this.fx.play('firebatLaunch', sx, sy, sz, ang, 1.2);
          BS.audio.play('firebatFire', { vol: 0.45, gap: 0.3 });
        } else {
          const p = { kind: 'grenade', friendly: true, fx: sx, fy: sy, fz: sz, tx: hx, ty: hy, tz: hz, t: 0, dur: 0.3, arc: 0.6, target, dmg, def: d, impact: 'marauderImpact' };
          this.fx.play('marauderLaunch', sx, sy, sz, ang);
          this.projectileModel(p, 'projMarauder', 'marauderMissile');
          this.projectiles.push(p);
          BS.audio.play('marauderFire', { vol: 0.4, gap: 0.2 });
        }
    }
    // Medivac on Station: parked at the bunker's shoulder; heals the troops inside and the most hurt SCV nearby.
    updateStation(dt) {
      for (const u of this.bunker.units) u.hp = Math.min(u.maxHp, u.hp + 9 * dt);
      for (const m of this.squad) if (!m.dead) m.hp = Math.min(m.maxHp, m.hp + 9 * dt);
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
          t.aim = ang; t.handle.play(/^attack/i, false);           // the hull stays put; the turret traverses (updateVisuals)
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
      if (frac > 0.4) for (const st of this.structures) if (st.state === 'ready' && !st.dead && st.hp < st.maxHp - 1) jobs.push({ x: st.x, y: st.y, r: st.radius, cost: 0.3, obj: st, need: st.hp / st.maxHp });
      if (frac > 0.4) for (const m of this.mechs) if (m.state === 'ready' && !m.dead && m.hp < m.maxHp - 1) jobs.push({ x: m.x, y: m.y, r: m.radius, cost: 0.4, obj: m, need: m.hp / m.maxHp });
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

    // Air strike: fly in, hover over a post firing missiles at ground zerg passing below, then leave.
    updateBanshees(dt) {
      const D = T().banshee, homeY = this.mode === 'lane' ? -18 : -36;
      for (let i = this.banshees.length - 1; i >= 0; i--) {
        const b = this.banshees[i];
        b.t += dt;
        if (b.state === 'in') {
          const dx = b.post[0] - b.x, dy = b.post[1] - b.y, dz = D.fly - b.z, dd = Math.hypot(dx, dy, dz);
          if (dd > 0.2) { const sp = Math.min(dd, D.speed * dt) / dd; b.x += dx * sp; b.y += dy * sp; b.z += dz * sp; b.yaw = Math.atan2(dy, dx); }
          else { b.state = 'hover'; b.t = 0; b.handle.play(/^stand$/i, true); }
        } else if (b.state === 'hover') {
          const bob = 0.15 * Math.sin(this.time * 2.1 + i);
          const list = this.enemiesInRange(b.x, b.y, D.range, e => !e.def.flying);
          if (list.length) b.yaw += clamp(angDelta(b.yaw, Math.atan2(list[0].y - b.y, list[0].x - b.x)), -4 * dt, 4 * dt);
          b.cd -= dt;
          if (b.cd <= 0 && list.length) {
            b.cd = D.cd;
            const tg = list[0];
            for (let k = 0; k < D.shots; k++) {
              const ox = -Math.sin(b.yaw) * (k ? 0.4 : -0.4), oy = Math.cos(b.yaw) * (k ? 0.4 : -0.4);
              const p = { kind: 'rocket', friendly: true, fx: b.x + ox, fy: b.y + oy, fz: b.z - 0.3, tx: tg.x, ty: tg.y, tz: tg.z + 0.3, t: -k * 0.1, dur: 0.35, dmg: D.dmg, target: tg, impact: 'bansheeImpact' };
              this.projectileModel(p, 'projBanshee', 'bansheeMissile');
              this.projectiles.push(p);
            }
            b.standAt = this.time + (b.handle.play(/^attack/i, false) || 0.5);
            BS.audio.play('bansheeFire', { vol: 0.5, gap: 0.2 });
          } else if (b.cd <= 0) b.cd = 0.1;
          if (b.standAt && this.time > b.standAt) { b.standAt = 0; b.handle.play(/^stand$/i, true); }
          b.z = D.fly + bob;
          if (b.t > D.hover) { b.state = 'out'; b.t = 0; b.handle.play(/^walk|^fly/i, true); }
        } else {
          b.y -= D.speed * dt; b.z += 1.5 * dt;
          b.yaw += clamp(angDelta(b.yaw, -Math.PI / 2), -3 * dt, 3 * dt);
          if (b.y <= homeY) { b.handle.release(); this.banshees.splice(i, 1); continue; }
        }
        b.handle.setPos(b.x, b.y, b.z).setYaw(b.yaw);
      }
    }

    updateProjectiles(dt) {
      const P = this.projectiles;
      for (let i = P.length - 1; i >= 0; i--) {
        const p = P[i];
        p.t += dt;
        if (p.t < 0) continue;
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
        if (p.impact) this.fx.play(p.impact, p.tx, p.ty, p.tz, Math.atan2(p.fy - p.ty, p.fx - p.tx), p.impactScale || 1);
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
            if (p.target && p.target.hp > 0 && p.dmg) { p.target.slowUntil = this.time + p.def.slow; p.target.anim = ''; this.hurtEnemy(p.target, p.dmg + (p.target.def.armored ? p.def.dmgArmored : 0)); }
          } else if (p.kind === 'missile') {
            if (p.splash) { for (const v of this.enemiesInRange(p.tx, p.ty, p.splash, p.pred)) this.hurtEnemy(v, p.dmg); }
            else if (p.target && p.target.hp > 0) this.hurtEnemy(p.target, p.dmg);
          } else if (p.kind === 'sam') {
            if (p.target && p.target.hp > 0) this.hurtEnemy(p.target, p.dmg);
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
      for (const t of this.tanks) {
        t.flash = Math.max(0, t.flash - dt * 4);
        // Turret traverse at 360 deg/s toward the last target, relative to the hull (which faces up the lane).
        if (t.state === 'ready' && t.handle && t.aim !== undefined) {
          const want = angDelta(Math.PI / 2, t.aim);
          t.turret = (t.turret || 0) + clamp(angDelta(t.turret || 0, want), -Math.PI * 2 * dt, Math.PI * 2 * dt);
          t.handle.setTurret('Ref_TurretZ', t.turret);
        }
      }
      for (const s of this.scvs) s.flash = Math.max(0, s.flash - dt * 4);
      // Stimmed troopers outside glow red-orange and pulse, as in StarCraft II.
      const stimmed = this.ab.stimUntil > this.time, pulse = 0.5 + 0.5 * Math.sin(this.time * 9);
      for (const m of this.squad) {
        m.flash = Math.max(0, m.flash - dt * 4);
        if (stimmed) m.handle.setTint(1.15, 0.62 + 0.12 * pulse, 0.5 + 0.1 * pulse);
        else if (m.handle.tint) { m.handle.setTint(1, 1, 1); m.handle.tint = null; }
      }
      for (const st of this.structures) st.flash = Math.max(0, st.flash - dt * 4);
      for (const m of this.mechs) m.flash = Math.max(0, m.flash - dt * 4);
      for (let i = this.beams.length - 1; i >= 0; i--) { this.beams[i].t += dt; if (this.beams[i].t > this.beams[i].life) this.beams.splice(i, 1); }
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
