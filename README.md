# Bunker Survivors

A Terran fireteam, one bunker, and the Swarm. Hold the line against ever-growing Zerg waves with
marines, firebats, marauders, stimpacks, SCV repairs, air-dropped siege tanks, Banshee strikes and
weapon upgrades. Plays in portrait on a phone (lane defense) and unlocks a harder 360-degree arena in
a desktop browser.

The game renders **real StarCraft II models, textures, icons and sounds**, streamed on demand from
**your own StarCraft II install** by a small companion server. Nothing from the game is copied,
extracted or redistributed; the repository contains no Blizzard assets.

## Requirements

- Windows PC with StarCraft II installed (any recent 5.x build; the Starter Edition is enough).
- Node.js 18 or newer.
- A browser with WebGL. Phones connect over Wi-Fi to the PC.

## Run

```bash
npm install
node scripts/patch-viewer.js
node server/index.js
```

Or double-click `start.cmd`. The server prints the addresses to open:

- On the PC: `http://localhost:8642/`
- On a phone (same Wi-Fi): the `http://<pc-name>.local:8642/` or `http://192.168.x.x:8642/` line, also shown
  as a QR code on the start screen.

If Windows Firewall asks, allow Node.js on private networks. If the install is somewhere unusual, set
`SC2_DIR` to the StarCraft II folder (the one containing `.build.info`).

## Controls

| Action | Phone | Desktop |
| --- | --- | --- |
| Focus fire | tap a zerg | click a zerg |
| Pan | drag | drag or arrow keys |
| Zoom | pinch | mouse wheel or `+` / `-` |
| Reset camera | double-tap | double-click |
| Stim / Air strike / Repair | buttons | `S` / `A` / `R` |
| Pause | II button | `P` or `Esc` |
| Rotate camera (arena) | - | `Q` / `E` |

## How it works

- `server/casc.js` reads the local CASC storage (`.idx` indices, encoding table, BLTE decoding) in pure Node.
- `server/mndx.js` is a JavaScript port of CascLib's MNDX root parser (the compressed file-name trie SC2 uses).
- `server/index.js` serves the client and `/asset/<path>` (decoded files), `/icon/<name>.png` (DDS button icons
  decoded to PNG) and converts IMA-ADPCM `.wav` sounds to PCM so browsers can play them.
- `client/` is a plain JavaScript game on top of `mdx-m3-viewer` (WebGL M3 model renderer). The M3 parser is
  patched by `scripts/patch-viewer.js` for newer SC2 material/layer structure versions.

## Credits

- CascLib by Ladislav Zezula (MIT) for the MNDX format reference.
- mdx-m3-viewer by Chananya Freiman (MIT).
- M3 structure definitions from the m3addon project.
- StarCraft II is a trademark of Blizzard Entertainment; this is an unofficial fan project that requires your own copy.
