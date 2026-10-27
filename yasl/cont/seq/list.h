#pragma once

#include <yasl/core/check.h>
#include <yasl/core/exception.h>
#include <yasl/core/hash.h>
#include <yasl/core/utility.h>
#include <yasl/mem/resource.h>

#include <initializer_list>  // language support for braced lists

namespace yasl {
namespace detail {
struct ListLink {
  ListLink* prev;
  ListLink* next;
};
}  // namespace detail

// Doubly linked list with nodes from a mem::Resource (std::list). Iterators and
// references stay valid until their own element is erased, also across
// splice(). splice() and reverse() are O(1) / O(n) pointer moves; sort() is a
// stable merge sort that needs no extra memory.
//
//     List<int> l = {3, 1, 2};
//     l.sort();
//     l.push_front(0);
//     other.splice(other.end(), l);       // moves everything in O(1)
//
// Lists that exchange nodes with splice() must use equal resources.
template <class T>
class List {
  struct Node : detail::ListLink {
    template <class... Args>
    explicit Node(Args&&... args) : value(yasl::forward<Args>(args)...) {}

    T value;
  };

  template <bool Const>
  class Iter {
    friend class List;
    template <bool>
    friend class Iter;

   public:
    using value_type = T;
    using difference_type = ptrdiff_t;
    using reference = Conditional<Const, const T&, T&>;
    using pointer = Conditional<Const, const T*, T*>;

    Iter() noexcept = default;

    template <bool C2>
      requires(Const && !C2)
    Iter(const Iter<C2>& other) noexcept : n_(other.n_) {}

    reference operator*() const noexcept {
      return static_cast<Node*>(n_)->value;
    }
    pointer operator->() const noexcept {
      return &static_cast<Node*>(n_)->value;
    }

    Iter& operator++() noexcept {
      n_ = n_->next;
      return *this;
    }
    Iter operator++(int) noexcept {
      Iter c = *this;
      n_ = n_->next;
      return c;
    }
    Iter& operator--() noexcept {
      n_ = n_->prev;
      return *this;
    }
    Iter operator--(int) noexcept {
      Iter c = *this;
      n_ = n_->prev;
      return c;
    }

    friend bool operator==(const Iter& a, const Iter& b) noexcept {
      return a.n_ == b.n_;
    }

   private:
    explicit Iter(detail::ListLink* n) noexcept : n_(n) {}

    detail::ListLink* n_ = nullptr;
  };

 public:
  using value_type = T;
  using iterator = Iter<false>;
  using const_iterator = Iter<true>;

  List() noexcept : res_(mem::default_resource()) { init(); }

  explicit List(mem::Resource* res) noexcept : res_(res) { init(); }

  List(std::initializer_list<T> il,
       mem::Resource* res = mem::default_resource())
      : res_(res) {
    init();
    for (const T& v : il) {
      push_back(v);
    }
  }

  List(const List& other) : List(other, other.res_) {}

  List(const List& other, mem::Resource* res) : res_(res) {
    init();
    for (const T& v : other) {
      push_back(v);
    }
  }

  List(List&& other) noexcept : res_(other.res_) {
    init();
    take(other);
  }

  ~List() { clear(); }

  List& operator=(const List& other) {
    if (this != &other) {
      List tmp(other, res_);
      swap(tmp);
    }
    return *this;
  }

  List& operator=(List&& other) noexcept {
    if (this != &other) {
      clear();
      res_ = other.res_;
      take(other);
    }
    return *this;
  }

  size_t size() const noexcept { return size_; }
  bool empty() const noexcept { return size_ == 0; }
  mem::Resource* resource() const noexcept { return res_; }

  iterator begin() noexcept { return iterator(head_.next); }
  iterator end() noexcept { return iterator(&head_); }
  const_iterator begin() const noexcept {
    return const_iterator(const_cast<detail::ListLink*>(head_.next));
  }
  const_iterator end() const noexcept {
    return const_iterator(const_cast<detail::ListLink*>(&head_));
  }
  const_iterator cbegin() const noexcept { return begin(); }
  const_iterator cend() const noexcept { return end(); }

