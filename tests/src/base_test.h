#ifndef TUTORIAL_BASE_TEST_H
#define TUTORIAL_BASE_TEST_H

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

#include <multi_queue_worker.h>
#include <thread>
#include <future>

#pragma GCC push_options
#pragma GCC optimize ("O0")
volatile int
__attribute__((optimize("O0"))) fibonacci(int N)
{
  if (N == 1 || N == 2)
    return 1;
  return fibonacci(N - 1) + fibonacci(N - 2);
}
#pragma GCC pop_options

class SimpleConsumer : public IConsumer<int, std::pair<int, int> >
{
public:
  SimpleConsumer(const int max_value)
      : max_value(max_value)
  {}
  SimpleConsumer(const SimpleConsumer& other)
      : max_value(other.max_value)
  {
    m_counter.store(other.m_counter);
  }

  SimpleConsumer(SimpleConsumer&& other)
      : max_value(other.max_value)
  {
    m_counter.store(other.m_counter);
  }

  void Consume(int id, const std::pair<int, int>& value) override
  {
    assert(id==value.first);
#pragma optimize("", off)
    volatile int f_num = fibonacci(30);
#pragma optimize("", on)
    //
    auto new_value = m_counter.fetch_add(value.second, std::memory_order_relaxed) + value.second;
    if (new_value == max_value)
      cv.notify_one();
//    printf("Consume id: %d, value: %d, new_value: %d\n", id, value,new_value);
//    fflush(stdout);
  }

  void wait()
  {
    std::unique_lock<std::mutex> lk(m);
    cv.wait(lk, [&] { return m_counter == max_value; });
  }
  void validate() { EXPECT_EQ(m_counter, max_value); }

  // private:
  int max_value;
  std::atomic<int> m_counter{};

  std::mutex m;
  std::condition_variable cv;
};

template<class WORKER>
void
prepare(int enqueue_count, int consumer_count, int& border, std::unique_ptr<WORKER>& processor, std::vector<std::pair<int, int>>& data)
{
  std::random_device rd;
  std::mt19937 g(rd());
  border = enqueue_count * (enqueue_count - 1) / 2;
  processor = std::make_unique<WORKER>();
  for (int i = 0; i < consumer_count; i++) {
    for (int j = 0; j < enqueue_count; j++) {
      data.push_back({ i + 1, j });
    }
  }

  std::shuffle(data.begin(), data.end(), g);
}

template<class WORKER>
void
producer_thread(WORKER& processor, std::vector<std::pair<int, int>>::const_iterator start, std::vector<std::pair<int, int>>::const_iterator end)
{
  //  printf("producer_thread\n");
  std::for_each(start, end, [&](const std::pair<int, int>& p) {
    auto id = p.first;
    auto value = p.second;
    //    printf("producer_thread id:  %4d, value: %4d\n",id, value);
    //    fflush(stdout);
    processor.Enqueue(id, std::make_pair(id,value));
  });
}
template<class WORKER, bool PRODUCE_IN_THREAD = false, bool VALIDATE = true>
void
single_thread(int enqueue_count = 200, int consumer_count = 4, int producer_count = 8)
{
  std::unique_ptr<WORKER> processor;
  std::vector<std::pair<int, int>> data;
  int border;
  prepare<WORKER>(enqueue_count, consumer_count, border, processor, data);

  std::vector<SimpleConsumer> consumers(consumer_count, SimpleConsumer(border));
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

#endif // TUTORIAL_BASE_TEST_H
