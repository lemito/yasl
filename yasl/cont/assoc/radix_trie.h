#pragma once

#include <yasl/algo/algo.h>
#include <yasl/cont/seq/vector.h>
#include <yasl/mem/resource.h>
#include <yasl/str/view.h>
#include <yasl/vocab/optional.h>

#include <initializer_list>  // language support for braced lists

namespace yasl {
// Prefix tree with compressed paths (radix tree): an edge carries a whole
// substring, so a node exists only where keys branch or end. It has the same
// interface as Trie, but needs far fewer nodes (and allocations) for long keys.
//
// Children are kept sorted by their first byte, so traversal is in
// lexicographic (unsigned byte) order. Every node, label and child array is
// allocated from the trie's mem::Resource.
//
//     RadixTrie<int> t;
//     t.insert("car", 1);
//     t.insert("cart", 2);
//     t.for_each_with_prefix("car", [](StringView key, int& v) { ... });
//     auto m = t.longest_prefix_of("cartography");  // m.length == 4
template <class V>
class RadixTrie {
  struct Node {
    Node* parent;
    Vector<char> label;  // bytes of the edge from the parent; empty for root
    Vector<Node*> kids;  // sorted by first byte of label
    Optional<V> value;

    Node(Node* p, mem::Resource* res) : parent(p), label(res), kids(res) {}

    unsigned char first() const noexcept {
      return static_cast<unsigned char>(label[0]);
    }
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
    friend class RadixTrie;
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

    // `start`'s whole key (its label included) must already be in key_.
    void begin_at(Node* start) {
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
          drop(top.node->label.size());
          stack_.pop_back();
          continue;
        }
        Node* kid = top.node->kids[top.next++];
        key_.append(kid->label.begin(), kid->label.end());
        stack_.push_back({kid, 0});
        if (kid->value) {
          cur_ = kid;
          fix_entry();
          return;
        }
      }
      entry_.reset();
    }

    void drop(size_t n) { key_.erase(key_.end() - n, key_.end()); }

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

  RadixTrie() noexcept : res_(mem::default_resource()) {}

  explicit RadixTrie(mem::Resource* res) noexcept : res_(res) {}

  RadixTrie(std::initializer_list<Pair<StringView, V>> il,
            mem::Resource* res = mem::default_resource())
      : RadixTrie(res) {
    for (const auto& [key, value] : il) {
      insert_or_assign(key, value);
    }
  }

  RadixTrie(const RadixTrie& other) : RadixTrie(other, other.res_) {}

  RadixTrie(const RadixTrie& other, mem::Resource* res) : RadixTrie(res) {
    other.for_each(
        [this](StringView key, const V& value) { try_emplace(key, value); });
  }

  RadixTrie(RadixTrie&& other) noexcept
      : root_(yasl::exchange(other.root_, nullptr)),
        size_(yasl::exchange(other.size_, size_t(0))),
        res_(other.res_) {}

  ~RadixTrie() { clear(); }

  RadixTrie& operator=(const RadixTrie& other) {
    if (this != &other) {
      RadixTrie tmp(other, res_);
      swap(tmp);
    }
    return *this;
  }

  RadixTrie& operator=(RadixTrie&& other) noexcept {
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
      tidy(n);
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
    Node* n = locate(key, true);
    return n && n->value ? &*n->value : nullptr;
  }

  const V* find(StringView key) const noexcept {
    Node* n = locate(key, true);
    return n && n->value ? &*n->value : nullptr;
  }

  bool contains(StringView key) const noexcept { return find(key) != nullptr; }

  bool erase(StringView key) {
    Node* n = locate(key, true);
    if (!n || !n->value) {
      return false;
    }
    n->value.reset();
    --size_;
    tidy(n);
    return true;
  }

