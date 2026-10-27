#pragma once

#include <yasl/core/check.h>
#include <yasl/core/utility.h>

namespace yasl {
// Link fields of an IntrusiveList element: put one in the element (a base class
// or a member). With AutoUnlink an element that dies (or is destroyed) while
// linked removes itself from its list, so the list never sees a dangling node.
template <bool AutoUnlink = true>
class BasicListNode {
 public:
  BasicListNode() noexcept = default;

  // Copies are not linked anywhere.
  BasicListNode(const BasicListNode&) noexcept {}
  BasicListNode& operator=(const BasicListNode&) noexcept { return *this; }

  ~BasicListNode() {
    if constexpr (AutoUnlink) {
      unlink();
    }
  }

  bool linked() const noexcept { return next_ != nullptr; }

  // Removes the node from whatever list holds it. O(1); does nothing if not
  // linked.
  void unlink() noexcept {
    if (next_) {
      prev_->next_ = next_;
      next_->prev_ = prev_;
      prev_ = next_ = nullptr;
    }
  }

 private:
  template <class T, class Node, Node T::*Member>
  friend class IntrusiveList;

  BasicListNode* prev_ = nullptr;
  BasicListNode* next_ = nullptr;
};

using ListNode = BasicListNode<true>;

// Doubly linked list of objects that carry their own links: no allocation, no
// ownership. The elements must outlive their membership (or use auto-unlink
// nodes, the default). The interface mirrors List<T>. Unlike List, size() is
// O(n): with auto-unlink an element can leave on its own, so no counter could
// stay right.
//
//     struct Job: ListNode { int id; };
//     IntrusiveList<Job> queue;                  // links through the base
//     class queue.push_back(job);
//
//     struct Item { int v; ListNode hook; };
//     IntrusiveList<Item, ListNode, &Item::hook> items;     // links through a
//     member
template <class T, class Node = ListNode, Node T::*Member = nullptr>
class IntrusiveList {
  template <bool Const>
  class Iter {
    friend class IntrusiveList;
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

    reference operator*() const noexcept { return *IntrusiveList::object(n_); }
    pointer operator->() const noexcept { return IntrusiveList::object(n_); }

    Iter& operator++() noexcept {
      n_ = n_->next_;
      return *this;
    }
    Iter operator++(int) noexcept {
      Iter c = *this;
      n_ = n_->next_;
      return c;
    }
    Iter& operator--() noexcept {
      n_ = n_->prev_;
      return *this;
    }
    Iter operator--(int) noexcept {
      Iter c = *this;
      n_ = n_->prev_;
      return c;
    }

    friend bool operator==(const Iter& a, const Iter& b) noexcept {
      return a.n_ == b.n_;
    }

   private:
    explicit Iter(Node* n) noexcept : n_(n) {}

    Node* n_ = nullptr;
  };

 public:
  using value_type = T;
  using iterator = Iter<false>;
  using const_iterator = Iter<true>;

  IntrusiveList() noexcept { init(); }

  IntrusiveList(const IntrusiveList&) = delete;
  IntrusiveList& operator=(const IntrusiveList&) = delete;

  IntrusiveList(IntrusiveList&& other) noexcept {
    init();
    take(other);
  }

  IntrusiveList& operator=(IntrusiveList&& other) noexcept {
    if (this != &other) {
      clear();
      take(other);
    }
    return *this;
  }

  // Unlinks the elements that are still in the list.
  ~IntrusiveList() { clear(); }

  bool empty() const noexcept { return head_.next_ == &head_; }

  // O(n).
  size_t size() const noexcept {
    size_t n = 0;
    for (const Node* p = head_.next_; p != &head_; p = p->next_) {
      ++n;
    }
    return n;
  }

  iterator begin() noexcept { return iterator(head_.next_); }
  iterator end() noexcept { return iterator(&head_); }
  const_iterator begin() const noexcept {
    return const_iterator(const_cast<Node*>(head_.next_));
  }
  const_iterator end() const noexcept {
    return const_iterator(const_cast<Node*>(&head_));
  }

  T& front() noexcept {
    YASL_ASSERT(!empty());
    return *begin();
  }
  T& back() noexcept {
    YASL_ASSERT(!empty());
    return *--end();
  }

  // The element must not be in a list.
  void push_back(T& x) noexcept { insert(end(), x); }
  void push_front(T& x) noexcept { insert(begin(), x); }

  iterator insert(const_iterator pos, T& x) noexcept {
    Node* n = node(&x);
    YASL_ASSERT(!n->linked());
    link_before(pos.n_, n);
    return iterator(n);
  }

  void pop_back() noexcept {
    YASL_ASSERT(!empty());
    erase(--end());
  }
  void pop_front() noexcept {
    YASL_ASSERT(!empty());
    erase(begin());
  }

