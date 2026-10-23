# Builds the editor module, runs an automated playtest window and prints screenshots + key log lines.
# Usage: powershell -File Scripts\playtest.ps1 -Name lane1 -Mode lane -ResX 540 -ResY 960 -Quit 70 -Every 8 [-Speed 1.5] [-Zoom 0.45] [-NoBuild]
#        [-UnrealDir "C:\Program Files\Epic Games\UE_5.8"]
param(
  [string]$Name = "lane1", [string]$Mode = "lane", [int]$ResX = 540, [int]$ResY = 960,
  [int]$Quit = 70, [double]$Every = 8, [double]$Speed = 1.5, [string]$Zoom = "", [switch]$NoBuild, [string]$Extra = "",
  [string]$UnrealDir = "C:\Program Files\Epic Games\UE_5.8"
)
$proj = Split-Path -Parent $PSScriptRoot          # Unreal\BunkerSurvivors
$ue = $UnrealDir
if (-not $NoBuild) {
  $out = & "$ue\Engine\Build\BatchFiles\Build.bat" BunkerSurvivorsEditor Win64 Development "-Project=$proj\BunkerSurvivors.uproject" -WaitMutex -NoHotReload 2>&1
  $errs = $out | Select-String -Pattern "error|Result:" | ForEach-Object { $_.Line -replace [regex]::Escape("$proj\Source\BunkerSurvivors\"), '' } | Select-Object -Unique -First 30
  $errs
  if ($LASTEXITCODE -ne 0) { "BUILD FAILED"; exit 1 }
}
$shots = "$proj\Saved\Shots\$Name"
New-Item -ItemType Directory -Force $shots | Out-Null
Get-ChildItem $shots -Filter *.png -ErrorAction SilentlyContinue | ForEach-Object { [IO.File]::Delete($_.FullName) }
$log = "$proj\Saved\$Name.log"
$args = @("`"$proj\BunkerSurvivors.uproject`"", '-game', '-windowed', "-ResX=$ResX", "-ResY=$ResY", '-nosplash',
  "-BSAutoStart=$Mode", '-BSAutoPlay', "-BSSpeed=$Speed", "-BSShotDir=$shots", "-BSShotEvery=$Every", "-BSQuitAfter=$Quit", '-log', "-abslog=$log")
if ($Zoom -ne "") { $args += "-BSZoom=$Zoom" }
if ($Extra -ne "") { $args += $Extra.Split(' ') }
$sw = [Diagnostics.Stopwatch]::StartNew()
$p = Start-Process -FilePath "$ue\Engine\Binaries\Win64\UnrealEditor.exe" -ArgumentList $args -PassThru
$p.WaitForExit(1500000) | Out-Null
"exited=$($p.HasExited) code=$($p.ExitCode) elapsed=$([int]$sw.Elapsed.TotalSeconds)s"
Get-ChildItem $shots -Filter *.png | ForEach-Object { "$($_.Name) $($_.Length)" }
Select-String -Path $log -Pattern "BunkerSurvivors assets|BSShot|BSFinal|Missing model|appError|Assertion|Error: (?!Visual C)|Ensure condition|Warning: .*BunkerSurvivors" |
  Select-Object -First 40 | ForEach-Object { $_.Line.Substring(0, [Math]::Min(260, $_.Line.Length)) }
