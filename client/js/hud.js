// HUD: command bar, squad card, radar, abilities, armoury, toasts, wave banner, and the overlay canvas
// (floating text, health bars, focus ring, off-screen threat markers). Mirrors the Unreal build's HUD design.
window.BS = window.BS || {};
(function (BS) {
  'use strict';

  const $ = id => document.getElementById(id);
  const icon = name => '/icon/' + name + '.png';
  const pad2 = n => (n < 10 ? '0' : '') + n;

  class Hud {
    constructor(game, renderer) {
      this.g = game; this.r = renderer;
      this.ctx = $('fx').getContext('2d');
      this.radar = $('radar'); this.rctx = this.radar.getContext('2d');
      this.cache = {};
      this.toastShown = -1; this.toastT = 0; this.bannerSerial = null;
      this.buildSegments(); this.buildAbilities(); this.buildArmoury();
      // Tap a trooper in the bunker to send them outside; tap one outside to bring them back in.
      $('slots').onclick = ev => { const el = ev.target.closest('[data-i]'); if (el) game.eject(+el.dataset.i); };
      $('outside').onclick = ev => { const el = ev.target.closest('[data-o]'); if (el) game.board(+el.dataset.o); };
      $('bunkerPortrait').onclick = () => this.toggleView();
      $('armouryToggle').onclick = () => { const c = $('armoury').classList.toggle('closed'); $('armouryToggle').classList.toggle('closed', c); };
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

    // Armoury: troops, support and upgrades, one card each. Native horizontal scrolling means a swipe never buys.
    buildArmoury() {
      const g = this.g, T = BS.TERRAN;
      const items = [];
      // Surprise cards first: hidden until their tech tree is maxed out (see Game.checkTech).
      for (const c of BS.TECH) items.push({ name: c.name, tech: c.id, icon: () => c.icon, cost: () => g.techState(c.id).cost,
        state: () => g.techState(c.id).state, wait: () => g.techState(c.id).wait, hidden: () => !g.techUnlocked[c.id], buy: () => g.buyTech(c.id) });
      for (const [k, d] of Object.entries(BS.INFANTRY)) items.push({ name: d.name, icon: () => d.icon, cost: () => d.cost,
        state: () => g.troopsFull() ? 'full' : '', buy: () => g.addInfantry(k) });
      items.push({ name: 'SCV', icon: () => 'btn-unit-terran-scv', cost: () => T.scv.cost, state: () => g.scvs.length >= T.scv.max ? 'max' : '', buy: () => g.addScv() });
      items.push({ name: 'Missile Turret', icon: () => 'btn-building-terran-missileturret', cost: () => T.sam.cost, state: () => g.samCount() >= T.sam.max ? 'max' : '', buy: () => g.buildStructure('sam') });
      items.push({ name: 'Sensor Tower', icon: () => 'btn-building-terran-sensortower-silver', cost: () => T.sensor.cost, state: () => g.structures.some(s => s.kind === 'sensor') ? 'max' : '', buy: () => g.buildStructure('sensor') });
      items.push({ name: 'Siege Tank', icon: () => 'btn-unit-terran-siegetank', cost: () => g.tankCost(), state: () => g.tanks.length >= g.tankMax ? 'max' : '', buy: () => g.dropTank() });
      for (const [k, u] of Object.entries(BS.UPGRADES)) items.push({ name: u.name, up: k,
        icon: () => u.icons[Math.min(g.upgradeLevel(k), u.icons.length - 1)], cost: () => g.upgradeCost(k),
        state: () => g.upgradeLevel(k) >= u.costs.length ? 'max' : (!g.upgradeAvailable(k) ? 'locked' : ''),
        level: () => u.costs.length > 1 && g.upgradeLevel(k) ? 'LV ' + g.upgradeLevel(k) : '', buy: () => g.buyUpgrade(k) });
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

    refresh() {
      const g = this.g, b = g.bunker;
      this.set('minerals', String(Math.floor(g.minerals)));
      this.set('wave', pad2(g.wave));
      this.set('kills', String(g.kills));
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
      for (const x of this.abEls) {
        const id = x.a.id;
        const cd = id === 'stim' ? g.ab.stimCd : id === 'strike' ? g.ab.strikeCd : id === 'repair' ? g.ab.repairCd : g.ab.yamatoCd;
        const max = id === 'stim' ? BS.TERRAN.stim.cooldown : id === 'strike' ? BS.TERRAN.banshee.cooldown : id === 'repair' ? BS.TERRAN.repair.cooldown : BS.TERRAN.yamato.cooldown;
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
        const st = x.it.state(), cost = x.it.cost(), poor = !st && g.minerals < cost, wait = st === 'wait' ? Math.ceil(x.it.wait()) : 0;
        const key = st + '|' + cost + '|' + poor + '|' + x.it.icon() + '|' + (x.it.level ? x.it.level() : '') + '|' + wait;
        if (x.last === key) continue;
        if (x.last === 'hidden' || x.last === '') x.el.style.display = '';
        x.last = key;
        x.el.className = 'item' + (x.it.tech ? ' tech' : '') + (st ? ' ' + st : '') + (poor ? ' poor' : '');
        x.img.src = icon(x.it.icon());
        x.price.textContent = st === 'full' ? 'FULL' : st === 'max' ? 'MAX' : st === 'locked' ? 'LOCKED' : st === 'wait' ? wait + 's' : String(cost);
        x.lvl.textContent = x.it.level ? x.it.level() : '';
      }
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
      this.drawThreats(c, W, H);
      this.drawRadar();
    }

    // Wave 100 finale: a long health bar per boss under the squad card.
    drawBosses(c, W) {
      const bosses = this.g.bosses();
      if (!bosses.length) return;
      const sq = document.getElementById('squad').getBoundingClientRect(), w = Math.min(420, W - 40), x = (W - w) / 2;
      c.font = '700 11px Eurostile, system-ui, sans-serif';
      bosses.forEach((e, i) => {
        const y = sq.bottom + 12 + i * 30, f = Math.max(0, e.hp / e.maxHp);
        c.fillStyle = 'rgba(10,14,20,0.82)'; c.fillRect(x - 6, y - 4, w + 12, 26);
        c.textAlign = 'left'; c.fillStyle = '#e8f1fb'; c.fillText(e.def.name.toUpperCase(), x, y + 7);
        c.textAlign = 'right'; c.fillStyle = '#c08cff'; c.fillText(Math.ceil(e.hp) + ' / ' + e.maxHp, x + w, y + 7);
        c.fillStyle = 'rgba(0,0,0,0.6)'; c.fillRect(x, y + 12, w, 6);
        c.fillStyle = f < 0.25 ? '#ff6a5a' : '#bd6bff'; c.fillRect(x, y + 12, w * f, 6);
      });
      c.textAlign = 'center';
    }

    // Off-screen zerg, grouped into arrows at the screen edge with a count.
    drawThreats(c, W, H) {
      const g = this.g, r = this.r, groups = new Map();
      const sq = document.getElementById('squad').getBoundingClientRect(), top = sq.bottom + 16, bottom = H - 270;
      const cam = r.cam;
      for (const e of g.enemies) {
        if (e.def.tumor) continue;                 // creep tumors stay put: not a threat to point at
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
      for (const v of g.vikingWing.concat(g.banshees)) diamond(v.x, v.y, 2.5);
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
