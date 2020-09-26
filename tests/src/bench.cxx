#include <vector>

#include <benchmark/benchmark.h>
#include "base_test.h"

template <bool PRODUCE_IN_THREAD = false>
static void single_thread_new(benchmark::State& state) {
  for (auto _ : state) {
    single_thread<MultiQueueWorker<int, std::pair<int, int>>, PRODUCE_IN_THREAD, false>();
  }
}

static void single_thread_old(benchmark::State& state) {
  for (auto _ : state) {
    single_thread<MultiQueueWorkerOld<int, std::pair<int, int>>, false, false>();
  }
}

static void multi_thread_new(benchmark::State& state) {
  for (auto _ : state) {
    single_thread<MultiQueueWorker<int, std::pair<int, int>>, true, false>();
  }
}

static void multi_thread_old(benchmark::State& state) {
  for (auto _ : state) {
    single_thread<MultiQueueWorkerOld<int, std::pair<int, int>>, true, false>();
  }
}

BENCHMARK(single_thread_new);
BENCHMARK(single_thread_old);

BENCHMARK(multi_thread_new);
BENCHMARK(multi_thread_old);

BENCHMARK_MAIN();