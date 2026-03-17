# macOS Quick Benchmark Baseline

This document records the baseline performance metrics for TexasSolver on macOS (Apple M-series) using the `QuickProfile` benchmark script (120 iterations).

## Environment
- **OS**: macOS 15.3.0 (Darwin 25.3.0)
- **CPU**: Apple M-series (arm64)
- **Compiler**: Apple clang version 17.0.0 (with OpenMP via libomp)
- **Qt**: 5.15.18
- **Benchmark Script**: `benchmark/benchmark_texassolver_profile.txt` (overridden for 120 iterations)

## Baseline Results (Solve Median ms)

| Threads | HF Mode | Solve Median | Iterations | Exploitability | Fetch (ms) | Regret (ms) | Lock Wait (ms) |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| 8 | 0 | 75662 | 121 | 0.973698 | 948.01 | 969.56 | 993.49 |
| 8 | 1 | 49826 | 121 | 0.973697 | 569.82 | 682.54 | 647.40 |
| 8 | 2 | 52721 | 121 | 0.925172 | 430.53 | 541.63 | 665.30 |
| 14 | 0 | 45052 | 121 | 0.973698 | 639.01 | 664.88 | 722.86 |
| 14 | 1 | 44662 | 121 | 0.973697 | 641.20 | 675.54 | 2446.65 |
| 14 | 2 | 46617 | 121 | 0.925172 | 517.97 | 680.41 | 2567.75 |

## Observations & Notes
- **Scaling**: Increasing threads from 8 to 14 provides significant speedup (~1.1-1.6x).
- **HF Contention**: HF1 and HF2 modes show high `Lock Wait` times on 14 threads, indicating a bottleneck in river cache management at higher thread counts on this architecture.
- **Accuracy**: Quick benchmark exploitability target is reached as expected (~0.92-0.97).
