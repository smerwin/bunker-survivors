// Bootstrap: start screen over a staged scene, the Hyperion (perks between runs), asset preload, input (tap to focus
// fire, drag to pan, pinch/wheel to zoom), pause and game-over flow, and the main loop.
window.BS = window.BS || {};
(function (BS) {
  'use strict';

  const $ = id => document.getElementById(id);
  let renderer, game = null, hud = null, mode = 'lane', attract = [], last = performance.now();
  const touch = matchMedia('(pointer: coarse)').matches;
  // Phones get a smaller cap on live zerg and a lower render resolution, so big swarms stay smooth.
  BS.perfScale = touch ? 0.65 : 1;

  const isPortrait = () => window.innerHeight > window.innerWidth;

  async function boot() {
    // The companion server, or (web edition) the player's own StarCraft II folder; see link.js. First, so that the
    // renderer's own textures (cliffs, creep) already come through it.
    let st = null;
    try {
      st = await BS.linkSC2();
      $('status').textContent = 'STARCRAFT II ' + st.sc2Version + ' LINKED' + (st.hostname ? '  ' + st.hostname.toUpperCase() : '');
    } catch (e) { $('status').textContent = String(e.message || e).toUpperCase(); }
    $('relink').onclick = () => BS.relinkSC2().then(() => $('relink').classList.add('hidden'));
    renderer = new BS.Renderer($('gl'), $('fx'), { maxDpr: touch ? 1.5 : 2, animEvery: touch ? 2 : 1 });
    window.renderer = renderer;              // for poking at from the browser console
    try {
      const bl = localStorage.getItem('bs.best.lane') || 0, ba = localStorage.getItem('bs.best.arena') || 0;
      $('best').textContent = 'BEST  LANE ' + bl + '   ARENA ' + ba;
    } catch (e) { /* private mode */ }
    $(isPortrait() ? 'tagLane' : 'tagArena').textContent = isPortrait() ? 'BEST ON PHONE' : 'BEST ON DESKTOP';
    document.querySelectorAll('[data-mode]').forEach(b => b.addEventListener('click', () => start(b.dataset.mode)));
    $('hyperionBtn').onclick = () => { renderHyperion(); $('hyperion').classList.remove('hidden'); };
    $('hyperionClose').onclick = () => $('hyperion').classList.add('hidden');
    $('perks').onclick = ev => { const el = ev.target.closest('[data-p]'); if (el && BS.Meta.buyPerk(el.dataset.p)) renderHyperion(); };
    renderHyperion();
    setupInput();
    stageAttract();
    renderer.fx.preload(BS.CORE_FX);
    renderer.preload(BS.CORE_MODELS, (d, n) => { $('progress').textContent = 'LOADING SC2 ART ' + d + ' / ' + n; }).then(() => {
      $('progress').textContent = '';
      if (st && st.mode === 'local') cacheAllArt();
    });
    requestAnimationFrame(loop);
    const m = new URLSearchParams(location.search).get('mode');
    if (m === 'lane' || m === 'arena') start(m);
  }

  // The Hyperion: perks bought with vespene, and every shop card (locked ones say what unlocks them).
  function renderHyperion() {
    const s = BS.Meta.state();
    $('gas').textContent = s.gas;
    $('gasLine').textContent = s.gas + ' VESPENE';
    $('perks').innerHTML = BS.META.perks.map(p => {
      const l = BS.Meta.perk(p.id), cost = BS.Meta.perkCost(p.id);
      const cls = 'perk' + (cost === null ? ' max' : s.gas < cost ? ' poor' : '');
      const pips = p.costs.length > 1 ? '<span class="pips">' + p.costs.map((c, i) => '<i' + (i < l ? ' class="on"' : '') + '></i>').join('') + '</span>' : '';
      return '<button class="' + cls + '" data-p="' + p.id + '"><img src="icon/' + p.icon + '.png" alt=""><span><b>' + p.name.toUpperCase() + '</b><small>' + p.desc + '</small>' + pips + '</span>' +
        '<span class="cost">' + (cost === null ? 'MAX' : cost) + '</span></button>';
    }).join('');
    $('codex').innerHTML = BS.CARDS.map(c => {
      const open = BS.Meta.unlocked(c), name = BS.Meta.cardName(c), ic = c.icon || BS.UPGRADES[c.up].icons[0];
      return '<div class="codex' + (open ? '' : ' locked') + '" style="--rc:' + BS.RARITY[c.rarity].color + '"><img src="icon/' + ic + '.png" alt=""><span><b>' + name.toUpperCase() + '</b>' +
        '<small>' + (open ? (s.buys[c.id] ? 'Bought ' + s.buys[c.id] + 'x' : BS.RARITY[c.rarity].name.toLowerCase()) : BS.Meta.unlockText(c)) + '</small></span></div>';
    }).join('');
  }

  // Web edition, first visit with the folder linked: read every model, effect, sound and icon the game can use into the
  // browser's cache now, so later visits play without the folder.
  async function cacheAllArt() {
    const keys = Object.keys(BS.MODELS).concat(Object.keys(BS.EXTRA_MODELS || {}));
    const icons = new Set();
    (function scan(o) { if (o && typeof o === 'object') for (const [k, v] of Object.entries(o)) { if (k === 'icon' && typeof v === 'string') icons.add(v); else scan(v); } })(BS);
    let done = 0;
    const total = keys.length + icons.size + 1;
    const tick = () => { done++; $('progress').textContent = done < total ? 'CACHING SC2 ART ' + Math.round(done / total * 100) + '%' : ''; };
    for (const k of keys) { try { await renderer.ensureModel(k); } catch (e) { /* logged */ } tick(); }
    await Promise.all([...icons].map(i => fetch('icon/' + i + '.png').catch(() => null).then(tick)));
    renderer.fx.preload(Object.keys(BS.fxList()));
    BS.audio.preload();
    tick();
  }

  // A dug-in position mid-fight behind the start screen, the camera slowly orbiting.
  function stageAttract() {
    renderer.setGroundShape('lane'); renderer.setCameraMode('lane');
    renderer.cam.dist = 17; renderer.updateCamera();
    const put = (key, x, y, yaw, anim, team) => { const h = renderer.spawn(key, x, y, 0, yaw).setTeam(team); h.play(anim, true, 0.8 + Math.random() * 0.3); attract.push(h); };
    put('bunker', 0, 0, Math.PI / 2, /^stand work/i, 1);
    put('siegetank', -3.4, -0.4, Math.PI / 2, /^stand$/i, 1);
    put('scv', 2.6, -2.9, 2.4, /^work/i, 1);
    const zerg = [['zergling', -1.5, 7], ['zergling', 0.8, 7.6], ['zergling', 2.4, 8.4], ['zergling', -3, 8.8], ['hydralisk', -0.4, 10.5], ['roach', 2.8, 11.2], ['ultralisk', -2.2, 14.5], ['baneling', 1.2, 9.6]];
    for (const [k, x, y] of zerg) put(k, x, y, Math.atan2(-y, -x) + (Math.random() - 0.5) * 0.6, /^stand/i, 3);
  }

  function start(m) {
    if (game) return;
    mode = m;
    BS.audio.unlock();
    for (const h of attract) h.release();
    attract = [];
    $('start').classList.add('hidden');
    $('hud').classList.remove('hidden');
    renderer.setGroundShape(mode);
    renderer.setCameraMode(mode);
    game = new BS.Game(renderer, { mode });
    hud = new BS.Hud(game, renderer);
    window.game = game;                     // for poking at from the browser console
    game.beginOpening();
    game.started = true;
    // Playtesting: ?tech=max researches everything (unlocking the high-tech cards) and banks minerals.
    if (new URLSearchParams(location.search).get('tech') === 'max') setTimeout(() => game.grantAllTech(), 1500);
    // Playtesting: ?wave=100 jumps straight to that wave (the Kerrigan finale); combine with ?tech=max.
    const jump = parseInt(new URLSearchParams(location.search).get('wave'), 10);
    if (jump > 1) game.skipToWave(jump);
    // Stream the rest in the background: zerg in unlock order, then variants and projectiles, then effects.
    const rest = Object.keys(BS.MODELS).filter(k => !BS.CORE_MODELS.includes(k));
    rest.sort((a, b) => (BS.ZERG[a] ? BS.ZERG[a].unlock : 5) - (BS.ZERG[b] ? BS.ZERG[b].unlock : 5));
    (async () => {
      for (const k of rest.concat(Object.keys(BS.EXTRA_MODELS))) { try { await renderer.ensureModel(k); } catch (e) { /* logged */ } }
      renderer.fx.preload(Object.keys(BS.fxList()));
    })();
    $('pause').onclick = togglePause;
    $('resume').onclick = togglePause;
    $('mute').onclick = () => BS.audio.setMuted(!BS.audio.muted);
    $('redeploy').onclick = () => { location.search = '?mode=' + mode; };
    document.querySelectorAll('.menuBtn').forEach(b => { b.onclick = () => { location.search = ''; }; });
    document.addEventListener('visibilitychange', () => { if (document.hidden && game && !game.over) setPaused(true); });
  }

  function setPaused(p) { if (!game || game.over) return; game.paused = p; $('pausedModal').classList.toggle('hidden', !p); }
  function togglePause() { if (game) setPaused(!game.paused); }

  function loop(t) {
    const dt = Math.min(0.1, (t - last) / 1000); last = t;
    if (game) {
      game.update(dt);
      const vdt = game.paused || game.shop ? 0 : dt;
      game.updateVisuals(vdt);
      renderer.shake = game.shake;
      renderer.frame(vdt);
      hud.drawOverlay();
      hud.refresh();
      if (game.over && $('overModal').classList.contains('hidden')) showGameOver();
    } else {
      renderer.cam.yaw = Math.sin(t / 9000) * 0.35; renderer.updateCamera();
      renderer.frame(dt);
    }
    requestAnimationFrame(loop);
  }

  function showGameOver() {
    $('overModal').classList.remove('hidden');
    $('overStats').innerHTML = [['WAVE', game.wave], ['KILLS', game.kills], ['SCORE', game.finalScore]].map(([k, v]) => '<div><small>' + k + '</small><b>' + v + '</b></div>').join('');
    const b = $('overBest');
    b.textContent = game.newBest ? 'NEW BEST' : 'BEST ' + game.best;
    b.classList.toggle('new', !!game.newBest);
    const mr = game.metaResult;
    $('overMeta').innerHTML = mr ? '+' + mr.gas + ' VESPENE FOR THE HYPERION' + mr.unlocked.map(c => '<span>NEW CARD: ' + BS.Meta.cardName(c).toUpperCase() + '</span>').join('') : '';
  }

  // ---- Input ------------------------------------------------------------------------------------------
  function setupInput() {
    const el = $('fx'), pointers = new Map();
    let dragging = false, moved = 0, pinch = 0, downT = 0, lastTap = 0;
    el.addEventListener('pointerdown', ev => {
      BS.audio.unlock();
      el.setPointerCapture(ev.pointerId);
      pointers.set(ev.pointerId, { x: ev.clientX, y: ev.clientY });
      if (pointers.size === 1) { dragging = true; moved = 0; downT = performance.now(); }
      if (pointers.size === 2) { const [a, b] = [...pointers.values()]; pinch = Math.hypot(a.x - b.x, a.y - b.y); }
    });
    el.addEventListener('pointermove', ev => {
      const p = pointers.get(ev.pointerId); if (!p || !game) return;
      const dx = ev.clientX - p.x, dy = ev.clientY - p.y;
      p.x = ev.clientX; p.y = ev.clientY;
      if (pointers.size === 1 && dragging) { moved += Math.abs(dx) + Math.abs(dy); if (moved > 6) renderer.panBy(dx, dy); }
      else if (pointers.size === 2) {
        const [a, b] = [...pointers.values()], d = Math.hypot(a.x - b.x, a.y - b.y);
        if (pinch > 0 && d > 0) renderer.zoomBy(pinch / d, (a.x + b.x) / 2, (a.y + b.y) / 2);
        pinch = d; moved = 100;
      }
    });
    const up = ev => {
      const had = pointers.delete(ev.pointerId);
      if (!had || pointers.size || !game) { if (!pointers.size) { dragging = false; pinch = 0; } return; }
      if (dragging && moved <= 6 && performance.now() - downT < 350) {
        const now = performance.now();
        if (now - lastTap < 300) { if (renderer.cam.fp) renderer.setBunkerView(true); else renderer.setCameraMode(mode); lastTap = 0; }
        else { lastTap = now; const w = renderer.screenToGround(ev.clientX, ev.clientY); if (w && game.focusAt(w[0], w[1])) game.toast('Focus fire!'); }
      }
      dragging = false; pinch = 0;
    };
    el.addEventListener('pointerup', up); el.addEventListener('pointercancel', up);
    el.addEventListener('wheel', ev => { ev.preventDefault(); if (game) renderer.zoomBy(ev.deltaY > 0 ? 1.12 : 0.89, ev.clientX, ev.clientY); }, { passive: false });
    window.addEventListener('keydown', ev => {
      if (!game) { if (ev.key === '1') start('lane'); if (ev.key === '2') start('arena'); return; }
      const k = ev.key.toLowerCase();
      // Shop: 1-4 buy, X rerolls, Enter or Space starts the next wave.
      if (game.shop) {
        if (k >= '1' && k <= '4') { game.buyCard(+k - 1); return; }
        if (k === 'x') { game.reroll(); return; }
        if (k === 'enter' || k === ' ') { ev.preventDefault(); game.closeShop(); return; }
      }
      if (k === 'p' || k === 'escape') togglePause();
      else if (k === 'v') hud.toggleView();
      else if (k === 's') game.useStim(); else if (k === 'a') game.callAirStrike(); else if (k === 'r') game.emergencyRepair(); else if (k === 'y') game.useYamato();
      else if (k === '+' || k === '=') renderer.zoomBy(0.85); else if (k === '-') renderer.zoomBy(1.18);
      else if (k === 'arrowup') renderer.panBy(0, 40); else if (k === 'arrowdown') renderer.panBy(0, -40);
      else if (k === 'arrowleft') renderer.panBy(40, 0); else if (k === 'arrowright') renderer.panBy(-40, 0);
      else if (k === 'home') { if (renderer.cam.fp) renderer.setBunkerView(true); else renderer.setCameraMode(mode); }
      else if (k === 'q' && mode === 'arena') { renderer.cam.yaw -= 0.2; renderer.updateCamera(); }
      else if (k === 'e' && mode === 'arena') { renderer.cam.yaw += 0.2; renderer.updateCamera(); }
    });
    window.addEventListener('resize', () => renderer.resize());
  }

  window.addEventListener('DOMContentLoaded', boot);
})(window.BS);