  T& front() noexcept {
    YASL_ASSERT(size_ > 0);
    return *begin();
  }
  const T& front() const noexcept {
    YASL_ASSERT(size_ > 0);
    return *begin();
  }
  T& back() noexcept {
    YASL_ASSERT(size_ > 0);
    return *--end();
  }
  const T& back() const noexcept {
    YASL_ASSERT(size_ > 0);
    return *--end();
  }

  void push_back(const T& v) { emplace_back(v); }
  void push_back(T&& v) { emplace_back(yasl::move(v)); }
  void push_front(const T& v) { emplace_front(v); }
  void push_front(T&& v) { emplace_front(yasl::move(v)); }

  template <class... Args>
  T& emplace_back(Args&&... args) {
    return *emplace(end(), yasl::forward<Args>(args)...);
  }

  template <class... Args>
  T& emplace_front(Args&&... args) {
    return *emplace(begin(), yasl::forward<Args>(args)...);
  }

  // Inserts before pos; returns an iterator to the new element.
  template <class... Args>
  iterator emplace(const_iterator pos, Args&&... args) {
    Node* n = res_->create<Node>(yasl::forward<Args>(args)...);
    link_before(pos.n_, n);
    ++size_;
    return iterator(n);
  }

  iterator insert(const_iterator pos, const T& v) { return emplace(pos, v); }
  iterator insert(const_iterator pos, T&& v) {
    return emplace(pos, yasl::move(v));
  }

  void pop_back() noexcept {
    YASL_ASSERT(size_ > 0);
    erase(--end());
  }
  void pop_front() noexcept {
    YASL_ASSERT(size_ > 0);
    erase(begin());
  }

  // Returns the element after the erased one.
  iterator erase(const_iterator pos) noexcept {
    YASL_ASSERT(pos.n_ != &head_);
    detail::ListLink* next = pos.n_->next;
    unlink(pos.n_);
    --size_;
    res_->destroy(static_cast<Node*>(pos.n_));
    return iterator(next);
  }

  iterator erase(const_iterator first, const_iterator last) noexcept {
    while (first != last) {
      first = erase(first);
    }
    return iterator(last.n_);
  }

  void clear() noexcept {
    detail::ListLink* n = head_.next;
    while (n != &head_) {
      detail::ListLink* next = n->next;
      res_->destroy(static_cast<Node*>(n));
      n = next;
    }
    init();
  }

  // Moves all of `other` before pos in O(1).
  void splice(const_iterator pos, List& other) noexcept {
    YASL_ASSERT(this != &other && res_->is_equal(*other.res_));
    if (other.empty()) {
      return;
    }
    detail::ListLink* first = other.head_.next;
    detail::ListLink* last = other.head_.prev;
    size_t moved = other.size_;
    other.init();
    link_range_before(pos.n_, first, last);
    size_ += moved;
  }

  // Moves one element of `other` before pos in O(1). Its iterators stay valid.
  void splice(const_iterator pos, List& other, const_iterator it) noexcept {
    YASL_ASSERT(it.n_ != &other.head_ && res_->is_equal(*other.res_));
    if (pos.n_ == it.n_ || pos.n_ == it.n_->next) {
      return;
    }
    other.unlink(it.n_);
    --other.size_;
    link_before(pos.n_, it.n_);
    ++size_;
  }

  void reverse() noexcept {
    detail::ListLink* n = &head_;
    do {
      yasl::swap(n->prev, n->next);
      n = n->prev;
    } while (n != &head_);
  }

