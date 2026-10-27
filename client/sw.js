// Service worker for the web edition (no companion server). The game asks for its StarCraft II art at /asset/, /fx/,
// /fxtex/, /icon/ and /font/, exactly as it would from server/index.js. Answers come from the browser's cache when the
// file was read before, otherwise from the page's SC2 worker (client/js/link.js), which reads the player's own install.
'use strict';
const CACHE = 'bs-sc2-v1';
const SC2 = /^\/(asset|fx|fxtex|icon|font)\//;

self.addEventListener('install', () => self.skipWaiting());
self.addEventListener('activate', e => e.waitUntil(self.clients.claim()));

self.addEventListener('fetch', e => {
  const u = new URL(e.request.url);
  if (u.origin !== location.origin || !SC2.test(u.pathname)) return;
  e.respondWith(serve(e, u));
});

async function serve(e, u) {
  const cache = await caches.open(CACHE);
  const key = u.origin + u.pathname;              // query strings only ever bust the browser's image cache
  const hit = await cache.match(key);
  if (hit) return hit;
  const client = (e.clientId && await self.clients.get(e.clientId)) || (await self.clients.matchAll({ type: 'window' }))[0];
  if (!client) return new Response('no page to read StarCraft II through', { status: 503 });
  const r = await new Promise(resolve => {
    const ch = new MessageChannel(), t = setTimeout(() => resolve(null), 120000);
    ch.port1.onmessage = m => { clearTimeout(t); resolve(m.data); };
    client.postMessage({ type: 'sc2-get', path: u.pathname }, [ch.port2]);
  });
  if (!r) return new Response('timed out reading StarCraft II', { status: 504 });
  const resp = new Response(r.body, { status: r.status, headers: { 'Content-Type': r.type } });
  if (r.status === 200) await cache.put(key, resp.clone());
  return resp;
}
