# Bunker Survivors (Unreal Engine 5.8)

The Unreal version of the game. It plays the same as the browser version (see the [main README](../README.md) for
how to play), with Unreal's lighting and effects, and streams to phones and browsers with Pixel Streaming. It uses
models, animations, effects, icons, sounds and fonts converted from **your own** StarCraft II install.

**Windows only.** Building it needs a Windows PC with StarCraft II, Unreal Engine and Visual Studio. (On a Mac, use
the browser version.)

## What you need

- Windows 10 or 11, with StarCraft II installed from Battle.net (the free Starter Edition is enough).
- [Node.js](https://nodejs.org) 18 or newer and [git](https://git-scm.com).
- **Unreal Engine 5.8** from the Epic Games Launcher.
- **Visual Studio 2022** with the "Game development with C++" workload (Unreal's setup docs list the exact pieces).
- About 15 GB free for the engine's build output, cooked game and caches.
- Optional: [Tailscale](https://tailscale.com) to play from anywhere on your own devices. Without it you can stream
  on your home network instead.

The commands below are run from the repository folder in a Command Prompt or PowerShell. They assume Unreal is
installed at `C:\Program Files\Epic Games\UE_5.8`; change that path if yours differs (also in
`BunkerSurvivors\Scripts\playtest.ps1`).

## Build it

### 1. Export StarCraft II assets

```bash
npm install
node scripts/export-assets.js
```

This reads your StarCraft II install and writes `SC2Export/` (models as glTF, textures, icons, sounds, effects,
fonts). It takes a few minutes. Set `SC2_DIR` first if StarCraft II is not in a standard place.

### 2. Compile the game module

```bash
"C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" BunkerSurvivorsEditor Win64 Development -Project="%CD%\Unreal\BunkerSurvivors\BunkerSurvivors.uproject" -WaitMutex
```

### 3. Import the assets into the project

```bash
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%CD%\Unreal\BunkerSurvivors\BunkerSurvivors.uproject" -run=pythonscript -script="%CD%\Unreal\BunkerSurvivors\Scripts\import_sc2.py" -unattended -nullrhi
```

This imports `SC2Export/` into `Content/SC2` and builds the materials (it takes a while the first time). You can now
open `Unreal\BunkerSurvivors\BunkerSurvivors.uproject` in the editor and press Play.

### 4. Package the game

```bash
"C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun -project="%CD%\Unreal\BunkerSurvivors\BunkerSurvivors.uproject" -platform=Win64 -clientconfig=Development -build -cook -stage -pak -iostore -archive -archivedirectory="%CD%\Unreal\Packaged" -unattended
```

The game lands in `Unreal\Packaged\Windows\BunkerSurvivors.exe`. You can run it directly to play on the PC.

## Stream it to a phone or browser

### 5. Set up Pixel Streaming (once)

```bash
powershell -ExecutionPolicy Bypass -File Unreal\setup-pixelstreaming.ps1
```

This downloads Epic's [PixelStreamingInfrastructure](https://github.com/EpicGamesExt/PixelStreamingInfrastructure)
(the UE5.7 branch, which the Pixel Streaming 2 plugin in UE 5.8 uses) into `Unreal\PixelStreaming`, builds the
signalling server and web player, and patches it so the player page listens only on the address you choose while the
game's own connections stay on the PC.

### 6. Start streaming

With Tailscale running:

```bash
powershell -ExecutionPolicy Bypass -File Unreal\stream.ps1
```

Without Tailscale, give it the PC's address on your home network instead (find it with `ipconfig`):

```bash
powershell -ExecutionPolicy Bypass -File Unreal\stream.ps1 -Bind 192.168.1.20
```

It prints a link like `http://100.x.y.z:8650/...`; open it on your phone or another computer. A portrait phone gets
the lane layout; a desktop browser gets landscape. Add `-Lite` for weak Wi-Fi (540p, 30 fps, about 3 Mbit/s with
error correction). Stop everything with `stream.ps1 -Stop`. Allow the signalling server (Node.js) and the game through
Windows Firewall on private networks if asked.

## Controls

| Action | Touch | Desktop |
| --- | --- | --- |
| Focus fire | tap a zerg | click a zerg |
| Pan (turn, in bunker view) | drag | drag, arrow keys |
| Zoom (the camera tilts with zoom) | pinch | wheel, `+` / `-` |
| Rotate (arena) | two-finger twist | `Q` / `E` |
| Reset camera | double-tap | double-click, `Home` |
| Bunker view on / off | tap the bunker picture on the squad card | `V` |
| Send a soldier out / back in | tap their portrait on the squad card | click it |
| Stim / Air strike / Repair / Yamato | ability tiles | `S` / `A` / `R` / `Y` |
| Pause | pause button | `P`, `Esc` |

## Folders

| Path | What it is |
| --- | --- |
| `BunkerSurvivors/Source/BunkerSurvivors/` | The C++ game: simulation (`BSGame`), terrain (`BSTerrain`), camera, Slate HUD, particle effects |
| `BunkerSurvivors/Scripts/import_sc2.py` | Imports `../SC2Export` into `/Game/SC2` and builds the materials and map |
| `BunkerSurvivors/Scripts/playtest.ps1` | Automated playtest: builds, runs a match with a bot and saves screenshots |
| `setup-pixelstreaming.ps1` | Fetches, builds and patches Epic's Pixel Streaming infrastructure into `PixelStreaming/` |
| `stream.ps1` | Starts the signalling server and the packaged game, then prints the link |
| `PixelStreaming/`, `Packaged/` | Generated by the steps above (gitignored) |

`SC2Export/`, `BunkerSurvivors/Content/SC2/` and `Packaged/` contain material derived from StarCraft II. They are
gitignored and must never be shared or uploaded.

## Development notes

- The simulation is a port of `client/js/game.js` (and `BSTerrain` of `client/js/terrain.js`); keep the two in step.
- Playtests: `powershell -File Unreal\BunkerSurvivors\Scripts\playtest.ps1 -Name lane1 -Mode lane` runs a match and
  writes screenshots to `Saved/Shots/<name>`. Useful extra flags (via `-Extra "..."`): `-BSTestSquad` (squad,
  turrets, sensor tower, air strike), `-BSBunkerView` (turns slowly in the bunker view), `-BSTestAbilities`,
  `-BSSpawn=ultralisk@0@8` (place zerg), `-BSTechMax` (research everything, unlocking the high-tech cards;
  add `-BSTestTech` to buy them all).
- After adding a model or effect to `client/js/units.js`, rerun the export, then the import with
  `BS_ONLY=<model keys>` set to import just those models.
