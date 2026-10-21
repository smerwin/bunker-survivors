# Bunker Survivors - Pixel Streaming launcher.
# Starts Epic's signalling/web server bound to this PC's Tailscale address and the packaged game streaming to it,
# then prints the link to open on a phone or desktop browser that is on the same tailnet.
#   powershell -ExecutionPolicy Bypass -File Unreal\stream.ps1 [-Port 8650] [-Bind <ip>] [-Stop]
param([int]$Port = 8650, [string]$Bind = "", [switch]$Stop)
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$infra = Join-Path $root "PixelStreaming"
$game = Join-Path $root "Packaged\Windows\BunkerSurvivors.exe"
$logs = Join-Path $root "Packaged\logs"
New-Item -ItemType Directory -Force $logs | Out-Null

function Stop-Stream {
  Get-CimInstance Win32_Process -Filter "Name='node.exe'" | Where-Object { $_.CommandLine -like "*SignallingWebServer*" } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }
  Get-Process BunkerSurvivors* -ErrorAction SilentlyContinue | Stop-Process -Force
}
if ($Stop) { Stop-Stream; "Stopped."; exit 0 }
Stop-Stream

if (-not $Bind) {
  $ts = Get-Command tailscale -ErrorAction SilentlyContinue
  $tsExe = if ($ts) { $ts.Source } else { "C:\Program Files\Tailscale\tailscale.exe" }
  if (Test-Path $tsExe) { $Bind = (& $tsExe ip -4 | Select-Object -First 1).Trim() }
}
if (-not $Bind) { Write-Error "No Tailscale address found; pass -Bind <ip>."; exit 1 }
if (-not (Test-Path $game)) { Write-Error "Packaged game not found at $game"; exit 1 }

# 1. Signalling + web frontend (player page on the Tailscale address only; streamer socket on localhost).
$env:PS_BIND_HOST = $Bind
$env:PS_STREAMER_HOST = "127.0.0.1"
$wilbur = Join-Path $infra "SignallingWebServer\dist\index.js"
$www = Join-Path $infra "SignallingWebServer\www"
Start-Process -FilePath "node" -ArgumentList @("`"$wilbur`"", "--serve", "--http_root", "`"$www`"", "--player_port", "$Port", "--streamer_port", "8888",
  "--sfu_port", "8889", "--homepage", "player.html", "--console_messages", "basic", "--log_folder", "`"$logs`"") -WindowStyle Hidden `
  -RedirectStandardOutput (Join-Path $logs "signalling.out.log") -RedirectStandardError (Join-Path $logs "signalling.err.log")
Start-Sleep -Seconds 2

# 2. The game, rendering offscreen and streaming. The browser can resize it (MatchViewportRes) so a portrait phone
#    gets a portrait stream and a desktop gets landscape; the game picks lane or arena layout from that.
Start-Process -FilePath $game -ArgumentList @("-PixelStreamingConnectionURL=ws://127.0.0.1:8888", "-RenderOffscreen", "-ResX=720", "-ResY=1280", "-ForceRes",
  "-PixelStreamingAllowPixelStreamingCommands", "-PixelStreamingEncoderCodec=H264", "-PixelStreamingWebRTCMaxBitrate=12000000", "-PixelStreamingWebRTCFps=60",
  "-Unattended", "-NoSplash", "-log", "-abslog=`"$(Join-Path $logs 'game.log')`"")

$url = "http://${Bind}:$Port/?MatchViewportRes=true&AutoConnect=true&AutoPlayVideo=true&HoveringMouse=true&StartVideoMuted=false"
""
"Bunker Survivors is streaming."
"Open on your phone or desktop (Tailscale on):"
"  $url"
"Stop with: powershell -File `"$($MyInvocation.MyCommand.Path)`" -Stop"
