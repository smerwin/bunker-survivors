# Bunker Survivors: Unreal Engine port plan

> **Historical design notes.** This is the original plan from 2026-10-20, kept for its design reasoning (camera,
> UI, delivery). The port is done: for how to build and run it today, see [Unreal/README.md](../Unreal/README.md).

Decision (2026-10-20): delivery is **Pixel Streaming over Tailscale**. The native Android option is dropped.

## What already works

`node scripts/export-assets.js` reads StarCraft II from the local install and writes `SC2Export/`
(gitignored, never share it):

| Output | Format | Notes |
| --- | --- | --- |
| 31 unit, building, death and effect models | `.glb` | Skinned mesh, full skeleton, every SC2 animation sequence sampled at 30 fps, attachment points as `Attach_*` nodes, PNG textures embedded |
| 23 command-card icons | `.png` | Marine, firebat, stim, weapons levels, zerg portraits |
| 37 sounds | `.wav` 16-bit PCM | ADPCM originals decoded so Unreal can import them |
| Ground texture | `.png` | Char tileset |
| `manifest.json` | JSON | Per model: animation names, lengths, movement speeds, attachments, bounds |

Model conventions Unreal needs to respect:

- **Team colour** lives in the base-colour texture's alpha channel. The master material lerps base colour toward a team-colour parameter by that alpha.
- **Additive glow parts** (engine flares, eyes) are tagged `extras.additive` on their glTF material. They need an additive material instance, not translucency.
- **Facing**: SC2 models face -Y. The unit actor's mesh component gets a -90 degree yaw so units face +X like Unreal characters.
- **Scale**: one SC2 unit is one glTF metre, so 100 Unreal units. A zergling is roughly 2 m long.
- **Particle-only effects** (tank muzzle blast, banshee impact) have no mesh. Those become Niagara systems.

## Architecture

One C++ module (`Unreal/BunkerSurvivors/Source/BunkerSurvivors`): `BSGame` holds the whole simulation (a port of
`client/js/game.js`) and builds the world at runtime, `BSTerrain` the lane chokepoint, `BSCameraPawn` the camera below,
`SBSHud` the Slate HUD, `BSFx` the StarCraft II particle player, and `BSAssets` finds the imported art under
`/Game/SC2`. Imports run through an editor Python script (`Scripts/import_sc2.py`) using Interchange (glTF to skeletal
mesh plus animation sequences), so re-exporting after a StarCraft II patch is one command.

## Camera: angle changes with zoom

One zoom value `z` from 0 (closest) to 1 (furthest), driven by pinch, mouse wheel or keys, smoothed
with a critically damped spring so it settles without overshoot.

| Property | Zoomed in (`z = 0`) | Zoomed out (`z = 1`) | Curve |
| --- | --- | --- | --- |
| Spring arm length | 9 m | 42 m | exponential, so each pinch step feels equal |
| Pitch | 28 degrees down | 68 degrees down | ease-in-out |
| Field of view | 52 degrees | 40 degrees | linear |
| Focus point | 2 m ahead of the bunker, toward the threat | bunker centre | linear |
| Depth of field | on, focused on the bunker | off | fades out past `z = 0.3` |

Close in, the view drops toward a low, over-the-shoulder angle looking up the lane at incoming zerg.
Far out, it becomes a near top-down tactical view. In the 360 arena, a two-finger twist or `Q`/`E`
orbits the yaw. Double-tap springs back to the default `z = 0.45`. Big hits add a short camera shake
scaled by `1 - z`, so it is strongest when you are close.

## UI: Terran command console

The flat dark panels become a diegetic Terran console, built in UMG with material-driven effects so it
stays cheap on a phone stream.

- **Frames**: chamfered, bolted gunmetal plates with hazard-stripe edges. The bottom deck is a single angled console slab that the ability buttons sit in.
- **Holographic layer**: readouts glow cyan on a faint scanline and CRT-flicker material. A slow sweep line crosses the deck every few seconds.
- **Minerals**: a spinning crystal icon. Gains tick up digit by digit with a blue flash. Spending flashes red and shakes the counter when you can't afford something.
- **Bunker integrity**: a segmented armour bar of 20 plates. Each plate cracks and sparks as it is lost. Below 25 percent it pulses red, and an "INTEGRITY CRITICAL" klaxon plays once.
- **Fireteam slots**: portrait cards with the SC2 icon, a vertical health strip and a stim glow. A new recruit slides in with a drop-pod thud.
- **Abilities**: hex-cut buttons with a radial cooldown wipe and a cost plate. Stim, Air Strike and Repair each get their own accent colour. A button pulses when it comes off cooldown and you can afford it.
- **Shop**: a slide-up armoury drawer with tilted cards. Cards you can't afford dim with a red cost. Maxed cards get a gold "MAX" stamp.
- **Wave banner**: a full-width sweep reading "WAVE 7 // HEAVY ASSAULT" with a radar ping, plus edge-of-screen threat arrows in the 360 arena.
- **Fonts**: a condensed military stencil face for headings and a monospaced readout face for numbers.

## Delivery: phone and browser

Unreal no longer exports to the web (HTML5 support ended with 4.24), so neither the phone nor the
browser can run an Unreal build directly the way they ran the JS version.

**Recommended: Pixel Streaming.** The game runs on the PC and streams video to any browser, with touch
and mouse input sent back. It keeps the current workflow: one link over Tailscale opens it on the phone,
and a desktop browser gets the 360 arena. Portrait clients get lane mode and landscape clients get the
arena, chosen from the resolution the browser reports.

- **Pro:** the SC2 assets never leave the PC, and there is one build for every device.
- **Con:** the PC must be running and online, and there is about 50 to 100 ms of extra input latency.
- **GPU:** integrated graphics are enough: plan for 720p streaming with Lumen and Nanite off. Pixel Streaming supports AMD, NVIDIA and Intel hardware encoders.

**Alternative: a native Android app.** It plays offline with no stream latency, but needs the Android
SDK and NDK, copies the converted SC2 assets onto the phone, and has no browser 360 mode. An iPhone
build would also need a Mac.

## Build order

1. Create the C++ project and import `SC2Export` through the editor script.
2. Set up the materials (team colour, additive glow, ground).
3. Port the simulation from `client/js/game.js`, keeping the same SC2 stats.
4. Build the camera pawn and the console UI.
5. Add Niagara effects.
6. Package and turn on Pixel Streaming, served on the Tailscale address.
