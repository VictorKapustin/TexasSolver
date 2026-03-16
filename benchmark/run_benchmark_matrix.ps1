param(
    [string]$ExePath = "C:\TexasSolver\release\TexasSolverConsole.exe",
    [string]$BaseScript = "C:\TexasSolver\benchmark\benchmark_texassolver_profile.txt",
    [string]$OutputRoot = "",
    $ThreadCounts = @(16, 32),
    [int]$RunsPerThread = 3,
    $PinningProfiles = @("none", "ccd0"),
    [string]$AffinityMaskPhysical = "0x55555555",
    [string]$AffinityMaskSmt = "0xFFFFFFFF",
    [string]$AffinityMaskCcd0 = "0x0000FFFF",
    [string]$AffinityMaskCcd1 = "0xFFFF0000",
    [switch]$UseOmpPinning,
    [string]$OmpProcBind = "close",
    [string]$OmpPlaces = "cores"
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

function Normalize-AffinityMask {
    param([string]$Mask)

    if ([string]::IsNullOrWhiteSpace($Mask)) {
        return ""
    }

    $normalized = $Mask.Trim()
    if ($normalized.StartsWith("0x", [System.StringComparison]::OrdinalIgnoreCase)) {
        $normalized = $normalized.Substring(2)
    }
    return $normalized
}

function Resolve-AffinityMask {
    param(
        [string]$PinningProfile,
        [int]$Threads
    )

    switch ($PinningProfile.ToLowerInvariant()) {
        "none" { return "" }
        "auto" {
            if ($Threads -le 16) {
                return $AffinityMaskPhysical
            }
            return $AffinityMaskSmt
        }
        "physical" { return $AffinityMaskPhysical }
        "smt" { return $AffinityMaskSmt }
        "ccd0" { return $AffinityMaskCcd0 }
        "ccd1" { return $AffinityMaskCcd1 }
        default {
            throw "Unknown pinning profile '$PinningProfile'. Supported values: none, auto, physical, smt, ccd0, ccd1."
        }
    }
}

function Invoke-SolverRun {
    param(
        [string]$ExePath,
        [string]$ScriptPath,
        [string]$AffinityMask,
        [bool]$ApplyOmpPinning,
        [string]$OmpProcBind,
        [string]$OmpPlaces
    )

    $startInfo = New-Object System.Diagnostics.ProcessStartInfo
    $startInfo.FileName = $ExePath
    $startInfo.Arguments = "--input_file `"$ScriptPath`""
    $startInfo.UseShellExecute = $false

    if ($ApplyOmpPinning) {
        if (-not [string]::IsNullOrWhiteSpace($OmpProcBind)) {
            $startInfo.EnvironmentVariables["OMP_PROC_BIND"] = $OmpProcBind
        }
        if (-not [string]::IsNullOrWhiteSpace($OmpPlaces)) {
            $startInfo.EnvironmentVariables["OMP_PLACES"] = $OmpPlaces
        }
    }

    $process = [System.Diagnostics.Process]::Start($startInfo)
    if ($null -eq $process) {
        throw "Failed to start process: $ExePath"
    }

    if (-not [string]::IsNullOrWhiteSpace($AffinityMask)) {
        $normalizedMask = Normalize-AffinityMask $AffinityMask
        if (-not [string]::IsNullOrWhiteSpace($normalizedMask)) {
            $affinityValue = [Convert]::ToInt64($normalizedMask, 16)
            try {
                $process.ProcessorAffinity = [IntPtr]$affinityValue
            } catch {
                Write-Warning "Unable to set affinity mask '$AffinityMask' for process $($process.Id): $($_.Exception.Message)"
            }
        }
    }

    $process.WaitForExit()
    if ($process.ExitCode -ne 0) {
        throw "Solver exited with code $($process.ExitCode) for script '$ScriptPath'."
    }
}

# Definitive normalization of parameters
Write-Host "--- DEBUG PARAMETERS ---"
Write-Host "Input ThreadCounts: $ThreadCounts ($($ThreadCounts.GetType().Name))"
Write-Host "Input PinningProfiles: $PinningProfiles ($($PinningProfiles.GetType().Name))"

# Definitive normalization: handles hardcoded defaults and command-line inputs
# Splits anything that might have been joined into a single string by PowerShell
$ThreadCounts = ([string[]](@($ThreadCounts)) -join ',' -split '[\s,]+' | Where-Object { $_ -match '^\d+$' } | ForEach-Object { [int]$_ } | Sort-Object -Unique)
$PinningProfiles = ([string[]](@($PinningProfiles)) -join ',' -split '[\s,]+' | Where-Object { $_ -ne "" } | Sort-Object -Unique)

Write-Host "--- Benchmark Configuration ---"
Write-Host "ThreadCounts: $($ThreadCounts -join ', ')"
Write-Host "PinningProfiles: $($PinningProfiles -join ', ')"
Write-Host "RunsPerThread: $RunsPerThread"
Write-Host "-------------------------------"
Write-Host "------------------------"

if (-not (Test-Path $ExePath)) {
    throw "Executable not found: $ExePath"
}

if (-not (Test-Path $BaseScript)) {
    throw "Base script not found: $BaseScript"
}

if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $stamp = Get-Date -Format "yyyyMMdd_HHmmss"
    $OutputRoot = Join-Path "C:\TexasSolver\benchmark\benchmark_outputs" "matrix_$stamp"
}

$scriptsDir = Join-Path $OutputRoot "scripts"
New-Item -ItemType Directory -Force -Path $scriptsDir | Out-Null

$baseContent = Get-Content $BaseScript -Raw
$summaryRows = New-Object System.Collections.Generic.List[object]

foreach ($pinningProfile in $PinningProfiles) {
    foreach ($threads in $ThreadCounts) {
        # Check for oversubscription on CCD pinning (16 LPs per CCD on 5950X)
        if (($pinningProfile.ToLower().StartsWith("ccd")) -and ($threads -gt 16)) {
            Write-Host "--- Skipping Config: $threads threads on $pinningProfile (Oversubscribed) ---" -ForegroundColor Yellow
            continue
        }

        $affinityMask = Resolve-AffinityMask -PinningProfile $pinningProfile -Threads $threads
        foreach ($run in 1..$RunsPerThread) {
            $tag = ("p{0}_t{1}_run{2}" -f $pinningProfile.ToLowerInvariant(), $threads, $run)
            $scriptPath = Join-Path $scriptsDir ("{0}.txt" -f $tag)
            $logPath = Join-Path $OutputRoot ("{0}.jsonl" -f $tag)
            $resultPath = Join-Path $OutputRoot ("{0}_result.json" -f $tag)

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

            $affinityLabel = if ([string]::IsNullOrWhiteSpace($affinityMask)) { "OS-default" } else { $affinityMask }
            Write-Host ("[{0}] Starting run {1}/{2} | threads={3} | pinning={4} | affinity={5}" -f (Get-Date -Format "HH:mm:ss"), $run, $RunsPerThread, $threads, $pinningProfile, $affinityLabel)
            $sw = [System.Diagnostics.Stopwatch]::StartNew()
            Invoke-SolverRun -ExePath $ExePath -ScriptPath $scriptPath -AffinityMask $affinityMask -ApplyOmpPinning:$UseOmpPinning -OmpProcBind $OmpProcBind -OmpPlaces $OmpPlaces
            $sw.Stop()

            if (-not (Test-Path $logPath)) {
                throw "Benchmark log was not created: $logPath"
            }

            $events = Get-Content $logPath | Where-Object { $_.Trim() -ne "" } | ForEach-Object { $_ | ConvertFrom-Json }
            $lastSummary = $events | Where-Object { $_.type -eq "iteration_summary" } | Select-Object -Last 1
            $solveDone = $events | Where-Object { $_.type -eq "benchmark_solve_done" } | Select-Object -Last 1
            $finalStatics = $events | Where-Object { $_.type -eq "final_statics" } | Select-Object -Last 1

            if ($null -eq $lastSummary) {
                $lastProfile = $events | Where-Object { $_.type -eq "iteration_profile" } | Select-Object -Last 1
                if ($null -ne $lastProfile) {
                    $lastSummary = [pscustomobject]@{
                        iteration = [int]$lastProfile.iteration
                        exploitibility = [double]::NaN
                        best_response_ms = [double]::NaN
                    }
                }
            }

            if ($null -eq $lastSummary -or $null -eq $solveDone) {
                throw "Failed to parse benchmark summary from $logPath"
            }

            $row = [pscustomobject]@{
                pinning_profile      = $pinningProfile
                affinity_mask        = $affinityLabel
                threads              = $threads
                run                  = $run
                iteration            = [int]$lastSummary.iteration
                exploitability       = [double]$lastSummary.exploitibility
                solve_wall_ms        = [int]$solveDone.solve_wall_ms
                total_elapsed_ms     = [int]$sw.ElapsedMilliseconds
                best_response_ms     = [double]$lastSummary.best_response_ms
                # New Metrics
                allocator_calls      = [int]$lastSummary.allocator_profile.calls
                allocator_mb         = [math]::Round([double]$lastSummary.allocator_profile.megabytes, 2)
                allocator_ms         = [math]::Round([double]$lastSummary.solver_profile.allocator, 2)
                river_hit_rate       = [math]::Round([double]$lastSummary.river_cache.hit_rate, 4)
                river_lookup_ms      = [math]::Round([double]$lastSummary.river_cache.lookup_ms, 2)
                river_build_ms       = [math]::Round([double]$lastSummary.river_cache.build_ms, 2)
                river_lock_wait_ms   = [math]::Round([double]$lastSummary.river_cache.lock_wait_ms, 2)
                # Node counts
                final_action_nodes   = [int]$finalStatics.node_counts.action
                final_chance_nodes   = [int]$finalStatics.node_counts.chance
                final_showdown_nodes = [int]$finalStatics.node_counts.showdown
                final_terminal_nodes = [int]$finalStatics.node_counts.terminal
                log_file             = $logPath
                result_file          = $resultPath
            }

            $summaryRows.Add($row) | Out-Null
            Write-Host ("[{0}] Finished run {1}/{2} | threads={3} | pinning={4} | solve={5} ms | iter={6} | exploitability={7:N6} | alloc={8} ms" -f (Get-Date -Format "HH:mm:ss"), $run, $RunsPerThread, $threads, $pinningProfile, $row.solve_wall_ms, $row.iteration, $row.exploitability, $row.allocator_ms)
        }
    }
}

function Write-MarkdownReport {
    param(
        [object[]]$Aggregate,
        [object[]]$Summary,
        [string]$Path
    )

    $report = @"
# Benchmark Report - $(Get-Date -Format "yyyy-MM-dd HH:mm:ss")

## Environment
- **Executable**: $ExePath
- **Base Script**: $BaseScript
- **Thread Counts**: $($ThreadCounts -join ", ")
- **Pinning Profiles**: $($PinningProfiles -join ", ")

## Aggregate Results (Solve Time ms)

| Pinning | Threads | Runs | Min | Max | Average | Median |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
$(foreach ($a in $Aggregate) {
    "| {0} | {1} | {2} | {3} | {4} | {5} | {6} |" -f $a.pinning_profile, $a.threads, $a.runs, $a.min_solve_wall_ms, $a.max_solve_wall_ms, $a.avg_solve_wall_ms, $a.median_solve_wall_ms
})

## Performance Efficiency (Median)

| Pinning | Threads | Iterations | Exploitability | BR (ms) | Alloc (ms) | River Hit Rate |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
$(foreach ($a in $Aggregate) {
    "| {0} | {1} | {2} | {3:N6} | {4:N2} | {5:N2} | {6:P2} |" -f $a.pinning_profile, $a.threads, $a.median_iteration, $a.median_exploitability, $a.median_best_response_ms, $a.median_allocator_ms, $a.median_river_hit_rate
})

## Detailed Resource Usage (Median)

| Pinning | Threads | Alloc MB | River Lookup (ms) | River Build (ms) | Lock Wait (ms) |
| :--- | :--- | :--- | :--- | :--- | :--- |
$(foreach ($a in $Aggregate) {
    "| {0} | {1} | {2:N2} | {3:N2} | {4:N2} | {5:N2} |" -f $a.pinning_profile, $a.threads, $a.median_allocator_mb, $a.median_river_lookup_ms, $a.median_river_build_ms, $a.median_river_lock_wait_ms
})

"@

    Set-Content -Path $Path -Value $report -Encoding UTF8
}

$summaryCsv = Join-Path $OutputRoot "summary.csv"
$summaryJson = Join-Path $OutputRoot "summary.json"

$summaryRows | Export-Csv -Path $summaryCsv -NoTypeInformation -Encoding UTF8
$summaryRows | ConvertTo-Json -Depth 5 | Set-Content -Path $summaryJson -Encoding UTF8

$aggregate = New-Object System.Collections.Generic.List[object]
foreach ($pinningProfile in $PinningProfiles) {
    foreach ($threads in $ThreadCounts) {
        $group = $summaryRows | Where-Object { $_.pinning_profile -eq $pinningProfile -and $_.threads -eq $threads }
        if (-not $group -or $group.Count -eq 0) {
            continue
        }
        $solveTimes = $group.solve_wall_ms | Measure-Object -Min -Max -Average
        $aggregate.Add([pscustomobject]@{
            pinning_profile = $pinningProfile
            threads = $threads
            runs = $group.Count
            # Statistics
            min_solve_wall_ms = $solveTimes.Minimum
            max_solve_wall_ms = $solveTimes.Maximum
            avg_solve_wall_ms = [math]::Round($solveTimes.Average, 2)
            median_solve_wall_ms = [int](Get-Median ($group.solve_wall_ms))
            
            median_iteration = [int](Get-Median ($group.iteration))
            median_exploitability = [math]::Round((Get-Median ($group.exploitability)), 6)
            median_best_response_ms = [math]::Round((Get-Median ($group.best_response_ms)), 4)
            # New Metrics
            median_allocator_ms = [math]::Round((Get-Median ($group.allocator_ms)), 2)
            median_allocator_mb = [math]::Round((Get-Median ($group.allocator_mb)), 2)
            median_river_hit_rate = [math]::Round((Get-Median ($group.river_hit_rate)), 4)
            median_river_lookup_ms = [math]::Round((Get-Median ($group.river_lookup_ms)), 2)
            median_river_build_ms = [math]::Round((Get-Median ($group.river_build_ms)), 2)
            median_river_lock_wait_ms = [math]::Round((Get-Median ($group.river_lock_wait_ms)), 2)
            # Node Counts
            median_action_nodes = [int](Get-Median ($group.final_action_nodes))
            median_chance_nodes = [int](Get-Median ($group.final_chance_nodes))
            median_showdown_nodes = [int](Get-Median ($group.final_showdown_nodes))
            median_terminal_nodes = [int](Get-Median ($group.final_terminal_nodes))
        }) | Out-Null
    }
}

$aggregateCsv = Join-Path $OutputRoot "aggregate.csv"
$aggregateJson = Join-Path $OutputRoot "aggregate.json"
$reportMd = Join-Path $OutputRoot "report.md"

$aggregate | Export-Csv -Path $aggregateCsv -NoTypeInformation -Encoding UTF8
$aggregate | ConvertTo-Json -Depth 5 | Set-Content -Path $aggregateJson -Encoding UTF8
Write-MarkdownReport -Aggregate $aggregate -Summary $summaryRows -Path $reportMd

Write-Host ""
Write-Host "Per-run summary:"
$summaryRows | Sort-Object pinning_profile, threads, run | Format-Table pinning_profile, threads, run, iteration, exploitability, solve_wall_ms, best_response_ms, allocator_ms -AutoSize

Write-Host ""
Write-Host "Aggregate summary:"
$aggregate | Sort-Object pinning_profile, threads | Format-Table pinning_profile, threads, runs, median_solve_wall_ms, avg_solve_wall_ms, median_iteration, median_exploitability, median_allocator_ms -AutoSize

Write-Host ""
Write-Host ("Saved files under: {0}" -f $OutputRoot)
