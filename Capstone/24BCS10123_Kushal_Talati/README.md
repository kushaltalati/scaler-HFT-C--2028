# Low-Latency Order Book (HFT C++ capstone)

- Kushal Talati, 24BCS10123, kushal.24bcs10123@sst.scaler.com
- Arjun Aggarwal, 24BCS10109, arjun.24bcs10109@sst.scaler.com

This one PR is the submission for both of us. It lives under Kushal's folder because the PR comes from his fork, the commit history shows who did what.

## what this is

Three order books with the same interface, one benchmark that drives all three with the same 1,000,000 op stream, and a report on which one is fastest and why. No matching, orders rest until cancelled.

- **A** `OrderBookMap` - `std::map` per side + `unordered_map` of orders. the obvious one.
- **B** `OrderBookHeap` - `priority_queue` of candidate best prices with lazy deletion + hash maps.
- **C** `OrderBookArray` - flat arrays indexed by price tick, occupancy bitmap for the best, slab of orders indexed by id.

Full writeup with numbers is in [REPORT.md](REPORT.md).

## layout

```
order_book_api.hpp     Side, Level, BBO, tick helpers, constants
order_book_map.hpp     impl A
order_book_heap.hpp    impl B
order_book_array.hpp   impl C
workload.hpp/.cpp      deterministic op generator (mt19937, seed 42)
bench.cpp              the one binary: --impl, --verify, --runs, --ops, --seed, --json, --notime
tests/edge_cases.cpp   hand written edge cases, runs on all three
scripts/run_perf.sh    linux: perf stat + perf record for A/B/C, then the timed runs
scripts/run_macos.sh   mac: build, verify, timed runs (no perf on macOS)
scripts/make_table.py  builds results/table.md from the json + perf stat files
perf/                  stat_X.txt, report_X.txt (raw perf output, committed)
results/linux/         bench_X.json + .txt from the docker container
results/macos/         bench_X.json + .txt, verify.txt from the mac
results/table.md       the table in REPORT.md
Makefile
```

## build

```
make            # bench + edge_tests, -O2 -std=c++17 -Wall -Wextra -Wpedantic, zero warnings
make verify     # ./bench --verify && ./edge_tests
make verify-asan
make perf-build # same -O2 plus -g -fno-omit-frame-pointer, for perf
make clean
```

Or just: `g++ -O2 -std=c++17 -Wall -Wextra -Wpedantic bench.cpp workload.cpp -o bench`

On a mac with homebrew gcc use `make CXX=g++-14`. For asan on mac use the apple compiler, `make CXX=/usr/bin/g++ verify-asan`, homebrew gcc has no libasan there. For timings on the mac also use apple clang (`scripts/run_macos.sh` does), with libstdc++ the `steady_clock` only ticks every microsecond on macOS.

## run

```
./bench --verify                  # A, B, C in lockstep on the same stream
./edge_tests
./bench --impl A                  # 5 timed runs (+1 warm-up), prints each run + median
./bench --impl C --runs 3 --ops 200000 --seed 7
./bench --impl B --json           # one json line per run
./bench --impl C --notime         # no per-op clocks, used for perf
bash scripts/run_perf.sh          # linux, needs perf
bash scripts/run_macos.sh         # mac, timings only
```

perf does not exist on macOS, so we ran the perf part in a Linux container on the same laptop (Docker Desktop, `ubuntu:24.04` with `g++ make python3 linux-tools-generic`), with the submission folder copied in and `scripts/run_perf.sh` run there. It is real Linux and real perf, but the VM does not expose the CPU's hardware counters, so only task-clock and page-faults have values. Everything in `perf/` and `results/linux/` came out of that container, everything in `results/macos/` from the mac itself.

## interface changes

