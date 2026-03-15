# Benchmark Matrix Summary

Per-run results:

| Threads | Run | Iteration | Exploitability | Solve wall ms | Best response ms |
|---|---:|---:|---:|---:|---:|
| 16 | 1 | 250 | 0.297149 | 153374 | 487.2642 |
| 16 | 2 | 250 | 0.297149 | 181849 | 605.0745 |
| 16 | 3 | 250 | 0.297149 | 199200 | 602.3116 |
| 32 | 1 | 250 | 0.297149 | 174696 | 551.1890 |
| 32 | 2 | 250 | 0.297149 | 170569 | 551.9096 |
| 32 | 3 | 250 | 0.297149 | 175956 | 553.8505 |

Aggregate:

| Threads | Runs | Min solve wall ms | Median solve wall ms | Avg solve wall ms | Max solve wall ms | Median best response ms |
|---|---:|---:|---:|---:|---:|---:|
| 16 | 3 | 153374 | 181849 | 178141.00 | 199200 | 602.3116 |
| 32 | 3 | 170569 | 174696 | 173740.33 | 175956 | 551.9096 |

Notes:
- All six runs reached target accuracy at iteration 250, so the 300-iteration cap was not binding.
- On this spot, 32 threads are about 3.9% faster by median solve wall time and about 8.4% faster by median best response time.
- 16 threads had the single fastest run, but showed much higher variance.
