//
// Created by as90 on 26.09.2020.
//

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

class SimpleConsumer : public IConsumer<int, int>
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

  void Consume(int id, const int& value) override
  {
    //
    auto new_value =
        m_counter.fetch_add(value, std::memory_order_relaxed) + value;
    if (new_value == max_value)
      cv.notify_one();
    //    printf("Consume id: %d, value: %d, new_value: %d\n", id, value,
    //    new_value);
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

void
prepare(int enqueue_count,
        int consumer_count,
        int& border,
        std::unique_ptr<MultiQueueWorker<int, int>>& processor,
        std::vector<std::pair<int, int>>& data)
{
  std::random_device rd;
  std::mt19937 g(rd());
  border = enqueue_count * (enqueue_count - 1) / 2;
  processor = std::make_unique<MultiQueueWorker<int, int>>();
  for (int i = 0; i < consumer_count; i++) {
    for (int j = 0; j < enqueue_count; j++) {
      data.push_back({ i + 1, j });
    }
  }

  std::shuffle(data.begin(), data.end(), g);
}

void
producer_thread(MultiQueueWorker<int, int>& processor,
                std::vector<std::pair<int, int>>::const_iterator start,
                std::vector<std::pair<int, int>>::const_iterator end)
{
  printf("producer_thread\n");
  std::for_each(start, end, [&](const std::pair<int, int>& p) {
    auto id = p.first;
    auto value = p.second;
//    printf("producer_thread id:  %4d, value: %4d\n",id, value);
//    fflush(stdout);
    processor.Enqueue(id, value);
  });
}
template<bool PRODUCE_IN_THREAD=false, bool VALIDATE = true>
void
single_thread()
{
  int enqueue_count = 600;
  int consumer_count = 12;
  std::unique_ptr<MultiQueueWorker<int, int>> processor;
  std::vector<std::pair<int, int>> data;
  int border;
  prepare(enqueue_count, consumer_count, border, processor, data);



  std::vector<SimpleConsumer> consumers(consumer_count, SimpleConsumer(border));
  for (int i = 0; i < consumer_count; i++) {
    processor->Subscribe(i + 1, &consumers[i]);
  }
//  printf("Border: %d\n", border);


  int producer_count = 14;
  std::vector<std::unique_ptr<std::thread>> producers(producer_count);

  if(PRODUCE_IN_THREAD) {
    for (int i = 0; i < producer_count; i++) {
      int block_size = (data.size() + producer_count - 1) / producer_count;
      std::vector<std::pair<int, int>>::const_iterator start = data.begin() + block_size * i;
      auto end_pos = std::min<size_t>(block_size * (i+1), data.size());
      std::vector<std::pair<int, int>>::const_iterator end = data.begin()+end_pos;

      producers[i] = std::make_unique<std::thread>(
          producer_thread,
          std::ref(*processor),
          start,
          end);
    }
    for (auto& p : producers)
      p->join();
  }else{
    producer_thread(std::ref(*processor), data.begin(), data.end());
  }


  for (auto& c : consumers)
    c.wait();

  processor.release();
  if (VALIDATE) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    for (auto& c : consumers)
      c.validate();
  }
}

#endif // TUTORIAL_BASE_TEST_H
