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
  const P = BS.Game.prototype;

  P.initSwarm = function () {
    const S = BS.SWARM;
    this.sw = { minerals: S.minerals, gas: S.gas, drones: 0, extractors: 0, hatcheries: 1, queens: 0, tier: 0, strains: { zergling: true },
      evo: { melee: 0, missile: 0, carapace: 0 }, larva: 0, broodT: S.brood, kills: 0, heroAt: 0, hero: null, next: null, ang: 0, think: 0 };
    this.income *= S.terran.income;
    // Every Terran the swarm kills feeds it (BS.SWARM.bounty).
    const hurt = this.damageDefender;
    this.damageDefender = function (tg, dmg) {
      const alive = tg && tg.obj && !tg.obj.dead && tg.kind !== 'bunker';
      hurt.call(this, tg, dmg);
      if (alive && tg.obj.dead) {
        const b = BS.SWARM.bounty, m = tg.kind === 'mech' ? b.mech * (tg.obj.kind === 'odin' ? 3 : tg.obj.kind === 'thor' ? 2 : 1) : b[tg.kind] || b.air;
        this.sw.minerals += m; this.sw.kills++;
        this.floater(tg.x, tg.y, 2, '+' + m, '#8de08a');
      }
    };
  };

  // ---- The swarm's economy ------------------------------------------------------------------------------------
  P.swarmIncome = function () { const S = BS.SWARM; return S.income + S.drone.income * this.sw.drones; };
  P.swarmGasIncome = function () { return BS.SWARM.extractor.gas * this.sw.extractors; };
  // Larva (the brood budget, BS.ZERG budget) a second: hatcheries, more with each Spawn Larva.
  P.larvaRate = function () { const S = BS.SWARM; return S.hatch.larva * this.sw.hatcheries * (1 + S.queen.boost * this.sw.queens); };

  P.updateSwarm = function (dt) {
    const S = BS.SWARM, sw = this.sw;
    sw.minerals += this.swarmIncome() * dt;
    sw.gas += this.swarmGasIncome() * dt;
    sw.larva = Math.min(this.larvaRate() * S.brood * S.bank, sw.larva + this.larvaRate() * dt);
    if ((sw.broodT -= dt) <= 0) { sw.broodT = S.brood; this.hatchBrood(); }
    // Kerrigan down: she can be summoned again once the hive has recovered (BS.SWARM.hero.respawn).
    if (sw.hero && sw.hero.hp <= 0) { sw.hero = null; sw.heroAt = this.time + S.hero.respawn; this.toast('Kerrigan has fallen: summon her again when the hive recovers'); }
    // The Terran commander.
    if ((sw.think -= dt) <= 0) { sw.think = 0.5; this.terranThink(); }
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
      sw.larva -= this.strainBudget(sw.next);
      this.spawnEnemy(sw.next.id, ang);
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
  P.swarmCost = function (kind, id) {
    const S = BS.SWARM, sw = this.sw;
    switch (kind) {
      case 'drone': return sw.drones < S.drone.max ? [S.drone.cost, 0] : null;
      case 'extractor': return sw.extractors < S.extractor.max ? [S.extractor.cost, 0] : null;
      case 'hatch': return sw.hatcheries - 1 < S.hatch.costs.length ? [S.hatch.costs[sw.hatcheries - 1], 0] : null;
      case 'queen': return sw.queens < S.queen.costs.length ? [S.queen.costs[sw.queens], 0] : null;
      case 'tier': return sw.tier + 1 < S.tiers.length ? S.tiers[sw.tier + 1].cost : null;
      case 'hero': return this.heroAlive() ? null : S.hero.cost;
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
  // (Kerrigan, until the hive recovers from her fall).
  P.swarmState = function (kind, id) {
    if (!this.swarmCost(kind, id)) return 'max';
    if (this.swarmNeeds(kind, id) > this.sw.tier) return 'locked';
    if (kind === 'hero' && this.time < this.sw.heroAt) return 'wait';
    if (kind === 'evo') { const v = BS.SWARM.evo.find(x => x.id === id); if (v.need && !this.sw.strains[v.need]) return 'need'; }
    return '';
  };
  P.swarmBuy = function (kind, id) {
    const S = BS.SWARM, sw = this.sw, st = this.swarmState(kind, id), cost = this.swarmCost(kind, id);
    if (st === 'max' || st === 'wait') return false;
    if (st === 'locked') return this.toast('Needs a ' + S.tiers[this.swarmNeeds(kind, id)].name);
    if (st === 'need') return this.toast('Breed ' + BS.ZERG[S.evo.find(x => x.id === id).need].name + 's first');
    if (sw.minerals < cost[0]) return this.toast('Not enough minerals');
    if (sw.gas < cost[1]) return this.toast('Not enough vespene');
    sw.minerals -= cost[0]; sw.gas -= cost[1];
    switch (kind) {
      case 'drone': sw.drones++; break;
      case 'extractor': sw.extractors++; break;
      case 'hatch': sw.hatcheries++; this.toast('Hatchery ' + sw.hatcheries + ' is up'); break;
      case 'queen': sw.queens++; this.toast('Spawn Larva level ' + sw.queens); break;
      case 'tier': {
        sw.tier++;
        const t = S.tiers[sw.tier];
        this.waveBanner = { text: 'THE ' + t.name.toUpperCase() + ' IS COMPLETE', t: this.time };
        this.toast(sw.tier === S.hero.tier ? 'The hive can summon Kerrigan' : 'New strains and evolutions open');
        break;
      }
      case 'strain': sw.strains[id] = true; this.toast(S.strains.find(s => s.id === id).name + ' join the broods'); break;
      case 'evo': {
        sw.evo[id] = (sw.evo[id] || 0) + 1;
        const v = S.evo.find(x => x.id === id);
        this.toast('Evolved: ' + v.name + (v.costs ? ' level ' + sw.evo[id] : ''));
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
    const F = BS.FINALE, e = this.spawnEnemy('kerrigan', 0);
    e.hero = true; e.order = null; e.hold = null; e.power = BS.SWARM.hero.power;
    this.sw.hero = e;
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

  // ---- The Terran commander ------------------------------------------------------------------------------------
  // The balance bots' "balanced" commander (the Unreal build's AutoPlay, extended with the high-tech cards): every half
  // second it repairs, stims, calls strikes, buys troops, tanks, SCVs, turrets and tech.
  P.terranThink = function () {
    const g = this, T = BS.TERRAN, n = g.enemies.length;
    const air = () => g.enemies.some(e => e.hp > 0 && e.def.flying);
    if (g.bunker.hp < g.bunker.maxHp * 0.45) g.emergencyRepair();
    if (g.up.stim && g.ab.stimCd <= 0 && n > 6) g.useStim();
    if (n > 14 && g.ab.strikeCd <= 0 && g.minerals > 260) g.callAirStrike();
    if (n > 18 && g.ab.yamatoCd <= 0 && g.bc && g.minerals > T.yamato.cost + 100) g.useYamato();
    if (g.techUnlocked.nuke && g.techCd.nuke <= 0 && n > 40 && g.minerals > 600) g.buyTech('nuke');
    if (g.techUnlocked.vikings && g.techCd.vikings <= 0 && air() && g.minerals > 500) g.buyTech('vikings');
    const N = g.bunker.units.length + g.pendingInfantry();
    const fb = g.bunker.units.filter(u => u.type === 'firebat').length, nm = g.bunker.units.filter(u => u.type === 'marauder').length;
    if (g.pendingInfantry()) return;        // the last trooper reaches the bunker first
    if (fb < 1) { if (g.minerals >= 100) g.addInfantry('firebat'); return; }
    if (g.bunker.slots > 4 && fb < 2) { if (g.minerals >= 100) g.addInfantry('firebat'); return; }
    if (g.bunker.slots > 4 && nm < 1) { if (g.minerals >= 100) g.addInfantry('marauder'); return; }
    if (N < 3 && g.minerals >= 50) return g.addInfantry('marine');
    if (N >= 4 && g.tanks.length < 1) { if (g.minerals >= g.tankCost()) g.dropTank(); return; }
    if (N < g.bunker.slots && g.minerals >= 100) return g.addInfantry(fb < 1 ? 'firebat' : (N % 2 ? 'marauder' : 'marine'));
    if (g.scvs.length < 2 && g.minerals >= 50) return g.addScv();
    if (g.tanks.length < Math.min(g.tankMax, 2) && g.minerals >= g.tankCost()) return g.dropTank();
    if (g.tanks.length < g.tankMax && g.minerals >= g.tankCost()) return g.dropTank();
    if (air() && g.samCount() < 2 && g.minerals >= 100) return g.buildStructure('sam');
    // Late game: several purchases a think, each falling through when it is not possible right now.
    for (const k of ['goliath', 'thor', 'odin']) if (g.techUnlocked[k] && g.techCount(k) < T[k].max && g.minerals >= T[k].cost) g.buyTech(k);
    if (g.techUnlocked.bc && !g.bc && g.minerals >= T.bc.cost) g.buyTech('bc');
    if (g.scvs.length < 4 && g.minerals >= 50) g.addScv();
    const treesDone = ['weapons', 'vehicle', 'plating', 'rails'].every(id => !g.upgradeAvailable(id));
    for (let i = 0; i < 6 && g.squad.length + g.pendingInfantry() < T.squad.max && g.minerals >= (treesDone ? 150 : 800); i++) if (g.addInfantry(['marine', 'marauder', 'firebat'][(g.squad.length + i) % 3]) !== true) break;
    if (g.minerals > 2000 && g.bunker.hp < g.bunker.maxHp * 0.8) g.emergencyRepair();
  };
  // Its shop between rounds: research first, then high tech, rerolling while it can afford to.
  const PRI = 'weapons,neosteel,stim,medivac,plating,hisec,vehicle,rails,refinery,shield,firesup,spoils,incinerator,bounty,mule,scvrepair,medpacks,concussive,rotors,recruit,siegerange,goliath,thor,nuke,bc,odin,vikings'.split(',');
  P.terranShop = function () {
    const g = this;
    for (let pass = 0; pass < 4 && g.shop; pass++) {
      const offers = g.shop.offers.map((o, i) => ({ o, i, p: PRI.indexOf(o.id) })).filter(x => !x.o.bought && x.p >= 0).sort((a, b) => a.p - b.p);
      let any = false;
      for (const x of offers) { const c = g.card(x.o.id); if ((x.p < 11 || g.minerals > 500) && g.minerals >= g.cardCost(c) && g.buyCard(x.i) === true) any = true; }
      if (!any && g.minerals > g.rerollCost() + 150) { g.reroll(); continue; }
      break;
    }
    g.closeShop();
  };
})(window.BS);
