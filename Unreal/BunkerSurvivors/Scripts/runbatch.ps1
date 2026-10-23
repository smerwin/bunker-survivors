# Runs a batch of headless test games in parallel and collects telemetry into one JSON-lines file.
# Usage: powershell -File Scripts\runbatch.ps1 [-Out <file.jsonl>] [-Parallel 3] [-Seeds 11,12] [-Modes lane] [-Bots balanced,mech]
# Summarise the result with: python Scripts\analyze_batch.py <file.jsonl> report.html
param([string]$Out = "", [int]$Parallel = 3, [int[]]$Seeds = @(11, 12, 13, 14),
  [string[]]$Modes = @("lane", "arena"), [string[]]$Bots = @("balanced", "marines", "mech", "random", "passive"))
$proj = Split-Path -Parent $PSScriptRoot          # Unreal\BunkerSurvivors
$sim = Join-Path $proj "Saved\Sim"
if (-not $Out) { $Out = Join-Path $sim "batch.jsonl" }
$script = Join-Path $PSScriptRoot "simgame.ps1"
if (Test-Path $Out) { [IO.File]::Delete($Out) }
$queue = New-Object System.Collections.Queue
foreach ($m in $Modes) { foreach ($b in $Bots) { foreach ($s in $Seeds) { $queue.Enqueue(@($m, $b, $s)) } } }
$total = $queue.Count; $done = 0; $jobs = @()
while ($queue.Count -gt 0 -or $jobs.Count -gt 0) {
  while ($jobs.Count -lt $Parallel -and $queue.Count -gt 0) {
    $g = $queue.Dequeue()
    $jobs += Start-Job -ScriptBlock { param($sc, $m, $b, $s) & $sc -Mode $m -Bot $b -Seed $s } -ArgumentList $script, $g[0], $g[1], $g[2]
  }
  $finished = $jobs | Where-Object { $_.State -ne 'Running' }
  foreach ($j in $finished) { $done++; "[$done/$total] $(Receive-Job $j)"; Remove-Job $j }
  $jobs = @($jobs | Where-Object { $_.State -eq 'Running' })
  Start-Sleep -Seconds 2
}
# Merge per-game files (written by simgame.ps1 into Saved\Sim\<mode>-<bot>-<seed>.jsonl).
foreach ($m in $Modes) { foreach ($b in $Bots) { foreach ($s in $Seeds) {
  $f = Join-Path $sim "$m-$b-$s.jsonl"
  if (Test-Path $f) { Get-Content $f | ForEach-Object { $_.Replace('{"kind"', "{`"seed`":$s,`"kind`"") } | Add-Content -Path $Out -Encoding utf8 }
} } }
"BATCH DONE: $total games -> $Out"
