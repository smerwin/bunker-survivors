// Lane-mode terrain: a Y-shaped chokepoint. The low ground is a stem running north from the bunker that splits into
// north-west and north-east arms; a mesa sits between the arms and plateaus flank the stem. Ramps lead from each arm
// up onto its side plateau, where ranged zerg can take the high ground over the bunker.
// Used by the simulation (heights, walkability, routes) and the renderer (the ground mesh). Game units, Z up.
window.BS = window.BS || {};
(function (BS) {
  'use strict';

  const H = 2.0;                  // plateau height
  const clamp = (v, a, b) => v < a ? a : v > b ? b : v;
  const smooth = (a, b, x) => { const t = clamp((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); };

  function segDist(px, py, ax, ay, bx, by) {
    const dx = bx - ax, dy = by - ay, t = clamp(((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy), 0, 1);
    return Math.hypot(px - ax - dx * t, py - ay - dy * t);
  }
  // Cheap smooth noise for ragged cliff edges.
  const noise = (x, y) => 0.5 * Math.sin(x * 1.7 + Math.sin(y * 0.9) * 2) * Math.sin(y * 1.3 + Math.sin(x * 0.7) * 2) + 0.25 * Math.sin(x * 3.1 - y * 2.3);

  // Ramps: from the arm floor (a) up to the plateau (b).
  const RAMPS = [-1, 1].map(s => ({ a: [7.2 * s, 13.7], b: [11.1 * s, 10.9], w: 1.3 }));
  // The arms run from the fork (2, 9.5) out to (14.5, 27), mirrored.
  const ARM = { ax: 2.0, ay: 9.5, bx: 14.5, by: 27, r: 3.6 };

  // Signed distance to the low ground (negative inside).
  function lowSdf(x, y) {
    const ax = Math.abs(x);
    const w = 3.9 + 3.9 * clamp((3 - y) / 2.5, 0, 1);               // a narrow stem that widens into a pad around the bunker and tanks
    const stem = Math.max(ax - w, y - 10.5);
    const arm = segDist(ax, y, ARM.ax, ARM.ay, ARM.bx, ARM.by) - ARM.r; // the two arms (mirrored)
    const north = 26.5 - y;                                         // open ground to the north where waves gather
    return Math.min(stem, arm, north) + noise(x, y) * 0.35;
  }

  // Ramp influence (0 = none, 1 = on the ramp) and progress along it (0 at the bottom, 1 at the top).
  function rampAt(x, y) {
    for (const r of RAMPS) {
      const dx = r.b[0] - r.a[0], dy = r.b[1] - r.a[1], L2 = dx * dx + dy * dy, L = Math.sqrt(L2);
      const t = ((x - r.a[0]) * dx + (y - r.a[1]) * dy) / L2;
      if (t < -0.25 || t > 1.25) continue;
      const perp = Math.abs(((x - r.a[0]) * dy - (y - r.a[1]) * dx) / L);
      const k = 1 - smooth(r.w, r.w + 0.6, perp);
      if (k > 0) return { k: k * (1 - smooth(1, 1.25, t)) * (1 - smooth(0, -0.25, t)), t: clamp(t, 0, 1) };
    }
    return null;
  }

  function height(x, y) {
    if (!BS.Terrain.active) return 0;
    const d = lowSdf(x, y);
    let h = H * smooth(0.0, 1.15, d);
    const r = rampAt(x, y);
    if (r && r.k > 0) h = h + (H * r.t - h) * r.k;
    return h;
  }
  // The simulation asks for heights and slopes constantly (every zerg step), and the exact field is a stack of noise and
  // distance terms. Cache both on a 10 cm grid over the play area, filled lazily: heights interpolated between exact
  // corner samples, slopes per cell. Outside the grid (and for the renderer's ground mesh) the exact functions answer.
  const GX0 = -40, GY0 = -40, GS = 0.1, GNX = 800, GNY = 900;
  let hGrid = null, sGrid = null;
  function hCorner(i, j) {
    const k = j * (GNX + 1) + i;
    let v = hGrid[k];
    if (v !== v) v = hGrid[k] = height(GX0 + i * GS, GY0 + j * GS);
    return v;
  }
  function heightFast(x, y) {
    if (!BS.Terrain.active) return 0;
    const fx = (x - GX0) / GS, fy = (y - GY0) / GS, i = Math.floor(fx), j = Math.floor(fy);
    if (i < 0 || j < 0 || i >= GNX || j >= GNY) return height(x, y);
    if (!hGrid) { hGrid = new Float32Array((GNX + 1) * (GNY + 1)).fill(NaN); sGrid = new Float32Array(GNX * GNY).fill(NaN); }
    const tx = fx - i, ty = fy - j;
    const a = hCorner(i, j), b = hCorner(i + 1, j), c = hCorner(i, j + 1), d = hCorner(i + 1, j + 1);
    return (a + (b - a) * tx) * (1 - ty) + (c + (d - c) * tx) * ty;
  }
  function slopeFast(x, y) {
    const i = Math.floor((x - GX0) / GS), j = Math.floor((y - GY0) / GS);
    if (i < 0 || j < 0 || i >= GNX || j >= GNY || !BS.Terrain.active) return slope(x, y);
    if (!sGrid) heightFast(x, y);
    const k = j * GNX + i;
    let v = sGrid[k];
    if (v !== v) v = sGrid[k] = slope(GX0 + (i + 0.5) * GS, GY0 + (j + 0.5) * GS);
    return v;
  }
  function slope(x, y) {
    const e = 0.15;
    return Math.hypot(height(x + e, y) - height(x - e, y), height(x, y + e) - height(x, y - e)) / (2 * e);
  }

  BS.Terrain = {
    active: false, H, height: heightFast, heightExact: height, slope: slopeFast, lowSdf, ramps: RAMPS, arm: ARM,
    setMode(mode) { this.active = mode === 'lane'; },
    // Ground units cannot climb cliff faces (the ramps are gentle enough to walk).
    walkable(x, y) { return !this.active || slopeFast(x, y) < 1.05; },
    onHigh(x, y) { return this.active && heightFast(x, y) > H * 0.6; },
    // Waypoints for a ground unit spawning at (x, y): down an arm into the stem, or up a ramp to a perch.
    route(x, y, high) {
      if (!this.active) return [];
      const s = x === 0 ? (Math.random() < 0.5 ? -1 : 1) : Math.sign(x);
      const pts = [];
      // From the open ground in the north: skirt the mesa's tip, then drop into the arm.
      if (y > 24.5) pts.push([12.8 * s + (Math.random() - 0.5), 27.4], [11.8 * s + (Math.random() - 0.5) * 0.8, 22.5]);
      if (high) {
        const r = RAMPS[s < 0 ? 0 : 1];
        pts.push([r.a[0] - 0.4 * s, r.a[1] + 0.6], [r.b[0] + 0.3 * s, r.b[1] - 0.4]);
        // Perch on the cliff edge overlooking the bunker.
        pts.push([(5.7 + Math.random() * 0.5) * s, 3.3 + Math.random() * 1.0]);
      } else pts.push([1.6 * s + (Math.random() - 0.5) * 1.5, 9]);
      return pts;
    },
    // The way from the bunker up onto the side plateau east (s = 1) or west (-1) of the stem: up the stem to the fork,
    // into the arm, then up its ramp (walked in reverse to come back down).
    ledgeRoute(s) {
      const r = RAMPS[s < 0 ? 0 : 1];
      return [[2.2 * s, 5.5], [2.9 * s, 10.4], [r.a[0] - 0.4 * s, r.a[1] + 0.6], [r.b[0] + 0.3 * s, r.b[1] - 0.4]];
    },
  };
})(window.BS);
