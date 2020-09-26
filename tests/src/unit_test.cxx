#include <algorithm>
#include <condition_variable>
#include <gtest/gtest.h>
#include <memory>
#include <multi_queue_worker.h>
#include <utility>
#include <vector>

#include "base_test.h"
using std::string;

TEST(Queue, SingleThread)
{
  single_thread<MultiQueueWorker<int, std::pair<int, int>>, false, true>();
}

TEST(Queue, MultiThread)
{
  single_thread<MultiQueueWorker<int, std::pair<int, int>>, true, true>();
}

class QueueWorkerTest : public   testing::TestWithParam< ::std::tuple<int, int, int> > {
protected:
  void SetUp() override {
//    bool force_on_the_fly;
//    int max_precalculated;
//    std::tie(force_on_the_fly, max_precalculated) = GetParam();
//    table_ = new HybridPrimeTable(force_on_the_fly, max_precalculated);
  }
  void TearDown() override {
//    delete table_;
//    table_ = nullptr;
  }
//  HybridPrimeTable* table_;
};


TEST_P(QueueWorkerTest, OddYearsAreNotLeapYears) {
  ::testing::get<0>(GetParam());
  typedef MultiQueueWorker<int, std::pair<int, int>, 10> WORKER;
  int enqueue_count = std::get<0>(GetParam());
  int consumer_count = std::get<1>(GetParam());
  int producer_count = std::get<2>(GetParam());
  std::unique_ptr<WORKER> processor;
  std::vector<std::pair<int, int>> data;
  int border;
  prepare<WORKER>(enqueue_count, consumer_count, border, processor, data);

  std::vector<SimpleConsumer> consumers(consumer_count, SimpleConsumer(border));
  for (int i = 0; i < consumer_count; i++) {
    processor->Subscribe(i + 1, &consumers[i]);
  }
  std::vector<std::unique_ptr<std::thread>> producers(producer_count);

  producer_thread<WORKER>(std::ref(*processor), data.begin(), data.end());

  for (auto& c : consumers)
    c.wait();

  processor.reset();
  std::this_thread::sleep_for(std::chrono::seconds(1));
  for (auto& c : consumers)
    c.validate();

}

INSTANTIATE_TEST_CASE_P(
    MacCapacity, QueueWorkerTest,
    ::testing::Combine(::testing::Values(5, 100, 200), ::testing::Values(1, 4), ::testing::Values(1, 4)));

//TEST(Queue, MaxCapacity)
//{
//
//}
