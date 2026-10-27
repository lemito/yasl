#pragma once

#include <yasl/core/utility.h>
#include <yasl/vocab/optional.h>

#include <atomic>

#include "mutex.h"

namespace yasl {
// Runs an initializer exactly once, even when many threads race for it
// (call_once / Rust Once). If f throws the Once stays unset and the next caller
// tries again.
class Once {
 public:
  template <class F>
  void call_once(F&& f) {
    if (done_.load(std::memory_order_acquire)) {
      return;
    }
    LockGuard<Mutex> g(m_);
    if (!done_.load(std::memory_order_relaxed)) {
      f();
      done_.store(true, std::memory_order_release);
    }
  }

  bool is_done() const noexcept {
    return done_.load(std::memory_order_acquire);
  }

 private:
  Mutex m_;
  std::atomic<bool> done_{false};
};

// A value set at most once, readable without locking afterwards (Rust
// OnceLock<T>).
//
//     OnceLock<Config> cfg;
//     const Config& c = cfg.get_or_init([] { return load_config(); });
template <class T>
class OnceLock {
 public:
  OnceLock() noexcept = default;

  OnceLock(const OnceLock&) = delete;
  OnceLock& operator=(const OnceLock&) = delete;

  ~OnceLock() {
    if (once_.is_done()) {
      yasl::destroy_at(ptr());
    }
  }

  // f() returns the value; it runs only if the lock is still empty.
  template <class F>
  T& get_or_init(F&& f) {
    once_.call_once([&] { yasl::construct_at(ptr(), f()); });
    return *ptr();
  }

  Optional<T&> get() noexcept {
    if (once_.is_done()) {
      return *ptr();
    }
    return none;
  }

  bool is_set() const noexcept { return once_.is_done(); }

 private:
  T* ptr() noexcept { return yasl::launder(reinterpret_cast<T*>(storage_)); }

  Once once_;
  alignas(T) unsigned char storage_[sizeof(T)];
};

// A value computed on first use (C# Lazy<T>), thread-safe.
//
//     auto table = lazy([] { return build_table(); });
//     use(table->at(3));
template <class T, class F>
class Lazy {
 public:
  explicit Lazy(F f) : f_(yasl::move(f)) {}

  T& get() { return cell_.get_or_init(f_); }

  T& operator*() { return get(); }

  T* operator->() { return &get(); }

  bool is_initialized() const noexcept { return cell_.is_set(); }

 private:
  F f_;
  OnceLock<T> cell_;
};

template <class F>
Lazy<RemoveCVRef<decltype(declval<F&>()())>, RemoveCVRef<F>> lazy(F&& f) {
  return Lazy<RemoveCVRef<decltype(declval<F&>()())>, RemoveCVRef<F>>(
      yasl::forward<F>(f));
}
}  // namespace yasl
