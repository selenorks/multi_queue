#include <vector>

#include "test_common.h"
#include <benchmark/benchmark.h>

enum
{
  BENCH_ENQUEUE_COUNT = 400000,
  BENCH_CONSUMER_COUNT = 4,
  BENCH_PRODUCER_COUNT = 8
};

template<bool PRODUCE_IN_THREAD = false>
static void
multi_queue_one_thread_producer_new(benchmark::State& state)
{
  for (auto _ : state) {
    test_broadcast<MultiQueue<int, std::pair<int, int>>, PRODUCE_IN_THREAD, false>(BENCH_ENQUEUE_COUNT, BENCH_CONSUMER_COUNT, BENCH_PRODUCER_COUNT);
  }
}


static void
multi_queue_multi_thread_producer_new(benchmark::State& state)
{
  for (auto _ : state) {
    test_broadcast<MultiQueue<int, std::pair<int, int>>, true, false>(BENCH_ENQUEUE_COUNT, BENCH_CONSUMER_COUNT, BENCH_PRODUCER_COUNT);
  }
}


BENCHMARK(multi_queue_one_thread_producer_new);
BENCHMARK(multi_queue_multi_thread_producer_new);

BENCHMARK_MAIN();