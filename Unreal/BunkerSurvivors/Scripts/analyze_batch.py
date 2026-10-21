"""Summarises automated test games (batch.jsonl) into a self-contained HTML report with charts."""
import json, sys, statistics as st
from collections import defaultdict

src = sys.argv[1]
out = sys.argv[2]
rows = [json.loads(l) for l in open(src, encoding="utf-8-sig") if l.strip()]
waves = [r for r in rows if r["kind"] == "wave"]
finals = [r for r in rows if r["kind"] == "final"]
BOTS = ["balanced", "marines", "mech", "random", "passive"]
MODES = ["lane", "arena"]


def mean(xs):
    return st.mean(xs) if xs else 0


# ---- Per mode/bot outcomes ----
outcome = {}
for m in MODES:
    for b in BOTS:
        f = [r for r in finals if r["mode"] == m and r["bot"] == b]
        if not f:
            continue
        outcome[(m, b)] = dict(
            games=len(f), wave=mean([r["wave"] for r in f]), wmin=min(r["wave"] for r in f), wmax=max(r["wave"] for r in f),
            minutes=mean([r["t"] for r in f]) / 60, kills=mean([r["kills"] for r in f]), peak=mean([r["peakAlive"] for r in f]),
            first=mean([r["firstDamageAt"] for r in f if r["firstDamageAt"] >= 0]) / 60 if any(r["firstDamageAt"] >= 0 for r in f) else -1,
            capped=sum(1 for r in f if r["end"] == "timecap"), ppm=mean([r["purchases"] / max(r["t"] / 60, 0.01) for r in f]),
        )

# ---- Per-wave curves (by mode, all bots except passive, and balanced alone) ----
def curve(mode, bots, key, agg=mean):
    by = defaultdict(list)
    for r in waves:
        if r["mode"] == mode and r["bot"] in bots:
            by[r["wave"]].append(r[key])
    return {w: agg(v) for w, v in sorted(by.items()) if len(v) >= 2}


# ---- Fun heuristics per mode for the "skilled" bots ----
skilled = ["balanced", "marines", "mech"]
fun = {}
for m in MODES:
    w = [r for r in waves if r["mode"] == m and r["bot"] in skilled]
    if not w:
        continue
    threatened = sum(1 for r in w if r["minHpPct"] < 0.9) / len(w)
    idle_frac = mean([r["idle"] / max(r["duration"], 1) for r in w])
    floating = mean([r["avgMinerals"] for r in w])
    cliffs = 0
    games = defaultdict(list)
    for r in w:
        games[(r["bot"], r["seed"])].append(r)
    for g in games.values():
        g.sort(key=lambda r: r["wave"])
        for i in range(1, len(g)):
            if g[i - 1]["minHpPct"] > 0.75 and g[i]["minHpPct"] <= 0.0:
                cliffs += 1
    early_bored = mean([sum(1 for r in g if r["wave"] <= 6 and r["minHpPct"] >= 0.999) for g in games.values()])
    fun[m] = dict(threatened=threatened, idle=idle_frac, floating=floating, cliffs=cliffs, games=len(games), early_bored=early_bored)

# ---- Horde size ----
horde = {m: curve(m, BOTS, "peakAlive", max) for m in MODES}
spawned = {m: curve(m, ["balanced"], "spawned") for m in MODES}
hp = {m: {b: curve(m, [b], "minHpPct") for b in BOTS} for m in MODES}


# ---- SVG chart helper ----
def chart(series, title, ymax=None, yfmt="{:.0f}", w=560, h=220):
    pad_l, pad_b, pad_t, pad_r = 40, 26, 24, 10
    xs = sorted({x for s in series.values() for x in s})
    if not xs:
        return ""
    xmax = max(xs)
    ym = ymax or max(max(s.values()) for s in series.values() if s) or 1
    sx = lambda x: pad_l + (x - 1) / max(xmax - 1, 1) * (w - pad_l - pad_r)
    sy = lambda y: pad_t + (1 - y / ym) * (h - pad_t - pad_b)
    colors = ["#3b82f6", "#f59e0b", "#10b981", "#a855f7", "#94a3b8", "#ef4444"]
    parts = [f'<svg viewBox="0 0 {w} {h}" class="chart" role="img" aria-label="{title}">',
             f'<text x="{pad_l}" y="15" class="ct">{title}</text>']
    for i in range(5):
        y = ym * i / 4
        parts.append(f'<line x1="{pad_l}" x2="{w - pad_r}" y1="{sy(y):.1f}" y2="{sy(y):.1f}" class="grid"/>')
        parts.append(f'<text x="{pad_l - 6}" y="{sy(y) + 4:.1f}" class="ax" text-anchor="end">{yfmt.format(y)}</text>')
    for x in xs:
        if x == 1 or x % 5 == 0:
            parts.append(f'<text x="{sx(x):.1f}" y="{h - 8}" class="ax" text-anchor="middle">{x}</text>')
    for i, (name, s) in enumerate(series.items()):
        if not s:
            continue
        pts = " ".join(f"{sx(x):.1f},{sy(min(y, ym)):.1f}" for x, y in sorted(s.items()))
        parts.append(f'<polyline points="{pts}" fill="none" stroke="{colors[i % 6]}" stroke-width="2"/>')
    legend = "".join(f'<span><i style="background:{colors[i % 6]}"></i>{n}</span>' for i, n in enumerate(series))
    return "".join(parts) + "</svg>" + f'<div class="legend">{legend}</div>'


