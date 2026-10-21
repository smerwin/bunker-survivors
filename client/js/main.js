// Bootstrap: mode selection, asset preload, input (tap to focus fire, drag to pan, pinch/wheel to zoom), main loop.
window.BS = window.BS || {};
(function (BS) {
  'use strict';

  const $ = id => document.getElementById(id);
  let renderer, game, hud, mode = 'lane';

  function isPortrait() { return window.innerHeight > window.innerWidth; }

  async function boot() {
    const status = $('status');
    try {
      const st = await fetch('/api/status').then(r => r.json());
      status.textContent = 'Connected to StarCraft II ' + st.sc2Version + ' on ' + st.hostname;
      const urls = st.urls || [];
      $('lan').innerHTML = urls.length ? 'Phone: open <b>' + urls[urls.length - 1] + '</b> on the same Wi-Fi <img class="qr" src="/qr.svg?u=' + encodeURIComponent(urls[urls.length - 1]) + '" alt="QR">' : '';
    } catch (e) {
      status.textContent = 'Companion server not reachable. Run: node server/index.js';
    }
    renderer = new BS.Renderer($('gl'), $('fx'));
    const defaultMode = isPortrait() ? 'lane' : 'arena';
    document.querySelectorAll('[data-mode]').forEach(b => {
      b.classList.toggle('primary', b.dataset.mode === defaultMode);
      b.addEventListener('click', () => start(b.dataset.mode));
    });
    try { const best = localStorage.getItem('bs.best.lane') || 0, bestA = localStorage.getItem('bs.best.arena') || 0; $('best').textContent = 'Best - Lane: ' + best + '  |  360 Arena: ' + bestA; } catch (e) { /* ignore */ }
    // Warm the core models while the player reads the start screen.
    renderer.preload(BS.CORE_MODELS, (d, n) => { $('progress').textContent = 'Loading SC2 art ' + d + '/' + n; }).then(() => { $('progress').textContent = 'Ready'; });
  }

  async function start(m) {
    mode = m;
    BS.audio.unlock();
    $('start').classList.add('hidden');
    $('hud').classList.remove('hidden');
    renderer.setGroundShape(mode);
    renderer.setCameraMode(mode);
    await renderer.preload(BS.CORE_MODELS);
    game = new BS.Game(renderer, { mode });
    hud = new BS.Hud(game, renderer);
    window.game = game; window.renderer = renderer; window.hud = hud;
    // Stream the rest in the background, roughly in the order they are needed.
    const rest = Object.keys(BS.MODELS).filter(k => !BS.CORE_MODELS.includes(k));
    rest.sort((a, b) => (BS.ZERG[a] ? BS.ZERG[a].unlock : 5) - (BS.ZERG[b] ? BS.ZERG[b].unlock : 5));
    (async () => { for (const k of rest) { try { await renderer.ensureModel(k); } catch (e) { /* logged */ } } })();
    game.started = true;
    hud.toast(mode === 'lane' ? 'Hold the line. Tap a zerg to focus fire.' : '360 Arena: they come from everywhere. Greater rewards, greater danger.', 4);
    setupInput();
    $('pause').addEventListener('click', togglePause);
    $('mute').addEventListener('click', () => { BS.audio.setMuted(!BS.audio.muted); $('mute').textContent = BS.audio.muted ? 'SND OFF' : 'SND ON'; });
    $('mute').textContent = BS.audio.muted ? 'SND OFF' : 'SND ON';
    $('restart').addEventListener('click', () => location.reload());
    $('resume').addEventListener('click', togglePause);
    document.addEventListener('visibilitychange', () => { if (document.hidden && game && !game.over) setPaused(true); });
    requestAnimationFrame(loop);
  }

  function setPaused(p) { if (!game || game.over) return; game.paused = p; $('paused').classList.toggle('hidden', !p); }
  function togglePause() { setPaused(!game.paused); }

  let last = performance.now();
  function loop(t) {
    const dt = Math.min(0.1, (t - last) / 1000); last = t;
    game.update(dt);
    renderer.frame(game.paused ? 0 : dt);
    hud.drawOverlay();
    hud.refresh();
    if (game.over && !$('gameover').classList.contains('shown')) showGameOver();
    requestAnimationFrame(loop);
  }

  function showGameOver() {
    const el = $('gameover');
    el.classList.add('shown');
    $('go-stats').innerHTML = 'Wave <b>' + game.wave + '</b> &middot; Kills <b>' + game.kills + '</b> &middot; Score <b>' + game.finalScore + '</b>' + (game.newBest ? '<br><span class="best">NEW BEST!</span>' : '<br>Best: ' + game.best);
  }

  // ---- Input ------------------------------------------------------------------------------------
  function setupInput() {
    const el = $('fx');
    const pointers = new Map();
    let dragging = false, moved = 0, pinchDist = 0, downT = 0, lastTap = 0;
    el.addEventListener('pointerdown', ev => {
      BS.audio.unlock();
      el.setPointerCapture(ev.pointerId);
      pointers.set(ev.pointerId, { x: ev.clientX, y: ev.clientY, sx: ev.clientX, sy: ev.clientY });
      if (pointers.size === 1) { dragging = true; moved = 0; downT = performance.now(); }
      if (pointers.size === 2) { const [a, b] = [...pointers.values()]; pinchDist = Math.hypot(a.x - b.x, a.y - b.y); }
    });
    el.addEventListener('pointermove', ev => {
      const p = pointers.get(ev.pointerId); if (!p) return;
      const dx = ev.clientX - p.x, dy = ev.clientY - p.y;
      p.x = ev.clientX; p.y = ev.clientY;
      if (pointers.size === 1 && dragging) { moved += Math.abs(dx) + Math.abs(dy); if (moved > 6) renderer.panBy(dx, dy); }
      else if (pointers.size === 2) {
        const [a, b] = [...pointers.values()];
        const d = Math.hypot(a.x - b.x, a.y - b.y);
        if (pinchDist > 0 && d > 0) renderer.zoomBy(pinchDist / d, (a.x + b.x) / 2, (a.y + b.y) / 2);
        pinchDist = d; moved = 100;
      }
    });
    const up = ev => {
      const p = pointers.get(ev.pointerId);
      pointers.delete(ev.pointerId);
      if (!p) return;
      if (pointers.size === 0) {
        const quick = performance.now() - downT < 350;
        if (dragging && moved <= 6 && quick) {
          const now = performance.now();
          if (now - lastTap < 300) { renderer.setCameraMode(mode); lastTap = 0; }
          else { lastTap = now; const w = renderer.screenToGround(ev.clientX, ev.clientY); if (w && game.focusAt(w[0], w[1])) hud.toast('Focus fire!', 1); }
        }
        dragging = false; pinchDist = 0;
      }
    };
    el.addEventListener('pointerup', up); el.addEventListener('pointercancel', up);
    el.addEventListener('wheel', ev => { ev.preventDefault(); renderer.zoomBy(ev.deltaY > 0 ? 1.12 : 0.89, ev.clientX, ev.clientY); }, { passive: false });
    window.addEventListener('keydown', ev => {
      if (ev.key === 'p' || ev.key === 'Escape') togglePause();
      if (ev.key === 's') game.useStim();
      if (ev.key === 'a') game.callAirStrike();
      if (ev.key === 'r') game.emergencyRepair();
      if (ev.key === '+' || ev.key === '=') renderer.zoomBy(0.85);
      if (ev.key === '-') renderer.zoomBy(1.18);
      if (ev.key === 'ArrowUp') renderer.panBy(0, 40); if (ev.key === 'ArrowDown') renderer.panBy(0, -40);
      if (ev.key === 'ArrowLeft') renderer.panBy(40, 0); if (ev.key === 'ArrowRight') renderer.panBy(-40, 0);
      if (ev.key === 'q' && mode === 'arena') { renderer.cam.yaw -= 0.2; renderer.updateCamera(); }
      if (ev.key === 'e' && mode === 'arena') { renderer.cam.yaw += 0.2; renderer.updateCamera(); }
    });
    window.addEventListener('resize', () => { if (renderer) { renderer.resize(); } });
  }

  window.addEventListener('DOMContentLoaded', boot);
})(window.BS);
