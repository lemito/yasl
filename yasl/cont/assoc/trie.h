#pragma once

#include <yasl/algo/algo.h>
#include <yasl/cont/seq/vector.h>
#include <yasl/mem/resource.h>
#include <yasl/str/view.h>
#include <yasl/vocab/optional.h>

#include <initializer_list>  // language support for braced lists

namespace yasl {
// Prefix tree mapping byte strings to values.
//
// Children are kept in a sorted Vector per node, so traversal is in
// lexicographic (unsigned byte) order and memory stays compact. Every node
// and child array is allocated from the trie's mem::Resource.
//
//     Trie<int> t;
//     t.insert("car", 1);
//     t.insert("cart", 2);
//     t.for_each_with_prefix("car", [](StringView key, int& v) { ... });
//     auto m = t.longest_prefix_of("cartography");  // m.length == 4, *m.value
//     == 2
template <class V>
class Trie {
  struct Node;

  struct Edge {
    unsigned char ch;
    Node* node;
  };

  struct Node {
    Node* parent;
    Vector<Edge> kids;
    Optional<V> value;

    Node(Node* p, mem::Resource* res) : parent(p), kids(res) {}
  };

  struct Frame {
    Node* node;
    size_t next;
  };

  // Depth-first iterator in key order. It owns one key buffer and one frame
  // stack that are reused from step to step, so advancing does not allocate
  // (except when the buffers grow). The element is a proxy {key, value}; `key`
  // is valid until the iterator moves.
  template <bool Const>
  class Iter {
    friend class Trie;
    template <bool>
    friend class Iter;

   public:
    using ValueRef = Conditional<Const, const V&, V&>;

    struct Entry {
      StringView key;
      ValueRef value;
    };

    using value_type = Entry;
    using reference = Entry&;
    using pointer = Entry*;
    using difference_type = ptrdiff_t;

    // The proxy points into this iterator's own key buffer, so a copy rebuilds
    // it.
    Iter(const Iter& other)
        : stack_(other.stack_), key_(other.key_), cur_(other.cur_) {
      fix_entry();
    }

    Iter(Iter&&) noexcept = default;

    Iter& operator=(const Iter& other) {
      Iter tmp(other);
      *this = yasl::move(tmp);
      return *this;
    }

    Iter& operator=(Iter&&) noexcept = default;

    template <bool C2>
      requires(Const && !C2)
    Iter(const Iter<C2>& other)
        : stack_(other.stack_), key_(other.key_), cur_(other.cur_) {
      fix_entry();
    }

    Entry& operator*() noexcept { return *entry_; }

    Entry* operator->() noexcept { return &*entry_; }

    Iter& operator++() {
      advance();
      return *this;
    }

    friend bool operator==(const Iter& a, const Iter& b) noexcept {
      return a.cur_ == b.cur_;
    }

   private:
    Iter(mem::Resource* res) noexcept : stack_(res), key_(res) {}

    void fix_entry() {
      if (cur_) {
        entry_.reset();
        entry_.emplace(
            Entry{StringView(key_.data(), key_.size()), *cur_->value});
      }
    }

    void begin_at(Node* start, StringView prefix) {
      key_.append(prefix.begin(), prefix.end());
      stack_.push_back({start, 0});
      if (start->value) {
        cur_ = start;
        fix_entry();
      } else {
        advance();
      }
    }

    void advance() {
      cur_ = nullptr;
      while (!stack_.empty()) {
        Frame& top = stack_.back();
        if (top.next == top.node->kids.size()) {
          stack_.pop_back();
          if (!stack_.empty()) {
            key_.pop_back();
          }
          continue;
        }
        const Edge& e = top.node->kids[top.next++];
        key_.push_back(static_cast<char>(e.ch));
        stack_.push_back({e.node, 0});
        if (e.node->value) {
          cur_ = e.node;
          fix_entry();
          return;
        }
      }
      entry_.reset();
    }

    Vector<Frame> stack_;
    Vector<char> key_;
    Node* cur_ = nullptr;
    Optional<Entry> entry_;
  };

  // The keys with a given prefix, as a range for `for`.
  template <class It>
  struct Range {
    It first;
    It last;

    It begin() const { return first; }

    It end() const { return last; }
  };

 public:
  template <class P>
  struct BasicMatch {
    size_t length = 0;
    P value = nullptr;

    explicit operator bool() const noexcept { return value != nullptr; }
  };

  using Match = BasicMatch<V*>;
  using ConstMatch = BasicMatch<const V*>;

  Trie() noexcept : res_(mem::default_resource()) {}

  explicit Trie(mem::Resource* res) noexcept : res_(res) {}

  Trie(std::initializer_list<Pair<StringView, V>> il,
       mem::Resource* res = mem::default_resource())
      : Trie(res) {
    for (const auto& [key, value] : il) {
      insert_or_assign(key, value);
    }
  }

  Trie(const Trie& other) : Trie(other, other.res_) {}

