#pragma once

#include <yasl/cont/seq/deque.h>
#include <yasl/cont/seq/vector.h>
#include <yasl/vocab/optional.h>

namespace yasl {
namespace detail {
template <class C, class T>
concept StackContainer = requires(C c, const C cc, T v) {
  c.push_back(yasl::move(v));
  c.emplace_back();
  c.pop_back();
  c.back();
  cc.size();
  cc.empty();
};

template <class C, class T>
concept QueueContainer = requires(C c, const C cc, T v) {
  c.push_back(yasl::move(v));
  c.emplace_back();
  c.pop_front();
  c.front();
  c.back();
  cc.size();
  cc.empty();
};
}  // namespace detail

// LIFO stack over any container with push_back/pop_back/back (Vector by
// default; List and Deque work too).
//
//     Stack<int> s;
//     s.push(1); s.push(2);
//     s.top();                 // 2, checked only by YASL_ASSERT
//     Optional<int> x = s.pop();   // 2, or none when empty
template <class T, class C = Vector<T>>
  requires detail::StackContainer<C, T>
class Stack {
 public:
  using value_type = T;
  using container_type = C;

  Stack() = default;
  explicit Stack(mem::Resource* res) : c_(res) {}

  bool empty() const noexcept { return c_.empty(); }
  size_t size() const noexcept { return c_.size(); }
  mem::Resource* resource() const noexcept { return c_.resource(); }

  void push(const T& v) { c_.push_back(v); }
  void push(T&& v) { c_.push_back(yasl::move(v)); }

  template <class... Args>
  T& emplace(Args&&... args) {
    return c_.emplace_back(yasl::forward<Args>(args)...);
  }

  T& top() noexcept {
    YASL_ASSERT(!c_.empty());
    return c_.back();
  }
  const T& top() const noexcept {
    YASL_ASSERT(!c_.empty());
    return c_.back();
  }

  // Removes and returns the top element, none if the stack is empty.
  Optional<T> pop() {
    if (c_.empty()) {
      return none;
    }
    Optional<T> out(yasl::move(c_.back()));
    c_.pop_back();
    return out;
  }

  void clear() noexcept { c_.clear(); }

  const C& container() const noexcept { return c_; }

 private:
  C c_;
};

// FIFO queue over any container with push_back/pop_front/front/back (Deque by
// default; List works too).
//
//     Queue<int> q;
//     q.push(1); q.push(2);
//     q.front();               // 1
//     Optional<int> x = q.pop();   // 1
template <class T, class C = Deque<T>>
  requires detail::QueueContainer<C, T>
class Queue {
 public:
  using value_type = T;
  using container_type = C;

  Queue() = default;
  explicit Queue(mem::Resource* res) : c_(res) {}

  bool empty() const noexcept { return c_.empty(); }
  size_t size() const noexcept { return c_.size(); }
  mem::Resource* resource() const noexcept { return c_.resource(); }

  void push(const T& v) { c_.push_back(v); }
  void push(T&& v) { c_.push_back(yasl::move(v)); }

  template <class... Args>
  T& emplace(Args&&... args) {
    return c_.emplace_back(yasl::forward<Args>(args)...);
  }

  T& front() noexcept {
    YASL_ASSERT(!c_.empty());
    return c_.front();
  }
  const T& front() const noexcept {
    YASL_ASSERT(!c_.empty());
    return c_.front();
  }
  T& back() noexcept {
    YASL_ASSERT(!c_.empty());
    return c_.back();
  }
  const T& back() const noexcept {
    YASL_ASSERT(!c_.empty());
    return c_.back();
  }

  // Removes and returns the oldest element, none if the queue is empty.
  Optional<T> pop() {
    if (c_.empty()) {
      return none;
    }
    Optional<T> out(yasl::move(c_.front()));
    c_.pop_front();
    return out;
  }

  void clear() noexcept { c_.clear(); }

  const C& container() const noexcept { return c_; }

 private:
  C c_;
};
}  // namespace yasl
