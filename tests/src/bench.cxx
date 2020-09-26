#include <vector>

#include <benchmark/benchmark.h>
#include "base_test.h"


static void single_thread(benchmark::State& state) {
  single_thread<false, false>();
}

BENCHMARK(single_thread);

BENCHMARK_MAIN();