  Trie(const Trie& other, mem::Resource* res) : Trie(res) {
    other.for_each(
        [this](StringView key, const V& value) { try_emplace(key, value); });
  }

  Trie(Trie&& other) noexcept
      : root_(yasl::exchange(other.root_, nullptr)),
        size_(yasl::exchange(other.size_, size_t(0))),
        res_(other.res_) {}

  ~Trie() { clear(); }

  Trie& operator=(const Trie& other) {
    if (this != &other) {
      Trie tmp(other, res_);
      swap(tmp);
    }
    return *this;
  }

  Trie& operator=(Trie&& other) noexcept {
    if (this != &other) {
      clear();
      root_ = yasl::exchange(other.root_, nullptr);
      size_ = yasl::exchange(other.size_, size_t(0));
      res_ = other.res_;
    }
    return *this;
  }

  size_t size() const noexcept { return size_; }

  bool empty() const noexcept { return size_ == 0; }

  mem::Resource* resource() const noexcept { return res_; }

  // Inserts V(args...) if the key is absent. Returns the stored value and
  // whether it was inserted.
  template <class... Args>
  Pair<V*, bool> try_emplace(StringView key, Args&&... args) {
    Node* n = descend_create(key);
    if (n->value) {
      return {&*n->value, false};
    }
    try {
      n->value.emplace(yasl::forward<Args>(args)...);
    } catch (...) {
      prune(key);
      throw;
    }
    ++size_;
    return {&*n->value, true};
  }

  bool insert(StringView key, const V& value) {
    return try_emplace(key, value).second;
  }

  bool insert(StringView key, V&& value) {
    return try_emplace(key, yasl::move(value)).second;
  }

  // Returns true if the key was new.
  template <class U>
  bool insert_or_assign(StringView key, U&& value) {
    auto [slot, inserted] = try_emplace(key, yasl::forward<U>(value));
    if (!inserted) {
      *slot = yasl::forward<U>(value);
    }
    return inserted;
  }

  V& operator[](StringView key) { return *try_emplace(key).first; }

  V* find(StringView key) noexcept {
    Node* n = locate(key);
    return n && n->value ? &*n->value : nullptr;
  }

  const V* find(StringView key) const noexcept {
    Node* n = locate(key);
    return n && n->value ? &*n->value : nullptr;
  }

  bool contains(StringView key) const noexcept { return find(key) != nullptr; }

  bool erase(StringView key) {
    Node* n = locate(key);
    if (!n || !n->value) {
      return false;
    }
    n->value.reset();
    --size_;
    prune(key);
    return true;
  }

  void clear() noexcept {
    // Walks down via kids and back up via parent links: no extra memory needed.
    Node* n = root_;
    while (n) {
      if (!n->kids.empty()) {
        Node* child = n->kids.back().node;
        n->kids.pop_back();
        n = child;
      } else {
        Node* parent = n->parent;
        res_->destroy(n);
        n = parent;
      }
    }
    root_ = nullptr;
    size_ = 0;
  }

  // True if some key starts with `prefix`.
  bool has_prefix(StringView prefix) const noexcept {
    return size_ != 0 && locate(prefix) != nullptr;
  }

  // The longest key that is a prefix of `text`.
  Match longest_prefix_of(StringView text) noexcept {
    auto m = static_cast<const Trie&>(*this).longest_prefix_of(text);
    return {m.length, const_cast<V*>(m.value)};
  }

  ConstMatch longest_prefix_of(StringView text) const noexcept {
    ConstMatch m;
    Node* n = root_;
    if (!n) {
      return m;
    }
    if (n->value) {
      m = {0, &*n->value};
    }
    for (size_t i = 0; i < text.size(); ++i) {
      n = child(n, text[i]);
      if (!n) {
        break;
      }
      if (n->value) {
        m = {i + 1, &*n->value};
      }
    }
    return m;
  }

  using iterator = Iter<false>;
  using const_iterator = Iter<true>;

  iterator begin() { return make_iter<iterator>(root_, {}); }

  iterator end() { return iterator(res_); }

  const_iterator begin() const { return make_iter<const_iterator>(root_, {}); }

  const_iterator end() const { return const_iterator(res_); }

  // Iterator at `key`, end() if absent. Continuing from it walks the rest of
  // the trie.
  iterator find_iter(StringView key) { return find_at<iterator>(key); }

  const_iterator find_iter(StringView key) const {
    return find_at<const_iterator>(key);
  }

  // All keys starting with `prefix`, in order.
  Range<iterator> with_prefix(StringView prefix) {
    return {make_iter<iterator>(locate(prefix), prefix), end()};
  }

  Range<const_iterator> with_prefix(StringView prefix) const {
    return {make_iter<const_iterator>(locate(prefix), prefix), end()};
  }

  // Calls f(StringView key, V& value) for every key in lexicographic order.
  // The key view is only valid during the call. Do not modify the trie from f.
  template <class F>
  void for_each(F&& f) {
    walk({}, f);
  }

