#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <forward_list>
#include <functional>
#include <list>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <multi_queue.h>

template<typename Key, typename Value>
class MultiQueueWorkerOld
{
  enum
  {
    MaxCapacity = 1000000
  };

public:
  MultiQueueWorkerOld()
      : running{ true }
      , th(&MultiQueueWorkerOld::Process, this)
  {}

  ~MultiQueueWorkerOld()
  {
    StopProcessing();
    th.join();
  }

  void StopProcessing() { running = false; }

  void Subscribe(Key id, IConsumer<Key, Value>* consumer)
  {
    std::lock_guard<std::recursive_mutex> lock{ consumers_mtx };
    auto iter = consumers.find(id);
    if (iter == consumers.end()) {
      consumers.insert(std::make_pair(id, consumer));
    }
  }

  void Unsubscribe(Key id)
  {
    std::lock_guard<std::recursive_mutex> lock{ consumers_mtx };
    auto iter = consumers.find(id);
    if (iter != consumers.end())
      consumers.erase(id);
  }

  void Enqueue(Key id, Value value)
  {
    std::lock_guard<std::recursive_mutex> lock{ mtx };
    auto iter = queues.find(id);
    if (iter != queues.end()) {
      if (iter->second.size() < MaxCapacity)
        iter->second.push_back(value);
    } else {
      queues.insert(std::make_pair(id, std::list<Value>()));
      iter = queues.find(id);
      if (iter != queues.end()) {
        if (iter->second.size() < MaxCapacity)
          iter->second.push_back(value);
      }
    }
  }

  Value Dequeue(Key id)
  {
    std::lock_guard<std::recursive_mutex> lock{ mtx };
    auto iter = queues.find(id);
    if (iter != queues.end()) {
      if (iter->second.size() > 0) {
        auto front = iter->second.front();
        iter->second.pop_front();
        return front;
      }
    }
    return Value{};
  }

protected:
  void Process()
  {
    while (running) {
      std::this_thread::sleep_for(std::chrono::microseconds(1));

      std::lock_guard<std::recursive_mutex> lock{ mtx };
      std::lock_guard<std::recursive_mutex> consumers_lock{ consumers_mtx };

      for (auto iter = queues.begin(); iter != queues.end(); ++iter) {
        auto consumerIter = consumers.find(iter->first);
        if (consumerIter != consumers.end()) {
          Value front = Dequeue(iter->first);
          if (front != Value{})
            consumerIter->second->Consume(iter->first, front);
        }
      }
    }
  }

protected:
  std::recursive_mutex consumers_mtx;
  std::map<Key, IConsumer<Key, Value>*> consumers;

  std::map<Key, std::list<Value>> queues;
  std::recursive_mutex mtx;

  bool running;

  std::thread th;
};
