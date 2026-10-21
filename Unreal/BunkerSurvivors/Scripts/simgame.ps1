# Runs one headless test game (no rendering, no sound) and appends telemetry to a JSON-lines file.
# Usage: simgame.ps1 -Mode lane -Bot balanced -Seed 1 -Out results.jsonl [-Speed 20] [-MaxTime 900]
param([string]$Mode = "lane", [string]$Bot = "balanced", [int]$Seed = 1, [string]$Out, [double]$Speed = 20, [int]$MaxTime = 900)
$proj = "C:\tmp\bunker-survivors\Unreal\BunkerSurvivors"
$ue = "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
$tmp = "$proj\Saved\Sim\$Mode-$Bot-$Seed.jsonl"
New-Item -ItemType Directory -Force (Split-Path $tmp) | Out-Null
if (Test-Path $tmp) { [IO.File]::Delete($tmp) }
$args = @("`"$proj\BunkerSurvivors.uproject`"", "-game", "-nullrhi", "-nosound", "-unattended", "-nosplash", "-BSNoHud",
  "-BSAutoStart=$Mode", "-BSAutoPlay", "-BSBot=$Bot", "-BSSeed=$Seed", "-BSSpeed=$Speed", "-BSExitOnOver", "-BSMaxTime=$MaxTime",
  "-BSTelemetry=$tmp", "-abslog=$proj\Saved\Sim\$Mode-$Bot-$Seed.log")
$sw = [Diagnostics.Stopwatch]::StartNew()
$p = Start-Process -FilePath $ue -ArgumentList $args -PassThru -WindowStyle Hidden
if (-not $p.WaitForExit(1800000)) { $p.Kill(); "TIMEOUT $Mode $Bot $Seed" }
if ((Test-Path $tmp) -and $Out) { Get-Content $tmp | Add-Content -Path $Out -Encoding utf8 }
$final = if (Test-Path $tmp) { Get-Content $tmp | Select-String '"kind":"final"' | Select-Object -Last 1 } else { $null }
"$Mode $Bot seed=$Seed  $([int]$sw.Elapsed.TotalSeconds)s  $final"
