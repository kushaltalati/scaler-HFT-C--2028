Linux (ubuntu docker container), median of 5 runs, 1,000,000 ops, seed 42. perf rows from `perf stat -r 5`, n/a = `<not supported>` in the VM.

| Metric | Implementation A | Implementation B | Implementation C |
|---|---:|---:|---:|
| Total time (ms) | 288.6 | 196.6 | 83.9 |
| ns / add | 204.5 | 109.0 | 40.6 |
| ns / cancel | 562.7 | 370.4 | 113.5 |
| ns / BBO | 42.7 | 43.8 | 46.1 |
| IPC | n/a | n/a | n/a |
| Branch misses (%) | n/a | n/a | n/a |
| L1-dcache-load-misses | n/a | n/a | n/a |
| Cache misses | n/a | n/a | n/a |
| Page faults | 8,156 | 7,925 | 2,227 |

Same benchmark natively on the mac (apple clang), median of 5. no perf on macOS.

| Metric | Implementation A | Implementation B | Implementation C |
|---|---:|---:|---:|
| Total time (ms) | 232.8 | 152.0 | 54.4 |
| ns / add | 159.5 | 75.4 | 24.2 |
| ns / cancel | 461.8 | 264.5 | 53.9 |
| ns / BBO | 22.9 | 37.0 | 20.6 |
