// Bootstrap: start screen over a staged scene, the Hyperion (perks between runs), asset preload, input (tap to focus
// fire, drag to pan, pinch/wheel to zoom), pause and game-over flow, and the main loop.
window.BS = window.BS || {};
(function (BS) {
  'use strict';

  const $ = id => document.getElementById(id);
  let renderer, game = null, hud = null, mode = 'lane', swarm = false, attract = [], last = performance.now(), savedShop = null;
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
    document.querySelectorAll('[data-mode]').forEach(b => b.addEventListener('click', () => { Checkpoint.clear(); start(b.dataset.mode); }));
    // The Swarm: open once a run has escaped Kerrigan (?swarm=1 opens it for playtesting).
    const q0 = new URLSearchParams(location.search), swarmOpen = BS.Meta.swarmOpen() || q0.get('swarm') === '1';
    $('swarmMode').classList.toggle('locked', !swarmOpen);
    if (swarmOpen) $('swarmLine').textContent = 'Play the zerg: feed the hives, summon Kerrigan and raze the bunker before the Hyperion gets them out.';
    $('swarmMode').onclick = () => { if (swarmOpen) start('lane', null, true); };
    // The Protoss campaign: open once the swarm has razed the bunker (?zeratul=1 opens it).
    if (BS.Meta.protossOpen() || q0.get('zeratul') === '1') $('protossMode').classList.remove('hidden');
    $('protossMode').onclick = () => zeratul();
    // A run the browser threw away while it was in the background (or lost to a dropped WebGL context): carry on.
    const cp = Checkpoint.load();
    if (cp) {
      $('resumeLine').textContent = (cp.mode === 'arena' ? '360 Arena' : 'Lane Defence') + ', in the shop after wave ' + cp.wave + ', ' + Math.floor(cp.time / 60) + ' minutes in.';
      $('resumeRun').classList.remove('hidden');
      $('resumeRun').onclick = () => resumeRun(cp);
    }
    $('gl').addEventListener('webglcontextlost', () => { if (game) { Checkpoint.save(game); try { sessionStorage.setItem('bs.autoResume', '1'); } catch (e) { /* ignore */ } } location.reload(); });
    $('hyperionBtn').onclick = () => { renderHyperion(); $('hyperion').classList.remove('hidden'); };
    $('hyperionClose').onclick = () => $('hyperion').classList.add('hidden');
    $('perks').onclick = ev => { const el = ev.target.closest('[data-p]'); if (el && BS.Meta.buyPerk(el.dataset.p)) renderHyperion(); };
    renderHyperion();
    setupInput();
    stageAttract();
    renderer.fx.preload(BS.CORE_FX);
    const ready = renderer.preload(BS.CORE_MODELS, (d, n) => { $('progress').textContent = 'LOADING SC2 ART ' + d + ' / ' + n; }).then(() => {
      $('progress').textContent = '';
      if (st && st.mode === 'local') cacheAllArt();
    });
    requestAnimationFrame(loop);
    const m = new URLSearchParams(location.search).get('mode');
    if (m === 'lane' || m === 'arena') { Checkpoint.clear(); start(m, null, q0.get('swarm') === '1'); }
    else if (cp && (document.wasDiscarded || takeFlag('bs.autoResume'))) ready.then(() => resumeRun(cp));
  }
  // Resuming puts a whole army in place at once: load every model it needs first, so nothing pops in half-built.
  async function resumeRun(cp) {
    if (game || $('resumeRun').disabled) return;
    $('resumeRun').disabled = true;
    const keys = new Set(['bunker', 'scv', 'medivac']);
    cp.bunker.units.concat(cp.squad).forEach(([t]) => keys.add(t));
    if (cp.tanks.length) ['siegetank', 'siegetankMorph'].forEach(k => keys.add(k));
    cp.structures.forEach(([k]) => keys.add(k === 'sam' ? 'missileturret' : 'sensortower'));
    cp.mechs.forEach(([k]) => keys.add(k));
    if (cp.bc) keys.add('battlecruiser');
    if ((cp.reapers || []).length) keys.add('reaper');
    if ((cp.ghosts || []).length) keys.add('ghost');
    let done = 0;
    const line = () => { $('resumeLine').textContent = 'Bringing the army back in: ' + done + ' / ' + keys.size; };
    line();
    await Promise.all([...keys].map(k => Promise.resolve().then(() => renderer.ensureModel(k)).catch(() => null).then(() => { done++; line(); })));
    start(cp.mode, cp);
  }
  function takeFlag(k) { try { const v = sessionStorage.getItem(k); sessionStorage.removeItem(k); return !!v; } catch (e) { return false; } }

  // The last shop of the run in progress, in localStorage (see Game.snapshot); gone once the run ends.
  const Checkpoint = {
    key: 'bs.run',
    save(g) { const s = g.snapshot(); if (s) try { localStorage.setItem(this.key, JSON.stringify(s)); } catch (e) { /* private mode, full */ } },
    load() { try { const s = JSON.parse(localStorage.getItem(this.key)); return s && s.v === 1 && Date.now() - s.savedAt < 7 * 864e5 ? s : null; } catch (e) { return null; } },
    clear() { try { localStorage.removeItem(this.key); } catch (e) { /* ignore */ } },
  };

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

  // The Protoss campaign, in full: Zeratul on a loop, YTMND style. The live model turns over a spinning wallpaper of him,
  // huge outlined words cycle and pulse, the Brood War protoss theme loops forever and he says something now and then.
  function zeratul() {
    BS.audio.unlock();
    for (const h of attract) h.release();
    attract = [];
    $('start').classList.add('hidden'); $('zeratul').classList.remove('hidden'); $('app').classList.add('ytmnd');
    renderer.setGroundShape('arena'); renderer.setCameraMode('arena');
    Object.assign(renderer.cam, { tx: 0, ty: 0, dist: 7.5, pitch: 12 * Math.PI / 180, minDist: 3, maxDist: 20 }); renderer.updateCamera();
    const z = renderer.spawn('zeratul', 0, 0, 0.6, -Math.PI / 2).setTeam(2).setScale(1.6);
    const seqs = [/^stand$/i, /^attack/i, /^spell/i, /^stand/i, /^walk/i];
    let si = 0;
    const next = () => { const len = z.play(seqs[si++ % seqs.length], false) || 2; setTimeout(next, Math.max(1200, len * 1000)); };
    setTimeout(next, 600);
    const stopTheme = BS.audio.loop('protossTheme', 0.7);
    const vo = setInterval(() => BS.audio.play('zeratulVo', { vol: 1, gap: 3, rate: 1 }), 7000);
    setTimeout(() => BS.audio.play('zeratulVo', { vol: 1, rate: 1 }), 1500);
    const words = ['ZERATUL', 'YOU HAVE COME FAR', 'THE PROTOSS CAMPAIGN', 'THIS IS IT', 'ADUN TORO', 'HE BLINKED', 'BEHIND YOU', 'VOID SHADES', 'NO SUPPLY DEPOT', 'ZERATUL'];
    const cv = $('ytmnd'), cx = cv.getContext('2d'), t0 = performance.now(), tile = new Image();
    tile.src = 'icon/btn-unit-protoss-zeratul.png';
    let spin = 0, alive = true;
    const draw = () => {
      if (!alive) return;
      const t = (performance.now() - t0) / 1000, W = cv.clientWidth, H = cv.clientHeight, dpr = window.devicePixelRatio || 1;
      if (cv.width !== Math.round(W * dpr)) { cv.width = Math.round(W * dpr); cv.height = Math.round(H * dpr); }
      cx.setTransform(dpr, 0, 0, dpr, 0, 0); cx.clearRect(0, 0, W, H);
      // The wallpaper: his portrait tiled, turning and breathing.
      if (tile.complete && tile.naturalWidth) {
        cx.save(); cx.globalAlpha = 0.16; cx.translate(W / 2, H / 2); cx.rotate(t * 0.6);
        const s = 1 + 0.25 * Math.sin(t * 1.7), R = Math.hypot(W, H) / 2 / s;
        cx.scale(s, s);
        for (let x = -R; x < R; x += 96) for (let y = -R; y < R; y += 96) cx.drawImage(tile, x, y, 96, 96);
        cx.restore();
      }
      spin += 0.02; z.setYaw(-Math.PI / 2 + Math.sin(spin) * 0.9).setPos(0, 0, 0.6 + Math.abs(Math.sin(t * 2.4)) * 0.25);
      const word = words[Math.floor(t / 2.2) % words.length], k = 1 + 0.12 * Math.sin(t * 9);
      for (const [y, txt] of [[H * 0.18, word], [H * 0.84, word === 'ZERATUL' ? 'EN TARO ZERATUL' : 'ZERATUL']]) {
        const size = Math.min(W * 0.92 / (txt.length * 0.56), H / 5) * k;
        cx.save(); cx.translate(W / 2, y); cx.rotate(Math.sin(t * 3) * 0.05);
        cx.font = '900 ' + Math.round(size) + 'px Impact, Haettenschweiler, "Arial Black", sans-serif';
        cx.textAlign = 'center'; cx.textBaseline = 'middle'; cx.lineJoin = 'round';
        cx.lineWidth = Math.max(4, size / 10); cx.strokeStyle = '#000'; cx.strokeText(txt, 0, 0);
        cx.fillStyle = 'hsl(' + ((t * 120 + (y > H / 2 ? 180 : 0)) % 360) + ', 100%, 62%)'; cx.fillText(txt, 0, 0);
        cx.restore();
      }
      requestAnimationFrame(draw);
    };
    draw();
    $('zeratulClose').onclick = () => { alive = false; clearInterval(vo); stopTheme(); location.search = ''; };
  }

  function start(m, resume, sw) {
    if (game) return;
    mode = m; swarm = !!sw;
    BS.audio.unlock();
    for (const h of attract) h.release();
    attract = [];
    $('start').classList.add('hidden');
    $('hud').classList.remove('hidden');
    renderer.setGroundShape(mode);
    renderer.flip = swarm;                  // swarm mode: the lane seen from the zerg side
    renderer.setCameraMode(mode);
    game = new BS.Game(renderer, { mode, swarm });
    hud = new BS.Hud(game, renderer);
    window.game = game;                     // for poking at from the browser console
    if (resume) game.restore(resume);
    else game.beginOpening();
    game.started = true;
    // Playtesting: ?tech=max researches everything (unlocking the high-tech cards) and banks minerals.
    if (new URLSearchParams(location.search).get('tech') === 'max') setTimeout(() => game.grantAllTech(), 1500);
    // Playtesting: ?wave=30 jumps straight to that wave, ?clock=28 sets the run clock to that many minutes (28: the next
    // wave is the Kerrigan finale, 29.5: the Hyperion is on her way); combine with ?tech=max.
    const q = new URLSearchParams(location.search), jump = parseInt(q.get('wave'), 10), clock = parseFloat(q.get('clock'));
    if (clock > 0) game.time = clock * 60;
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
    $('redeploy').onclick = () => { Checkpoint.clear(); location.search = '?mode=' + mode + (swarm ? '&swarm=1' : ''); };
    document.querySelectorAll('.menuBtn').forEach(b => { b.onclick = () => { if (game.over) Checkpoint.clear(); location.search = ''; }; });
    document.addEventListener('visibilitychange', () => { if (document.hidden && game && !game.over) { setPaused(true); Checkpoint.save(game); } });
    window.addEventListener('pagehide', () => { if (game && !game.over) Checkpoint.save(game); });
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
      if (game.shop && game.shop !== savedShop) { savedShop = game.shop; Checkpoint.save(game); }      // each wave's shop
      if (game.over && $('overModal').classList.contains('hidden')) { Checkpoint.clear(); showGameOver(); }
    } else {
      renderer.cam.yaw = Math.sin(t / 9000) * 0.35; renderer.updateCamera();
      renderer.frame(dt);
    }
    requestAnimationFrame(loop);
  }

  function showGameOver() {
    $('overModal').classList.remove('hidden');
    $('overTitle').textContent = game.swarm ? (game.won ? 'BUNKER RAZED' : game.hiveDown ? 'HIVE DESTROYED' : 'THEY GOT AWAY') : game.won ? 'EVACUATED' : 'BUNKER LOST';
    document.querySelector('#overModal .card').classList.toggle('won', !!game.won);
    $('overStats').innerHTML = [['WAVE', game.wave], ['KILLS', game.swarm ? game.sw.kills : game.kills], ['SCORE', game.finalScore]].map(([k, v]) => '<div><small>' + k + '</small><b>' + Number(v).toLocaleString('en-US') + '</b></div>').join('');
    const b = $('overBest');
    b.textContent = game.newBest ? 'NEW BEST' : 'BEST ' + Number(game.best).toLocaleString('en-US');
    b.classList.toggle('new', !!game.newBest);
    const mr = game.metaResult;
    $('overMeta').innerHTML = mr ? '+' + mr.gas + ' VESPENE FOR THE HYPERION' + mr.unlocked.map(c => '<span>NEW CARD: ' + BS.Meta.cardName(c).toUpperCase() + '</span>').join('') + (mr.swarm ? '<span>NEW MODE: THE SWARM</span>' : '') + (mr.protoss ? '<span>NEW: THE PROTOSS CAMPAIGN</span>' : '') : '';
  }

  // ---- Input ------------------------------------------------------------------------------------------
  // iOS Safari ignores user-scalable=no: a quick double tap (buying twice on the armoury, say) zooms the page, and the
  // game's own pinch handling then leaves no way to zoom back out. Swallow the second tap's zoom (still clicking what
  // it hit), block the page pinch, and if the page does end up zoomed, reset the viewport.
  function guardPageZoom() {
    let lastEnd = 0;
    document.addEventListener('touchend', ev => {
      const now = Date.now();
      if (now - lastEnd < 350 && ev.target.closest && !ev.target.closest('#fx')) {
        ev.preventDefault();
        if (ev.target.click) ev.target.click();          // a click bubbles to whatever handles it
      }
      lastEnd = now;
    }, { passive: false });
    for (const g of ['gesturestart', 'gesturechange']) document.addEventListener(g, ev => ev.preventDefault(), { passive: false });
    document.addEventListener('dblclick', ev => ev.preventDefault(), { passive: false });
    const vv = window.visualViewport, meta = document.querySelector('meta[name=viewport]');
    if (vv && meta) vv.addEventListener('resize', () => {
      if (vv.scale <= 1.01) return;
      const c = meta.content;
      meta.content = c + ', minimum-scale=1';
      setTimeout(() => { meta.content = c; }, 50);
    });
  }

  function setupInput() {
    guardPageZoom();
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
        else {
          lastTap = now; const w = renderer.screenToGround(ev.clientX, ev.clientY);
          // Swarm mode: a tap sends Kerrigan there.
          if (w && game.swarm) { if (!game.baseTap(w[0], w[1])) game.heroOrder(w[0], w[1]); }
          else if (w && game.focusAt(w[0], w[1])) game.toast('Focus fire!');
        }
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
      // Swarm mode: K summons Kerrigan (or sends her back to advancing), Q W E R T Y her spells.
      else if (game.swarm && k === 'k') hud.heroButton();
      else if (game.swarm && k === ' ') { ev.preventDefault(); game.broodHold(); }
      else if (game.swarm && BS.SWARM.abilities.some(a => a.key.toLowerCase() === k)) game.heroCast(BS.SWARM.abilities.find(a => a.key.toLowerCase() === k).id);
      else if (game.swarm && 'sary'.includes(k)) { /* the Terrans' abilities */ }
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
