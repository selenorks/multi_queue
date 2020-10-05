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

template<typename Value>
struct IQueue
{
  virtual std::optional<Value> dequeue() = 0;
  virtual void enqueue(const Value& value) = 0;
};

template<class Value, size_t MaxCapacity>
class Queue : public IQueue<Value>
{
public:
  std::optional<Value> dequeue() override
  {
    std::unique_lock<std::mutex> lk(mtx);
    size_t size = values.size();
    if (size) {
      auto front = values.front();
      values.pop_front();
      lk.unlock();

      dequeue_cv.notify_one();

      return { front };
    }
    return {};
  }

  void enqueue(const Value& value) override
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

  size_t size() const
  {
    std::unique_lock<std::mutex> lk(mtx);
    return values.size();
  }

private:
  std::condition_variable_any dequeue_cv;
  mutable std::mutex mtx;
  std::list<Value> values;
};

template<typename Key, typename Value, size_t MaxCapacity = 1000, class QueueType = Queue<Value, MaxCapacity>>
class MultiQueue
{
public:
  MultiQueue()
      : running{ true }
      , worker_thread(&MultiQueue::process, this)
  {}

  ~MultiQueue()
  {
    stopProcessing();
    worker_thread.join();
  }

  void stopProcessing()
  {
    running = false;
    notify_new_value();
  }

  void subscribe(const Key& id, IConsumer<Key, Value>* consumer)
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

  void unsubscribe(const Key& id)
  {
    // consumers rw
    std::unique_lock lock(consumers_mtx);
    consumers.erase(id);
  }

  void enqueue(const Key& id, const Value& value)
  {
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

    notify_new_value();
  }

  std::optional<Value> dequeue(const Key& id)
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
      auto iter = queues.find(c.first);
      if (iter != queues.end()) {
        // queue values rw
        const std::optional<Value>& front = iter->second->dequeue();

        queues_lock.unlock();
        if (front.has_value()) {

          c.second->Consume(c.first, front.value());
        } else {
          std::unique_lock queues_lock(queues_mtx, std::defer_lock);
          if (queues_lock.try_lock()) {
            auto iter = queues.find(c.first);
            if (!iter->second->size()) {
              queues.erase(iter);
              continue;
            }
          }
        }

        more_values_available = true;
      }
    }
    return more_values_available;
  }

  void process()
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

  std::shared_mutex queues_mtx;
  std::map<Key, std::unique_ptr<QueueType>> queues;

  Event new_value_event;

  bool running;
  std::thread worker_thread;
};
