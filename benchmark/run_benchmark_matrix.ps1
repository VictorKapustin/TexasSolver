param(
    [string]$ExePath = "C:\TexasSolver\release\TexasSolverConsole.exe",
    [string]$BaseScript = "C:\TexasSolver\benchmark\benchmark_texassolver_profile.txt",
    [string]$OutputRoot = "",
    [int[]]$ThreadCounts = @(16, 32),
    [int]$RunsPerThread = 3
)

$ErrorActionPreference = "Stop"

function Get-Median {
    param([double[]]$Values)

    if (-not $Values -or $Values.Count -eq 0) {
        return $null
    }

    $sorted = $Values | Sort-Object
    $count = $sorted.Count
    if ($count % 2 -eq 1) {
        return [double]$sorted[[int][math]::Floor($count / 2)]
    }

    $mid = [int][math]::Floor($count / 2)
    $left = [double]$sorted[$mid - 1]
    $right = [double]$sorted[$mid]
    return ($left + $right) / 2.0
}

if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $stamp = Get-Date -Format "yyyyMMdd_HHmmss"
    $OutputRoot = Join-Path "C:\TexasSolver\benchmark\benchmark_outputs" "matrix_$stamp"
}

$scriptsDir = Join-Path $OutputRoot "scripts"
New-Item -ItemType Directory -Force -Path $scriptsDir | Out-Null

$baseContent = Get-Content $BaseScript -Raw
$summaryRows = New-Object System.Collections.Generic.List[object]

foreach ($threads in $ThreadCounts) {
    foreach ($run in 1..$RunsPerThread) {
        $scriptPath = Join-Path $scriptsDir ("t{0}_run{1}.txt" -f $threads, $run)
        $logPath = Join-Path $OutputRoot ("t{0}_run{1}.jsonl" -f $threads, $run)
        $resultPath = Join-Path $OutputRoot ("t{0}_run{1}_result.json" -f $threads, $run)

        $scriptContent = $baseContent -replace '(?m)^set_thread_num\s+\d+\s*$', ("set_thread_num {0}" -f $threads)
        $scriptContent = $scriptContent -replace '(?m)^set_log_file\s+.+$', (("set_log_file {0}" -f $logPath) -replace '\\', '/')
        $scriptContent = $scriptContent -replace '(?m)^dump_result\s+.+$', (("dump_result {0}" -f $resultPath) -replace '\\', '/')

        Set-Content -Path $scriptPath -Value $scriptContent -Encoding ascii

        if (Test-Path $logPath) {
            Remove-Item $logPath -Force
        }
        if (Test-Path $resultPath) {
            Remove-Item $resultPath -Force
        }

        Write-Host ("[{0}] Starting run {1}/{2} with {3} threads" -f (Get-Date -Format "HH:mm:ss"), $run, $RunsPerThread, $threads)
        $sw = [System.Diagnostics.Stopwatch]::StartNew()
        & $ExePath --input_file $scriptPath
        $sw.Stop()

        if (-not (Test-Path $logPath)) {
            throw "Benchmark log was not created: $logPath"
        }

        $events = Get-Content $logPath | Where-Object { $_.Trim() -ne "" } | ForEach-Object { $_ | ConvertFrom-Json }
        $lastSummary = $events | Where-Object { $_.type -eq "iteration_summary" } | Select-Object -Last 1
        $solveDone = $events | Where-Object { $_.type -eq "benchmark_solve_done" } | Select-Object -Last 1
        $finalStatics = $events | Where-Object { $_.type -eq "final_statics" } | Select-Object -Last 1

        if ($null -eq $lastSummary -or $null -eq $solveDone) {
            throw "Failed to parse benchmark summary from $logPath"
        }

        $row = [pscustomobject]@{
            threads            = $threads
            run                = $run
            iteration          = [int]$lastSummary.iteration
            exploitability     = [double]$lastSummary.exploitibility
            solve_wall_ms      = [int]$solveDone.solve_wall_ms
            total_elapsed_ms   = [int]$sw.ElapsedMilliseconds
            best_response_ms   = [double]$lastSummary.best_response_ms
            final_action_nodes = [int]$finalStatics.node_counts.action
            final_chance_nodes = [int]$finalStatics.node_counts.chance
            final_showdown_nodes = [int]$finalStatics.node_counts.showdown
            final_terminal_nodes = [int]$finalStatics.node_counts.terminal
            log_file           = $logPath
            result_file        = $resultPath
        }

        $summaryRows.Add($row) | Out-Null
        Write-Host ("[{0}] Finished run {1}/{2} with {3} threads in {4} ms, iteration {5}, exploitability {6:N6}" -f (Get-Date -Format "HH:mm:ss"), $run, $RunsPerThread, $threads, $row.solve_wall_ms, $row.iteration, $row.exploitability)
    }
}

$summaryCsv = Join-Path $OutputRoot "summary.csv"
$summaryJson = Join-Path $OutputRoot "summary.json"

$summaryRows | Export-Csv -Path $summaryCsv -NoTypeInformation -Encoding UTF8
$summaryRows | ConvertTo-Json -Depth 5 | Set-Content -Path $summaryJson -Encoding UTF8

$aggregate = foreach ($threads in $ThreadCounts) {
    $group = $summaryRows | Where-Object { $_.threads -eq $threads }
    [pscustomobject]@{
        threads = $threads
        runs = $group.Count
        median_solve_wall_ms = [int](Get-Median ($group.solve_wall_ms))
        avg_solve_wall_ms = [math]::Round((($group.solve_wall_ms | Measure-Object -Average).Average), 2)
        median_iteration = [int](Get-Median ($group.iteration))
        median_exploitability = [math]::Round((Get-Median ($group.exploitability)), 6)
        median_best_response_ms = [math]::Round((Get-Median ($group.best_response_ms)), 4)
    }
}

$aggregateCsv = Join-Path $OutputRoot "aggregate.csv"
$aggregateJson = Join-Path $OutputRoot "aggregate.json"

$aggregate | Export-Csv -Path $aggregateCsv -NoTypeInformation -Encoding UTF8
$aggregate | ConvertTo-Json -Depth 5 | Set-Content -Path $aggregateJson -Encoding UTF8

Write-Host ""
Write-Host "Per-run summary:"
$summaryRows | Sort-Object threads, run | Format-Table threads, run, iteration, exploitability, solve_wall_ms, best_response_ms -AutoSize

Write-Host ""
Write-Host "Aggregate summary:"
$aggregate | Sort-Object threads | Format-Table threads, runs, median_solve_wall_ms, avg_solve_wall_ms, median_iteration, median_exploitability, median_best_response_ms -AutoSize

Write-Host ""
Write-Host ("Saved files under: {0}" -f $OutputRoot)
