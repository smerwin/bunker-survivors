# Bunker Survivors (Unreal Engine 5.8)

The Unreal version of the game. It renders StarCraft II models, textures, icons and sounds converted from your
own StarCraft II install, and streams to phones and browsers with Pixel Streaming over Tailscale.

## Folders

| Path | What it is |
| --- | --- |
| `BunkerSurvivors/` | The C++ project (gameplay, camera, Slate HUD, particles) |
| `BunkerSurvivors/Scripts/import_sc2.py` | Imports `../SC2Export` into `/Game/SC2` and builds the materials and map |
| `BunkerSurvivors/Scripts/playtest.ps1` | Automated playtest: runs a match and saves screenshots |
| `PixelStreaming/` | Epic's Pixel Streaming infrastructure (signalling server and web player), built locally |
| `Packaged/` | The packaged Windows game |
| `stream.ps1` | Starts the signalling server and the game, then prints the link |

`SC2Export/`, `BunkerSurvivors/Content/SC2/` and `Packaged/` contain material derived from StarCraft II.
They are gitignored and must never be shared.

## Rebuild from scratch

```bash
node scripts/export-assets.js
```

```bash
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" Unreal\BunkerSurvivors\BunkerSurvivors.uproject -run=pythonscript -script=Unreal\BunkerSurvivors\Scripts\import_sc2.py -unattended -nullrhi
```

```bash
"C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun -project=Unreal\BunkerSurvivors\BunkerSurvivors.uproject -platform=Win64 -clientconfig=Development -build -cook -stage -pak -iostore -archive -archivedirectory=Unreal\Packaged -unattended
```

## Play

```bash
powershell -ExecutionPolicy Bypass -File Unreal\stream.ps1
```

It prints a link like `http://100.x.y.z:8650/...`. Open it on any device signed in to your tailnet. A portrait
phone gets the lane layout and a desktop browser gets landscape with the 360 arena recommended. Stop everything with
`stream.ps1 -Stop`.

## Controls

| Action | Touch | Desktop |
| --- | --- | --- |
| Focus fire | tap a zerg | click a zerg |
| Pan | drag | drag, arrow keys |
| Zoom (the camera tilts with zoom) | pinch | wheel, `+` / `-` |
| Rotate (arena) | two-finger twist | `Q` / `E` |
| Reset camera | double-tap | double-click, `Home` |
| Stim / Air strike / Repair | ability tiles | `S` / `A` / `R` |
| Pause | pause button | `P`, `Esc` |
