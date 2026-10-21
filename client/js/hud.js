// DOM HUD: resources, wave, bunker health, shop and abilities; plus the 2D world overlay drawing.
window.BS = window.BS || {};
(function (BS) {
  'use strict';

  const $ = id => document.getElementById(id);
  const icon = name => '/icon/' + name + '.png';

  class Hud {
    constructor(game, renderer) {
      this.g = game; this.r = renderer;
      this.lastRefresh = 0;
      this.el = {
        minerals: $('minerals'), wave: $('wave'), waveTimer: $('wave-timer'), kills: $('kills'), hpFill: $('hp-fill'), hpText: $('hp-text'),
        slots: $('slots'), shop: $('shop'), abilities: $('abilities'), toast: $('toast'), score: $('score'),
      };
      this.buildShop();
      this.buildAbilities();
    }

    buildShop() {
      const g = this.g, U = BS.UPGRADES, I = BS.INFANTRY;
      const items = [
        { id: 'marine', name: 'Marine', icon: I.marine.icon, cost: () => I.marine.cost, desc: I.marine.desc, buy: () => g.addInfantry('marine'), avail: () => g.bunker.units.length < g.bunker.slots },
        { id: 'firebat', name: 'Firebat', icon: I.firebat.icon, cost: () => I.firebat.cost, desc: I.firebat.desc, buy: () => g.addInfantry('firebat'), avail: () => g.bunker.units.length < g.bunker.slots },
        { id: 'marauder', name: 'Marauder', icon: I.marauder.icon, cost: () => I.marauder.cost, desc: I.marauder.desc, buy: () => g.addInfantry('marauder'), avail: () => g.bunker.units.length < g.bunker.slots },
        { id: 'scv', name: 'SCV', icon: 'btn-unit-terran-scv', cost: () => BS.TERRAN.scv.cost, desc: 'Auto-repairs the bunker and tanks (costs minerals per HP).', buy: () => g.addScv(), avail: () => g.scvs.length < BS.TERRAN.scv.max },
        { id: 'tank', name: 'Siege Tank drop', icon: 'btn-unit-terran-siegetank', cost: () => BS.TERRAN.tank.cost, desc: 'Medivac air-drops a siege tank. Long range splash.', buy: () => g.dropTank(), avail: () => g.tanks.length < g.tankMax },
        { id: 'stim', name: U.stim.name, icon: U.stim.icon, cost: () => U.stim.cost, desc: U.stim.desc, buy: () => g.buyUpgrade('stim'), avail: () => !g.up.stim },
        { id: 'weapons', name: U.weapons.name, icon: () => U.weapons.icons[Math.min(2, g.up.weapons)], cost: () => U.weapons.costs[g.up.weapons], desc: U.weapons.desc, buy: () => g.buyUpgrade('weapons'), avail: () => g.up.weapons < 3, level: () => g.up.weapons },
        { id: 'neosteel', name: U.neosteel.name, icon: U.neosteel.icon, cost: () => U.neosteel.cost, desc: U.neosteel.desc, buy: () => g.buyUpgrade('neosteel'), avail: () => !g.up.neosteel },
        { id: 'plating', name: U.plating.name, icon: U.plating.icon, cost: () => U.plating.cost, desc: U.plating.desc, buy: () => g.buyUpgrade('plating'), avail: () => !g.up.plating },
        { id: 'vehicle', name: U.vehicle.name, icon: U.vehicle.icon, cost: () => U.vehicle.costs[g.up.vehicle], desc: U.vehicle.desc, buy: () => g.buyUpgrade('vehicle'), avail: () => g.up.vehicle < 3 && g.tanks.length > 0, level: () => g.up.vehicle },
        { id: 'medivac', name: U.medivac.name, icon: U.medivac.icon, cost: () => U.medivac.cost, desc: U.medivac.desc, buy: () => g.buyUpgrade('medivac'), avail: () => !g.up.medivac && g.up.stim, locked: () => !g.up.stim ? 'NEEDS STIM' : null },
      ];
      this.shopItems = items;
      const shop = this.el.shop;
      shop.innerHTML = '';
      for (const it of items) {
        const card = document.createElement('button');
        card.className = 'card';
        card.innerHTML = '<img alt=""><span class="name"></span><span class="cost"></span><span class="lvl"></span>';
        card.title = it.desc;
        card.addEventListener('click', ev => { ev.preventDefault(); BS.audio.unlock(); if (it.buy()) this.flash(card); this.refresh(true); });
        card.addEventListener('pointerenter', () => this.showDesc(it));
        it.el = card;
        shop.appendChild(card);
      }
    }

    buildAbilities() {
      const g = this.g;
      const list = [
        { id: 'stim', name: 'STIM', icon: 'btn-ability-terran-stimpack', use: () => g.useStim(), cd: () => g.ab.stimCd, max: 13, avail: () => g.up.stim, hint: 'Stimpack: +50% fire rate, 11s' },
        { id: 'strike', name: 'AIR STRIKE', icon: 'btn-unit-terran-banshee', use: () => g.callAirStrike(), cd: () => g.ab.strikeCd, max: BS.TERRAN.banshee.cooldown, avail: () => true, cost: BS.TERRAN.banshee.cost, hint: 'Two Banshees strafe the field' },
        { id: 'repair', name: 'REPAIR', icon: 'btn-ability-terran-repair', use: () => g.emergencyRepair(), cd: () => g.ab.repairCd, max: 20, avail: () => true, cost: 75, hint: 'Emergency repair: +150 bunker HP' },
      ];
      this.abilityItems = list;
      const box = this.el.abilities;
      box.innerHTML = '';
      for (const a of list) {
        const b = document.createElement('button');
        b.className = 'ability';
        b.innerHTML = '<img alt="" src="' + icon(a.icon) + '"><span class="label">' + a.name + (a.cost ? ' <em>' + a.cost + '</em>' : '') + '</span><span class="cdmask"></span>';
        b.title = a.hint;
        b.addEventListener('click', ev => { ev.preventDefault(); BS.audio.unlock(); if (a.use()) this.flash(b); this.refresh(true); });
        a.el = b;
        box.appendChild(b);
      }
    }

    flash(el) { el.classList.remove('flash'); void el.offsetWidth; el.classList.add('flash'); }
    showDesc(it) { this.toast(it.name + ' - ' + it.desc, 2.5); }
    toast(msg, secs) {
      const t = this.el.toast;
      t.textContent = msg; t.classList.add('show');
      clearTimeout(this.toastTimer);
      this.toastTimer = setTimeout(() => t.classList.remove('show'), (secs || 2) * 1000);
    }

    refresh(force) {
      const g = this.g, now = performance.now();
      if (!force && now - this.lastRefresh < 150) return;
      this.lastRefresh = now;
      const e = this.el;
      e.minerals.textContent = Math.floor(g.minerals);
      e.wave.textContent = g.wave;
      e.waveTimer.textContent = Math.max(0, Math.ceil(g.waveTimer)) + 's';
      e.kills.textContent = g.kills;
      e.score.textContent = Math.round(g.score + g.kills * 10 + g.wave * 250);
      const pct = Math.max(0, g.bunker.hp / g.bunker.maxHp);
      e.hpFill.style.width = (pct * 100).toFixed(1) + '%';
      e.hpFill.style.background = pct > 0.5 ? '#39d353' : pct > 0.25 ? '#ffb020' : '#ff4040';
      e.hpText.textContent = Math.ceil(g.bunker.hp) + ' / ' + g.bunker.maxHp;
      // Bunker slots
      let html = '';
      for (let i = 0; i < g.bunker.slots; i++) {
        const u = g.bunker.units[i];
        if (u) html += '<span class="slot"><img src="' + icon(u.def.icon) + '" alt="' + u.type + '"><i style="height:' + Math.round(u.hp / u.maxHp * 100) + '%"></i></span>';
        else html += '<span class="slot empty"></span>';
      }
      if (this.slotsHtml !== html) { e.slots.innerHTML = html; this.slotsHtml = html; }
      const stim = g.ab.stimUntil > g.time;
      e.slots.classList.toggle('stim', stim);
      // Shop cards
      for (const it of this.shopItems) {
        const el = it.el;
        const avail = it.avail();
        const cost = it.cost();
        const ic = typeof it.icon === 'function' ? it.icon() : it.icon;
        const img = el.querySelector('img');
        const src = icon(ic);
        if (img.getAttribute('data-src') !== src) { img.src = src; img.setAttribute('data-src', src); }
        el.querySelector('.name').textContent = it.name;
        const lockText = it.locked ? it.locked() : null;
        el.querySelector('.cost').textContent = avail ? cost : (lockText || (it.level ? 'MAX' : (BS.INFANTRY[it.id] ? 'FULL' : (it.id === 'scv' || it.id === 'tank') ? 'MAX' : 'DONE')));
        el.querySelector('.lvl').textContent = it.level ? 'L' + it.level() : '';
        el.classList.toggle('disabled', !avail);
        el.classList.toggle('poor', avail && g.minerals < cost);
      }
      for (const a of this.abilityItems) {
        const cd = a.cd(), avail = a.avail();
        a.el.classList.toggle('disabled', !avail || (a.cost && g.minerals < a.cost));
        a.el.classList.toggle('active', a.id === 'stim' && stim);
        a.el.querySelector('.cdmask').style.height = (cd > 0 ? (cd / a.max * 100) : 0).toFixed(1) + '%';
      }
      if (g.lastToast && g.lastToast !== this.seenToast) { this.seenToast = g.lastToast; this.toast(g.lastToast.msg); }
    }

    // ---- World overlay (health bars, tracers, blasts, focus marker) ----------------------------
    drawOverlay() {
      const g = this.g, r = this.r, ctx = r.ctx2d, dpr = r.dpr;
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
      ctx.clearRect(0, 0, r.width, r.height);
      if (g.shake > 0) { const s = g.shake * 6; ctx.translate((Math.random() - 0.5) * s, (Math.random() - 0.5) * s); }
      const w2s = (x, y, z) => r.worldToScreen(x, y, z);
      // Projectiles
      for (const p of g.projectiles) {
        const k = Math.min(1, p.t / p.dur);
        let x, y, z;
        if (p.kind === 'shell') { x = p.x + (p.tx - p.x) * k; y = p.y + (p.ty - p.y) * k; z = p.z + Math.sin(k * Math.PI) * 4; }
        else { x = p.x + (p.tx - p.x) * k; y = p.y + (p.ty - p.y) * k; z = p.z + ((p.tz || 1) - p.z) * k; }
        const [sx, sy] = w2s(x, y, z);
        ctx.beginPath();
        ctx.fillStyle = p.friendly ? (p.kind === 'shell' ? '#ffd27a' : '#bfe8ff') : (p.kind === 'acid' ? '#9dff4a' : p.kind === 'spine' ? '#d6ff9a' : '#ffd0a0');
        ctx.arc(sx, sy, p.kind === 'shell' ? 4 : 2.5, 0, Math.PI * 2); ctx.fill();
      }
      // Effects
      for (const f of g.fx) {
        const k = f.t / f.dur;
        if (f.kind === 'tracer') {
          const a = w2s(f.x, f.y, f.z), b = w2s(f.tx, f.ty, f.tz);
          ctx.strokeStyle = f.color; ctx.globalAlpha = 1 - k; ctx.lineWidth = 1.5;
          ctx.beginPath(); ctx.moveTo(a[0], a[1]); ctx.lineTo(b[0], b[1]); ctx.stroke(); ctx.globalAlpha = 1;
        } else if (f.kind === 'flash') {
          const a = w2s(f.x, f.y, f.z); ctx.fillStyle = f.color; ctx.globalAlpha = 0.9 * (1 - k);
          ctx.beginPath(); ctx.arc(a[0], a[1], 6 * (1 - k * 0.5), 0, Math.PI * 2); ctx.fill(); ctx.globalAlpha = 1;
        } else if (f.kind === 'blast') {
          const a = w2s(f.x, f.y, 0.2), b = w2s(f.x + f.r, f.y, 0.2);
          const px = Math.max(4, Math.abs(b[0] - a[0])) * (0.3 + 0.7 * k);
          ctx.strokeStyle = f.color; ctx.fillStyle = f.color; ctx.globalAlpha = (1 - k) * 0.8; ctx.lineWidth = 3;
          ctx.beginPath(); ctx.ellipse(a[0], a[1], px, px * 0.6, 0, 0, Math.PI * 2); ctx.stroke();
          ctx.globalAlpha = (1 - k) * 0.35; ctx.fill(); ctx.globalAlpha = 1;
        } else if (f.kind === 'flame') {
          const a = w2s(f.x, f.y, f.z), b = w2s(f.tx, f.ty, 0.6);
          const grad = ctx.createLinearGradient(a[0], a[1], b[0], b[1]);
          grad.addColorStop(0, 'rgba(255,240,180,' + (0.9 * (1 - k)) + ')'); grad.addColorStop(1, 'rgba(255,90,20,0)');
          ctx.strokeStyle = grad; ctx.lineWidth = 10 + 16 * k; ctx.lineCap = 'round';
          ctx.beginPath(); ctx.moveTo(a[0], a[1]); ctx.lineTo(b[0], b[1]); ctx.stroke();
        } else if (f.kind === 'spines') {
          const a = w2s(f.x, f.y, 0.2), b = w2s(f.tx, f.ty, 0.2);
          ctx.strokeStyle = f.color; ctx.globalAlpha = 1 - k; ctx.lineWidth = 2; ctx.setLineDash([6, 8]);
          ctx.beginPath(); ctx.moveTo(a[0], a[1]); ctx.lineTo(b[0], b[1]); ctx.stroke(); ctx.setLineDash([]); ctx.globalAlpha = 1;
        }
      }
      // Health bars
      ctx.lineWidth = 1;
      const bar = (x, y, z, pct, w, color) => {
        const [sx, sy] = w2s(x, y, z);
        if (sx < -50 || sx > r.width + 50 || sy < -50 || sy > r.height + 50) return;
        ctx.fillStyle = 'rgba(0,0,0,0.6)'; ctx.fillRect(sx - w / 2 - 1, sy - 4, w + 2, 5);
        ctx.fillStyle = color; ctx.fillRect(sx - w / 2, sy - 3, w * pct, 3);
      };
      for (const e of g.enemies) if (e.hp < e.maxHp) bar(e.x, e.y, e.z + e.def.height + 0.4, e.hp / e.maxHp, 22 + e.radius * 12, '#c84bff');
      for (const t of g.tanks) if (t.state === 'ready' && t.hp < t.maxHp) bar(t.x, t.y, 2.2, t.hp / t.maxHp, 30, '#39d353');
      for (const s of g.scvs) if (s.hp < s.maxHp) bar(s.x, s.y, 1.6, s.hp / s.maxHp, 20, '#39d353');
      // Focus marker
      if (g.focus && g.focus.hp > 0) {
        const f = g.focus, a = w2s(f.x, f.y, 0.1), b = w2s(f.x + f.radius + 0.4, f.y, 0.1);
        const rr = Math.max(8, Math.abs(b[0] - a[0]));
        ctx.strokeStyle = '#ff4d4d'; ctx.lineWidth = 2;
        ctx.beginPath(); ctx.ellipse(a[0], a[1], rr, rr * 0.6, 0, 0, Math.PI * 2); ctx.stroke();
      }
      // Floating text
      ctx.font = 'bold 15px system-ui, sans-serif'; ctx.textAlign = 'center';
      for (const f of g.floaters) {
        const a = w2s(f.x, f.y, f.z + f.t * 1.5);
        ctx.globalAlpha = 1 - f.t / 1.2; ctx.fillStyle = f.color; ctx.fillText(f.text, a[0], a[1]); ctx.globalAlpha = 1;
      }
      // Incoming wave spawn hint (lane mode): a faint line at the fog edge
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    }
  }

  BS.Hud = Hud;
})(window.BS);
