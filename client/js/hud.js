// HUD: command bar, squad card, radar, abilities, armoury, the shop between waves, toasts, wave banner, and the overlay canvas
// (floating text, health bars, focus ring, off-screen threat markers). Mirrors the Unreal build's HUD design.
window.BS = window.BS || {};
(function (BS) {
  'use strict';

  const $ = id => document.getElementById(id);
  const icon = name => 'icon/' + name + '.png';
  const pad2 = n => (n < 10 ? '0' : '') + n;

  class Hud {
    constructor(game, renderer) {
      this.g = game; this.r = renderer;
      this.ctx = $('fx').getContext('2d');
      this.radar = $('radar'); this.rctx = this.radar.getContext('2d');
      this.cache = {};
      this.toastShown = -1; this.toastT = 0; this.bannerSerial = null;
      this.buildSegments();
      if (game.swarm) { $('broodBtn').onclick = () => game.broodHold(); $('hud').classList.add('swarm'); $('armouryLabel').textContent = 'HIVE CLUSTER'; this.buildSwarmAbilities(); this.buildSwarmArmoury(); }
      else { this.buildAbilities(); this.buildArmoury(); }
      // Tap a trooper in the bunker to send them outside; tap one outside to bring them back in (not the Terrans' in
      // swarm mode: the squad card there is the enemy's).
      $('slots').onclick = ev => { const el = ev.target.closest('[data-i]'); if (el && !game.swarm) game.eject(+el.dataset.i); };
      $('outside').onclick = ev => { const el = ev.target.closest('[data-o]'); if (el && !game.swarm) game.board(+el.dataset.o); };
      $('bunkerPortrait').onclick = () => this.toggleView();
      $('armouryToggle').onclick = () => { const c = $('armoury').classList.toggle('closed'); $('armouryToggle').classList.toggle('closed', c); };
      $('shopCards').onclick = ev => { const el = ev.target.closest('[data-c]'); if (el) game.buyCard(+el.dataset.c); };
      $('shopReroll').onclick = () => game.reroll();
      $('shopGo').onclick = () => game.closeShop();
      $('shopPeek').onclick = () => { const p = $('shop').classList.toggle('peek'); $('shopPeek').textContent = p ? 'SHOP' : 'ARMOURY'; };
    }

    // Sky view <-> bunker view. The bunker itself is hidden while we look out of it.
    toggleView() {
      const fp = !this.r.cam.fp;
      this.r.setBunkerView(fp);
      if (!fp) this.r.setCameraMode(this.g.mode);
      const h = this.g.bunker.handle;
      if (h && !this.g.bunker.dead) h.setScale(fp ? 0.001 : 1);
      $('hud').classList.toggle('fp', fp);
      $('bunkerPortrait').classList.toggle('on', fp);
      $('bunkerPortrait').title = fp ? 'Tap: back to the sky view' : 'Tap: view from inside the bunker';
    }

    set(id, val) { if (this.cache[id] !== val) { this.cache[id] = val; $(id).textContent = val; } }

    buildSegments() { const s = $('segments'); s.innerHTML = ''; for (let i = 0; i < 10; i++) s.appendChild(document.createElement('i')); }

    buildAbilities() {
      const box = $('abilities'); box.innerHTML = '';
      this.abEls = BS.ABILITIES.map(a => {
        const b = document.createElement('button');
        b.className = 'ability'; b.style.setProperty('--accent', a.accent);
        b.innerHTML = '<div class="tile"><img src="' + icon(a.icon) + '" alt=""><div class="cd"></div></div><b>' + a.name + '</b><small></small>';
        b.onclick = () => this.useAbility(a.id);
        box.appendChild(b);
        return { a, el: b, cd: b.querySelector('.cd'), sub: b.querySelector('small') };
      });
    }
    useAbility(id) {
      const g = this.g;
      if (id === 'stim') g.useStim(); else if (id === 'strike') g.callAirStrike(); else if (id === 'repair') g.emergencyRepair(); else g.useYamato();
    }

    // Armoury: troops, support and turrets, one card each (research comes from the shop). Native horizontal scrolling means a
    // swipe never buys.
    buildArmoury() {
      const g = this.g, T = BS.TERRAN;
      const items = [];
      // High tech first: hidden until a shop card puts it here.
      for (const c of BS.TECH) items.push({ name: c.name, tech: c.id, icon: () => c.icon, cost: () => g.techState(c.id).cost,
        state: () => g.techState(c.id).state, wait: () => g.techState(c.id).wait, hidden: () => !g.techUnlocked[c.id], buy: () => g.buyTech(c.id) });
      for (const [k, d] of Object.entries(BS.INFANTRY)) items.push({ name: d.name, icon: () => d.icon, cost: () => g.troopCost(k),
        state: () => g.troopsFull() ? 'full' : '', buy: () => g.addInfantry(k) });
      items.push({ name: 'SCV', icon: () => 'btn-unit-terran-scv', cost: () => T.scv.cost, state: () => g.scvs.length >= T.scv.max ? 'max' : '', buy: () => g.addScv() });
      items.push({ name: 'Missile Turret', icon: () => 'btn-building-terran-missileturret', cost: () => T.sam.cost, state: () => g.samCount() >= T.sam.max ? 'max' : '', buy: () => g.buildStructure('sam') });
      items.push({ name: 'Sensor Tower', icon: () => 'btn-building-terran-sensortower-silver', cost: () => T.sensor.cost, state: () => g.structures.some(s => s.kind === 'sensor') ? 'max' : '', buy: () => g.buildStructure('sensor') });
      items.push({ name: 'Siege Tank', icon: () => 'btn-unit-terran-siegetank', cost: () => g.tankCost(), state: () => g.tanks.length >= g.tankMax ? 'max' : '', buy: () => g.dropTank() });
      const box = $('armoury'); box.innerHTML = '';
      this.items = items.map(it => {
        const el = document.createElement('button');
        el.className = 'item' + (it.tech ? ' tech' : '');
        el.innerHTML = '<img alt=""><b></b><span class="price"></span><span class="lvl"></span>';
        el.querySelector('b').textContent = it.name.toUpperCase();
        el.onclick = () => it.buy();
        box.appendChild(el);
        return { it, el, img: el.querySelector('img'), price: el.querySelector('.price'), lvl: el.querySelector('.lvl'), last: '' };
      });
    }

    // Swarm mode: Kerrigan's button first (summon her; once she is out, send her back to advancing with the swarm), then
    // her spells.
    buildSwarmAbilities() {
      const g = this.g, box = $('abilities'), S = BS.SWARM;
      box.innerHTML = '';
      const mk = (id, name, ic, accent, onclick) => {
        const b = document.createElement('button');
        b.className = 'ability'; b.style.setProperty('--accent', accent);
        b.innerHTML = '<div class="tile"><img src="' + icon(ic) + '" alt=""><div class="cd"></div></div><b>' + name + '</b><small></small>';
        b.onclick = onclick;
        box.appendChild(b);
        return { a: { id }, el: b, cd: b.querySelector('.cd'), sub: b.querySelector('small'), name: b.querySelector('b') };
      };
      this.abEls = [mk('hero', 'KERRIGAN', 'btn-unit-zerg-kerriganinfested', '#ff7ad9', () => this.heroButton())]
        .concat(S.abilities.map(a => mk(a.id, a.name, a.icon, a.accent, () => g.heroCast(a.id))));
    }
    heroButton() { const g = this.g; if (g.heroAlive()) g.heroAdvance(); else g.swarmBuy('hero'); }
    // Red light, green light.
    refreshBrood() {
      const g = this.g, n = g.heldCount(), key = g.sw.hold + '|' + n;
      if (this.cache.brood === key) return;
      this.cache.brood = key;
      const b = $('broodBtn');
      b.classList.toggle('hold', g.sw.hold); b.classList.toggle('massed', !g.sw.hold && n > 0);
      b.innerHTML = g.sw.hold ? '<b>HOLDING</b><small>' + n + ' / ' + g.holdCap() + ' MASSED</small>' : n ? '<b>ATTACK</b><small>' + n + ' MASSED</small>' : '<b>ATTACKING</b><small>TAP TO HOLD</small>';
    }
    // Swarm mode: the hive cluster. Economy, the tiers, strains to breed, evolutions; two prices (minerals + vespene).
    buildSwarmArmoury() {
      const g = this.g, S = BS.SWARM, items = [];
      const price = (k, id) => { const c = g.swarmCost(k, id); return c ? c[0] + (c[1] ? ' + ' + c[1] + 'g' : '') : ''; };
      const poor = (k, id) => { const c = g.swarmCost(k, id); return !!c && (g.sw.minerals < c[0] || g.sw.gas < c[1]); };
      const label = (k, id, st, max) => {
        if (k === 'chamber' && st === 'max') return g.sw.autoEvo ? 'AUTO' : 'MANUAL';
        if (st === 'busy') { const r = g.researching(id); return Math.ceil(r.dur - r.t) + 's'; }
        return st === 'max' ? max : st === 'locked' ? S.tiers[g.swarmNeeds(k, id)].name.toUpperCase() : st === 'need' ? 'LOCKED' : st === 'nochamber' ? 'CHAMBER' : st === 'full' ? 'BUSY' : null;
      };
      const add = (k, id, name, ic, lvl, max) => items.push({ name, icon: ic, cost: () => price(k, id), poor: () => poor(k, id), state: () => g.swarmState(k, id),
        label: st => label(k, id, st, max || 'MAX'), level: lvl, buy: () => g.swarmBuy(k, id), swarm: true });
      add('drone', null, 'Drone', () => 'btn-unit-zerg-drone', () => String(g.sw.drones));
      add('extractor', null, 'Extractor', () => 'btn-building-zerg-extractor', () => String(g.sw.extractors));
      add('hatch', null, 'Hatchery', () => 'btn-building-zerg-hatchery', () => g.sw.hatcheries + '  ' + g.larvaRate().toFixed(1) + '/s');
      add('queen', null, 'Spawn Larva', () => 'btn-unit-zerg-queen', () => g.sw.queens ? 'LV ' + g.sw.queens : '');
      add('chamber', null, 'Evolution Chamber', () => 'btn-building-zerg-evolutionchamber', () => g.sw.chambers ? String(g.sw.chambers) : '', 'AUTO');
      add('tier', null, 'Evolve Tier', () => S.tiers[Math.min(g.sw.tier + 1, S.tiers.length - 1)].icon, () => S.tiers[g.sw.tier].name.toUpperCase(), 'HIVE');
      for (const s of S.strains) add('strain', s.id, s.name, () => s.icon, null, 'BRED');
      for (const v of S.evo) add('evo', v.id, v.name, () => v.icons ? v.icons[Math.min(g.sw.evo[v.id] || 0, v.icons.length - 1)] : v.icon, v.costs ? () => (g.sw.evo[v.id] ? 'LV ' + g.sw.evo[v.id] : '') : null, 'DONE');
      const box = $('armoury'); box.innerHTML = '';
      this.items = items.map(it => {
        const el = document.createElement('button');
        el.className = 'item';
        el.innerHTML = '<img alt=""><b></b><span class="price"></span><span class="lvl"></span>';
        el.querySelector('b').textContent = it.name.toUpperCase();
        el.onclick = () => it.buy();
        box.appendChild(el);
        return { it, el, img: el.querySelector('img'), price: el.querySelector('.price'), lvl: el.querySelector('.lvl'), last: '' };
      });
    }
    // Swarm mode: Kerrigan's bar.
    refreshSwarmAbilities() {
      const g = this.g, S = BS.SWARM, e = g.heroAlive(), K = BS.FINALE.kit;
      const max = { leap: BS.FINALE.leap.cd, apocalypse: BS.FINALE.apocalypse.cd, kinetic: K.kinetic.cd, grip: K.grip.cd, chain: K.chain.cd, mutation: K.mutation.cd };
      for (const x of this.abEls) {
        const id = x.a.id;
        let cls = 'ability', sub = '', cd = 0, full = 1;
        if (id === 'hero') {
          const st = g.swarmState('hero');
          if (e) { sub = Math.ceil(e.hp / e.maxHp * 100) + '%'; cls += e.order || e.hold ? '' : ' active'; }
          else if (st === 'locked') { sub = 'HIVE'; cls += ' locked'; }
          else if (st === 'wait') { sub = 'RECOVERING'; cls += ' locked'; cd = g.sw.heroAt - g.time; full = S.hero.respawn; }
          else { const c = g.swarmCost('hero'); sub = c[0] + ' + ' + c[1] + 'g'; if (g.sw.minerals < c[0] || g.sw.gas < c[1]) cls += ' poor'; }
          const nm = e ? (e.order || e.hold ? 'ADVANCE' : 'KERRIGAN') : 'SUMMON';
          if (x.nmT !== nm) { x.nmT = nm; x.name.textContent = nm; }
        } else {
          if (!e) { cls += ' locked'; sub = ''; }
          else { cd = g.heroCd(id); full = max[id]; sub = BS.SWARM.abilities.find(a => a.id === id).key; }
        }
        if (x.cls !== cls) { x.cls = cls; x.el.className = cls; }
        if (x.subT !== sub) { x.subT = sub; x.sub.textContent = sub; }
        const cdT = cd > 0 ? String(Math.ceil(cd)) : '';
        if (x.cdT !== cdT) { x.cdT = cdT; x.cd.textContent = cdT; }
        x.cd.style.setProperty('--p', (cd > 0 ? cd / full * 100 : 0) + '%');
      }
    }

    refresh() {
      const g = this.g, b = g.bunker;
      this.set('minerals', String(Math.floor(g.swarm ? g.sw.minerals : g.minerals)));
      if (g.swarm) this.set('gasT', String(Math.floor(g.sw.gas)));
      this.set('wave', pad2(g.wave));
      this.set('kills', String(g.swarm ? g.sw.kills : g.kills));
      const ev = Math.ceil(g.evacIn());
      this.set('evacT', pad2(Math.floor(ev / 60)) + ':' + pad2(ev % 60));
      if (this.cache.evacHot !== (ev <= BS.EVAC.at - BS.EVAC.finale)) $('evacT').classList.toggle('amber', this.cache.evacHot = ev <= BS.EVAC.at - BS.EVAC.finale);
      const secs = Math.max(0, Math.ceil(g.waveTimer));
      this.set('waveSecs', String(secs));
      $('waveArc').style.strokeDashoffset = String(94.2 * (1 - Math.max(0, g.waveTimer) / g.waveLen));
      // Squad card.
      const frac = Math.max(0, b.hp / b.maxHp), lit = Math.ceil(frac * 10);
      const segKey = lit + '|' + (frac < 0.3 ? 'crit' : frac < 0.6 ? 'warn' : '');
      if (this.cache.seg !== segKey) {
        this.cache.seg = segKey;
        const segs = $('segments'); segs.className = frac < 0.3 ? 'crit' : frac < 0.6 ? 'warn' : '';
        [...segs.children].forEach((s, i) => s.classList.toggle('on', i < lit));
        $('bunkerLabel').classList.toggle('crit', frac < 0.3);
        this.set('bunkerLabel', frac < 0.3 ? 'INTEGRITY CRITICAL' : 'BUNKER');
      }
      this.set('hpText', Math.ceil(Math.max(0, b.hp)) + ' / ' + b.maxHp);
      const stim = g.ab.stimUntil > g.time;
      this.set('stimTag', stim ? 'STIMMED' : '');
      const low = u => u.hp <= u.def.stimHp ? ' low' : '';
      const slotKey = b.slots + ':' + b.units.map(u => u.type + Math.round(u.hp / u.maxHp * 10) + low(u)).join(',') + (stim ? 's' : '');
      if (this.cache.slots !== slotKey) {
        this.cache.slots = slotKey;
        let html = '';
        for (let i = 0; i < b.slots; i++) {
          const u = b.units[i];
          html += u ? '<div class="slot' + (stim ? ' stim' : '') + low(u) + '" data-i="' + i + '"><img src="' + icon(u.def.icon) + '" alt=""><i style="width:' + Math.round(u.hp / u.maxHp * 100) + '%"></i></div>' : '<div class="slot"></div>';
        }
        $('slots').innerHTML = html;
      }
      const outKey = g.squad.map(m => m.type + Math.round(m.hp / m.maxHp * 10) + low(m) + (m.boarding ? 'b' : '')).join(',') + (stim ? 's' : '');
      if (this.cache.outside !== outKey) {
        this.cache.outside = outKey;
        $('outside').classList.toggle('dense', g.squad.length > 24);   // past 24 the portraits shrink
        $('outside').innerHTML = g.squad.map((m, i) => '<div class="slot' + low(m) + (m.boarding ? ' boarding' : '') + (stim ? ' stim' : '') + '" data-o="' + i + '"><img src="' + icon(m.def.icon) + '" alt=""><i style="width:' + Math.round(m.hp / m.maxHp * 100) + '%"></i></div>').join('');
      }
      // Battlecruiser hull, only while it is deployed.
      const bc = g.bc;
      if (this.cache.bcOn !== !!bc) { this.cache.bcOn = !!bc; $('bcRow').classList.toggle('hidden', !bc); this.bcLastHp = bc ? bc.hp : 0; }
      if (bc) {
        const f = Math.max(0, bc.hp / bc.maxHp);
        if (bc.hp < this.bcLastHp) this.bcHitAt = performance.now();
        this.bcLastHp = bc.hp;
        const key = Math.round(f * 100) + '|' + (performance.now() - (this.bcHitAt || -1e9) < 250);
        if (this.cache.bc !== key) {
          this.cache.bc = key;
          $('bcRow').className = (f <= 0.25 ? 'crit' : f <= 0.5 ? 'warn' : '') + (key.endsWith('true') ? ' hit' : '');
          $('bcBar').firstChild.style.width = (f * 100).toFixed(0) + '%';
        }
        this.set('bcLabel', f <= 0.25 ? 'BC HULL CRITICAL' : 'BATTLECRUISER');
        this.set('bcHp', Math.ceil(Math.max(0, bc.hp)) + ' / ' + bc.maxHp);
      }
      // Abilities.
      if (g.swarm) { this.refreshSwarmAbilities(); this.refreshBrood(); }
      else for (const x of this.abEls) {
        const id = x.a.id;
        const cd = id === 'stim' ? g.ab.stimCd : id === 'strike' ? g.ab.strikeCd : id === 'repair' ? g.ab.repairCd : g.ab.yamatoCd;
        const max = id === 'stim' ? BS.TERRAN.stim.cooldown : id === 'strike' ? g.strikeCooldown() : id === 'repair' ? BS.TERRAN.repair.cooldown : BS.TERRAN.yamato.cooldown;
        const cost = id === 'stim' ? 0 : id === 'strike' ? BS.TERRAN.banshee.cost : id === 'repair' ? BS.TERRAN.repair.cost : BS.TERRAN.yamato.cost;
        const locked = id === 'stim' && !g.up.stim, poor = cost > 0 && g.minerals < cost;
        const cls = 'ability' + (locked ? ' locked' : '') + (poor ? ' poor' : '') + (id === 'stim' && stim ? ' active' : '');
        if (x.cls !== cls) { x.cls = cls; x.el.className = cls; }
        const sub = locked ? 'LOCKED' : id === 'stim' ? '-10 HP' : String(cost);
        if (x.subT !== sub) { x.subT = sub; x.sub.textContent = sub; }
        const cdT = cd > 0 ? String(Math.ceil(cd)) : '';
        if (x.cdT !== cdT) { x.cdT = cdT; x.cd.textContent = cdT; }
        x.cd.style.setProperty('--p', (cd > 0 ? cd / max * 100 : 0) + '%');
      }
      // Armoury.
      for (const x of this.items) {
        const hidden = x.it.hidden ? x.it.hidden() : false;
        if (hidden) { if (x.last !== 'hidden') { x.last = 'hidden'; x.el.style.display = 'none'; } continue; }
        const st = x.it.state(), cost = x.it.cost(), poor = !st && (x.it.poor ? x.it.poor() : g.minerals < cost), wait = st === 'wait' ? Math.ceil(x.it.wait()) : 0;
        const key = st + '|' + cost + '|' + poor + '|' + x.it.icon() + '|' + (x.it.level ? x.it.level() : '') + '|' + wait;
        if (x.last === key) continue;
        if (x.last === 'hidden' || x.last === '') x.el.style.display = '';
        x.last = key;
        x.el.className = 'item' + (x.it.tech ? ' tech' : '') + (st ? ' ' + st : '') + (poor ? ' poor' : '');
        x.img.src = icon(x.it.icon());
        const lb = x.it.label ? x.it.label(st) : null;
        x.price.textContent = lb !== null ? lb : st === 'full' ? 'FULL' : st === 'max' ? 'MAX' : st === 'locked' ? 'LOCKED' : st === 'wait' ? wait + 's' : String(cost);
        x.lvl.textContent = x.it.level ? x.it.level() : '';
      }
      this.refreshShop();
      // Toasts and the wave banner.
      const last = g.toasts[g.toasts.length - 1];
      if (last && last !== this.toastShown) { this.toastShown = last; this.toastT = performance.now(); $('toast').textContent = last.msg.toUpperCase(); $('toast').classList.add('show'); }
      if (performance.now() - this.toastT > 2200) $('toast').classList.remove('show');
      const bn = $('banner');
      if (g.waveBanner && g.waveBanner !== this.bannerSerial) { this.bannerSerial = g.waveBanner; bn.textContent = g.waveBanner.text; this.bannerAt = performance.now(); }
      const bt = (performance.now() - (this.bannerAt || -1e9)) / 1000;
      bn.style.opacity = bt < 0.3 ? bt / 0.3 : bt < 1.8 ? 1 : Math.max(0, 1 - (bt - 1.8) / 0.6);
      $('mute').classList.toggle('off', BS.audio.muted);
    }

    // The shop between waves (Game.endWave): the offered cards, reroll, next wave. Rebuilt only when something changes.
    refreshShop() {
      const g = this.g, s = g.shop;
      if (this.cache.shopOn !== !!s) { this.cache.shopOn = !!s; $('shop').classList.toggle('hidden', !s); $('shop').classList.remove('peek'); $('shopPeek').textContent = 'ARMOURY'; }
      if (!s) return;
      if (s.zerg) return this.refreshZergShop(s);
      this.set('shopTitle', 'WAVE ' + s.wave + ' CLEARED');
      this.set('shopHint', 'Troops, tanks, SCVs and turrets stay in the armoury.');
      this.set('shopBonus', [s.bonus ? '+' + s.bonus + ' CLEAR BONUS' : 'WAVE HELD', s.interest ? '+' + s.interest + ' INTEREST' : ''].filter(Boolean).join('   '));
      const m = Math.floor(g.minerals);
      this.set('shopMinerals', m + ' MINERALS');
      const key = s.offers.map(o => o.id + (o.bought ? '!' : '')).join(',') + '|' + s.offers.map(o => g.minerals >= g.cardCost(g.card(o.id))).join('');
      if (this.cache.shopCards !== key) {
        this.cache.shopCards = key;
        $('shopCards').innerHTML = s.offers.map((o, i) => {
          const c = g.card(o.id), R = BS.RARITY[c.rarity], lv = g.cardLevel(c), max = g.cardMax(c), cost = g.cardCost(c);
          // Bought: the level it was bought at.
          const lvText = max > 1 ? 'LV ' + (o.bought ? lv : lv + 1) + (c.grow ? '' : ' / ' + max) : '';
          const cls = 'offer' + (o.bought ? ' bought' : m < cost ? ' poor' : '');
          return '<button class="' + cls + '" data-c="' + i + '" style="--rc:' + R.color + '"><img src="' + icon(g.cardIcon(c)) + '" alt="">' +
            '<span class="rar">' + R.name + '</span><b>' + g.cardName(c).toUpperCase() + '</b><span class="lv">' + lvText + '</span>' +
            '<p>' + g.cardDesc(c) + '</p><span class="price">' + (o.bought ? 'BOUGHT' : cost) + '</span></button>';
        }).join('') || '<p class="hint">Nothing left to offer.</p>';
      }
      const rc = g.rerollCost();
      this.set('shopReroll', rc ? 'REROLL  ' + rc : 'FREE REROLL');
      $('shopReroll').classList.toggle('poor', m < rc);
    }

    // Swarm mode: the hive's mutations between rounds.
    refreshZergShop(s) {
      const g = this.g, sw = g.sw;
      this.set('shopTitle', 'WAVE ' + s.wave + '  //  MUTATIONS');
      this.set('shopBonus', 'CHOOSE ONE');
      this.set('shopHint', 'The hive cluster stays below.');
      this.set('shopPeek', $('shop').classList.contains('peek') ? 'SHOP' : 'HIVE');
      this.set('shopMinerals', Math.floor(sw.minerals) + ' MINERALS   ' + Math.floor(sw.gas) + ' VESPENE');
      const key = 'z' + s.offers.map(o => o.id + (o.bought ? '!' : '')).join(',') + '|' + s.offers.map(o => { const c = g.zcardCost(g.zcard(o.id)); return sw.minerals >= c[0] && sw.gas >= c[1]; }).join('');
      if (this.cache.shopCards !== key) {
        this.cache.shopCards = key;
        $('shopCards').innerHTML = s.offers.map((o, i) => {
          const c = g.zcard(o.id), R = BS.RARITY[c.rarity], lv = g.zcardLevel(c.id), max = c.max || 99, cost = g.zcardCost(c);
          const lvText = max > 1 && max < 99 ? 'LV ' + (o.bought ? lv : lv + 1) + ' / ' + max : '';
          const poor = sw.minerals < cost[0] || sw.gas < cost[1];
          const price = o.bought ? 'BOUGHT' : cost[0] || cost[1] ? cost[0] + (cost[1] ? ' + ' + cost[1] + 'g' : '') : 'FREE';
          return '<button class="offer' + (o.bought ? ' bought' : poor ? ' poor' : '') + '" data-c="' + i + '" style="--rc:' + R.color + '"><img src="' + icon(c.icon) + '" alt="">' +
            '<span class="rar">' + R.name + '</span><b>' + c.name.toUpperCase() + '</b><span class="lv">' + lvText + '</span>' +
            '<p>' + c.desc + '</p><span class="price">' + price + '</span></button>';
        }).join('') || '<p class="hint">Nothing left to offer.</p>';
      }
      const rc = g.zergRerollCost();
      this.set('shopReroll', 'REROLL  ' + rc);
      $('shopReroll').classList.toggle('poor', sw.minerals < rc);
    }

    // Overlay canvas: floating text, enemy health bars, focus ring, threat markers; plus the radar.
    drawOverlay() {
      const g = this.g, r = this.r, c = this.ctx, dpr = r.dpr, W = r.width, H = r.height;
      c.setTransform(dpr, 0, 0, dpr, 0, 0);
      c.clearRect(0, 0, W, H);
      // Health bars over damaged zerg.
      for (const e of g.enemies) {
        if (e.hp >= e.maxHp || !r.inFront(e.x, e.y, e.z)) continue;
        const [sx, sy] = r.worldToScreen(e.x, e.y, e.z + e.def.height + 0.5);
        if (sx < -20 || sx > W + 20 || sy < -20 || sy > H + 20) continue;
        const w = 18 + e.radius * 14;
        c.fillStyle = 'rgba(0,0,0,0.55)'; c.fillRect(sx - w / 2 - 1, sy - 1, w + 2, 5);
        c.fillStyle = '#bd6bff'; c.fillRect(sx - w / 2, sy, w * Math.max(0, e.hp / e.maxHp), 3);
      }
      // Swarm mode: the base's taps. Hatcheries ready to inject glow green, evolution chambers show their research (amber
      // ring: idle), the chrysalis pulses pink while Kerrigan can be summoned.
      if (g.swarm && g.mode === 'lane') {
        const D = BS.SWARM.base, pulse = 0.55 + 0.45 * Math.sin(performance.now() / 260);
        const ring = (x, y, rx, color, frac) => {
          const [sx, sy] = r.worldToScreen(x, y, BS.Terrain.height(x, y) + 0.1), k = rx * 9;
          c.strokeStyle = color; c.lineWidth = 3; c.beginPath();
          if (frac === undefined) c.ellipse(sx, sy, k, k * 0.5, 0, 0, Math.PI * 2); else c.ellipse(sx, sy, k, k * 0.5, 0, -Math.PI / 2, -Math.PI / 2 + frac * Math.PI * 2);
          c.stroke();
        };
        g.hatchSpots().forEach(([x, y], i) => { if (g.injectReady(i)) ring(x, y, i ? 2.2 : 3, 'rgba(141,224,138,' + pulse + ')'); });
        for (let i = 0; i < D.chamber.spots.length; i++) { if (!g.sw.chamberAt[i]) continue; const [x, y] = D.chamber.spots[i], rs = g.sw.research[i]; if (rs) ring(x, y, 1.8, '#5fe3ff', rs.t / rs.dur); else ring(x, y, 1.8, 'rgba(255,191,60,' + pulse + ')'); }
        if (g.sw.base.parts.chrysalis && !g.swarmState('hero') && g.sw.minerals >= BS.SWARM.hero.cost[0] && g.sw.gas >= BS.SWARM.hero.cost[1]) ring(D.chrysalis[0], D.chrysalis[1], 1.6, 'rgba(255,122,217,' + pulse + ')');
      }
      // Swarm mode: where Kerrigan was sent.
      const he = g.swarm && g.heroAlive(), spot = he && (he.order || he.hold);
      if (spot && r.inFront(spot.x, spot.y, 0)) {
        const [sx, sy] = r.worldToScreen(spot.x, spot.y, BS.Terrain.height(spot.x, spot.y) + 0.05);
        c.strokeStyle = he.order ? '#ff7ad9' : 'rgba(255,122,217,0.45)'; c.lineWidth = 2; c.beginPath(); c.ellipse(sx, sy, 20, 10, 0, 0, Math.PI * 2); c.stroke();
      }
      if (g.focus && g.focus.hp > 0 && r.inFront(g.focus.x, g.focus.y, 0)) {
        const [sx, sy] = r.worldToScreen(g.focus.x, g.focus.y, 0.05);
        c.strokeStyle = '#ff6a5a'; c.lineWidth = 2; c.beginPath(); c.ellipse(sx, sy, 22 + g.focus.radius * 14, 12 + g.focus.radius * 7, 0, 0, Math.PI * 2); c.stroke();
      }
      c.textAlign = 'center'; c.font = '700 15px Eurostile, system-ui, sans-serif';
      for (const f of g.floaters) {
        if (!r.inFront(f.x, f.y, f.z)) continue;
        const [sx, sy] = r.worldToScreen(f.x, f.y, f.z + f.t * 1.2);
        c.globalAlpha = Math.max(0, 1 - f.t / 1.3);
        c.fillStyle = 'rgba(0,0,0,0.6)'; c.fillText(f.text, sx + 1, sy + 1);
        c.fillStyle = f.color; c.fillText(f.text, sx, sy);
      }
      c.globalAlpha = 1;
      this.drawBosses(c, W);
      if (!g.swarm) this.drawThreats(c, W, H);       // (in swarm mode the zerg are the player's own)
      this.drawRadar();
    }

    // The finale: a long health bar per boss under the squad card.
    drawBosses(c, W) {
      const bosses = this.g.bosses();
      if (!bosses.length) return;
      const sq = document.getElementById('squad').getBoundingClientRect(), w = Math.min(420, W - 40), x = (W - w) / 2;
      c.font = '700 11px Eurostile, system-ui, sans-serif';
      bosses.forEach((e, i) => {
        const y = sq.bottom + 12 + i * 30, f = Math.max(0, e.hp / e.maxHp);
        c.fillStyle = 'rgba(10,14,20,0.82)'; c.fillRect(x - 6, y - 4, w + 12, 26);
        c.textAlign = 'left'; c.fillStyle = '#e8f1fb'; c.fillText(e.def.name.toUpperCase(), x, y + 7);
        const run = e.def.untouchable;           // Kerrigan: no health to whittle down, only the Hyperion's countdown
        c.textAlign = 'right'; c.fillStyle = run ? '#ff6a5a' : '#c08cff'; c.fillText(run ? 'UNSTOPPABLE  //  RUN' : Math.ceil(e.hp) + ' / ' + e.maxHp, x + w, y + 7);
        c.fillStyle = 'rgba(0,0,0,0.6)'; c.fillRect(x, y + 12, w, 6);
        c.fillStyle = run || f < 0.25 ? '#ff6a5a' : '#bd6bff'; c.fillRect(x, y + 12, w * f, 6);
      });
      c.textAlign = 'center';
    }

    // Off-screen zerg, grouped into arrows at the screen edge with a count.
    drawThreats(c, W, H) {
      const g = this.g, r = this.r, groups = new Map();
      const sq = document.getElementById('squad').getBoundingClientRect(), top = sq.bottom + 16, bottom = H - 270;
      const cam = r.cam;
      for (const e of g.enemies) {
        if (e.def.tumor || e.def.ambient) continue;   // creep tumors stay put, ambient overlords never attack: not threats to point at
        let ang;
        if (cam.fp && !r.inFront(e.x, e.y, 0)) {
          // Behind the eye: point by bearing (right, left, or down for straight behind).
          const rel = Math.atan2(Math.sin(Math.atan2(e.x, e.y) - cam.fpYaw), Math.cos(Math.atan2(e.x, e.y) - cam.fpYaw));
          ang = Math.atan2(-Math.cos(rel), Math.sin(rel));
        } else {
          const [sx, sy] = r.worldToScreen(e.x, e.y, 0);
          if (sx >= 0 && sx <= W && sy >= top && sy <= H) continue;
          ang = Math.atan2(sy - H / 2, sx - W / 2);
        }
        const k = Math.round(ang / (Math.PI / 6));
        const gr = groups.get(k) || { n: 0, ang: k * Math.PI / 6 }; gr.n++; groups.set(k, gr);
      }
      c.font = '700 11px Eurostile, system-ui, sans-serif';
      for (const gr of groups.values()) {
        const dx = Math.cos(gr.ang), dy = Math.sin(gr.ang);
        const t = Math.min((W / 2 - 24) / Math.abs(dx || 1e-6), ((dy < 0 ? H / 2 - top : bottom - H / 2) - 10) / Math.abs(dy || 1e-6));
        const x = W / 2 + dx * t, y = H / 2 + dy * t;
        c.save(); c.translate(x, y); c.rotate(gr.ang);
        c.fillStyle = 'rgba(255,106,90,0.85)'; c.beginPath(); c.moveTo(9, 0); c.lineTo(-5, -7); c.lineTo(-2, 0); c.lineTo(-5, 7); c.closePath(); c.fill();
        c.restore();
        c.fillStyle = 'rgba(20,28,38,0.85)'; c.beginPath(); c.arc(x - dx * 18, y - dy * 18, 9, 0, Math.PI * 2); c.fill();
        c.fillStyle = '#e8f1fb'; c.fillText(String(gr.n), x - dx * 18, y - dy * 18 + 4);
      }
    }

    // Only while a sensor tower stands.
    drawRadar() {
      const on = this.g.sensorUp();
      if (this.cache.radar !== on) { this.cache.radar = on; this.radar.classList.toggle('offline', !on); }
      if (!on) return;
      const cv = this.radar, dpr = this.r.dpr, size = cv.clientWidth;
      if (cv.width !== Math.round(size * dpr)) { cv.width = cv.height = Math.round(size * dpr); }
      const c = this.rctx, R = size / 2, g = this.g;
      c.setTransform(dpr, 0, 0, dpr, 0, 0);
      c.clearRect(0, 0, size, size);
      c.fillStyle = 'rgba(20,28,38,0.82)'; c.beginPath(); c.arc(R, R, R - 1, 0, Math.PI * 2); c.fill();
      c.strokeStyle = 'rgba(150,190,230,0.18)'; c.lineWidth = 1; c.stroke();
      c.beginPath(); c.arc(R, R, R * 0.5, 0, Math.PI * 2); c.stroke();
      // Sweep.
      const a = (performance.now() / 1000 * 1.6) % (Math.PI * 2);
      const grad = c.createConicGradient ? c.createConicGradient(a - 0.6, R, R) : null;
      if (grad) { grad.addColorStop(0, 'rgba(95,227,255,0)'); grad.addColorStop(0.1, 'rgba(95,227,255,0.22)'); grad.addColorStop(0.1001, 'rgba(95,227,255,0)'); c.fillStyle = grad; c.beginPath(); c.arc(R, R, R - 2, 0, Math.PI * 2); c.fill(); }
      const scale = (R - 6) / (g.mode === 'lane' ? 32 : 30), cy = g.mode === 'lane' ? R + 8 * scale : R;
      const yaw = (this.r.cam.fp ? this.r.cam.fpYaw : this.r.cam.yaw) || 0, cs = Math.cos(yaw), sn = Math.sin(yaw);
      const plot = (x, y) => { const rx = x * cs - y * sn, ry = x * sn + y * cs; return [R + rx * scale, cy - ry * scale]; };
      c.fillStyle = '#8de08a';
      for (const p of [[0, 0], ...g.tanks.map(t => [t.x, t.y]), ...g.structures.map(s => [s.x, s.y])]) { const [x, y] = plot(p[0], p[1]); c.fillRect(x - 2.5, y - 2.5, 5, 5); }
      // Troopers outside: small dots; goliaths, thors and the odin: squares by size; the battlecruiser and air support:
      // cyan diamonds.
      for (const m of g.squad) { const [x, y] = plot(m.x, m.y); c.fillRect(x - 1, y - 1, 2, 2); }
      for (const m of g.mechs) { const [x, y] = plot(m.x, m.y), s = m.kind === 'odin' ? 4.5 : m.kind === 'thor' ? 3.5 : 2.5; c.fillRect(x - s, y - s, s * 2, s * 2); }
      c.fillStyle = '#5fe3ff';
      const diamond = (wx, wy, s) => { const [x, y] = plot(wx, wy); c.beginPath(); c.moveTo(x, y - s); c.lineTo(x + s, y); c.lineTo(x, y + s); c.lineTo(x - s, y); c.closePath(); c.fill(); };
      if (g.bc) diamond(g.bc.x, g.bc.y, 6);
      for (const v of g.vikingWing.concat(g.banshees, g.wraiths)) diamond(v.x, v.y, 2.5);
      if (g.vessel) diamond(g.vessel.x, g.vessel.y, 4);
      for (const e of g.enemies) {
        const [x, y] = plot(e.x, e.y);
        if (Math.hypot(x - R, y - R) > R - 3) continue;
        c.fillStyle = e.def.flying ? '#ffbf3c' : '#bd6bff';
        c.beginPath(); c.arc(x, y, e.def.budget >= 16 ? 2.6 : 1.6, 0, Math.PI * 2); c.fill();
      }
    }
  }

  BS.Hud = Hud;
})(window.BS);
