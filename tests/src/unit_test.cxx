#include <algorithm>
#include <condition_variable>
#include <gtest/gtest.h>
#include <iostream>
#include <iterator>
#include <multi_queue_worker.h>
#include <random>
#include <string>
#include <utility>
#include <vector>
#include <memory>

#include <benchmark/benchmark.h>
#include "base_test.h"
using std::string;


TEST(Queue, SingleThread)
{
  single_thread<false, true>();
}

TEST(Queue, MultiThread)
{
  single_thread<true, true>();
}