  // Stable.
  template <class Cmp = Less>
  void sort(Cmp cmp = {}) {
    if (size_ < 2) {
      return;
    }
    head_.prev->next = nullptr;
    detail::ListLink* first = merge_sort(head_.next, size_, cmp);
    // Relink the backward pointers.
    detail::ListLink* prev = &head_;
    for (detail::ListLink* n = first; n; n = n->next) {
      n->prev = prev;
      prev = n;
    }
    head_.next = first;
    head_.prev = prev;
    prev->next = &head_;
  }

  // Erases the elements for which pred(x) holds; returns how many.
  template <class Pred>
  size_t remove_if(Pred pred) {
    size_t removed = 0;
    for (iterator it = begin(); it != end();) {
      if (pred(*it)) {
        it = erase(it);
        ++removed;
      } else {
        ++it;
      }
    }
    return removed;
  }

  size_t remove(const T& value) {
    return remove_if([&](const T& x) { return x == value; });
  }

  // Removes consecutive duplicates.
  template <class Eq = EqualTo>
  size_t unique(Eq eq = {}) {
    size_t removed = 0;
    if (size_ < 2) {
      return 0;
    }
    iterator it = begin();
    while (true) {
      iterator next = it;
      ++next;
      if (next == end()) {
        break;
      }
      if (eq(*it, *next)) {
        erase(next);
        ++removed;
      } else {
        it = next;
      }
    }
    return removed;
  }

  void swap(List& other) noexcept {
    List tmp(yasl::move(other));
    other = yasl::move(*this);
    *this = yasl::move(tmp);
  }

  friend void swap(List& a, List& b) noexcept { a.swap(b); }

  friend bool operator==(const List& a, const List& b) {
    if (a.size_ != b.size_) {
      return false;
    }
    auto i = a.begin();
    auto j = b.begin();
    for (; i != a.end(); ++i, ++j) {
      if (!(*i == *j)) {
        return false;
      }
    }
    return true;
  }

 private:
  void init() noexcept {
    head_.prev = head_.next = &head_;
    size_ = 0;
  }

  // Steals the nodes of `other` (this list is empty).
  void take(List& other) noexcept {
    if (other.size_ == 0) {
      return;
    }
    head_.next = other.head_.next;
    head_.prev = other.head_.prev;
    head_.next->prev = &head_;
    head_.prev->next = &head_;
    size_ = other.size_;
    other.init();
  }

  static void link_before(detail::ListLink* pos, detail::ListLink* n) noexcept {
    n->prev = pos->prev;
    n->next = pos;
    pos->prev->next = n;
    pos->prev = n;
  }

  static void link_range_before(detail::ListLink* pos, detail::ListLink* first,
                                detail::ListLink* last) noexcept {
    first->prev = pos->prev;
    pos->prev->next = first;
    last->next = pos;
    pos->prev = last;
  }

  static void unlink(detail::ListLink* n) noexcept {
    n->prev->next = n->next;
    n->next->prev = n->prev;
  }

  // Sorts the null-terminated singly linked chain of n nodes; only `next` is
  // valid.
  template <class Cmp>
  static detail::ListLink* merge_sort(detail::ListLink* first, size_t n,
                                      Cmp& cmp) {
    if (n < 2) {
      return first;
    }
    size_t half = n / 2;
    detail::ListLink* mid = first;
    for (size_t i = 1; i < half; ++i) {
      mid = mid->next;
    }
    detail::ListLink* second = mid->next;
    mid->next = nullptr;
    first = merge_sort(first, half, cmp);
    second = merge_sort(second, n - half, cmp);

    detail::ListLink dummy;
    detail::ListLink* tail = &dummy;
    while (first && second) {
      // Take from `second` only when strictly smaller: keeps the sort stable.
      if (cmp(static_cast<Node*>(second)->value,
              static_cast<Node*>(first)->value)) {
        tail->next = second;
        second = second->next;
      } else {
        tail->next = first;
        first = first->next;
      }
      tail = tail->next;
    }
    tail->next = first ? first : second;
    return dummy.next;
  }

  detail::ListLink head_;
  size_t size_ = 0;
  mem::Resource* res_;
};
}  // namespace yasl
