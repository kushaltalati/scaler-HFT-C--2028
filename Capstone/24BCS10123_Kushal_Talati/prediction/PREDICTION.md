# Predicted perf stat numbers for a real Linux box

NOT a measurement. NOT for the PR. This is what we expect `perf stat -r 5 -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults ./bench --impl X --runs 1 --notime` to print on a physical Linux machine, so the real run can be checked against it.

Whole process (workload generation + warm-up + 1 run), same as perf stat sees it.

## Measured inputs (same binary, same workload)

| | A | B | C | source |
|---|---:|---:|---:|---|
| instructions (mac hw, apple clang, x3 avg) | 1.61 G | 1.82 G | 0.63 G | /usr/bin/time -l |
| cycles (mac hw) | 1.42 G | 1.34 G | 0.247 G | /usr/bin/time -l |
| IPC (mac hw) | 1.13 | 1.35 | 2.55 | derived |
| instructions (aarch64 linux build, cachegrind I refs) | 1.38 G | 1.47 G | 0.53 G | cachegrind |
| branches (cachegrind) | 200.4 M | 182.5 M | 49.9 M | cachegrind |
| branch mispredicts (cachegrind) | 18.1 M (9.0%) | 5.3 M (2.9%) | 2.6 M (5.2%) | cachegrind |
| L1 D read misses (cachegrind D1 rd) | 29.4 M | 31.7 M | 3.5 M | cachegrind |
| L1 D write misses (cachegrind D1 wr) | 2.1 M | 13.7 M | 0.9 M | cachegrind |
| LL data misses (cachegrind LLd) | 17.7 M | 15.2 M | 2.6 M | cachegrind |
| page faults (docker linux perf stat) | 8,156 | 7,925 | 2,227 | perf stat in VM |

## Predicted ranges on a real x86-64 Linux machine

| counter | A | B | C | why the range |
|---|---:|---:|---:|---|
| task-clock (ms) | 300 - 650 | 200 - 450 | 50 - 110 | VM numbers were 767 / 389 / 78 and noisy, a desktop core should be faster |
| instructions | 1.2 - 1.7 G | 1.3 - 1.9 G | 0.45 - 0.65 G | x86 usually needs a bit fewer instructions than arm64 for the same code |
| IPC | 0.9 - 1.4 | 1.2 - 1.8 | 2.0 - 3.2 | M3 is very wide, x86 cores a bit lower, but ordering A < B < C should hold |
| branches | 170 - 220 M | 150 - 200 M | 40 - 60 M | counts are mostly ISA independent |
| branch-misses % | 6 - 10 % | 2 - 4 % | 3.5 - 6 % | real predictors beat cachegrind's model so absolutes may land lower, ordering A worst, B best should hold |
| L1-dcache-load-misses | 20 - 40 M | 22 - 45 M | 2.5 - 6 M | perf counts loads only, so B's 13.7 M write misses from the depth heap copy do not show, A and B should be close |
| cache-misses (LLC) | 5 - 20 M | 4 - 18 M | 0.5 - 4 M | depends a lot on LLC size, a big L3 absorbs a lot of A/B's node traffic. C lowest for sure |
| dTLB-load-misses | A and B well above C | | | no input for this one, pure expectation: A/B chase pointers across ~16 MB of nodes, C mostly hits its 80 KB levels |
| page-faults | 8 k - 25 k | 8 k - 25 k | 2 k - 9 k | the VM had transparent hugepages, a stock distro may fault 4 KB pages so counts can be ~3x higher, ratio A/C about 3.5 - 4 should hold |

## What to check first when the real numbers arrive

1. ordering: A slowest, C fastest on task-clock, IPC and every miss counter. if that flips something is wrong with the run (wrong flags, debug build, machine busy)
2. branch-miss %: A should be roughly 2 - 3x B's rate. C in between B and A in rate but far lowest in absolute count
3. L1 load misses: A and B within ~30% of each other, C 6 - 10x lower
4. page faults: C about a quarter of A
5. if IPC for all three is < 1 and nearly equal, the box is probably a VM after all (check `perf stat -e cycles true` prints a number)
