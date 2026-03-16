# Benchmark Report - 2026-03-15 23:37:37

## Environment
- **Executable**: C:\TexasSolver\release\TexasSolverConsole.exe
- **Base Script**: C:\TexasSolver\benchmark\benchmark_texassolver_profile.txt
- **Thread Counts**: 16, 32
- **Pinning Profiles**: ccd0, ccd1, none

## Aggregate Results (Solve Time ms)

| Pinning | Threads | Runs | Min | Max | Average | Median |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| ccd0 | 16 | 3 | 152062 | 157799 | 155342,67 | 156167 | 
| ccd1 | 16 | 3 | 161998 | 162449 | 162234,67 | 162257 | 
| none | 16 | 3 | 168611 | 168923 | 168745,67 | 168703 | 
| none | 32 | 3 | 161701 | 164451 | 163184,33 | 163401 |

## Performance Efficiency (Median)

| Pinning | Threads | Iterations | Exploitability | BR (ms) | Alloc (ms) | River Hit Rate |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| ccd0 | 16 | 180 | 0,482308 | 723,57 | 579,58 | 100,00% | 
| ccd1 | 16 | 180 | 0,482308 | 733,72 | 601,84 | 100,00% | 
| none | 16 | 180 | 0,482308 | 788,78 | 583,76 | 100,00% | 
| none | 32 | 180 | 0,482308 | 936,73 | 977,19 | 100,00% |

## Detailed Resource Usage (Median)

| Pinning | Threads | Alloc MB | River Lookup (ms) | River Build (ms) | Lock Wait (ms) |
| :--- | :--- | :--- | :--- | :--- | :--- |
| ccd0 | 16 | 4 547,38 | 2 515,46 | 0,00 | 1 456,55 | 
| ccd1 | 16 | 4 546,69 | 2 559,10 | 0,00 | 1 480,23 | 
| none | 16 | 4 497,72 | 3 528,84 | 0,00 | 2 137,33 | 
| none | 32 | 4 318,76 | 17 694,58 | 0,00 | 11 869,55 |

