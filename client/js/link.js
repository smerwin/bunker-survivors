// Linking StarCraft II. Two ways to play:
//  - with the companion server (npm start): it reads the install and serves the art, phones included;
//  - on the public web page: the player points the browser at their own StarCraft II folder once. A worker reads the
//    files in place (nothing is uploaded), the service worker (sw.js) keeps what the game used in the browser's cache,
//    and later visits start straight from that cache.
window.BS = window.BS || {};
(function (BS) {
  'use strict';

  const $ = id => document.getElementById(id);
  const mac = /mac/i.test((navigator.userAgentData && navigator.userAgentData.platform) || navigator.platform || '');
  const DEFAULT_DIR = mac ? '/Applications/StarCraft II' : 'C:\\Program Files (x86)\\StarCraft II';
  const LINKED = 'bs.sc2.linked';
  let worker = null;
  const store = (k, v) => { try { if (v === undefined) return localStorage.getItem(k); localStorage.setItem(k, v); } catch (e) { return null; } };

  // The service worker forwards cache misses here; the SC2 worker answers them straight back on the given port.
  function relay(e) {
    if (!e.data || e.data.type !== 'sc2-get') return;
    const port = e.ports[0];
    if (worker) worker.postMessage({ type: 'get', path: e.data.path, port }, [port]);
    else {
      port.postMessage({ status: 503, type: 'text/plain', body: new TextEncoder().encode('StarCraft II folder not linked').buffer });
      const r = $('relink'); if (r) r.classList.remove('hidden');
    }
  }

  // Only the CASC storage matters: SC2Data/ and .build.info (the rest of the install is never looked at).
  const wanted = path => /(^|\/)sc2data\//i.test(path) || /(^|\/)\.build\.info$/i.test(path);

  function openWorker(entries) {
    entries = entries.filter(x => wanted(x.path));
    return new Promise((resolve, reject) => {
      const w = new Worker('/gen/sc2-worker.js');
      w.onmessage = e => {
        if (e.data.type !== 'opened') return;
        if (e.data.ok) { worker = w; resolve(e.data); } else { w.terminate(); reject(new Error(e.data.error)); }
      };
      w.onerror = e => { w.terminate(); reject(new Error(e.message || 'the StarCraft II reader failed to start')); };
      w.postMessage({ type: 'open', entries });
    });
  }

  // Drag and drop: walk the dropped folder (directory entries, so no "upload" prompt and no blocked-folder rules).
  async function walk(entry, out) {
    if (entry.isFile) { out.push({ path: entry.fullPath.replace(/^\//, ''), file: await new Promise((res, rej) => entry.file(res, rej)) }); return; }
    if (entry.fullPath.split('/').length > 2 && !wanted(entry.fullPath.slice(1) + '/')) return;   // skip Versions/, Support/ ...
    const reader = entry.createReader();
    for (;;) {
      const batch = await new Promise((res, rej) => reader.readEntries(res, rej));
      if (!batch.length) break;
      for (const c of batch) await walk(c, out);
    }
  }

  // The first-visit card: pick (or drop) the StarCraft II folder. Resolves once the install is open.
  function setup() {
    return new Promise(resolve => {
      const card = $('link'), err = $('linkErr'), busy = $('linkBusy');
      $('linkPath').textContent = DEFAULT_DIR;
      $('linkHint').innerHTML = mac
        ? 'In the folder dialog press <b>&#8984; &#8679; G</b>, paste the path, press Return, then choose <b>Upload</b>.'
        : 'In the folder dialog paste the path into the address bar, press Enter, then choose <b>Upload</b>.';
      card.classList.remove('hidden');
      const go = async entries => {
        err.textContent = ''; busy.textContent = 'READING STARCRAFT II...';
        try {
          const st = await openWorker(entries);
          store(LINKED, st.version);
          card.classList.add('hidden');
          resolve({ mode: 'local', sc2Version: st.version });
        } catch (e) { err.textContent = e.message; busy.textContent = ''; }
      };
      $('linkCopy').onclick = () => { navigator.clipboard && navigator.clipboard.writeText(DEFAULT_DIR); $('linkCopy').textContent = 'COPIED'; };
      $('linkPick').onclick = () => $('linkInput').click();
      $('linkInput').onchange = e => { const fs = [...e.target.files]; if (fs.length) go(fs.map(f => ({ path: f.webkitRelativePath || f.name, file: f }))); };
      card.ondragover = e => { e.preventDefault(); card.classList.add('drag'); };
      card.ondragleave = () => card.classList.remove('drag');
      card.ondrop = async e => {
        e.preventDefault(); card.classList.remove('drag');
        const item = [...e.dataTransfer.items].map(i => i.webkitGetAsEntry && i.webkitGetAsEntry()).find(x => x && x.isDirectory);
        if (!item) { err.textContent = 'Drop the StarCraft II folder itself.'; return; }
        busy.textContent = 'LISTING FILES...';
        const out = []; await walk(item, out); go(out);
      };
    });
  }

  // Art requested before the service worker could answer (start-screen icons, the UI fonts) is fetched again.
  function refreshArt() {
    document.querySelectorAll('img[src^="/icon/"]').forEach(img => { img.src = img.getAttribute('src').split('?')[0] + '?l=1'; });
    for (const [file, family, weight] of [['eurostile-reg', 'Eurostile', '400'], ['eurostile-bol', 'Eurostile', '700'], ['eurostileext-med', 'Eurostile Wide', '500']]) {
      const f = new FontFace(family, 'url(/font/' + file + '.otf?l=1)', { weight });
      f.load().then(x => document.fonts.add(x)).catch(() => {});
    }
  }

  BS.linkSC2 = async function () {
    try {
      const r = await fetch('/api/status');
      if (r.ok) return Object.assign({ mode: 'server' }, await r.json());
    } catch (e) { /* no companion server: the web edition */ }
    if (!('serviceWorker' in navigator)) throw new Error('This browser cannot run the web edition (no service workers). Try Chrome, Edge, Firefox or Safari.');
    navigator.serviceWorker.addEventListener('message', relay);
    await navigator.serviceWorker.register('/sw.js');
    await navigator.serviceWorker.ready;
    if (!navigator.serviceWorker.controller) await new Promise(r => navigator.serviceWorker.addEventListener('controllerchange', r, { once: true }));
    const was = store(LINKED);
    const cached = was && await caches.open('bs-sc2-v1').then(c => c.match(location.origin + '/asset/' + (BS.MODELS.bunker.path || BS.MODELS.bunker))).catch(() => null);
    const st = cached ? { mode: 'cached', sc2Version: was } : await setup();
    refreshArt();
    return st;
  };

  // Re-link from the start screen (art the cache does not have yet needs the folder again).
  BS.relinkSC2 = () => setup().then(refreshArt);
})(window.BS);
