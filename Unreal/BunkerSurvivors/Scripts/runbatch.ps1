# Runs a batch of headless test games in parallel and collects telemetry into one JSON-lines file.
param([string]$Out = "C:\tmp\bunker-survivors\Unreal\BunkerSurvivors\Saved\Sim\batch.jsonl", [int]$Parallel = 3, [int[]]$Seeds = @(11, 12, 13, 14),
  [string[]]$Modes = @("lane", "arena"), [string[]]$Bots = @("balanced", "marines", "mech", "random", "passive"))
$script = "C:\tmp\bunker-survivors\Unreal\BunkerSurvivors\Scripts\simgame.ps1"
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
  $f = "C:\tmp\bunker-survivors\Unreal\BunkerSurvivors\Saved\Sim\$m-$b-$s.jsonl"
  if (Test-Path $f) { Get-Content $f | ForEach-Object { $_.Replace('{"kind"', "{`"seed`":$s,`"kind`"") } | Add-Content -Path $Out -Encoding utf8 }
} } }
"BATCH DONE: $total games -> $Out"
