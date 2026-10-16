// one binary: --impl A|B|C picks the book, everything else is shared
#include "order_book_map.hpp"
#include "order_book_heap.hpp"
#include "order_book_array.hpp"
#include "workload.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using Clock = std::chrono::steady_clock;

static inline uint64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
}

// cheap mixing so the compiler can't throw results away
static inline uint64_t fold(uint64_t h, uint64_t v) {
    return h ^ (v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2));
}

static uint64_t fold_bbo(uint64_t h, const BBO& b) {
    h = fold(h, b.has_bid);
    if (b.has_bid) { h = fold(h, to_tick(b.bid_price)); h = fold(h, b.bid_qty); }
    h = fold(h, b.has_ask);
    if (b.has_ask) { h = fold(h, to_tick(b.ask_price)); h = fold(h, b.ask_qty); }
    return h;
}

static uint64_t fold_levels(uint64_t h, const std::vector<Level>& v) {
    h = fold(h, v.size());
    for (const Level& l : v) { h = fold(h, to_tick(l.price)); h = fold(h, l.qty); }
    return h;
}

struct Acc {
    uint64_t ns = 0;
    uint64_t n = 0;
    std::vector<uint32_t> samples;  // per call ns, for p99
    void hit(uint64_t d) { ns += d; ++n; samples.push_back(static_cast<uint32_t>(d)); }
    double mean() const { return n ? double(ns) / n : 0.0; }
    uint32_t p99() {
        if (samples.empty()) return 0;
        size_t k = (samples.size() - 1) * 99 / 100;
        std::nth_element(samples.begin(), samples.begin() + k, samples.end());
        return samples[k];
    }
};

struct RunResult {
    double total_ms = 0;
    double ns_add = 0, ns_cancel = 0, ns_bbo = 0, ns_depth = 0;
    uint32_t p99_add = 0, p99_cancel = 0, p99_bbo = 0, p99_depth = 0;
    uint64_t checksum = 0;
    BBO final_bbo;
};

template <class Book>
static RunResult run(const Workload& w, bool timed = true) {
    Book book;
    Acc add, cancel, bbo, depth;
    add.samples.reserve(w.n_add);
    cancel.samples.reserve(w.n_cancel);
    bbo.samples.reserve(w.n_bbo);
    depth.samples.reserve(w.ops.size() / 100 + 1);
    uint64_t cs = 0;

    uint64_t t_start = now_ns();
    for (size_t i = 0; i < w.ops.size(); ++i) {
        const Op& op = w.ops[i];
        // timed=false (--notime) is for perf runs, otherwise clock_gettime swamps the profile
        uint64_t t0 = timed ? now_ns() : 0, t1 = 0;
        switch (op.type) {
        case OpType::Add:
            cs = fold(cs, book.add_order(op.id, op.side, to_price(op.tick), op.qty));
            if (timed) { t1 = now_ns(); add.hit(t1 - t0); }
            break;
        case OpType::Cancel:
            cs = fold(cs, book.cancel_order(op.id));
            if (timed) { t1 = now_ns(); cancel.hit(t1 - t0); }
            break;
        case OpType::Bbo:
            cs = fold_bbo(cs, book.get_bbo());
            if (timed) { t1 = now_ns(); bbo.hit(t1 - t0); }
            break;
        }
        if ((i + 1) % 100 == 0) {
            if (timed) t0 = now_ns();
            std::vector<Level> b = book.get_bids(10);
            std::vector<Level> a = book.get_asks(10);
            if (timed) { t1 = now_ns(); depth.hit(t1 - t0); }
            cs = fold_levels(cs, b);
            cs = fold_levels(cs, a);
        }
    }
    uint64_t t_end = now_ns();

    RunResult r;
    r.total_ms = (t_end - t_start) / 1e6;
    r.ns_add = add.mean();
    r.ns_cancel = cancel.mean();
    r.ns_bbo = bbo.mean();
    r.ns_depth = depth.mean();
    r.p99_add = add.p99();
    r.p99_cancel = cancel.p99();
    r.p99_bbo = bbo.p99();
    r.p99_depth = depth.p99();
    r.checksum = cs;
    r.final_bbo = book.get_bbo();
    return r;
}

static double clock_overhead_ns() {
    const int n = 1000000;
    uint64_t acc = 0;
    for (int i = 0; i < n; ++i) {
        uint64_t a = now_ns();
        uint64_t b = now_ns();
        acc += b - a;
    }
    return double(acc) / n;
}

static double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    size_t n = v.size();
    return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2.0;
}

static void print_bbo(const BBO& b) {
    printf("Final BBO: ");
    if (b.has_bid) printf("Bid: %.2f x %ld", b.bid_price, b.bid_qty); else printf("Bid: -");
    printf(" / ");
    if (b.has_ask) printf("Ask: %.2f x %ld\n", b.ask_price, b.ask_qty); else printf("Ask: -\n");
}