  // Unlinks the element (it is not destroyed); returns the next one.
  iterator erase(const_iterator pos) noexcept {
    YASL_ASSERT(pos.n_ != &head_);
    Node* next = pos.n_->next_;
    pos.n_->unlink();
    return iterator(next);
  }

  void clear() noexcept {
    while (!empty()) {
      head_.next_->unlink();
    }
  }

  // True if x is in some list (not necessarily this one).
  static bool is_linked(const T& x) noexcept { return node(&x)->linked(); }

  // Unlinks x from its list, whichever it is.
  static void remove(T& x) noexcept { node(&x)->unlink(); }

  void splice(const_iterator pos, IntrusiveList& other) noexcept {
    YASL_ASSERT(this != &other);
    if (other.empty()) {
      return;
    }
    Node* first = other.head_.next_;
    Node* last = other.head_.prev_;
    other.init();
    first->prev_ = pos.n_->prev_;
    pos.n_->prev_->next_ = first;
    last->next_ = pos.n_;
    pos.n_->prev_ = last;
  }

  void splice(const_iterator pos, IntrusiveList& other,
              const_iterator it) noexcept {
    YASL_ASSERT(it.n_ != &other.head_);
    (void)other;
    if (pos.n_ == it.n_ || pos.n_ == it.n_->next_) {
      return;
    }
    it.n_->unlink();
    link_before(pos.n_, it.n_);
  }

  void reverse() noexcept {
    Node* n = &head_;
    do {
      yasl::swap(n->prev_, n->next_);
      n = n->prev_;
    } while (n != &head_);
  }

  // Stable.
  template <class Cmp = Less>
  void sort(Cmp cmp = {}) {
    if (head_.next_ == head_.prev_) {
      return;
    }
    head_.prev_->next_ = nullptr;
    Node* first = merge_sort(head_.next_, count_chain(head_.next_), cmp);
    Node* prev = &head_;
    for (Node* n = first; n; n = n->next_) {
      n->prev_ = prev;
      prev = n;
    }
    head_.next_ = first;
    head_.prev_ = prev;
    prev->next_ = &head_;
  }

  // Unlinks the elements for which pred(x) holds; returns how many.
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

  // Unlinks consecutive duplicates.
  template <class Eq>
  size_t unique(Eq eq) {
    size_t removed = 0;
    if (empty()) {
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

 private:
  static Node* node(T* x) noexcept {
    if constexpr (Member == nullptr) {
      return static_cast<Node*>(x);
    } else {
      return &(x->*Member);
    }
  }

  static const Node* node(const T* x) noexcept {
    return node(const_cast<T*>(x));
  }

  static T* object(Node* n) noexcept {
    if constexpr (Member == nullptr) {
      return static_cast<T*>(n);
    } else {
      return reinterpret_cast<T*>(reinterpret_cast<char*>(n) - member_offset());
    }
  }

  static size_t member_offset() noexcept {
    alignas(T) static char storage[sizeof(T)];
    T* fake = reinterpret_cast<T*>(storage);
    return size_t(reinterpret_cast<char*>(&(fake->*Member)) - storage);
  }

  void init() noexcept { head_.prev_ = head_.next_ = &head_; }

  void take(IntrusiveList& other) noexcept {
    if (other.empty()) {
      return;
    }
    head_.next_ = other.head_.next_;
    head_.prev_ = other.head_.prev_;
    head_.next_->prev_ = &head_;
    head_.prev_->next_ = &head_;
    other.init();
  }

  static void link_before(Node* pos, Node* n) noexcept {
    n->prev_ = pos->prev_;
    n->next_ = pos;
    pos->prev_->next_ = n;
    pos->prev_ = n;
  }

  static size_t count_chain(Node* n) noexcept {
    size_t c = 0;
    for (; n; n = n->next_) {
      ++c;
    }
    return c;
  }

  template <class Cmp>
  static Node* merge_sort(Node* first, size_t n, Cmp& cmp) {
    if (n < 2) {
      return first;
    }
    size_t half = n / 2;
    Node* mid = first;
    for (size_t i = 1; i < half; ++i) {
      mid = mid->next_;
    }
    Node* second = mid->next_;
    mid->next_ = nullptr;
    first = merge_sort(first, half, cmp);
    second = merge_sort(second, n - half, cmp);

    Node dummy;
    Node* tail = &dummy;
    while (first && second) {
      if (cmp(*object(second), *object(first))) {
        tail->next_ = second;
        second = second->next_;
      } else {
        tail->next_ = first;
        first = first->next_;
      }
      tail = tail->next_;
    }
    tail->next_ = first ? first : second;
    Node* result = dummy.next_;
    dummy.next_ =
        nullptr;  // not linked into anything: keep its destructor quiet
    return result;
  }

  Node head_;
};
}  // namespace yasl
