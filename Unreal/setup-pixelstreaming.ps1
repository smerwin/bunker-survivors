# Bunker Survivors - fetches and builds Epic's Pixel Streaming infrastructure (signalling server + web player) into
# Unreal\PixelStreaming, then patches it so the player page listens only on the address stream.ps1 gives it (your
# Tailscale IP) while the game's streamer and SFU sockets stay on localhost.
#   powershell -ExecutionPolicy Bypass -File Unreal\setup-pixelstreaming.ps1 [-Dir <folder>] [-Branch UE5.7]
# Needs git and Node.js. Safe to run again: it reuses an existing checkout and re-applies the patch.
param([string]$Dir = "", [string]$Branch = "UE5.7")
$ErrorActionPreference = "Stop"
if (-not $Dir) { $Dir = Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) "PixelStreaming" }

if (-not (Test-Path (Join-Path $Dir "package.json"))) {
  "Downloading PixelStreamingInfrastructure ($Branch)..."
  git clone --depth 1 --branch $Branch https://github.com/EpicGamesExt/PixelStreamingInfrastructure.git $Dir
  if ($LASTEXITCODE -ne 0) { throw "git clone failed" }
}
Push-Location $Dir
try {
  "Installing and building (a few minutes)..."
  npm install --no-audit --no-fund
  if ($LASTEXITCODE -ne 0) { throw "npm install failed" }
  npm run build:all:cjs
  if ($LASTEXITCODE -ne 0) { throw "build failed" }

  # Bind the player web server to PS_BIND_HOST, and keep the streamer and SFU sockets on localhost.
  $patches = @(
    @{ File = "Signalling\dist\cjs\WebServer.js"; From = 'this.httpServer.listen(config.httpPort, () =>'; To = 'this.httpServer.listen(config.httpPort, process.env.PS_BIND_HOST || undefined, () =>' },
    @{ File = "Signalling\dist\cjs\SignallingServer.js"; From = '{ port: config.streamerPort, backlog: 1 }'; To = '{ port: config.streamerPort, host: process.env.PS_STREAMER_HOST || "127.0.0.1", backlog: 1 }' },
    @{ File = "Signalling\dist\cjs\SignallingServer.js"; From = '{ port: config.sfuPort, backlog: 1 }'; To = '{ port: config.sfuPort, host: "127.0.0.1", backlog: 1 }' }
  )
  foreach ($p in $patches) {
    $path = Join-Path $Dir $p.File
    $text = [IO.File]::ReadAllText($path)
    if ($text.Contains($p.To)) { "  already patched: $($p.File)"; continue }
    if (-not $text.Contains($p.From)) { throw "Patch target not found in $($p.File) - the upstream code changed; see the patch list in this script." }
    [IO.File]::WriteAllText($path, $text.Replace($p.From, $p.To))
    "  patched: $($p.File)"
  }
  if (-not (Test-Path (Join-Path $Dir "SignallingWebServer\www\player.html"))) { throw "Web player was not built (SignallingWebServer\www\player.html missing)" }
  "Pixel Streaming is ready in $Dir"
} finally { Pop-Location }
