#pragma once

#include <yasl/core/utility.h>
#include <yasl/time/time.h>
#include <yasl/vocab/function.h>

namespace yasl {
// An OS thread (pthread). Like std::jthread, the destructor joins.
//
//     Thread t([&] { work(); });
//     t.join();                      // or let the destructor do it
class Thread {
 public:
  Thread() noexcept = default;

  // Starts running f right away. `res` holds the callable if it does not fit
  // inline; the new thread frees that memory, so a non-default `res` must be
  // thread-safe.
  template <class F>
    requires(!SameAs<RemoveCVRef<F>, Thread> && Invocable<RemoveCVRef<F>&>)
  explicit Thread(F&& f, mem::Resource* res = mem::default_resource())
      : Thread(Function<void()>(yasl::forward<F>(f), res), res) {}

  Thread(const Thread&) = delete;
  Thread& operator=(const Thread&) = delete;

  Thread(Thread&& other) noexcept
      : id_(other.id_), joinable_(yasl::exchange(other.joinable_, false)) {}

  Thread& operator=(Thread&& other) noexcept {
    if (this != &other) {
      join();
      id_ = other.id_;
      joinable_ = yasl::exchange(other.joinable_, false);
    }
    return *this;
  }

  ~Thread() { join(); }

  bool joinable() const noexcept { return joinable_; }

  // Waits for the thread to finish; does nothing if it is not joinable.
  void join() noexcept;

  // Lets the thread run on its own.
  void detach() noexcept;

  // Number of hardware threads (at least 1).
  static unsigned hardware_concurrency() noexcept;

 private:
  Thread(Function<void()>&& f, mem::Resource* res);

  uint64 id_ = 0;  // the pthread_t, stored as an integer
  bool joinable_ = false;
};

namespace this_thread {
void sleep_for(Duration d) noexcept;
void yield() noexcept;
// Stable per-thread number, handy for logs.
uint64 id() noexcept;
}  // namespace this_thread
}  // namespace yasl
