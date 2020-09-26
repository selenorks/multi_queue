#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <list>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <forward_list>

template<typename Key, typename Value>
struct IConsumer
{
  virtual void Consume(Key id, const Value& value)
  {
    id;
    value;
  }
};

class Event{
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
  Value dequeue(bool* more_values = nullptr)
  {
    std::unique_lock<std::mutex> lk(mtx);
    size_t size = values.size();
    if (size) {
      auto front = values.front();
      values.pop_front();
      lk.unlock();

      if (more_values) {
        *more_values = size != 1;
        if(size == MaxCapacity) {
          dequeue_max.set();
        }
      }
      return front;
    }
    return Value{};
  }

  void enqueue(Value value)
  {
    while (true) {
      std::unique_lock<std::mutex> lk(mtx);
      if (values.size() < MaxCapacity) {
        values.push_back(value);
        return;
      }
      else {
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



template<typename Key, typename Value, size_t MaxCapacity=100000>
class MultiQueueWorker
{
public:
  constexpr static size_t max_capacity = MaxCapacity;

  MultiQueueWorker()
      : running{ true }
      , th(&MultiQueueWorker::Process, this)
  {}

  ~MultiQueueWorker()
  {
    StopProcessing();
    th.join();
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
      consumers.insert(std::make_pair(id, consumer));
    }

    notify_new_value();
  }

  void Unsubscribe(Key id)
  {
    // consumers rw
    std::unique_lock lock(consumers_mtx);
    auto iter = consumers.find(id);
    if (iter != consumers.end())
      consumers.erase(id);
  }

  void Enqueue(Key id, Value value)
  {
    // queue ro
    // queue values rw
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
      std::shared_lock lock{ queues_mtx };
      is_inserted = add_to_exist_queue();
    }
    if (!is_inserted) {
      std::unique_lock lock{ queues_mtx };
      is_inserted = add_to_exist_queue();
      if (!is_inserted) {
        const auto [iter, success] = queues.insert(std::make_pair(id, std::make_unique<QueueType>()));
        add_value(iter);
      }
    }

    notify_new_value();
  }

  Value Dequeue(Key id)
  {
    // queue ro
    // queue values rw
    std::shared_lock lock{ queues_mtx };
    auto iter = queues.find(id);
    if (iter != queues.end()) {
      return iter->second->dequeue();
    }
    return Value{};
  }

protected:
  void notify_new_value()
  {
    event_new_value.set();
  }

  void wait_new_values()
  {
    event_new_value.wait();
    printf("Process()\n");
    fflush(stdout);
  }

  bool consume_once()
  {
    bool more_values_available = false;
    std::shared_lock consumers_lock{ consumers_mtx };

    // consumers ro
    for (auto c : consumers) {
      std::shared_lock queues_lock{ queues_mtx };
      // queue ro
      auto iter = queues.find(c.first);
      if (iter != queues.end()) {
        // queue values rw
        bool is_more_values = false;

        Value front = iter->second->dequeue(&is_more_values);
        more_values_available |= is_more_values;
        if (front != Value{})
          c.second->Consume(c.first, front);
      }
    }
    return more_values_available;
  }

  void Process()
  {
    while (running) {
      wait_new_values();

      while (consume_once()) {
      }
    }
  }

protected:
  std::shared_mutex consumers_mtx;
  std::map<Key, IConsumer<Key, Value>*> consumers;

  typedef Queue<Value, MaxCapacity> QueueType;
  std::shared_mutex queues_mtx;
  std::map<Key, std::unique_ptr<QueueType>> queues;

  Event event_new_value;

  bool running;
  std::thread th;
};

template<typename Key, typename Value>
class MultiQueueWorkerOld
{
  enum {MaxCapacity=100000};
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