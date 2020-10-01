#include <algorithm>
#include <condition_variable>
#include <gtest/gtest.h>
#include <memory>
#include <multi_queue.h>
#include <utility>
#include <vector>

#include "test_common.h"

typedef MultiQueue<int, std::pair<int, int>> MultiQueueBaseType;
TEST(Queue, OneThreadProducer)
{
  test_broadcast<MultiQueueBaseType, false, true>();
}

TEST(Queue, MultiThreadProducer)
{
  test_broadcast<MultiQueueBaseType, true, true>(400, 4, 6);
}

class QueueWorkerTest : public testing::TestWithParam<::std::tuple<int, int, int>>
{
protected:
  void SetUp() override {}
  void TearDown() override {}
};

TEST_P(QueueWorkerTest, QueueOverflow)
{
  typedef MultiQueue<int, std::pair<int, int>, 10> MultiQueueLimit;
  int enqueue_count = std::get<0>(GetParam());
  int consumer_count = std::get<1>(GetParam());
  int producer_count = std::get<2>(GetParam());
  std::unique_ptr<MultiQueueLimit> processor;
  std::vector<std::pair<int, int>> data;
  int final_score;
  prepare_data<MultiQueueLimit>(enqueue_count, consumer_count, final_score, processor, data);

  auto consumers = create_consumers(consumer_count, final_score);

  for (int i = 0; i < consumer_count; i++) {
    processor->Subscribe(i + 1, &consumers[i]);
  }
  std::vector<std::unique_ptr<std::thread>> producers(producer_count);

  producer_thread<MultiQueueLimit>(std::ref(*processor), data.begin(), data.end());

  for (auto& c : consumers)
    c.wait();

  processor.reset();
  std::this_thread::sleep_for(std::chrono::seconds(1));
  for (auto& c : consumers)
    c.validate();
}

INSTANTIATE_TEST_CASE_P(MaxCapacity, QueueWorkerTest, ::testing::Combine(::testing::Values(5, 100, 200), ::testing::Values(1, 4), ::testing::Values(1, 4)));

TEST(Subscribing, OneByOne)
{
  int enqueue_count = 200;
  int consumer_count = 10;
  int producer_count = 10;
  std::unique_ptr<MultiQueueBaseType> processor;
  std::vector<std::pair<int, int>> data;
  int final_score;
  prepare_data<MultiQueueBaseType>(enqueue_count, consumer_count, final_score, processor, data);

  auto consumers = create_consumers(consumer_count, final_score);

  std::vector<std::unique_ptr<std::thread>> producers(producer_count);

  producer_thread<MultiQueueBaseType>(std::ref(*processor), data.begin(), data.end());

  for (int i = 0; i < consumer_count; i++) {
    processor->Subscribe(i + 1, &consumers[i]);
    consumers[i].wait();
    consumers[i].validate();
  }
  processor.reset();
  std::this_thread::sleep_for(std::chrono::seconds(1));
  for (auto& c : consumers)
    c.validate();
}

TEST(Subscribing, ByGroups)
{
  int enqueue_count = 300;

  int group_count = 4;
  int group_size = 3;
  int consumer_count = group_count * group_size;

  int producer_count = 10;

  std::unique_ptr<MultiQueueBaseType> processor;
  std::vector<std::pair<int, int>> data;
  int final_score;
  prepare_data<MultiQueueBaseType>(enqueue_count, consumer_count, final_score, processor, data);

  auto consumers = create_consumers(consumer_count, final_score);

  std::vector<std::unique_ptr<std::thread>> producers(producer_count);

  producer_thread<MultiQueueBaseType>(std::ref(*processor), data.begin(), data.end());

  for (int j = 0; j < group_count; j++) {
    int start = j * group_size;
    int stop = (j + 1) * group_size;
    for (int i = start; i < stop; i++) {
      processor->Subscribe(i + 1, &consumers[i]);
    }

    for (int i = start; i < stop; i++) {
      consumers[i].wait();
      consumers[i].validate();
    }
  }

  processor.reset();
  std::this_thread::sleep_for(std::chrono::seconds(1));
  for (auto& c : consumers)
    c.validate();
}

TEST(Unsubscribe, ByGroups)
{
  int enqueue_count = 300;

  int group_count = 4;
  int group_size = 3;
  int consumer_count = group_count * group_size;

  int producer_count = 10;

  std::unique_ptr<MultiQueueBaseType> processor;
  std::vector<std::pair<int, int>> data;
  int final_score;
  prepare_data<MultiQueueBaseType>(enqueue_count, consumer_count, final_score, processor, data);

  auto consumers = create_consumers(consumer_count, final_score);

  std::vector<std::unique_ptr<std::thread>> producers(producer_count);

  producer_thread<MultiQueueBaseType>(std::ref(*processor), data.begin(), data.end());

  processor->Enqueue(consumer_count + 1, std::make_pair(consumer_count + 1, 100));

  for (int j = 0; j < group_count; j++) {
    int start = j * group_size;
    int stop = (j + 1) * group_size;
    for (int i = start; i < stop; i++) {
      processor->Subscribe(i + 1, &consumers[i]);
    }

    for (int i = start; i < stop; i++) {
      consumers[i].wait();
      consumers[i].validate();

      processor->Unsubscribe(i + 1);
      processor->Enqueue(i + 1, std::make_pair(i * 10, 10));
    }
  }

  // checks that old consumers didn`t receive a new value 10
  for (auto& c : consumers)
    c.validate();

  processor.reset();
  std::this_thread::sleep_for(std::chrono::seconds(1));
  for (auto& c : consumers)
    c.validate();
}

TEST(Unsubscribe, DoubleUnsubscribe)
{
  std::unique_ptr<MultiQueueBaseType> processor = std::make_unique<MultiQueueBaseType>();

  int consumer_count = 2;
  auto consumers = create_consumers(consumer_count, 0);

  for (auto& c : consumers)
    processor->Subscribe(c.id(), &c);

  for (auto& c : consumers)
    processor->Unsubscribe(c.id());

  for (auto& c : consumers)
    processor->Unsubscribe(c.id());
}

TEST(Worker, DequeueManual)
{
  int consumer_count = 0;

  MultiQueueBaseType processor;

  int id = 0;
  processor.Enqueue(id, std::make_pair(id, 100));

  std::optional<std::pair<int, int>> v = processor.Dequeue(id);
  EXPECT_EQ(v.has_value(), true);
  EXPECT_EQ(v.value().first, id);
  EXPECT_EQ(v.value().second, 100);

  v = processor.Dequeue(consumer_count + 1);
  EXPECT_EQ(v.has_value(), false);
}
