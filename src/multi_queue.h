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

template<typename Key, typename Value>
struct IConsumer
{
  virtual void Consume(Key id, const Value& value) = 0;
};

class Event
{
public:
  void set()
  {
    {
      std::unique_lock<std::mutex> lk(mtx);
      state = true;
    }
    cv.notify_one();
  }

  void wait()
  {
    std::unique_lock<std::mutex> lk(mtx);
    cv.wait(lk, [&]() { return state; });
    state = false;
  }

private:
  std::condition_variable cv;
  std::mutex mtx;
  bool state = false;
};

template<class Value, size_t MaxCapacity>
class Queue
{
public:
  std::optional<Value> dequeue(bool* more_left = nullptr)
  {
    std::unique_lock<std::mutex> lk(mtx);
    size_t size = values.size();
    if (size) {
      auto front = values.front();
      values.pop_front();
      lk.unlock();

      if (more_left) {
        *more_left = size != 1;
        if (size == MaxCapacity) {
          dequeue_max.set();
        }
      }
      return { front };
    }
    return {};
  }

  void enqueue(const Value& value)
  {
    while (true) {
      std::unique_lock<std::mutex> lk(mtx);
      if (values.size() < MaxCapacity) {
        values.push_back(value);
        return;
      } else {
        // wait until queue will have enough free space
        lk.unlock();
        dequeue_max.wait();
      }
    }
  }

private:
  std::mutex mtx;
  std::list<Value> values;
  Event dequeue_max;
};

template<typename Key, typename Value, size_t MaxCapacity = 1000>
class MultiQueue
{
public:
  constexpr static size_t max_capacity = MaxCapacity;

  MultiQueue()
      : running{ true }
      , worker_thread(&MultiQueue::Process, this)
  {}

  ~MultiQueue()
  {
    StopProcessing();
    worker_thread.join();
  }

  void StopProcessing()
  {
    running = false;
    notify_new_value();
  }

  void Subscribe(Key id, IConsumer<Key, Value>* consumer)
  {
    // consumers rw
    std::unique_lock lock{ consumers_mtx };
    auto iter = consumers.find(id);
    if (iter == consumers.end()) {
      consumers.emplace(id, consumer);
      lock.unlock();
      notify_new_value();
    }
  }

  void Unsubscribe(Key id)
  {
    // consumers rw
    std::unique_lock lock(consumers_mtx);
    consumers.erase(id);
  }

  void Enqueue(Key id, Value value)
  {
    auto queues_iter = queues.end();

    auto add_value = [&](typename decltype(queues)::iterator iter) {
      if (iter != queues.end()) {
        iter->second->enqueue(value);
        return true;
      }
      return false;
    };

    auto add_to_exist_queue = [&]() {
      queues_iter = queues.find(id);
      return add_value(queues_iter);
    };

    bool is_inserted = false;
    {
      // queue ro
      // queue values rw
      std::shared_lock lock{ queues_mtx };
      is_inserted = add_to_exist_queue();
    }
    if (!is_inserted) {
      // queue rw
      // queue values rw
      std::unique_lock lock{ queues_mtx };
      is_inserted = add_to_exist_queue();
      if (!is_inserted) {
        const auto [iter, success] = queues.emplace(id, std::make_unique<QueueType>());
        add_value(iter);
      }
    }

    notify_new_value();
  }

  std::optional<Value> Dequeue(Key id)
  {
    // queue ro
    // queue values rw
    std::shared_lock lock{ queues_mtx };
    auto iter = queues.find(id);
    if (iter != queues.end()) {
      return { iter->second->dequeue() };
    }

    return {};
  }

protected:
  void notify_new_value() { new_value_event.set(); }

  void wait_new_values() { new_value_event.wait(); }

  bool consume_once()
  {
    bool more_values_available = false;
    std::shared_lock consumers_lock{ consumers_mtx };

    // consumers ro
    for (const auto& c : consumers) {
      std::shared_lock queues_lock{ queues_mtx };
      // queue ro
      const auto& iter = queues.find(c.first);
      if (iter != queues.end()) {
        // queue values rw
        bool more_left = false;

        const std::optional<Value>& front = iter->second->dequeue(&more_left);
        more_values_available |= more_left;
        if (front.has_value())
          c.second->Consume(c.first, front.value());
      }
    }
    return more_values_available;
  }

  void Process()
  {
    while (running) {
      while (consume_once()) {
      }
      wait_new_values();
    }
  }

protected:
  std::shared_mutex consumers_mtx;
  std::map<Key, IConsumer<Key, Value>*> consumers;

  typedef Queue<Value, MaxCapacity> QueueType;
  std::shared_mutex queues_mtx;
  std::map<Key, std::unique_ptr<QueueType>> queues;

  Event new_value_event;

  bool running;
  std::thread worker_thread;
};

template<typename Key, typename Value>
class MultiQueueWorkerOld
{
  enum
  {
    MaxCapacity = 100000
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