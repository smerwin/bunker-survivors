// Swarm mode: the tables turned. The player is the zerg: minerals and vespene feed hatcheries, a lair and a hive that hatch
// ever-evolving broods on their own; the goal is a hive and the economy to summon Kerrigan (with drop pods and a leviathan,
// as in the Terran runs' finale), who then answers to the player directly. The Terrans play themselves (the
// commander below, the same one the balance bots use) and win by holding out until the Hyperion takes them off.
// Unlocked by escaping Kerrigan once (BS.Meta: swarm). Extends BS.Game; data in BS.SWARM (units.js).
// Mirrors the Unreal build's BSSwarm.cpp; keep the two in step.
window.BS = window.BS || {};
(function (BS) {
  'use strict';

  const rnd = (a, b) => a + Math.random() * (b - a);
  const dist = (ax, ay, bx, by) => Math.hypot(ax - bx, ay - by);
  const pick = arr => arr[Math.floor(Math.random() * arr.length)];
  const P = BS.Game.prototype;

  P.initSwarm = function () {
    const S = BS.SWARM;
    this.sw = { minerals: S.minerals, gas: S.gas, drones: 0, extractors: 0, hatcheries: 1, queens: 0, tier: 0, strains: { zergling: true },
      evo: { melee: 0, missile: 0, carapace: 0 }, hold: false, inject: {}, chambers: 0, research: [], autoEvo: true, hatchAt: [], extAt: [], chamberAt: [], raidAt: 0, cards: {}, larva: 0, broodT: S.brood, kills: 0, heroAt: 0, hero: null, next: null, ang: 0, think: 0 };
    this.income *= S.terran.income;
    this.initBase();
    // Every Terran the swarm kills feeds it (BS.SWARM.bounty).
    const hurt = this.damageDefender;
    this.damageDefender = function (tg, dmg) {
      const alive = tg && tg.obj && !tg.obj.dead && tg.kind !== 'bunker';
      hurt.call(this, tg, dmg);
      if (alive && tg.obj.dead) {
        const b = BS.SWARM.bounty, m = (1 + 0.5 * this.zcardLevel('bounty')) * (tg.kind === 'mech' ? b.mech * (tg.obj.kind === 'odin' ? 3 : tg.obj.kind === 'thor' ? 2 : 1) : b[tg.kind] || b.air) | 0;
        this.sw.minerals += m; this.sw.kills++;
        this.floater(tg.x, tg.y, 2, '+' + m, '#8de08a');
      }
    };
  };

  // ---- The swarm's economy ------------------------------------------------------------------------------------
  P.swarmIncome = function () { const S = BS.SWARM; return S.income + S.drone.income * this.sw.drones * (1 + 0.2 * this.zcardLevel('drones')); };
  P.swarmGasIncome = function () { return BS.SWARM.extractor.gas * this.sw.extractors * (1 + 0.25 * this.zcardLevel('gas')); };
  // Larva (the brood budget, BS.ZERG budget) a second: hatcheries, more with each Spawn Larva.
  P.larvaRate = function () { const S = BS.SWARM; return S.hatch.larva * this.sw.hatcheries * (1 + S.queen.boost * this.sw.queens) * (1 + 0.15 * this.zcardLevel('larva')); };

  P.updateSwarm = function (dt) {
    const S = BS.SWARM, sw = this.sw;
    sw.minerals += this.swarmIncome() * dt;
    sw.gas += this.swarmGasIncome() * dt;
    sw.larva = Math.min(this.larvaRate() * S.brood * S.bank, sw.larva + this.larvaRate() * dt);
    if ((sw.broodT -= dt) <= 0) { sw.broodT = S.brood - this.zcardLevel('pulse'); this.hatchBrood(); }
    this.updateResearch(dt * (1 + 0.25 * this.zcardLevel('haste')));
    this.autoInject();
    this.updateSunkens(dt);
    // Kerrigan down: she can be summoned again once the hive has recovered (BS.SWARM.hero.respawn).
    if (sw.hero && sw.hero.hp <= 0) { sw.hero = null; sw.heroAt = this.time + S.hero.respawn; this.toast('Kerrigan has fallen: summon her again when the hive recovers'); }
    // The Terran commander.
    if ((sw.think -= dt) <= 0) { sw.think = 0.5; this.terranThink(); }
    if ((sw.base.t -= dt) <= 0) { sw.base.t = 0.5; this.syncBase(); }
  };

  // The strains the hives can hatch now (bought, and not at their cap; anti-air ones only while there is Terran air).
  P.swarmPick = function () {
    const S = BS.SWARM, have = {};
    for (const e of this.enemies) have[e.type] = (have[e.type] || 0) + 1;
    const air = this.airPresence();
    const list = S.strains.filter(s => this.sw.strains[s.id] && !(s.max && (have[s.id] || 0) >= s.max) && !(BS.ZERG[s.id].antiAir && !air) && !(s.lane && this.mode !== 'lane'));
    const wt = s => s.weight || BS.ZERG[s.id].weight || 1;
    let roll = Math.random() * list.reduce((a, s) => a + wt(s), 0);
    for (const s of list) { roll -= wt(s); if (roll <= 0) return s; }
    return list[list.length - 1] || null;
  };
  P.strainBudget = s => s.budget || BS.ZERG[s.id].budget;
  // A brood: everything the banked larva pays for, from one approach (mostly true north, sometimes down an arm).
  P.hatchBrood = function () {
    const sw = this.sw, lane = this.mode === 'lane';
    const ang = lane ? (Math.random() < 0.35 ? (Math.random() < 0.5 ? 1 : 2) : 0) : Math.random() * Math.PI * 2;
    for (let guard = 0; guard < 200; guard++) {
      if (!sw.next) sw.next = this.swarmPick();
      if (!sw.next || sw.larva < this.strainBudget(sw.next) || this.aliveCount() >= this.maxAlive()) break;
      if (sw.hold && this.heldCount() >= this.holdCap()) break;           // the hive holds only so many (the larva banks)
      sw.larva -= this.strainBudget(sw.next);
      this.spawnEnemy(sw.next.id, ang).held = sw.hold;
      sw.next = null;
    }
  };
  // The Terrans' round: their wave clock still runs (income, the shop between), but no zerg come from it.
  P.swarmRound = function (w) {
    this.spawnTimer = 0; this.waveTimer = this.waveLen;
    this.minerals += 20 + 8 * w;
  };

  // ---- Buying ------------------------------------------------------------------------------------------------
  // What the hive cluster can buy: { kind, id }. swarmCost: [minerals, gas], or null at its max.
  const freeSpot = at => { let i = 0; while (at[i]) i++; return i; };
  P.swarmCost = function (kind, id) {
    const S = BS.SWARM, sw = this.sw;
    switch (kind) {
      case 'drone': return sw.drones < S.drone.max ? [S.drone.cost, 0] : null;
      case 'extractor': return sw.extractors < S.extractor.max ? [S.extractor.cost, 0] : null;
      case 'hatch': return sw.hatcheries - 1 < S.hatch.costs.length ? [S.hatch.costs[sw.hatcheries - 1], 0] : null;
      case 'queen': return sw.queens < S.queen.costs.length ? [S.queen.costs[sw.queens], 0] : null;
      case 'chamber': return sw.chambers < S.chamber.max ? S.chamber.cost : null;
      case 'tier': return sw.tier + 1 < S.tiers.length ? S.tiers[sw.tier + 1].cost : null;
      case 'hero': return this.heroAlive() ? null : S.hero.cost.map(v => Math.round(v * (this.sw.cards.favor ? 0.75 : 1)));
      case 'strain': return sw.strains[id] ? null : S.strains.find(s => s.id === id).cost;
      case 'evo': {
        const v = S.evo.find(x => x.id === id), lv = sw.evo[id] || 0;
        return v.costs ? (lv < v.costs.length ? v.costs[lv] : null) : (lv ? null : v.cost);
      }
    }
    return null;
  };
  // The hive tier something needs (a strain, an evolution's next level), or 0.
  P.swarmNeeds = function (kind, id) {
    const S = BS.SWARM;
    if (kind === 'hero') return S.hero.tier;
    if (kind === 'strain') return S.strains.find(s => s.id === id).tier;
    if (kind === 'evo') { const v = S.evo.find(x => x.id === id); return v.tiers ? v.tiers[Math.min(this.sw.evo[id] || 0, v.tiers.length - 1)] : v.tier; }
    return 0;
  };
  // '' (buyable), 'max', 'locked' (the hive tier it needs), 'need' (a strain it improves is not bred yet), 'wait'
  // (Kerrigan, until the hive recovers from her fall); chamber evolutions: 'nochamber', 'busy' (this line is being
  // researched), 'full' (every chamber is busy).
  P.swarmState = function (kind, id) {
    if (!this.swarmCost(kind, id)) return 'max';
    if (this.swarmNeeds(kind, id) > this.sw.tier) return 'locked';
    if (kind === 'hero' && this.time < this.sw.heroAt) return 'wait';
    if (kind === 'evo') {
      const v = BS.SWARM.evo.find(x => x.id === id);
      if (v.need && !this.sw.strains[v.need]) return 'need';
      if (v.costs) { if (!this.sw.chambers) return 'nochamber'; if (this.researching(id)) return 'busy'; if (this.idleChamber() < 0) return 'full'; }
    }
    return '';
  };
  P.swarmBuy = function (kind, id) {
    const S = BS.SWARM, sw = this.sw, st = this.swarmState(kind, id), cost = this.swarmCost(kind, id);
    if (kind === 'chamber' && st === 'max') { sw.autoEvo = !sw.autoEvo; return this.toast('Evolution chambers: ' + (sw.autoEvo ? 'evolving by themselves each wave' : 'evolving only when tapped')); }
    if (st === 'max' || st === 'wait' || st === 'busy') return false;
    if (st === 'nochamber') return this.toast('Build an evolution chamber first');
    if (st === 'full') return this.toast('Every evolution chamber is busy');
    if (st === 'locked') return this.toast('Needs a ' + S.tiers[this.swarmNeeds(kind, id)].name);
    if (st === 'need') return this.toast('Breed ' + BS.ZERG[S.evo.find(x => x.id === id).need].name + 's first');
    if (sw.minerals < cost[0]) return this.toast('Not enough minerals');
    if (sw.gas < cost[1]) return this.toast('Not enough vespene');
    sw.minerals -= cost[0]; sw.gas -= cost[1];
    switch (kind) {
      case 'drone': sw.drones++; break;
      case 'extractor': sw.extractors++; sw.extAt[freeSpot(sw.extAt)] = true; break;
      case 'hatch': sw.hatcheries++; sw.hatchAt[freeSpot(sw.hatchAt)] = true; this.toast('Hatchery ' + sw.hatcheries + ' is up'); break;
      case 'queen': sw.queens++; this.toast(sw.queens <= sw.hatcheries ? 'A queen for hatchery ' + sw.queens + ': tap it to inject larva' : 'Spawn Larva level ' + sw.queens); break;
      case 'chamber': sw.chambers++; sw.chamberAt[freeSpot(sw.chamberAt)] = true; this.toast('Evolution chamber ' + sw.chambers + ': tap it to evolve'); break;
      case 'tier': {
        sw.tier++;
        const t = S.tiers[sw.tier];
        this.waveBanner = { text: 'THE ' + t.name.toUpperCase() + ' IS COMPLETE', t: this.time };
        this.toast(sw.tier === S.hero.tier ? 'The hive can summon Kerrigan' : 'New strains and evolutions open');
        break;
      }
      case 'strain': sw.strains[id] = true; this.toast(S.strains.find(s => s.id === id).name + ' join the broods'); break;
      case 'evo': {
        const v = S.evo.find(x => x.id === id), lv = sw.evo[id] || 0;
        if (v.costs) { sw.research[this.idleChamber()] = { id, t: 0, dur: S.chamber.times[Math.min(lv, S.chamber.times.length - 1)] }; this.toast('Evolving ' + v.name + ' level ' + (lv + 1)); break; }
        sw.evo[id] = lv + 1;
        this.toast('Evolved: ' + v.name);
        break;
      }
      case 'hero': this.spawnHero(); break;
    }
    return true;
  };

  // The swarm's strains as evolved now (BS.SWARM.evo); the player's Kerrigan (mortal, BS.SWARM.hero).
  P.swarmDef = function (type) {
    const base = BS.ZERG[type], S = BS.SWARM, v = this.sw.evo, H = S.hero;
    const key = type + '/' + this.sw.tier + '/' + Object.keys(v).sort().map(k => k + v[k]).join(','), c = this.evoDefs || (this.evoDefs = {});
    if (c[key]) return c[key];
    const d = Object.assign({}, base);
    if (type === 'kerrigan') {
      Object.assign(d, { untouchable: false, prefer: null, hp: H.hp * (1 + S.evoHp * v.carapace), dmg: H.dmg * (1 + S.evoDmg * v.melee), dmgStructure: H.structure * (1 + S.evoDmg * v.melee), armor: H.armor + v.carapace, bounty: 300 });
      return (c[key] = d);
    }
    const lv = base.range <= 1.5 ? v.melee : v.missile;
    if (base.dmg) d.dmg = base.dmg * (1 + S.evoDmg * lv);
    if (base.dmgStructure) d.dmgStructure = base.dmgStructure * (1 + S.evoDmg * lv);
    d.armor = base.armor + v.carapace;
    d.hp = base.hp * (1 + S.evoHp * v.carapace);
    if ((type === 'zergling' || type === 'wingling') && v.boost) d.speed = base.speed * 1.4;
    if ((type === 'zergling' || type === 'wingling') && v.adrenal) d.cd = base.cd * 0.7;
    if (type === 'baneling' && v.hooks) { d.speed = base.speed * 1.4; d.hp = d.hp + 15; }
    if (type === 'roach' && v.glial) d.speed = base.speed * 1.35;
    if (type === 'hydralisk' && v.spines) d.range = base.range + 1.5;
    if (type === 'ultralisk' && v.chitin) d.armor += 2;
    return (c[key] = d);
  };

  // ---- Kerrigan, the player's own ------------------------------------------------------------------------------
  // Summoned: she comes down the lane while drop pods slam down all round the base.
  P.spawnHero = function () {
    const F = BS.FINALE, at = BS.SWARM.base.chrysalis, e = this.spawnEnemy('kerrigan', 0, this.mode === 'lane' ? at : null);
    e.hero = true; e.order = null; e.hold = null; e.power = BS.SWARM.hero.power;
    this.sw.hero = e;
    this.syncBase();                                   // out of her chrysalis
    this.dropPods(BS.SWARM.hero.pods, 2, F.podEvery);
    this.waveBanner = { text: 'THE QUEEN OF BLADES TAKES THE FIELD', t: this.time };
    this.toast('Kerrigan answers the swarm: tap the ground to send her');
    BS.audio.play('finaleRoar', { vol: 1 });
    this.shake = 1;
    // Her spells start ready.
    Object.assign(e, { leapCd: 0, apocCd: 0, kineticCd: 0, gripCd: 0, chainCd: 0, waveCd: 0, mutateCd: 0, banesCd: 0 });
  };
  P.heroAlive = function () { const e = this.sw && this.sw.hero; return e && e.hp > 0 ? e : null; };
  // Send her somewhere (a tap on the ground): she fights whatever she meets on the way, then holds that spot.
  P.heroOrder = function (x, y) {
    const e = this.heroAlive();
    if (!e) return false;
    if (this.mode === 'lane') x = Math.max(-21, Math.min(21, x));
    e.order = { x, y }; e.hold = null; e.route = null; e.perched = false; e.retarget = 0;
    return true;
  };
  // Back to advancing with the swarm on her own.
  P.heroAdvance = function () {
    const e = this.heroAlive();
    if (!e) return false;
    e.order = null; e.hold = null; e.retarget = 0;
    return true;
  };
  const pointTarget = (x, y) => ({ kind: 'point', obj: { x, y }, x, y, radius: 0 });
  P.heroTarget = function (e, dt) {
    const H = BS.SWARM.hero;
    const near = (x, y, R) => { let best = null, bd = R; for (const t of this.tgs) { const d = dist(x, y, t.x, t.y) - t.radius; if (d < bd) { bd = d; best = t; } } return best; };
    if (e.order) {
      if (dist(e.x, e.y, e.order.x, e.order.y) < 0.7) { e.hold = e.order; e.order = null; }
      else { e.target = near(e.x, e.y, H.engage) || pointTarget(e.order.x, e.order.y); return; }
    }
    if (e.hold) { e.target = near(e.hold.x, e.hold.y, H.guard) || pointTarget(e.hold.x, e.hold.y); return; }
    if ((e.retarget -= dt) <= 0 || !this.valid(e.target) || e.target.kind === 'point') { e.target = near(e.x, e.y, 1e9); e.retarget = 0.6; }
  };
  // Her spells, on the player's word (a queued cast goes off as soon as she is free).
  P.heroKit = function (e, dt) {
    for (const k of ['kineticCd', 'gripCd', 'chainCd', 'waveCd', 'mutateCd', 'banesCd']) e[k] -= dt;
    if (this.sw.cards.psionic) for (const k of ['kineticCd', 'gripCd', 'chainCd', 'waveCd', 'mutateCd', 'banesCd', 'leapCd', 'apocCd']) e[k] -= dt / 3;   // Psionic Link
    if (e.queued) {
      const id = e.queued;
      e.queued = null;
      let t = null;
      if (id === 'leap') {
        // Onto the defender out of reach with the most others round it (the bunker if nothing else).
        const L = BS.FINALE.leap;
        let bn = -1;
        for (const tg of this.tgs) {
          const r = dist(e.x, e.y, tg.x, tg.y) - tg.radius;
          if (r > L.max || r < 1) continue;
          const n = this.tgs.filter(o => dist(o.x, o.y, tg.x, tg.y) < 2.5).length + (tg.structure ? -0.5 : 0);
          if (n > bn) { bn = n; t = tg; }
        }
      }
      if (this.kerriganAbility(e, id, t, true)) return true;
      this.toast('Nothing in range for ' + BS.SWARM.abilities.find(a => a.id === id).name.toLowerCase());
    }
    // Advancing on her own: the same unsticking as the finale's.
    return !e.order && !e.hold ? this.kerriganUnstick(e, dt) : false;
  };
  P.heroCd = function (id) { const e = this.heroAlive(); return e ? Math.max(0, e[HERO_CD[id]]) : 0; };
  P.heroCast = function (id) {
    const e = this.heroAlive(), a = BS.SWARM.abilities.find(x => x.id === id);
    if (!e) return this.toast(this.sw.tier < BS.SWARM.hero.tier ? 'Kerrigan answers only a hive' : 'Summon Kerrigan first');
    if (e[HERO_CD[id]] > 0) return false;
    e.queued = id;
    return true;
  };
  const HERO_CD = { leap: 'leapCd', apocalypse: 'apocCd', kinetic: 'kineticCd', grip: 'gripCd', chain: 'chainCd', wave: 'waveCd', mutation: 'mutateCd', banelings: 'banesCd' };
  BS.HERO_CD = HERO_CD;

  // ---- The hive cluster on the field ---------------------------------------------------------------------------
  // North of the lane, where the broods have always come from: the main hatchery (morphing to lair and hive) in the
  // middle, scaled-down hatcheries and extractors round it, a tech building for each strain line and the evolution
  // chamber, drones at work, larva by every hatchery, the queens, and Kerrigan's chrysalis once the hive can summon
  // her. Everything grows in as it is bought (BS.SWARM.base). Purely visual: the Terrans never reach it.
  P.initBase = function () {
    this.sw.base = { parts: {}, ents: {}, techSpot: {}, drones: [], t: 0 };
    this.syncBase();
  };
  // A building or creature of the base, (re)spawned when its model changes; it grows in from nothing.
  P.basePart = function (key, model, x, y, scale, yaw) {
    const B = this.sw.base, old = B.parts[key];
    if (old && old.model === model) return old;
    if (old) old.h.release();
    const h = this.r.spawn(model, x, y, BS.Terrain.height(x, y), yaw === undefined ? -Math.PI / 2 : yaw).setTeam(3).setScale(0.01);
    h.play(/^stand$/i, true);
    return (B.parts[key] = { h, model, x, y, scale, t: old ? 0.6 : 0 });
  };
  P.dropPart = function (key) { const B = this.sw.base, p = B.parts[key]; if (p) { p.h.release(); delete B.parts[key]; } };
  // What the base should show for what has been bought.
  P.syncBase = function () {
    const sw = this.sw, B = sw.base, D = BS.SWARM.base;
    if (!B || this.mode !== 'lane') return;
    const [cx, cy] = D.at;
    this.baseEntity('main', this.basePart('main', ['hatchery', 'lair', 'hive'][sw.tier], cx, cy, D.main));
    const hatch = [[cx, cy, D.main, 'main']];
    D.hatch.spots.forEach(([x, y], i) => { if (sw.hatchAt[i]) { this.baseEntity('hatch' + i, this.basePart('hatch' + i, 'hatchery', x, y, D.hatch.scale)); hatch.push([x, y, D.hatch.scale, 'hatch' + i]); } });
    D.extractor.spots.forEach(([x, y], i) => { if (sw.extAt[i]) this.baseEntity('extractor' + i, this.basePart('extractor' + i, 'extractor', x, y, D.extractor.scale)); });
    for (let i = 0; i < this.zcardLevel('sunken') && i < D.sunken.length; i++) { const [x, y] = D.sunken[i]; this.baseEntity('sunken' + i, this.basePart('sunken' + i, 'spinecrawler', x, y, 0.7)); }
    D.chamber.spots.forEach(([x, y], i) => { if (sw.chamberAt[i]) this.baseEntity('chamber' + i, this.basePart('chamber' + i, 'evolutionchamber', x, y, D.chamber.scale)); });
    // Tech: each building once anything it serves is bred, in the next free spot.
    for (const [model, serves] of D.tech.list) {
      if (!serves.some(s => sw.strains[s])) continue;
      if (B.techSpot[model] === undefined) B.techSpot[model] = Object.keys(B.techSpot).length;
      const spot = D.tech.spots[B.techSpot[model] % D.tech.spots.length];
      this.basePart('tech:' + model, model, spot[0], spot[1], D.tech.scale);
    }
    // Larva wriggling at every hatchery's foot, the queens round the main one.
    hatch.forEach(([x, y, s, key]) => {
      for (let k = 0; k < D.larva; k++) {
        const a = -Math.PI / 2 + (k - (D.larva - 1) / 2) * 0.5, r = 3.1 * s / D.main + 0.4;
        this.basePart('larva:' + key + ':' + k, 'larva', x + Math.cos(a) * r, y + Math.sin(a) * r, 0.9, Math.random() * Math.PI * 2);
      }
    });
    for (let i = 0; i < 6; i++) {
      if (i < sw.queens) { const a = Math.PI * (0.15 + 0.14 * i); this.basePart('queen' + i, 'queen', cx + Math.cos(a) * 4.2, cy - Math.sin(a) * 3.6, 0.85); }
      else this.dropPart('queen' + i);
    }
    // Kerrigan's chrysalis: there from the moment the hive can summon her until she breaks out of it.
    if (sw.tier >= BS.SWARM.hero.tier && !this.heroAlive()) this.basePart('chrysalis', 'chrysalis', D.chrysalis[0], D.chrysalis[1], 0.9);
    else if (B.parts.chrysalis) { this.fx.play('nydusBirth', D.chrysalis[0], D.chrysalis[1], BS.Terrain.height(D.chrysalis[0], D.chrysalis[1]), 0, 1.6); this.dropPart('chrysalis'); }
    // The mineral line behind the main hatchery.
    D.minerals.forEach(([x, y], i) => this.basePart('mineral' + i, 'mineral' + (i % 3), x, y, D.mineralScale, Math.random() * Math.PI * 2));
    // Drones: up to D.drones of them, three to an extractor and the rest mining the mineral line, back and forth from the
    // main hatchery.
    const want = Math.min(sw.drones, D.drones);
    while (B.drones.length < want) {
      const [x, y] = hatch[B.drones.length % hatch.length], h = this.r.spawn('drone', x, y - 2, BS.Terrain.height(x, y - 2), 0).setTeam(3).setScale(0.8);
      h.play(/^stand$/i, true);
      B.drones.push({ h, x, y: y - 2, tx: x, ty: y - 2, wait: Math.random() * 2, moving: false });
    }
  };
  // A base building's hit points: an entity in Game.enemies (the Terrans' guns find it; zerg do not target it) sharing
  // the part's model. Destroyed, it is gone from the base and the economy (BS.SWARM.base.hp); the main hatchery is the run.
  P.baseEntity = function (key, part) {
    const B = this.sw.base, D = BS.SWARM.base, kind = key.replace(/\d+$/, '');
    let e = B.ents[key];
    if (!e) {
      const hp = (kind === 'main' ? D.hp.main[this.sw.tier] : D.hp[kind]) * (1 + 0.5 * this.zcardLevel('hide'));
      const def = { name: kind === 'main' ? BS.SWARM.tiers[this.sw.tier].name : kind === 'hatch' ? 'Hatchery' : kind === 'extractor' ? 'Extractor' : kind === 'sunken' ? 'Sunken Colony' : 'Evolution Chamber', sunken: kind === 'sunken',
        hp, armor: 1, dmg: 0, cd: 1, range: 0, speed: 0, radius: D.radius[kind], armored: true, structure: true, baseBuilding: true, uncounted: true, ambient: true,
        bounty: D.bounty, budget: 0, unlock: 999, weight: 0, hitFrac: 0, height: 2 };
      e = B.ents[key] = { type: 'base:' + key, def, key, x: part.x, y: part.y, z: BS.Terrain.height(part.x, part.y), yaw: -Math.PI / 2, hp, maxHp: hp, radius: def.radius,
        life: 0, cd: 1, retarget: 0, target: null, attacking: false, anim: '', flash: 0, sx: 0, sy: 0, slowUntil: 0, handle: part.h, onDeath: () => this.baseLost(key) };
      this.enemies.push(e);
    }
    e.handle = part.h;
    // The main hatchery morphing: tougher, healed by what it gains.
    const mainHp = D.hp.main[this.sw.tier] * (1 + 0.5 * this.zcardLevel('hide'));
    if (kind === 'main' && e.maxHp < mainHp - 1) { const gain = mainHp - e.maxHp; e.maxHp += gain; e.hp += gain; e.def.name = BS.SWARM.tiers[this.sw.tier].name; }
  };
  P.baseLost = function (key) {
    const sw = this.sw, B = sw.base, kind = key.replace(/\d+$/, ''), i = +key.replace(/^\D+/, '');
    delete B.ents[key]; delete B.parts[key];         // (its model went with the entity)
    if (kind === 'main') return this.hiveLost();
    if (kind === 'hatch') { sw.hatchAt[i] = false; sw.hatcheries--; delete sw.inject[key]; for (let k = 0; k < BS.SWARM.base.larva; k++) this.dropPart('larva:' + key + ':' + k); }
    if (kind === 'extractor') { sw.extAt[i] = false; sw.extractors--; }
    if (kind === 'chamber') { sw.chamberAt[i] = false; sw.chambers--; sw.research[i] = null; }
    if (kind === 'sunken') sw.cards.sunken--;
    this.toast((kind === 'hatch' ? 'A hatchery' : kind === 'extractor' ? 'An extractor' : kind === 'sunken' ? 'A sunken colony' : 'An evolution chamber') + ' has been destroyed!');
    this.shake = Math.min(1, this.shake + 0.4);
  };
  P.hiveLost = function () {
    if (this.over) return;
    this.over = true; this.won = false; this.hiveDown = true;
    this.waveBanner = { text: 'THE HIVE HAS FALLEN', t: this.time };
    BS.audio.play('explosion', { vol: 0.9 });
    this.shake = 1;
    this.endRun();
  };
  // The Terran commander's raid: when the lane is empty and little is massed at the hive, a squad of stimmed marines
  // charges up the lane to burn the base (BS.SWARM.raid).
  P.terranRaid = function (r) {
    const R = BS.SWARM.raid, sw = this.sw;
    if (this.mode !== 'lane' || this.time < R.from || this.time < sw.raidAt || !this.up.stim) return;
    if (r.total - r.massed > R.maxField || r.massed > R.maxMassed) return;
    const marines = this.squad.filter(m => !m.dead && !m.raid && !m.fwd && !m.boarding && m.type === 'marine');
    if (marines.length < R.min) return;
    const D = BS.SWARM.base, route = BS.Terrain.route(D.at[0], D.at[1] - 4, false).slice().reverse().map(p => [p[0], p[1]]);
    for (const m of marines.slice(0, R.max)) m.raid = { route: route.map(p => p.slice()) };
    sw.raidAt = this.time + R.cd;
    if (this.ab.stimCd <= 0) this.useStim();
    this.waveBanner = { text: 'STIMMED MARINES RUSHING THE HIVE', t: this.time };
    this.toast('Marines are rushing the hive: hold the broods to defend it!');
  };
  // Where a raider goes next: along its route up the lane, then at the nearest base building.
  P.raidGoal = function (m) {
    const rt = m.raid.route;
    while (rt.length && Math.hypot(rt[0][0] - m.x, rt[0][1] - m.y) < 1.2) rt.shift();
    if (rt.length) return rt[0];
    let best = null, bd = 1e9;
    for (const e of this.enemies) if (e.def.baseBuilding && e.hp > 0) { const d = dist(m.x, m.y, e.x, e.y); if (d < bd) { bd = d; best = e; } }
    if (!best) return [m.x, m.y];
    const a = Math.atan2(m.y - best.y, m.x - best.x), rr = m.def.range * 0.85 + best.radius;
    return [best.x + Math.cos(a) * rr, best.y + Math.sin(a) * rr];
  };
  // Every frame: growing in, drones at work.
  P.updateBase = function (dt) {
    const B = this.sw && this.sw.base, D = BS.SWARM.base;
    if (!B) return;
    for (const p of Object.values(B.parts)) if (p.t < 1) { p.t = Math.min(1, p.t + dt / D.grow); const k = 1 - (1 - p.t) * (1 - p.t); p.h.setScale(Math.max(0.01, p.scale * k)); }
    const ext = D.extractor.spots.filter((p, i) => this.sw.extAt[i]);
    B.drones.forEach((d, n) => {
      if (d.moving) {
        const dx = d.tx - d.x, dy = d.ty - d.y, L = Math.hypot(dx, dy), s = D.droneSpeed * dt;
        if (L <= s) { d.x = d.tx; d.y = d.ty; d.moving = false; d.wait = d.home ? 0.4 : 1.5 + Math.random(); d.h.play(d.home ? /^stand$/i : /^stand work|^stand$/i, true); }
        else { d.x += dx / L * s; d.y += dy / L * s; d.h.setYaw(Math.atan2(dy, dx)); }
        d.h.setPos(d.x, d.y, BS.Terrain.height(d.x, d.y));
      } else if ((d.wait -= dt) <= 0) {
        // Out to its mineral field or extractor, then back to the hatchery with the load.
        d.home = !d.home;
        const work = n < ext.length * 3 ? ext[Math.floor(n / 3)] : D.minerals[n % D.minerals.length], [hx, hy] = D.at;
        const [x, y] = d.home ? [hx + (work[0] - hx) * 0.45, hy + (work[1] - hy) * 0.45] : work, a = Math.atan2(hy - y, hx - x);
        d.tx = x + (d.home ? 0 : Math.cos(a) * 0.9) + (Math.random() - 0.5) * 0.4; d.ty = y + (d.home ? 0 : Math.sin(a) * 0.9) + (Math.random() - 0.5) * 0.4; d.moving = true;
        d.h.play(/^walk$/i, true);
      }
    });
  };
  // ---- Mutations: the zerg's cards between rounds -------------------------------------------------------------------
  // After every Terran round the fight pauses for the hive's own shop: offers random cards from BS.SWARM.cards (weighted
  // by rarity as the Terrans' are), one of which may be bought with the swarm's minerals and vespene; rerolls cost
  // minerals. Event cards strike the Terrans at once; the rest last the run.
  P.zcard = id => BS.SWARM.cards.find(c => c.id === id);
  P.zcardLevel = function (id) { return this.sw.cards[id] || 0; };
  P.zcardCost = function (c) { const lv = this.zcardLevel(c.id), g = c.grow || [0, 0]; return [c.cost[0] + g[0] * lv, c.cost[1] + g[1] * lv]; };
  P.zcardOpen = function (c) { return this.zcardLevel(c.id) < (c.max || 99) && this.wave >= Math.max(BS.RARITY[c.rarity].from, c.from || 0) && !(c.need && !c.need(this)); };
  P.openZergShop = function () {
    if (this.over) return;
    this.shop = { zerg: true, wave: this.wave, rerolls: 0, offers: [] };
    this.shop.offers = this.rollZergOffers();
  };
  P.rollZergOffers = function () {
    const pool = BS.SWARM.cards.filter(c => this.zcardOpen(c)), out = [];
    while (out.length < BS.SWARM.shop.offers && pool.length) {
      let roll = Math.random() * pool.reduce((a, c) => a + BS.RARITY[c.rarity].weight, 0), i = 0;
      for (; i < pool.length - 1; i++) { roll -= BS.RARITY[pool[i].rarity].weight; if (roll <= 0) break; }
      out.push({ id: pool[i].id, bought: false });
      pool.splice(i, 1);
    }
    return out;
  };
  P.zergRerollCost = function () { const R = BS.SWARM.shop.reroll; return R[0] + R[1] * this.shop.rerolls; };
  P.zergReroll = function () {
    const cost = this.zergRerollCost();
    if (this.sw.minerals < cost) return this.toast('Not enough minerals');
    this.sw.minerals -= cost; this.shop.rerolls++;
    this.shop.offers = this.rollZergOffers();
    return true;
  };
  P.zergBuyCard = function (i) {
    const o = this.shop.offers[i];
    if (!o || o.bought) return false;
    const c = this.zcard(o.id), cost = this.zcardCost(c), sw = this.sw;
    if (sw.minerals < cost[0]) return this.toast('Not enough minerals');
    if (sw.gas < cost[1]) return this.toast('Not enough vespene');
    sw.minerals -= cost[0]; sw.gas -= cost[1];
    o.bought = true;
    sw.cards[c.id] = (sw.cards[c.id] || 0) + 1;
    if (c.id === 'tithe') { const m = 100 + 5 * this.wave; sw.minerals += m; this.toast('+' + m + ' minerals'); }
    if (c.id === 'gasdrop') { const v = 75 + 4 * this.wave; sw.gas += v; this.toast('+' + v + ' vespene'); }
    if (c.id === 'dronedrop') { const n = Math.min(4, BS.SWARM.drone.max - sw.drones); sw.drones += n; this.toast('+' + n + ' drones'); }
    if (c.event) this.zergEvent(c);
    if (c.id === 'sunken') this.syncBase();
    for (const x of this.shop.offers) x.bought = true;       // one card a round
    if (c.id === 'hide') for (const e of Object.values(sw.base.ents)) { e.maxHp *= 1.5 / (1 + 0.5 * (sw.cards.hide - 1)); e.hp *= 1.5 / (1 + 0.5 * (sw.cards.hide - 1)); }
    this.toast(c.name + ((c.max || 99) > 1 && c.id !== 'tithe' ? ' level ' + sw.cards[c.id] : ''));
    return true;
  };
  // Event cards: the swarm strikes at once.
  P.zergEvent = function (c) {
    const lane = this.mode === 'lane', ang = () => lane ? pick([0, 0, 1, 2]) : Math.random() * Math.PI * 2;
    switch (c.id) {
      case 'hydras': for (let i = 0; i < 10 + 2 * this.sw.tier; i++) this.spawnEnemy('hydralisk', ang()); break;
      case 'ultras': for (let i = 0; i < 3 + this.sw.tier; i++) this.spawnEnemy('ultralisk', ang()); break;
      case 'nydus': for (let i = 0; i < 2; i++) this.spawnEnemy('nydus', 0); break;
      case 'pods': this.dropPods(4, 1, 2); break;
      case 'darkswarm': this.swarms.push({ x: 0, y: 0, r: 4.5, until: this.time + 15, fx: this.fx.play('darkSwarm', 0, 0, 0, 0, 2) }); break;
      case 'plague':
        for (const tg of this.targets()) if (!tg.structure && Math.hypot(tg.x, tg.y) < 9) { tg.obj.plagueUntil = this.time + 6; this.fx.play('plague', tg.x, tg.y, BS.Terrain.height(tg.x, tg.y) + 0.5, 0); }
        BS.audio.play('plague', { vol: 0.7 });
        break;
    }
    this.waveBanner = { text: c.name.toUpperCase(), t: this.time };
    this.shake = Math.min(1, this.shake + 0.4);
  };
  // Sunken colonies: the base's guns, firing on any Terran in reach (raiders, mostly).
  P.updateSunkens = function (dt) {
    const S = BS.SWARM.sunken;
    for (const e of Object.values(this.sw.base.ents)) {
      if (!e.def.sunken || e.hp <= 0 || (e.cd -= dt) > 0) continue;
      let best = null, bd = S.range;
      for (const t of this.tgs) { if (t.kind === 'bunker') continue; const d = dist(e.x, e.y, t.x, t.y) - t.radius; if (d < bd) { bd = d; best = t; } }
      if (!best) { e.cd = 0.3; continue; }
      e.cd = S.cd;
      e.handle.play(/^attack/i, false);
      this.damageDefender(best, S.dmg);
      this.fx.play('ultraliskImpact', best.x, best.y, BS.Terrain.height(best.x, best.y), 0, 0.8);
      BS.audio.play('ultraliskAttack', { vol: 0.4, gap: 0.2 });
    }
  };
  // Brood Mothers: the queens inject on their own, a little after each inject is ready (BS.SWARM.autoInject of its
  // cooldown late).
  P.autoInject = function () {
    if (!this.sw.cards.autoinject) return;
    const late = BS.SWARM.inject.cd * BS.SWARM.autoInject;
    this.hatchSpots().forEach(([, , key], i) => { if (this.injectReady(i) && this.time >= (this.sw.inject[key] || 0) + late) this.inject(i); });
  };

  // ---- Base micro ------------------------------------------------------------------------------------------------
  // Red light, green light: while the broods are held they mass at the base (counting against the alive cap); released,
  // everything held charges together in a frenzy (BS.SWARM.hold).
  P.broodHold = function (on) {
    const sw = this.sw, H = BS.SWARM.hold;
    if (on === undefined) on = !sw.hold;
    sw.hold = on;
    if (on) return this.toast('Broods hold at the hive');
    let n = 0;
    for (const e of this.enemies) if (e.held) { e.held = false; e.frenzyUntil = this.time + H.frenzy + 3 * this.zcardLevel('frenzy'); e.retarget = 0; n++; }
    if (n) {
      this.waveBanner = { text: 'THE SWARM ATTACKS  //  ' + n + ' STRONG', t: this.time };
      BS.audio.play('finaleRoar', { vol: Math.min(1, 0.4 + n / 80) , gap: 2 });
    }
    return true;
  };
  // A held brood member with a Terran raider close: it fights instead of waiting.
  P.heldWakes = function (e) { for (const t of this.tgs) if (t.kind !== 'bunker' && dist(e.x, e.y, t.x, t.y) < BS.SWARM.raid.wake) return true; return false; };
  // How many the hive can hold massed: BS.SWARM.hold.cap + perWave a wave.
  P.holdCap = function () { const H = BS.SWARM.hold; return H.cap + H.perWave * this.wave; };
  P.heldCount = function () { let n = 0; for (const e of this.enemies) if (e.held && e.hp > 0) n++; return n; };

  // Queen injects: a hatchery with a queen of its own (one per Spawn Larva level, main hatchery first) can be injected
  // every inject.cd s for a burst of larva. Tap the hatchery when it glows.
  // The standing hatcheries, main first: [x, y, key].
  P.hatchSpots = function () { const D = BS.SWARM.base, out = [[D.at[0], D.at[1], 'main']]; D.hatch.spots.forEach(([x, y], i) => { if (this.sw.hatchAt[i]) out.push([x, y, 'hatch' + i]); }); return out; };
  P.injectReady = function (i) { const h = this.hatchSpots()[i]; return !!h && i < this.sw.queens && this.time >= (this.sw.inject[h[2]] || 0); };
  P.inject = function (i) {
    const sw = this.sw, I = BS.SWARM.inject;
    if (i >= sw.queens) return this.toast('That hatchery needs a queen: Spawn Larva');
    const [x, y, key] = this.hatchSpots()[i];
    if (!this.injectReady(i)) return this.toast('Inject ready in ' + Math.ceil(sw.inject[key] - this.time) + 's');
    const gain = I.larva + I.perTier * sw.tier;
    sw.larva += gain; sw.inject[key] = this.time + I.cd;
    this.fx.play('broodSpellImpact', x, y, BS.Terrain.height(x, y), 0, 1.4);
    this.floater(x, y, 3, '+' + gain + ' LARVA', '#8de08a');
    return true;
  };

  // Evolution chambers: melee, missile and carapace levels are researched in one, a level at a time over
  // chamber.times s. Each round (wave) idle chambers start the next affordable one on their own (unless AUTO is off);
  // tapping a chamber starts one now.
  P.chamberLines = () => BS.SWARM.evo.filter(v => v.costs);
  P.researching = function (id) { return this.sw.research.find(r => r && r.id === id) || null; };
  P.idleChamber = function () { const sw = this.sw; for (let i = 0; i < sw.chamberAt.length; i++) if (sw.chamberAt[i] && !sw.research[i]) return i; return -1; };
  // The evolution an idle chamber would take on next: the lowest level it can afford, melee before missile before carapace.
  P.nextEvolution = function () {
    let best = null;
    for (const v of this.chamberLines()) {
      if (this.swarmState('evo', v.id)) continue;
      const c = this.swarmCost('evo', v.id);
      if (this.sw.minerals < c[0] || this.sw.gas < c[1]) continue;
      if (!best || (this.sw.evo[v.id] || 0) < (this.sw.evo[best] || 0)) best = v.id;
    }
    return best;
  };
  P.chamberPull = function (ci) {
    const sw = this.sw, r = sw.research[ci];
    if (r) return this.toast(BS.SWARM.evo.find(v => v.id === r.id).name + ': ' + Math.ceil(r.dur - r.t) + 's');
    const id = this.nextEvolution();
    return id ? this.swarmBuy('evo', id) : this.toast('Nothing to evolve that the hive can afford');
  };
  P.chamberAuto = function () {
    if (!this.sw.autoEvo) return;
    for (let guard = 0; guard < 3 && this.idleChamber() >= 0; guard++) { const id = this.nextEvolution(); if (!id) break; this.swarmBuy('evo', id); }
  };
  P.updateResearch = function (dt) {
    const sw = this.sw;
    sw.research.forEach((r, i) => {
      if (!r || (r.t += dt) < r.dur) return;
      sw.research[i] = null;
      sw.evo[r.id] = (sw.evo[r.id] || 0) + 1;
      this.toast('Evolved: ' + BS.SWARM.evo.find(v => v.id === r.id).name + ' level ' + sw.evo[r.id]);
    });
  };

  // A tap on the base: a hatchery injects, a chamber evolves, the chrysalis summons Kerrigan. False when the tap hit none
  // of them (it sends Kerrigan instead).
  P.baseTap = function (x, y) {
    const D = BS.SWARM.base, sw = this.sw, near = (p, r) => Math.hypot(p[0] - x, p[1] - y) < r;
    if (this.mode !== 'lane') return false;
    if (sw.base.parts.chrysalis && near(D.chrysalis, 1.8)) { this.swarmBuy('hero'); return true; }
    for (let i = 0; i < D.chamber.spots.length; i++) if (sw.chamberAt[i] && near(D.chamber.spots[i], 2)) { this.chamberPull(i); return true; }
    const hs = this.hatchSpots();
    for (let i = 0; i < hs.length; i++) if (near(hs[i], i ? 2.4 : 3.2)) { this.inject(i); return true; }
    return false;
  };

  // Where a brood member comes out: at the foot of one of the hatcheries (the east or west ones for the arms).
  P.broodSpawn = function (ang) {
    const list = this.hatchSpots();
    const side = ang === 1 ? list.filter(p => p[0] > 0.5) : ang === 2 ? list.filter(p => p[0] < -0.5) : list;
    const [x, y] = pick(side.length ? side : list);
    return [x + rnd(-1.6, 1.6), y - rnd(2.2, 3.4)];
  };

  // ---- The Terran commander ------------------------------------------------------------------------------------
  // The balance bots' commander, made to read the swarm: every half second it looks at what is coming (and what is
  // massing at the hive, and which strains the hive has bred) and answers it: firebats and siege tanks against massed
  // or light broods, marauders against armour, marines, missile turrets and vikings against air; its shop picks follow
  // the same reading. It also repairs, stims and calls strikes and high tech.
  P.terranRead = function () {
    const r = { light: 0, armored: 0, air: 0, massed: 0, total: 0 }, sw = this.sw;
    for (const e of this.enemies) {
      const d = e.def;
      if (e.hp <= 0 || d.uncounted || d.ambient || d.tumor || d.boss || d.baseBuilding) continue;
      r.total++;
      if (e.held) r.massed++;
      if (d.flying) r.air++; else if (d.armored) r.armored++; else r.light++;
    }
    // What the hive has bred counts too (the commander scouts its tech).
    r.airTech = ['mutalisk', 'broodlord', 'guardian', 'devourer', 'scourge', 'viper', 'broodqueen'].some(k => sw.strains[k]);
    r.armorTech = ['roach', 'lurker', 'ultralisk'].some(k => sw.strains[k]);
    r.splash = r.massed + r.light >= BS.SWARM.terran.mass;
    return r;
  };
  // The trooper the swarm calls for now.
  P.terranTroop = function (r) {
    if (r.air >= Math.max(4, r.total * 0.3)) return 'marine';
    if (r.armored > r.light * 0.5 || (r.armorTech && r.armored >= 3)) return 'marauder';
    if (r.light >= 10 || r.massed >= 10) return 'firebat';
    return 'marine';
  };
  P.terranThink = function () {
    const g = this, T = BS.TERRAN, r = g.terranRead(), n = r.total;
    const air = r.air > 0 || r.airTech;
    if (g.bunker.hp < g.bunker.maxHp * 0.45) g.emergencyRepair();
    if (g.up.stim && g.ab.stimCd <= 0 && n - r.massed > 6) g.useStim();
    if (n - r.massed > 14 && g.ab.strikeCd <= 0 && g.minerals > 260) g.callAirStrike();
    if ((n > 18 || g.heroAlive()) && g.ab.yamatoCd <= 0 && g.bc && g.minerals > T.yamato.cost + 100) g.useYamato();
    if (g.techUnlocked.nuke && g.techCd.nuke <= 0 && n > 40 && g.minerals > 600) g.buyTech('nuke');
    if (g.techUnlocked.vikings && g.techCd.vikings <= 0 && r.air > 0 && g.minerals > 500) g.buyTech('vikings');
    if (g.techUnlocked.wraiths && g.techCd.wraiths <= 0 && r.air > 2 && g.minerals > 500) g.buyTech('wraiths');
    if (g.pendingInfantry()) return;        // the last trooper reaches the bunker first
    const N = g.bunker.units.length;
    // A full bunker first, of what the swarm calls for (a firebat before anything else).
    if (!g.bunker.units.some(u => u.type === 'firebat')) { if (g.minerals >= 100) g.addInfantry('firebat'); return; }
    if (N < 3 && g.minerals >= 50) return g.addInfantry('marine');
    if (N < g.bunker.slots) { if (g.minerals >= 100) g.addInfantry(g.terranTroop(r)); return; }
    // Splash before anything when the broods mass or come thick and light.
    if (r.splash && g.tanks.length < g.tankMax) { if (g.minerals >= g.tankCost()) g.dropTank(); return; }
    if (g.tanks.length < 1) { if (g.minerals >= g.tankCost()) g.dropTank(); return; }
    if (air && g.samCount() < (r.air > 6 ? T.sam.max : 2)) { if (g.minerals >= T.sam.cost) g.buildStructure('sam'); return; }
    if (g.scvs.length < 2 && g.minerals >= 50) return g.addScv();
    if (g.tanks.length < Math.min(g.tankMax, 2) && g.minerals >= g.tankCost()) return g.dropTank();
    if (g.tanks.length < g.tankMax && g.minerals >= g.tankCost()) return g.dropTank();
    // Late game: several purchases a think, each falling through when it is not possible right now.
    for (const k of ['goliath', 'thor', 'odin']) if (g.techUnlocked[k] && g.techCount(k) < T[k].max && g.minerals >= T[k].cost) g.buyTech(k);
    if (g.techUnlocked.bc && !g.bc && g.minerals >= T.bc.cost) g.buyTech('bc');
    if (g.scvs.length < 4 && g.minerals >= 50) g.addScv();
    // Troops outside: early when the swarm is massing or thick, otherwise once the research is done or the bank is big.
    const treesDone = ['weapons', 'vehicle', 'plating', 'rails'].every(id => !g.upgradeAvailable(id));
    const R = BS.SWARM.raid, quiet = g.up.stim && g.time > R.from && r.total - r.massed <= R.maxField;
    const floor = r.splash || r.total >= 30 || quiet ? 150 : treesDone ? 150 : 800;
    for (let i = 0; i < 6 && g.squad.length + g.pendingInfantry() < T.squad.max && g.minerals >= floor; i++) if (g.addInfantry(g.terranTroop(r)) !== true) break;
    if (g.minerals > 2000 && g.bunker.hp < g.bunker.maxHp * 0.8) g.emergencyRepair();
    g.terranRaid(r);
  };
  // Its shop between rounds: the cards that answer what it reads first, then research, then high tech, rerolling while
  // it can afford to.
  const PRI = 'weapons,neosteel,stim,medivac,plating,hisec,vehicle,rails,refinery,shield,firesup,spoils,incinerator,bounty,mule,scvrepair,medpacks,concussive,rotors,recruit,siegerange,goliath,thor,nuke,bc,odin,vikings'.split(',');
  P.terranShop = function () {
    const g = this, r = g.terranRead(), first = [];
    if (r.splash || r.light > r.armored) first.push('incinerator', 'siegerange', 'firesup', 'shield');
    if (r.armored > r.light * 0.5 || r.armorTech) first.push('concussive', 'vehicle', 'goliath');
    if (r.air > 0 || r.airTech) first.push('vikings', 'wraiths', 'goliath', 'ship');
    if (g.heroAlive()) first.push('bc', 'nuke');
    const pri = first.concat(PRI.filter(id => !first.includes(id)));
    for (let pass = 0; pass < 4 && g.shop; pass++) {
      const offers = g.shop.offers.map((o, i) => ({ o, i, p: pri.indexOf(o.id) })).filter(x => !x.o.bought && x.p >= 0).sort((a, b) => a.p - b.p);
      let any = false;
      for (const x of offers) { const c = g.card(x.o.id); if ((x.p < 11 || g.minerals > 500) && g.minerals >= g.cardCost(c) && g.buyCard(x.i) === true) any = true; }
      if (!any && g.minerals > g.rerollCost() + 150) { g.reroll(); continue; }
      break;
    }
    g.closeShop();
  };
})(window.BS);