  template <class F>
  void for_each(F&& f) const {
    walk({}, [&f](StringView key, V& value) {
      f(key, static_cast<const V&>(value));
    });
  }

  template <class F>
  void for_each_with_prefix(StringView prefix, F&& f) {
    walk(prefix, f);
  }

  template <class F>
  void for_each_with_prefix(StringView prefix, F&& f) const {
    walk(prefix, [&f](StringView key, V& value) {
      f(key, static_cast<const V&>(value));
    });
  }

  size_t count_with_prefix(StringView prefix) const {
    size_t n = 0;
    for_each_with_prefix(prefix, [&n](StringView, const V&) { ++n; });
    return n;
  }

  void swap(Trie& other) noexcept {
    yasl::swap(root_, other.root_);
    yasl::swap(size_, other.size_);
    yasl::swap(res_, other.res_);
  }

  friend void swap(Trie& a, Trie& b) noexcept { a.swap(b); }

 private:
  template <class It>
  It make_iter(Node* start, StringView prefix) const {
    It it(res_);
    if (start) {
      it.begin_at(start, prefix);
    }
    return it;
  }

  template <class It>
  It find_at(StringView key) const {
    It it(res_);
    Node* n = root_;
    if (!n) {
      return it;
    }
    for (char ch : key) {
      auto c = static_cast<unsigned char>(ch);
      Edge* e = find_edge(n, c);
      if (e == n->kids.end() || e->ch != c) {
        return It(res_);
      }
      it.stack_.push_back({n, size_t(e - n->kids.begin()) + 1});
      it.key_.push_back(ch);
      n = e->node;
    }
    if (!n->value) {
      return It(res_);
    }
    it.stack_.push_back({n, 0});
    it.cur_ = n;
    it.fix_entry();
    return it;
  }

  static Edge* find_edge(Node* n, unsigned char c) noexcept {
    return yasl::lower_bound(
        n->kids.begin(), n->kids.end(), c,
        [](const Edge& e, unsigned char x) { return e.ch < x; });
  }

  static Node* child(Node* n, char ch) noexcept {
    auto c = static_cast<unsigned char>(ch);
    Edge* e = find_edge(n, c);
    return e != n->kids.end() && e->ch == c ? e->node : nullptr;
  }

  Node* locate(StringView key) const noexcept {
    Node* n = root_;
    for (size_t i = 0; n && i < key.size(); ++i) {
      n = child(n, key[i]);
    }
    return n;
  }

  Node* descend_create(StringView key) {
    if (!root_) {
      root_ = res_->create<Node>(nullptr, res_);
    }
    Node* n = root_;
    for (char ch : key) {
      auto c = static_cast<unsigned char>(ch);
      Edge* e = find_edge(n, c);
      if (e != n->kids.end() && e->ch == c) {
        n = e->node;
        continue;
      }
      Node* nn = res_->create<Node>(n, res_);
      try {
        n->kids.insert(e, Edge{c, nn});
      } catch (...) {
        res_->destroy(nn);
        prune(key);
        throw;
      }
      n = nn;
    }
    return n;
  }

  // Removes the valueless leaf chain at the end of `key`'s path.
  void prune(StringView key) noexcept {
    Node* n = locate(key);
    if (!n) {
      // The path is incomplete (failed insert); prune from its deepest node.
      n = root_;
      for (size_t i = 0; n && i < key.size(); ++i) {
        Node* c = child(n, key[i]);
        if (!c) {
          break;
        }
        n = c;
      }
    }
    while (n && n != root_ && !n->value && n->kids.empty()) {
      Node* parent = n->parent;
      auto& kids = parent->kids;
      for (size_t i = 0; i < kids.size(); ++i) {
        if (kids[i].node == n) {
          kids.erase(kids.begin() + i);
          break;
        }
      }
      res_->destroy(n);
      n = parent;
    }
    if (root_ && size_ == 0 && root_->kids.empty()) {
      res_->destroy(root_);
      root_ = nullptr;
    }
  }

  template <class F>
  void walk(StringView prefix, F&& f) const {
    Node* start = locate(prefix);
    if (!start) {
      return;
    }
    Vector<Frame> stack(res_);
    Vector<char> key(prefix.begin(), prefix.end(), res_);
    auto key_view = [&key] { return StringView(key.data(), key.size()); };

    if (start->value) {
      f(key_view(), *start->value);
    }
    stack.push_back({start, 0});
    while (!stack.empty()) {
      Frame& top = stack.back();
      if (top.next == top.node->kids.size()) {
        stack.pop_back();
        if (!stack.empty()) {
          key.pop_back();
        }
        continue;
      }
      const Edge& e = top.node->kids[top.next++];
      key.push_back(static_cast<char>(e.ch));
      if (e.node->value) {
        f(key_view(), *e.node->value);
      }
      stack.push_back({e.node, 0});
    }
  }

  Node* root_ = nullptr;
  size_t size_ = 0;
  mem::Resource* res_;
};
}  // namespace yasl
