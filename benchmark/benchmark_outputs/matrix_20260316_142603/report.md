# Benchmark Report - 2026-03-16 14:49:25

## Environment
- **Executable**: C:\TexasSolver\release\TexasSolverConsole.exe
- **Base Script**: C:\TexasSolver\benchmark\benchmark_texassolver_profile.txt
- **Thread Counts**: 16, 32
- **Pinning Profiles**: ccd0, ccd1, none

## Aggregate Results (Solve Time ms)

| Pinning | Threads | Runs | Min | Max | Average | Median |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| ccd0 | 16 | 3 | 113246 | 113917 | 113649 | 113784 | 
| ccd1 | 16 | 3 | 112281 | 112810 | 112490,33 | 112380 | 
| none | 16 | 3 | 116197 | 118766 | 117321,33 | 117001 | 
| none | 32 | 3 | 120647 | 123584 | 121719,67 | 120928 |

## Performance Efficiency (Median)

| Pinning | Threads | Iterations | Exploitability | BR (ms) | Alloc (ms) | River Hit Rate |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| ccd0 | 16 | 180 | 0,476144 | 597,42 | 0,00 | 100,00% | 
| ccd1 | 16 | 180 | 0,476144 | 605,82 | 0,00 | 100,00% | 
| none | 16 | 180 | 0,476144 | 653,00 | 0,00 | 100,00% | 
| none | 32 | 180 | 0,476144 | 734,97 | 0,00 | 100,00% |

## Detailed Resource Usage (Median)

| Pinning | Threads | Alloc MB | River Lookup (ms) | River Build (ms) | Lock Wait (ms) |
| :--- | :--- | :--- | :--- | :--- | :--- |
| ccd0 | 16 | 0,00 | 1 301,06 | 0,00 | 632,93 | 
| ccd1 | 16 | 0,00 | 1 298,24 | 0,00 | 604,66 | 
| none | 16 | 0,00 | 2 261,11 | 0,00 | 1 200,70 | 
| none | 32 | 0,00 | 13 463,00 | 0,00 | 9 237,12 |

