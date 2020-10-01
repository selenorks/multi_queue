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
  virtual void Consume(Key id, const Value& value) noexcept = 0;
};

class Event
{
public:
  // can throws std::system_error
  void set()
  {
    {
      std::unique_lock<std::mutex> lk(mtx);
      state = true;
    }
    cv.notify_all();
  }

  // can throws std::system_error
  void wait()
  {
    std::unique_lock<std::mutex> lk(mtx);
    cv.wait(lk, [&]() { return state; });
    state = false;
  }

private:
  std::condition_variable cv;
  std::mutex mtx;
  bool state{ false };
};

template<class Value, size_t MaxCapacity>
class Queue
{
public:
  std::optional<Value> dequeue(size_t* values_left = nullptr)
  {
    std::unique_lock<std::mutex> lk(mtx);
    size_t size = values.size();
    if (size) {
      auto front = values.front();
      values.pop_front();
      lk.unlock();

      dequeue_cv.notify_one();
      if (values_left)
        *values_left = size - 1;

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
        break;
      }
      // wait until queue will have enough free space
      dequeue_cv.wait(lk, [&]() { return values.size() < MaxCapacity; });
    }
  }

private:
  std::condition_variable dequeue_cv;
  std::mutex mtx;
  std::list<Value> values;
};

template<typename Key, typename Value, size_t MaxCapacity = 1000>
class MultiQueue
{
public:
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
    std::shared_lock consumers_lock(consumers_mtx);
    auto queues_iter = queues.end();

    auto push_value = [&](typename decltype(queues)::iterator iter) {
      if (iter != queues.end()) {
        iter->second->enqueue(value);
        return true;
      }
      return false;
    };

    auto enqueue_to_exist_queue = [&]() {
      queues_iter = queues.find(id);
      return push_value(queues_iter);
    };

    bool is_inserted = false;
    {
      // queue ro
      // queue values rw
      std::shared_lock lock{ queues_mtx };
      is_inserted = enqueue_to_exist_queue();
    }
    if (!is_inserted) {
      // queue rw
      // queue values rw
      std::unique_lock lock{ queues_mtx };
      is_inserted = enqueue_to_exist_queue();
      if (!is_inserted) {
        const auto [iter, success] = queues.emplace(id, std::make_unique<QueueType>());
        push_value(iter);
      }
    }

    consumers_lock.unlock();
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
        size_t values_left;

        const std::optional<Value>& front = iter->second->dequeue(&values_left);
        queues_lock.unlock();

        more_values_available |= values_left != 0;
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