  void clear() noexcept {
    // Walks down via kids and back up via parent links: no extra memory needed.
    Node* n = root_;
    while (n) {
      if (!n->kids.empty()) {
        Node* child = n->kids.back();
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
    return size_ != 0 && locate(prefix, false) != nullptr;
  }

  // The longest key that is a prefix of `text`.
  Match longest_prefix_of(StringView text) noexcept {
    auto m = static_cast<const RadixTrie&>(*this).longest_prefix_of(text);
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
    size_t pos = 0;
    while (pos < text.size()) {
      Node* c = child(n, static_cast<unsigned char>(text[pos]));
      if (!c || common(c, text, pos) != c->label.size()) {
        break;
      }
      pos += c->label.size();
      n = c;
      if (n->value) {
        m = {pos, &*n->value};
      }
    }
    return m;
  }

  using iterator = Iter<false>;
  using const_iterator = Iter<true>;

  iterator begin() { return make_iter<iterator>(root_); }

  iterator end() { return iterator(res_); }

  const_iterator begin() const { return make_iter<const_iterator>(root_); }

  const_iterator end() const { return const_iterator(res_); }

  // Iterator at `key`, end() if absent. Continuing from it walks the rest of
  // the trie.
  iterator find_iter(StringView key) { return find_at<iterator>(key); }

  const_iterator find_iter(StringView key) const {
    return find_at<const_iterator>(key);
  }

  // All keys starting with `prefix`, in order.
  Range<iterator> with_prefix(StringView prefix) {
    return {make_iter<iterator>(locate(prefix, false)), end()};
  }

  Range<const_iterator> with_prefix(StringView prefix) const {
    return {make_iter<const_iterator>(locate(prefix, false)), end()};
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

  void swap(RadixTrie& other) noexcept {
    yasl::swap(root_, other.root_);
    yasl::swap(size_, other.size_);
    yasl::swap(res_, other.res_);
  }

  friend void swap(RadixTrie& a, RadixTrie& b) noexcept { a.swap(b); }

 private:
  // Appends the whole key of `n` (root to n) to `out`.
  static void key_of(const Node* n, Vector<char>& out) {
    size_t total = 0;
    for (const Node* p = n; p; p = p->parent) {
      total += p->label.size();
    }
    out.resize(total);
    char* end = out.data() + total;
    for (const Node* p = n; p; p = p->parent) {
      end -= p->label.size();
      for (size_t i = 0; i < p->label.size(); ++i) {
        end[i] = p->label[i];
      }
    }
  }

  template <class It>
  It make_iter(Node* start) const {
    It it(res_);
    if (start) {
      key_of(start, it.key_);
      it.begin_at(start);
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
    size_t pos = 0;
    while (pos < key.size()) {
      Node* c = child(n, static_cast<unsigned char>(key[pos]));
      if (!c || common(c, key, pos) != c->label.size()) {
        return It(res_);
      }
      it.stack_.push_back({n, index_of(n, c) + 1});
      it.key_.append(c->label.begin(), c->label.end());
      pos += c->label.size();
      n = c;
    }
    if (!n->value) {
      return It(res_);
    }
    it.stack_.push_back({n, 0});
    it.cur_ = n;
    it.fix_entry();
    return it;
  }

  // Index of the first kid whose first byte is >= c.
  static size_t lower(const Node* n, unsigned char c) noexcept {
    return static_cast<size_t>(
        yasl::lower_bound(
            n->kids.begin(), n->kids.end(), c,
            [](const Node* k, unsigned char x) { return k->first() < x; }) -
        n->kids.begin());
  }

  static size_t index_of(const Node* n, const Node* c) noexcept {
    return lower(n, c->first());
  }

  static Node* child(const Node* n, unsigned char c) noexcept {
    size_t i = lower(n, c);
    return i < n->kids.size() && n->kids[i]->first() == c ? n->kids[i]
                                                          : nullptr;
  }

  // Length of the common part of c's label and key[pos..].
  static size_t common(const Node* c, StringView key, size_t pos) noexcept {
    size_t max =
        c->label.size() < key.size() - pos ? c->label.size() : key.size() - pos;
    size_t l = 0;
    while (l < max && c->label[l] == key[pos + l]) {
      ++l;
    }
    return l;
  }

  // The node reached by `key`. With `exact` the key must end at the node;
  // otherwise it may end inside the node's edge.
  Node* locate(StringView key, bool exact) const noexcept {
    Node* n = root_;
    size_t pos = 0;
    while (n && pos < key.size()) {
      Node* c = child(n, static_cast<unsigned char>(key[pos]));
      if (!c) {
        return nullptr;
      }
      size_t l = common(c, key, pos);
      if (l < c->label.size() && (exact || pos + l < key.size())) {
        return nullptr;
      }
      pos += l;
      n = c;
    }
    return n;
  }

  Node* make_leaf(Node* parent, const char* begin, const char* end) {
    Node* n = res_->create<Node>(parent, res_);
    try {
      n->label.append(begin, end);
    } catch (...) {
      res_->destroy(n);
      throw;
    }
    return n;
  }

  Node* descend_create(StringView key) {
    if (!root_) {
      root_ = res_->create<Node>(nullptr, res_);
    }
    Node* n = root_;
    size_t pos = 0;
    while (pos < key.size()) {
      unsigned char ch = static_cast<unsigned char>(key[pos]);
      size_t i = lower(n, ch);
      Node* c = i < n->kids.size() && n->kids[i]->first() == ch ? n->kids[i]
                                                                : nullptr;
      if (!c) {
        Node* leaf = make_leaf(n, key.begin() + pos, key.end());
        try {
          n->kids.insert(n->kids.begin() + i, leaf);
        } catch (...) {
          res_->destroy(leaf);
          tidy(n);
          throw;
        }
        return leaf;
      }
      size_t l = common(c, key, pos);
      if (l == c->label.size()) {
        pos += l;
        n = c;
        continue;
      }
      // The key leaves c's edge after l bytes: split it with a middle node.
      Node* mid = make_leaf(n, c->label.begin(), c->label.begin() + l);
      Node* leaf = nullptr;
      try {
        if (pos + l < key.size()) {
          leaf = make_leaf(mid, key.begin() + pos + l, key.end());
        }
        mid->kids.reserve(2);
      } catch (...) {
        if (leaf) {
          res_->destroy(leaf);
        }
        res_->destroy(mid);
        throw;
      }
      // Nothing below throws: shorten c's label (trailing bytes move down).
      c->label.erase(c->label.begin(), c->label.begin() + l);
      c->parent = mid;
      n->kids[i] = mid;
      if (leaf && leaf->first() < c->first()) {
        mid->kids.push_back(leaf);
        mid->kids.push_back(c);
      } else {
        mid->kids.push_back(c);
        if (leaf) {
          mid->kids.push_back(leaf);
        }
      }
      return leaf ? leaf : mid;
    }
    return n;
  }

  // Restores the invariants around `n` after it lost its value or a kid:
  // a valueless leaf is removed, a valueless node with one kid is merged
  // with it. The root stays until the trie is empty.
  void tidy(Node* n) noexcept {
    while (n && n != root_ && !n->value && n->kids.empty()) {
      Node* parent = n->parent;
      parent->kids.erase(parent->kids.begin() + index_of(parent, n));
      res_->destroy(n);
      n = parent;
    }
    if (n && n != root_ && !n->value && n->kids.size() == 1) {
      merge_down(n);
    } else if (n == root_ && size_ == 0 && n->kids.empty()) {
      res_->destroy(root_);
      root_ = nullptr;
    }
  }

  // Replaces `n` by its only kid, which takes over n's label. If the label
  // cannot grow (out of memory) the tree is left uncompressed, still valid.
  void merge_down(Node* n) noexcept {
    Node* c = n->kids[0];
    try {
      c->label.insert(c->label.begin(), n->label.begin(), n->label.end());
    } catch (...) {
      return;
    }
    Node* parent = n->parent;
    parent->kids[index_of(parent, n)] = c;
    c->parent = parent;
    n->kids.clear();
    res_->destroy(n);
  }

  template <class F>
  void walk(StringView prefix, F&& f) const {
    Node* start = locate(prefix, false);
    if (!start) {
      return;
    }
    Vector<Frame> stack(res_);
    Vector<char> key(res_);
    key_of(start, key);
    auto key_view = [&key] { return StringView(key.data(), key.size()); };

    if (start->value) {
      f(key_view(), *start->value);
    }
    stack.push_back({start, 0});
    while (!stack.empty()) {
      Frame& top = stack.back();
      if (top.next == top.node->kids.size()) {
        key.erase(key.end() - top.node->label.size(), key.end());
        stack.pop_back();
        continue;
      }
      Node* kid = top.node->kids[top.next++];
      key.append(kid->label.begin(), kid->label.end());
      if (kid->value) {
        f(key_view(), *kid->value);
      }
      stack.push_back({kid, 0});
    }
  }

  Node* root_ = nullptr;
  size_t size_ = 0;
  mem::Resource* res_;
};
}  // namespace yasl