- Prices are integer ticks inside (`tick = llround(price * 100)`). The api still takes and returns `double`, every price out is `tick / 100.0`. Bids are ticks 5000..9999 (50.00..99.99), asks 10001..15000 (100.01..150.00).
- `add_order` returns `bool`. It returns false and does nothing for a price outside the side's band, qty <= 0, or an id that is already in the book. We reject instead of asserting so a bad input can't kill the book.
- `cancel_order` of an unknown or already cancelled id returns false.
- `get_bids(int depth = 10)` / `get_asks` as in the assignment, depth <= 0 returns an empty vector.
- `get_bbo()` is not `const` in B, popping stale heap tops mutates it. A and C are const.
- C assumes order ids are dense (the generator hands out 1, 2, 3, ...) and indexes a pre-sized `std::vector<OrderRec>` by id. Default slab is 2^20 ids, the constructor takes the size. Ids beyond the slab go to an `unordered_map` fallback so it is still correct with sparse ids, just slower. Tested in `edge_tests`.
- Extra flags: `--ops N`, `--seed S`, `--runs R`, `--json`, `--notime`.
- The benchmark is `template <class Book> run(...)` instantiated three times with a `switch` on `--impl`. No virtual calls in the loop so we measure the container, not vtable dispatch.

## how the three differ

| | A map | B heap | C array |
|---|---|---|---|
| order storage | `unordered_map<id, OrderRec>` | `unordered_map<id, OrderRec>` | `vector<OrderRec>` slab indexed by id, `live` flag (+ overflow map) |
| level storage | `std::map<tick, qty>` (rb tree), bids with `greater` | `unordered_map<tick, qty>` + `priority_queue<tick>` of candidates | `std::array<long, 5000>` per side indexed by tick |
| best lookup | `map::begin()` | heap `top()` after popping stale entries | cached best index + 79 word bitmap, `__builtin_ctzll` |
| cancel lookup | hash find, then tree find on the tick | hash find, then hash find on the tick | `slab[id]`, direct |

## correctness

`make verify`:

```
Running correctness test...
Implementation A: PASS
Implementation B: PASS
Implementation C: PASS
All implementations produce identical results.
Checksum: 0xa526e278ec915d19
Edge cases A: PASS (68 checks)
Edge cases B: PASS (68 checks)
Edge cases C: PASS (76 checks)
```

`make CXX=/usr/bin/g++ verify-asan` on the mac (-O1 -g -fsanitize=address,undefined), nothing reported. The checksum differs from the gcc build because apple's libc++ draws random numbers differently, see below:

```
Running correctness test...
Implementation A: PASS
Implementation B: PASS
Implementation C: PASS
All implementations produce identical results.
Checksum: 0xa526e278ec915d19
Edge cases A: PASS (66 checks)
Edge cases B: PASS (66 checks)
Edge cases C: PASS (74 checks)
```

The checksum folds every bbo, every depth level and every cancel result into one 64 bit value. It is identical for A/B/C and across runs on a given build. It does depend on the standard library, libstdc++ and libc++ implement `uniform_int_distribution` differently, so the op stream and the checksum differ between the linux (gcc) and mac (apple clang) builds. Same compiler -> same stream, and all three books agree on both.

## benchmark summary

perf only exists on linux, so the perf rows are from an Ubuntu 24.04 Docker container on this mac (Linux 6.12 linuxkit, aarch64, g++ 13.3) where the hardware counters are `<not supported>`. The second table is the same benchmark run natively on the mac (Apple M3 Pro, apple clang 21). Discussion in REPORT.md.

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

| Metric | Implementation A | Implementation B | Implementation C |
|---|---:|---:|---:|
| Total time (ms) | 232.8 | 152.0 | 54.4 |
| ns / add | 159.5 | 75.4 | 24.2 |
| ns / cancel | 461.8 | 264.5 | 53.9 |
| ns / BBO | 22.9 | 37.0 | 20.6 |

Full `./bench --impl X` output and the per-run json are in `results/linux/` and `results/macos/`. Raw perf output: [perf/stat_A.txt](perf/stat_A.txt), [perf/stat_B.txt](perf/stat_B.txt), [perf/stat_C.txt](perf/stat_C.txt), [perf/report_A.txt](perf/report_A.txt), [perf/report_B.txt](perf/report_B.txt), [perf/report_C.txt](perf/report_C.txt).

Hardware counters were not available inside the Docker VM: cycles, instructions, branches, branch-misses, cache-misses, L1-dcache-load-misses and dTLB-load-misses all report `<not supported>` in `perf/stat_*.txt`. task-clock and page-faults worked. So the IPC, branch miss and cache miss rows are n/a and the report argues those from the code.
