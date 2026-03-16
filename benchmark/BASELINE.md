# Benchmark Baseline

This document records the current benchmark baseline for the profiled CLI workflow on the user's Ryzen 9 5950X machine.
It was captured before the extended P0 diagnostics fields (`allocator_profile`, `river_cache`) were added to the profile log.

## Environment

- Date: `2026-03-15`
- CPU: `AMD Ryzen 9 5950X` (`16C/32T`, `2 CCD`)
- OS / binary: Windows, `release/TexasSolverConsole.exe`
- Source state: locally rebuilt working tree on top of git base `060f6bc`
- Benchmark script: `benchmark/benchmark_texassolver_profile.txt`
- Runs: `3x` at `16` threads and `3x` at `32` threads
- Stop condition: `set_accuracy 0.3`, `set_max_iteration 300`, `set_print_interval 10`
- Result: all six runs reached target accuracy at iteration `250`

## Spot Definition

The fixed benchmark spot is defined by `benchmark/benchmark_texassolver_profile.txt`.

Key parameters:

- Board: `Qs,Jh,2h`
- Pot: `6`
- Effective stack: `98`
- Isomorphism: enabled
- All-in threshold: `1.0`

Tree shape summary:

- Flop:
  - OOP bet `100`, raise `50`, all-in
  - IP bet `100`, raise `50`, all-in
- Turn:
  - OOP bet `100`, donk `100`, raise `50`, all-in
  - IP bet `100`, raise `50`
- River:
  - OOP bet `100`, donk `100`, raise `50`, all-in
  - IP bet `100`, raise `50`, all-in

Use the benchmark script itself as the exact source of truth for the ranges and sizings.

## Methodology

- Overall solve metrics use:
  - `benchmark_solve_done.solve_wall_ms`
  - the last `iteration_summary` for final iteration and exploitability
  - `final_statics.collect_statics_ms`
- Stage medians use `iteration_profile` only:
  - `226` profile samples per run
  - these are the ordinary non-summary iterations
- `best_response_ms` is not present in `iteration_profile`, so it is taken from `iteration_summary`:
  - `25` samples per run
- Aggregation rule:
  - first compute a median per run
  - then compute the median of those three run medians for each thread count

Important:

- `solver_profile.*` values are not exclusive wall-clock stage times.
- They come from `collectBenchmarkStatsJson()` summing thread-local timers across all worker threads.
- They are useful as accumulated thread-time / work indicators.
- They should not be interpreted as direct wall time for a stage.

## Overall Results

Per-run results:

| Threads | Run | Solve wall ms | Final iter | Final exploitability | Final best response ms | Collect statics ms |
|---|---:|---:|---:|---:|---:|---:|
| 16 | 1 | 153374 | 250 | 0.297149 | 487.2642 | 576.7031 |
| 16 | 2 | 181849 | 250 | 0.297149 | 605.0745 | 769.3350 |
| 16 | 3 | 199200 | 250 | 0.297149 | 602.3116 | 771.4038 |
| 32 | 1 | 174696 | 250 | 0.297149 | 551.1890 | 630.7933 |
| 32 | 2 | 170569 | 250 | 0.297149 | 551.9096 | 620.5257 |
| 32 | 3 | 175956 | 250 | 0.297149 | 553.8505 | 632.8022 |

Aggregate outcome:

| Metric | 16 threads | 32 threads | Delta 32 vs 16 |
|---|---:|---:|---:|
| Median solve wall ms | 181849 | 174696 | -3.93% |
| Average solve wall ms | 178141.00 | 173740.33 | -2.47% |
| Median final best response ms | 602.3116 | 551.9096 | -8.37% |
| Median initial best response ms | 742.7693 | 723.7335 | -2.56% |
| Median collect statics ms | 769.3350 | 630.7933 | -18.01% |

Notes:

- In the tables above, a negative delta is better because smaller wall time is better.
- `32` threads gave the better median solve time.
- `16` threads produced the single fastest run, but with much higher variance.

## Stage-Level Baseline

Wall-clock style metrics:

| Metric | 16 threads | 32 threads | Delta 32 vs 16 |
|---|---:|---:|---:|
| Median `iteration_profile.iteration_total_ms` | 706.3753 | 627.4011 | -11.18% |
| Median `player_cfr_ms[0]` | 371.9345 | 332.1580 | -10.69% |
| Median `player_cfr_ms[1]` | 332.9764 | 291.5159 | -12.45% |
| Median `iteration_summary.best_response_ms` | 602.3116 | 551.8631 | -8.38% |

Accumulated thread-time metrics from `solver_profile`:

- In the table below, a positive delta means `32` threads consumed more summed thread-time in that stage.

| Stage | 16 threads | 32 threads | Delta 32 vs 16 |
|---|---:|---:|---:|
| `strategy_fetch` | 851.0503 | 1425.7904 | +67.53% |
| `regret_update` | 933.5555 | 1251.6944 | +34.08% |
| `chance_setup` | 177.6504 | 214.8124 | +20.92% |
| `chance_merge` | 923.5096 | 1215.0480 | +31.57% |
| `showdown_eval` | 1848.5092 | 4267.5772 | +130.87% |
| `terminal_eval` | 1422.1651 | 1587.2896 | +11.61% |

Interpretation:

- `32` threads improve ordinary iteration wall time by about `11%`, but improve full solve wall time by only about `4%`.
- `best_response` improves by about `8%`.
- Accumulated thread-time rises sharply on `32` threads in `showdown_eval`, `strategy_fetch`, `regret_update`, and `chance_merge`.
- The strongest scaling warning signs in the current baseline are:
  - `showdown_eval`
  - `strategy_fetch`
  - `regret_update`
  - `chance_merge`

This pattern is consistent with limited scaling caused by memory traffic, cache pressure, and overhead in the current data layout / traversal strategy.

## Raw Data

Primary benchmark outputs:

- `benchmark/benchmark_outputs/matrix_20260315_16/`
- `benchmark/benchmark_outputs/matrix_20260315_32/`
- `benchmark/benchmark_outputs/matrix_20260315_combined_summary.md`

Helper script used to run the matrix:

- `benchmark/run_benchmark_matrix.ps1`

For fresh P0 baselines, rerun the matrix with pinning profiles (`none`, `ccd0`, `ccd1`) and compare by `(threads, pinning_profile)`.
