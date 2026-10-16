CXX ?= g++
STD = -std=c++17
WARN = -Wall -Wextra -Wpedantic
CXXFLAGS ?= -O2 $(STD) $(WARN)
SRCS = bench.cpp workload.cpp
HDRS = order_book_api.hpp order_book_map.hpp order_book_heap.hpp order_book_array.hpp workload.hpp

all: bench edge_tests

bench: $(SRCS) $(HDRS)
	$(CXX) $(CXXFLAGS) $(SRCS) -o bench

edge_tests: tests/edge_cases.cpp $(HDRS)
	$(CXX) $(CXXFLAGS) tests/edge_cases.cpp -o edge_tests

verify: bench edge_tests
	./bench --verify
	./edge_tests

# -O1 so asan output stays readable, same warnings
verify-asan: $(SRCS) $(HDRS) tests/edge_cases.cpp
	$(CXX) -O1 -g $(STD) $(WARN) -fsanitize=address,undefined -fno-omit-frame-pointer $(SRCS) -o bench_asan
	$(CXX) -O1 -g $(STD) $(WARN) -fsanitize=address,undefined -fno-omit-frame-pointer tests/edge_cases.cpp -o edge_tests_asan
	./bench_asan --verify
	./edge_tests_asan

# same -O2 build, just keeps frame pointers + debug info so perf can unwind
perf-build: $(SRCS) $(HDRS)
	$(CXX) -O2 -g -fno-omit-frame-pointer $(STD) $(WARN) $(SRCS) -o bench

clean:
	rm -f bench edge_tests bench_asan edge_tests_asan

.PHONY: all verify verify-asan perf-build clean
