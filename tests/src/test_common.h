#pragma once

#include <algorithm>
#include <condition_variable>
#include <gtest/gtest.h>
#include <iostream>
#include <iterator>

#include <cmath>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include <future>
#include <multi_queue.h>
#include <thread>

#pragma GCC push_options
#pragma GCC optimize("O0")
volatile int __attribute__((optimize("O0"))) fibonacci(int N)
{
  if (N == 1 || N == 2)
    return 1;
  return fibonacci(N - 1) + fibonacci(N - 2);
}
#pragma GCC pop_options

template<bool VALIDATE = false>
class SimpleConsumer : public IConsumer<int, std::pair<int, int>>
{
public:
  SimpleConsumer(int consumer_id, const int final_score)
      : consumer_id(consumer_id)
      , final_score(final_score)
  {}
  SimpleConsumer(const SimpleConsumer& other)
      : final_score(other.final_score)
      , consumer_id(other.consumer_id)
      , m_counter(other.m_counter)
  {}

  void Consume(int id, const std::pair<int, int>& value) noexcept override
  {
#pragma optimize("", off)
    // static load for worker thread
    fibonacci(10);
#pragma optimize("", on)

    if (VALIDATE) {
      GTEST_ASSERT_EQ(consumer_id, id);
      GTEST_ASSERT_EQ(id, value.first);
    }

    int new_value;
    {
      std::unique_lock lock(mtx);
      new_value = m_counter += value.second;
    }
    if (new_value == final_score)
      cv.notify_one();
  }

  void wait()
  {
    std::unique_lock<std::mutex> lk(mtx);
    cv.wait(lk, [&] { return m_counter == final_score; });
  }

  void validate() const { EXPECT_EQ(m_counter, final_score); }
  int id() const { return consumer_id; }

private:
  int consumer_id;
  int final_score;

  int m_counter = 0;

  std::mutex mtx;
  std::condition_variable cv;
};

template<class WORKER>
void
prepare_data(int enqueue_count, int consumer_count, int& final_score, std::unique_ptr<WORKER>& processor, std::vector<std::pair<int, int>>& data)
{
  processor = std::make_unique<WORKER>();

  final_score = enqueue_count;

  for (int i = 0; i < consumer_count; i++) {
    for (int j = 0; j < enqueue_count; j++) {
      data.push_back({ i + 1, 1 });
    }
  }

  std::random_device rd;
  std::mt19937 g(rd());
  std::shuffle(data.begin(), data.end(), g);
}

template<class WORKER>
void
producer_thread(WORKER& processor, std::vector<std::pair<int, int>>::const_iterator start, std::vector<std::pair<int, int>>::const_iterator end)
{
  std::for_each(start, end, [&](const std::pair<int, int>& p) {
    auto id = p.first;
    auto value = p.second;
    processor.Enqueue(id, std::make_pair(id, value));
  });
}

template<class Consumer = SimpleConsumer<true>>
std::vector<Consumer>
create_consumers(int consumer_count, int final_score)
{
  std::vector<Consumer> consumers;
  consumers.reserve(consumer_count);

  for (int i = 0; i < consumer_count; i++)
    consumers.emplace_back(i + 1, final_score);

  return consumers;
}

template<class WORKER, bool PRODUCE_IN_THREAD = false, bool VALIDATE = true>
void
test_broadcast(int enqueue_count = 2000, int consumer_count = 4, int producer_count = 8)
{
  std::unique_ptr<WORKER> processor;
  std::vector<std::pair<int, int>> data;
  int final_score;
  prepare_data<WORKER>(enqueue_count, consumer_count, final_score, processor, data);

  auto consumers = create_consumers<SimpleConsumer<VALIDATE>>(consumer_count, final_score);

  for (int i = 0; i < consumer_count; i++) {
    processor->Subscribe(i + 1, &consumers[i]);
  }
  std::vector<std::unique_ptr<std::thread>> producers(producer_count);

  if (PRODUCE_IN_THREAD) {
    for (int i = 0; i < producer_count; i++) {
      int block_size = (data.size() + producer_count - 1) / producer_count;
      auto start = data.begin() + block_size * i;
      auto end_pos = std::min<size_t>(block_size * (i + 1), data.size());
      auto end = data.begin() + end_pos;

      producers[i] = std::make_unique<std::thread>(producer_thread<WORKER>, std::ref(*processor), start, end);
    }

    for (auto& p : producers)
      p->join();

  } else {
    producer_thread<WORKER>(std::ref(*processor), data.begin(), data.end());
  }

  for (auto& c : consumers)
    c.wait();

  processor.reset();
  if (VALIDATE) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    for (auto& c : consumers)
      c.validate();
  }
}