# ---- HTML ----
def tbl(headers, body):
    th = "".join(f"<th>{h}</th>" for h in headers)
    tr = "".join("<tr>" + "".join(f"<td>{c}</td>" for c in r) + "</tr>" for r in body)
    return f"<table><thead><tr>{th}</tr></thead><tbody>{tr}</tbody></table>"


body = []
body.append(tbl(["Mode", "Bot", "Games", "Avg wave", "Range", "Minutes", "Kills", "Peak zerg", "First damage (min)", "Buys / min"],
                [[m, b, o["games"], f'{o["wave"]:.1f}', f'{o["wmin"]}–{o["wmax"]}', f'{o["minutes"]:.1f}', f'{o["kills"]:.0f}', f'{o["peak"]:.0f}',
                  (f'{o["first"]:.1f}' if o["first"] >= 0 else "never"), f'{o["ppm"]:.1f}'] for (m, b), o in outcome.items()]))
fun_rows = [[m, f'{f["threatened"] * 100:.0f}%', f'{f["idle"] * 100:.0f}%', f'{f["floating"]:.0f}', f'{f["early_bored"]:.1f} of 6', f["cliffs"]] for m, f in fun.items()]
fun_tbl = tbl(["Mode", "Waves with bunker damage", "Downtime (no zerg)", "Avg unspent minerals", "Damage-free early waves", "Sudden-death cliffs"], fun_rows)

charts = ""
for m in MODES:
    charts += f"<h3>{m.title()}</h3><div class='grid2'>"
    charts += "<div>" + chart({"peak zerg alive (max of all games)": horde[m], "spawned per wave (balanced bot)": spawned[m]}, "Horde size per wave") + "</div>"
    charts += "<div>" + chart({b: hp[m][b] for b in BOTS}, "Lowest bunker HP in the wave (average)", ymax=1, yfmt="{:.0%}") + "</div></div>"

# ---- Optional comparison with a previous batch ----
compare = ""
if len(sys.argv) > 3:
    old = [json.loads(l) for l in open(sys.argv[3], encoding="utf-8-sig") if l.strip()]
    of = [r for r in old if r["kind"] == "final"]
    ow = [r for r in old if r["kind"] == "wave"]
    rows_c = []
    for (m, b), o in outcome.items():
        f0 = [r for r in of if r["mode"] == m and r["bot"] == b]
        if not f0:
            continue
        w0, p0, t0 = mean([r["wave"] for r in f0]), mean([r["peakAlive"] for r in f0]), mean([r["t"] for r in f0]) / 60
        rows_c.append([m, b, f'{w0:.1f} → {o["wave"]:.1f}', f'{p0:.0f} → {o["peak"]:.0f}', f'{t0:.1f} → {o["minutes"]:.1f}'])
    for m in MODES:
        w0 = [r for r in ow if r["mode"] == m and r["bot"] in skilled]
        if w0 and m in fun:
            rows_c.append([m, "skilled, unspent minerals", "", "", f'{mean([r["avgMinerals"] for r in w0]):.0f} → {fun[m]["floating"]:.0f}'])
    compare = "<h2>Before and after the fixes</h2>" + tbl(["Mode", "Bot", "Avg wave", "Peak zerg", "Minutes (or minerals)"], rows_c)

data_json = json.dumps(dict(outcome={f"{m}/{b}": o for (m, b), o in outcome.items()}, fun=fun), indent=1)
html = f"""<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Bunker Survivors Playtest</title><style>
:root{{--bg:#0b0f16;--fg:#e5ecf5;--mut:#8b98ab;--line:#223047;--card:#121a26}}
@media (prefers-color-scheme: light){{:root{{--bg:#f7f9fc;--fg:#0f172a;--mut:#5b6b80;--line:#d8e0ea;--card:#fff}}}}
body{{margin:0;background:var(--bg);color:var(--fg);font:15px/1.5 system-ui,sans-serif}}
main{{max-width:1100px;margin:0 auto;padding:24px 16px}}
table{{border-collapse:collapse;width:100%;font-size:13px;margin:8px 0 24px;display:block;overflow-x:auto}}
th,td{{padding:6px 10px;border-bottom:1px solid var(--line);text-align:right;white-space:nowrap}} th:first-child,td:first-child,th:nth-child(2),td:nth-child(2){{text-align:left}}
th{{color:var(--mut);font-weight:600}} .grid2{{display:grid;grid-template-columns:repeat(auto-fit,minmax(320px,1fr));gap:16px}}
.chart{{width:100%;background:var(--card);border:1px solid var(--line);border-radius:10px}}
.chart .grid{{stroke:var(--line)}} .chart .ax{{fill:var(--mut);font-size:10px}} .chart .ct{{fill:var(--fg);font-size:12px;font-weight:600}}
.legend{{display:flex;flex-wrap:wrap;gap:12px;font-size:12px;color:var(--mut);margin:6px 0 0}} .legend i{{display:inline-block;width:10px;height:10px;border-radius:2px;margin-right:5px}}
</style></head><body><main>
<h1>Bunker Survivors: automated playtest</h1>
<p>{len(finals)} headless games, {len(waves)} waves. Five bot commanders per mode: balanced, marine-only, mech (tanks first), random, and passive (never buys anything).</p>
{compare}<h2>Outcomes</h2>{''.join(body)}
<h2>Fun signals (skilled bots: balanced, marines, mech)</h2>{fun_tbl}
<h2>Curves</h2>{charts}
<details><summary>Raw summary</summary><pre>{data_json}</pre></details>
</main></body></html>"""
open(out, "w", encoding="utf-8").write(html)
print(data_json)