static void print_json(char impl, int run_no, const Workload& w, uint32_t seed, const RunResult& r, double clock_ns) {
    printf("{\"impl\":\"%c\",\"run\":%d,\"ops\":%zu,\"seed\":%u,"
           "\"n_add\":%zu,\"n_cancel\":%zu,\"n_bbo\":%zu,\"n_depth\":%zu,\"degraded\":%zu,"
           "\"total_ms\":%.3f,\"ns_add\":%.2f,\"ns_cancel\":%.2f,\"ns_bbo\":%.2f,\"ns_depth\":%.2f,"
           "\"p99_add\":%u,\"p99_cancel\":%u,\"p99_bbo\":%u,\"p99_depth\":%u,"
           "\"clock_ns\":%.1f,\"checksum\":\"0x%016llx\"}\n",
           impl, run_no, w.ops.size(), seed, w.n_add, w.n_cancel, w.n_bbo, w.ops.size() / 100, w.degraded,
           r.total_ms, r.ns_add, r.ns_cancel, r.ns_bbo, r.ns_depth,
           r.p99_add, r.p99_cancel, r.p99_bbo, r.p99_depth,
           clock_ns, static_cast<unsigned long long>(r.checksum));
}

template <class Book>
static int bench(char impl, const Workload& w, uint32_t seed, int runs, bool json, bool timed) {
    double clock_ns = timed ? clock_overhead_ns() : 0.0;  // skip the 2M clock calls on perf runs
    if (!json) {
        printf("impl %c  ops=%zu seed=%u  (adds %zu, cancels %zu, bbo %zu, depth pairs %zu, degraded cancels %zu)\n",
               impl, w.ops.size(), seed, w.n_add, w.n_cancel, w.n_bbo, w.ops.size() / 100, w.degraded);
        printf("clock overhead per timed call: %.1f ns\n", clock_ns);
    }

    RunResult warm = run<Book>(w, timed);  // untimed warm up, result only used for the checksum check
    if (!json) printf("warm-up run done (not timed)\n");

    std::vector<double> tot, add, cancel, bbo, depth, p99a, p99c, p99b, p99d;
    RunResult last;
    for (int k = 1; k <= runs; ++k) {
        RunResult r = run<Book>(w, timed);
        if (r.checksum != warm.checksum) {
            fprintf(stderr, "checksum changed between runs! 0x%llx vs 0x%llx\n",
                    (unsigned long long)warm.checksum, (unsigned long long)r.checksum);
            return 2;
        }
        tot.push_back(r.total_ms); add.push_back(r.ns_add); cancel.push_back(r.ns_cancel);
        bbo.push_back(r.ns_bbo); depth.push_back(r.ns_depth);
        p99a.push_back(r.p99_add); p99c.push_back(r.p99_cancel); p99b.push_back(r.p99_bbo); p99d.push_back(r.p99_depth);
        if (json) print_json(impl, k, w, seed, r, clock_ns);
        else printf("run %d: total %.1f ms | add %.1f ns | cancel %.1f ns | bbo %.1f ns | depth %.1f ns | p99 add %u cancel %u bbo %u depth %u\n",
                    k, r.total_ms, r.ns_add, r.ns_cancel, r.ns_bbo, r.ns_depth, r.p99_add, r.p99_cancel, r.p99_bbo, r.p99_depth);
        last = r;
    }
    if (!json) {
        printf("median: total %.1f ms | add %.1f ns | cancel %.1f ns | bbo %.1f ns | depth %.1f ns | p99 add %.0f cancel %.0f bbo %.0f depth %.0f\n",
               median(tot), median(add), median(cancel), median(bbo), median(depth),
               median(p99a), median(p99c), median(p99b), median(p99d));
        print_bbo(last.final_bbo);
        printf("Checksum: 0x%016llx\n", (unsigned long long)last.checksum);
    }
    return 0;
}

static void usage(const char* me) {
    fprintf(stderr,
            "usage: %s --impl A|B|C [--runs 5] [--ops 1000000] [--seed 42] [--json] [--notime]\n", me);
}

int main(int argc, char** argv) {
    char impl = 0;
    bool json = false, timed = true;
    int runs = 5;
    size_t ops = 1000000;
    uint32_t seed = 42;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        bool has_next = i + 1 < argc;
        if (a == "--impl" && has_next) impl = argv[++i][0];
        else if (a == "--runs" && has_next) runs = std::atoi(argv[++i]);
        else if (a == "--ops" && has_next) ops = std::strtoull(argv[++i], nullptr, 10);
        else if (a == "--seed" && has_next) seed = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        else if (a == "--json") json = true;
        else if (a == "--notime") timed = false;  // perf runs, skip per op clocks
        else { usage(argv[0]); return 2; }
    }
    if (impl != 'A' && impl != 'B' && impl != 'C') { usage(argv[0]); return 2; }
    if (runs < 1 || ops == 0) { usage(argv[0]); return 2; }

    Workload w = gen_workload(ops, seed);  // everything pre generated, nothing random inside the timed loop
    switch (impl) {
    case 'A': return bench<OrderBookMap>(impl, w, seed, runs, json, timed);
    case 'B': return bench<OrderBookHeap>(impl, w, seed, runs, json, timed);
    default:  return bench<OrderBookArray>(impl, w, seed, runs, json, timed);
    }
}
