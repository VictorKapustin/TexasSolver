# Benchmark Guide

This document describes how to run a repeatable benchmark for TexasSolver, how to enable the extended profiling log, and how to compare runs after code changes.

The current measured baseline for the Ryzen 9 5950X benchmark runs is recorded in `benchmark/BASELINE.md`.

## Goal

The benchmark setup in this repository is intended to answer two questions:

1. How long does the full solve take for a fixed spot?
2. Which internal solver stages consume the time?

The profiling mode is useful when you are changing memory layout, parallelism, evaluator logic, or CFR update logic and want to see whether the change improved the real bottleneck.

## Benchmark Files

The benchmark folder now contains two main input files:

- `benchmark/benchmark_texassolver.txt`
  Plain benchmark input similar to the historical benchmark already referenced in the README.
- `benchmark/benchmark_texassolver_profile.txt`
  Benchmark input with profiling enabled and JSONL logging configured.

The profiling benchmark writes output to:

- `benchmark/benchmark_outputs/texassolver_profile_log.jsonl`
- `benchmark/benchmark_outputs/texassolver_profile_result.json`

## Benchmark Spot

The profiling benchmark uses a fixed flop spot:

- Board: `Qs,Jh,2h`
- Pot: `6`
- Effective stack: `98`
- Tree: the same sizing structure defined in `benchmark/benchmark_texassolver_profile.txt`

This spot is meant to stay stable between runs, so differences in the log can be attributed to code changes rather than to changed game-tree parameters.

## How the Benchmark Is Executed

The benchmark input is consumed by `CommandLineTool`, the same command parser used by:

- the console-oriented workflow
- the exported C FFI entry point in `src/api.cpp`

If you already have a console-enabled build, you can pass the benchmark input file to it directly.

If you are running the solver through the FFI layer, call:

```python
from ctypes import CDLL, c_char_p, c_int

api_library = CDLL("api.dll")  # use api.so or api.dylib on Linux/macOS
api_library.api.restype = c_int
api_library.api.argtypes = [c_char_p, c_char_p, c_char_p]

api_library.api(
    b"benchmark/benchmark_texassolver_profile.txt",
    b"./resources",
    b"holdem",
)
```

See `resources/ffi_api/README.MD` and `resources/ffi_api/python_ctypes.py` for the base FFI usage pattern.

## Profiling Commands

The profiling benchmark uses two new commands:

```text
set_profile_mode 1
set_log_file benchmark/benchmark_outputs/texassolver_profile_log.jsonl
```

`set_profile_mode 1` enables the extended benchmark log.

`set_log_file ...` selects the JSONL output path.

When profiling is enabled, the benchmark also records:

- `build_tree` wall-clock time
- `start_solve` wall-clock time
- `dump_result` wall-clock time

## Log Format

The profiling log is written as JSONL, one JSON object per line. This format is easy to diff, grep, or process with Python, jq, or PowerShell.

### Event Types

The log contains the following event types:

- `benchmark_setup`
  High-level benchmark parameters before the solve starts.
- `solver_session`
  Solver-level configuration such as thread count, iteration limit, and range sizes.
- `initial_best_response`
  The initial exploitability measurement before the training loop.
- `iteration_profile`
  Per-iteration timing data for iterations where exploitability was not printed.
- `iteration_summary`
  Per-iteration timing data plus exploitability for iterations where the solver printed a summary.
- `final_statics`
  Time spent in the final statistics collection pass after the solve loop.
- `benchmark_solve_done`
  End-to-end wall-clock time for `start_solve`.
- `benchmark_dump_done`
  Wall-clock time for `dump_result`.

### Important Fields

The most useful fields for optimization work are:

- `iteration_total_ms`
  Total wall-clock time for one training iteration.
- `player_cfr_ms`
  Two values, one CFR traversal for each player.
- `best_response_ms`
  Time spent measuring exploitability.
- `solver_profile.strategy_fetch`
  Time spent materializing strategy data from trainables.
- `solver_profile.regret_update`
  Time spent updating regrets and cumulative strategy state.
- `solver_profile.ev_update`
  Time spent updating EV tables.
- `solver_profile.chance_setup`
  Time spent preparing chance-node work such as valid-card filtering and reach-probability setup.
- `solver_profile.chance_merge`
  Time spent merging child results back into the chance-node result.
- `solver_profile.showdown_eval`
  Time spent in showdown evaluation logic.
- `solver_profile.terminal_eval`
  Time spent in terminal-node payoff logic.
- `node_counts`
  Number of action, chance, showdown, and terminal nodes touched during the profiled scope.

## What the Timings Mean

These timings are software instrumentation, not hardware performance counters.

That means:

- they are good for before/after comparisons on the same machine
- they are not a replacement for CPU hardware counters
- they include some instrumentation overhead

Because of that, always compare:

- profiled run vs profiled run
- non-profiled run vs non-profiled run

Do not compare the absolute wall time of a profiled run directly against an older non-profiled benchmark and treat the difference as pure solver speedup.

## Suggested Comparison Workflow

Use this workflow when testing an optimization:

1. Run the benchmark once on the current baseline and save the log.
2. Apply the optimization.
3. Run the exact same benchmark input again.
4. Compare:
   - `benchmark_solve_done.solve_wall_ms`
   - median `iteration_total_ms`
   - median `player_cfr_ms`
   - `best_response_ms`
   - the relevant `solver_profile.*` field for the subsystem you changed

Examples:

- If you optimize memory layout in trainables, watch `strategy_fetch`, `regret_update`, and `iteration_total_ms`.
- If you optimize chance-node traversal, watch `chance_setup`, `chance_merge`, and `player_cfr_ms`.
- If you optimize evaluator logic, watch `showdown_eval`, `terminal_eval`, and `best_response_ms`.

## Reproducibility Recommendations

For useful comparisons, keep the environment stable:

1. Use the same benchmark input file every time.
2. Use the same binary and compiler settings except for the change under test.
3. Run with the same thread count.
4. Avoid background CPU-heavy tasks.
5. On Ryzen 5950X, benchmark both `16` and `32` threads separately. The solver may be memory-bound, and `16` physical-core threads can be faster than `32` SMT threads.
6. Run at least 3 times and compare the median rather than a single run.

## Thread Count Notes

The profiling benchmark file currently sets:

```text
set_thread_num 16
```

This is intentional for CPUs like Ryzen 5950X where SMT does not always help a memory-heavy workload.

To compare SMT behavior, duplicate the benchmark file and change only:

```text
set_thread_num 32
```

Everything else should remain identical.

## Recommended Benchmark Variants

Keep at least two benchmark scenarios:

- `reference benchmark`
  Fixed spot, fixed thread count, profiling enabled. Use this for regression checks.
- `thread-scaling benchmark`
  Same spot, two files or two runs: one with `16` threads and one with `32` threads.

This gives you both an optimization signal and a scaling signal.

## Current Limitations

The current profiling log does not yet expose:

- River range cache hit/miss counts
- allocator statistics
- lock contention counters
- hardware cache-miss counters

If you need deeper analysis later, the next useful additions are:

- cache hit/miss counters in `RiverRangeManager`
- counters for trainable creation and lazy allocation
- optional hardware-counter collection through an external profiler
