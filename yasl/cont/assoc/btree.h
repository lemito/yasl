#pragma once

#include <yasl/core/check.h>
#include <yasl/core/utility.h>
#include <yasl/mem/resource.h>

#include <initializer_list>  // language support for braced lists

namespace yasl {
// Ordered container on a B+ tree: elements live only in the leaves (arrays
// inside the nodes, chained with prev/next pointers), inner nodes hold
// separator keys and subtree sizes. Lookups and iteration are cache-friendly
// and per-element overhead is tiny; walking the whole tree is O(n) without
// climbing back to the parents.
//
// Trade-off vs Treap: elements move between nodes on insert/erase, so ANY
// insertion or erasure invalidates all iterators, pointers and references.
// erase(it) returns a valid iterator to the next element.
//
// Because of the subtree sizes kth / rank / index_of cost O(log n).
// Separators are copies of keys (key_type must be copyable; use TreapMap for
// move-only keys), elements themselves are only ever moved, so values may be
// move-only.
//
// Invariant: separator keys[i - 1] <= every key in kids[i] < keys[i]. Erasing
// the minimum of a leaf leaves its separator as is (it is still a valid lower
// bound).
//
// The engine behind BTreeMap / BTreeSet; KeyOf extracts the key from T.
template <class T, class KeyOf = Identity, class Cmp = Less>
class BTree {
 public:
  using value_type = T;
  using key_type = RemoveCVRef<decltype(KeyOf{}(declval<const T&>()))>;

 protected:
  static_assert(CopyConstructible<key_type>,
                "BTree keeps copies of keys in inner nodes: for move-only keys "
                "use TreapMap");
  static_assert(NothrowMoveConstructible<key_type> &&
                NothrowMoveConstructible<T>);

  // Max elements per leaf; min is half of it.
  static constexpr uint32 kLeafCap =
      sizeof(T) <= 16 ? 32 : (sizeof(T) <= 64 ? 16 : 8);
  static constexpr uint32 kLeafMin = kLeafCap / 2;
  // Max children per inner node (keys = children - 1); min is half of it.
  static constexpr uint32 kFan =
      sizeof(key_type) <= 16 ? 32 : (sizeof(key_type) <= 64 ? 16 : 8);
  static constexpr uint32 kFanMin = kFan / 2;
  static constexpr uint32 kMaxDepth = 64;

  struct Inner;

  struct Node {
    Inner* parent = nullptr;
    uint16 pos = 0;    // index in parent->kids
    uint16 count = 0;  // elements (leaf) or keys (inner; children = count + 1)
    bool leaf;

    explicit Node(bool is_leaf) noexcept : leaf(is_leaf) {}
  };

  struct Leaf : Node {
    Leaf* prev = nullptr;
    Leaf* next = nullptr;
    alignas(T) unsigned char storage[kLeafCap * sizeof(T)];

    Leaf() noexcept : Node(true) {}

    T* slot(size_t i) noexcept {
      return yasl::launder(reinterpret_cast<T*>(storage) + i);
    }
  };

  // One spare key / child slot: a node may briefly hold kFan keys before it is
  // split.
  struct Inner : Node {
    alignas(key_type) unsigned char kstore[kFan * sizeof(key_type)];
    Node* kids[kFan + 1] = {};
    size_t sizes[kFan + 1] = {};

    Inner() noexcept : Node(false) {}

    key_type* key(size_t i) noexcept {
      return yasl::launder(reinterpret_cast<key_type*>(kstore) + i);
    }
  };

  struct Pos {
    Leaf* leaf = nullptr;
    uint32 idx = 0;
  };

  template <bool Const>
  class Iter {
    friend class BTree;
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
    Iter(const Iter<C2>& other) noexcept : p_(other.p_), t_(other.t_) {}

    reference operator*() const noexcept { return *p_.leaf->slot(p_.idx); }

    pointer operator->() const noexcept { return p_.leaf->slot(p_.idx); }

    Iter& operator++() noexcept {
      if (++p_.idx == p_.leaf->count) {
        p_ = {p_.leaf->next, 0};
      }
      return *this;
    }

    Iter operator++(int) noexcept {
      Iter copy = *this;
      ++*this;
      return copy;
    }

    Iter& operator--() noexcept {
      if (!p_.leaf) {
        p_ = t_->last_pos();
      } else if (p_.idx > 0) {
        --p_.idx;
      } else {
        p_.leaf = p_.leaf->prev;
        p_.idx = p_.leaf->count - 1;
      }
      return *this;
    }

    Iter operator--(int) noexcept {
      Iter copy = *this;
      --*this;
      return copy;
    }

    friend bool operator==(const Iter& a, const Iter& b) noexcept {
      return a.p_.leaf == b.p_.leaf && a.p_.idx == b.p_.idx;
    }

   private:
    Iter(Pos p, const BTree* t) noexcept : p_(p), t_(t) {
      if (!p_.leaf) {
        p_.idx = 0;
      }
    }

    Pos p_;
    const BTree* t_ = nullptr;
  };

 public:
  using const_iterator = Iter<true>;
  using iterator =
      Conditional<SameAs<KeyOf, Identity>, Iter<true>, Iter<false>>;

  // [begin(), end()) pair usable in a range-for.
  template <class It>
  class IterRange {
   public:
    IterRange(It b, It e) noexcept : b_(b), e_(e) {}

    It begin() const noexcept { return b_; }

    It end() const noexcept { return e_; }

   private:
    It b_;
    It e_;
  };

  BTree() noexcept : BTree(mem::default_resource()) {}

  explicit BTree(mem::Resource* res, Cmp cmp = Cmp()) noexcept
      : res_(res), cmp_(yasl::move(cmp)) {}

  BTree(std::initializer_list<T> il,
        mem::Resource* res = mem::default_resource(), Cmp cmp = Cmp())
      : BTree(res, yasl::move(cmp)) {
    for (const T& v : il) {
      try_emplace_with(key_of(v), v);
    }
  }

  BTree(const BTree& other) : BTree(other, other.res_) {}

  BTree(const BTree& other, mem::Resource* res) : BTree(res, other.cmp_) {
    Pos src = other.first_pos();
    build(other.size_, [&](T* dst) {
      yasl::construct_at(dst, *src.leaf->slot(src.idx));
      advance(src);
    });
  }

  BTree(BTree&& other) noexcept
      : root_(yasl::exchange(other.root_, nullptr)),
        first_leaf_(yasl::exchange(other.first_leaf_, nullptr)),
        last_leaf_(yasl::exchange(other.last_leaf_, nullptr)),
        size_(yasl::exchange(other.size_, size_t(0))),
        res_(other.res_),
        cmp_(yasl::move(other.cmp_)) {}

  ~BTree() { clear(); }

  BTree& operator=(const BTree& other) {
    if (this != &other) {
      BTree tmp(other, res_);
      swap(tmp);
    }
    return *this;
  }

  BTree& operator=(BTree&& other) noexcept {
    if (this != &other) {
      clear();
      root_ = yasl::exchange(other.root_, nullptr);
      first_leaf_ = yasl::exchange(other.first_leaf_, nullptr);
      last_leaf_ = yasl::exchange(other.last_leaf_, nullptr);
      size_ = yasl::exchange(other.size_, size_t(0));
      res_ = other.res_;
      cmp_ = yasl::move(other.cmp_);
    }
    return *this;
  }

  // Builds a tree in O(n) from a range sorted by key without duplicates;
  // elements are copied.
  template <class It>
  static BTree from_sorted(It first, It last,
                           mem::Resource* res = mem::default_resource(),
                           Cmp cmp = Cmp()) {
    size_t n = 0;
    for (It i = first; i != last; ++i) {
      ++n;
    }
    BTree out(res, yasl::move(cmp));
    out.build(n, [&](T* dst) {
      yasl::construct_at(dst, *first);
      ++first;
    });
    return out;
  }

  size_t size() const noexcept { return size_; }

  bool empty() const noexcept { return size_ == 0; }

  mem::Resource* resource() const noexcept { return res_; }

  iterator begin() noexcept { return {first_pos(), this}; }

  iterator end() noexcept { return {Pos{}, this}; }

  const_iterator begin() const noexcept { return {first_pos(), this}; }

  const_iterator end() const noexcept { return {Pos{}, this}; }

  const T& front() const noexcept {
    YASL_ASSERT(root_);
    return *first_leaf_->slot(0);
  }

  const T& back() const noexcept {
    YASL_ASSERT(root_);
    return *last_leaf_->slot(last_leaf_->count - 1);
  }

  template <class Q>
  iterator find(const Q& key) noexcept {
    return {find_pos(key), this};
  }

  template <class Q>
  const_iterator find(const Q& key) const noexcept {
    return {find_pos(key), this};
  }

  template <class Q>
  bool contains(const Q& key) const noexcept {
    return find_pos(key).leaf != nullptr;
  }

  template <class Q>
  iterator lower_bound(const Q& key) noexcept {
    return {bound_pos(key, false), this};
  }

  template <class Q>
  const_iterator lower_bound(const Q& key) const noexcept {
    return {bound_pos(key, false), this};
  }

  template <class Q>
  iterator upper_bound(const Q& key) noexcept {
    return {bound_pos(key, true), this};
  }

  template <class Q>
  const_iterator upper_bound(const Q& key) const noexcept {
    return {bound_pos(key, true), this};
  }

  // Elements with lo <= key < hi.
  template <class Q1, class Q2>
  IterRange<iterator> range(const Q1& lo, const Q2& hi) noexcept {
    return {lower_bound(lo), lower_bound(hi)};
  }

  template <class Q1, class Q2>
  IterRange<const_iterator> range(const Q1& lo, const Q2& hi) const noexcept {
    return {lower_bound(lo), lower_bound(hi)};
  }

  // i-th element in sorted order (0-based), end() if i >= size(). O(log n).
  iterator kth(size_t i) noexcept { return {kth_pos(i), this}; }

  const_iterator kth(size_t i) const noexcept { return {kth_pos(i), this}; }

  // Number of elements with key < `key`. O(log n).
  template <class Q>
  size_t rank(const Q& key) const noexcept {
    if (!root_) {
      return 0;
    }
    size_t r = 0;
    Node* n = root_;
    while (!n->leaf) {
      Inner* in = inner(n);
      uint32 c = child_index(in, key);
      for (uint32 j = 0; j < c; ++j) {
        r += in->sizes[j];
      }
      n = in->kids[c];
    }
    return r + lower_index(leaf(n), key);
  }

  // Position of an element in sorted order. O(log n).
  size_t index_of(const_iterator it) const noexcept {
    if (!it.p_.leaf) {
      return size_;
    }
    size_t r = it.p_.idx;
    for (Node* n = it.p_.leaf; n->parent; n = n->parent) {
      for (uint32 j = 0; j < n->pos; ++j) {
        r += n->parent->sizes[j];
      }
    }
    return r;
  }

  // If `key` is absent, constructs T(args...) and inserts it.
  template <class Q, class... Args>
  Pair<iterator, bool> try_emplace_with(const Q& key, Args&&... args) {
    if (Pos p = find_pos(key); p.leaf) {
      return {iterator(p, this), false};
    }
    // Build the element first: if that throws, the tree is untouched.
    T tmp(yasl::forward<Args>(args)...);
    Pos p = insert_value(yasl::move(tmp));
    ++size_;
    return {iterator(p, this), true};
  }

  // Removes the element, returns the iterator to the following one.
  iterator erase(const_iterator it) {
    alignas(T) unsigned char buf[sizeof(T)];
    T* removed = reinterpret_cast<T*>(buf);
    yasl::construct_at(removed, yasl::move(*it.p_.leaf->slot(it.p_.idx)));
    erase_at(it.p_);
    --size_;
    // Elements moved around; find the successor by key.
    Pos next = bound_pos(key_of(*removed), true);
    yasl::destroy_at(removed);
    return {next, this};
  }

  template <class Q>
    requires(!ConvertibleTo<const Q&, const_iterator>)
  size_t erase(const Q& key) {
    Pos p = find_pos(key);
    if (!p.leaf) {
      return 0;
    }
    erase_at(p);
    --size_;
    return 1;
  }

  void clear() noexcept {
    if (root_) {
      destroy_subtree(root_);
      root_ = nullptr;
      first_leaf_ = nullptr;
      last_leaf_ = nullptr;
    }
    size_ = 0;
  }

  void swap(BTree& other) noexcept {
    yasl::swap(root_, other.root_);
    yasl::swap(first_leaf_, other.first_leaf_);
    yasl::swap(last_leaf_, other.last_leaf_);
    yasl::swap(size_, other.size_);
    yasl::swap(res_, other.res_);
    yasl::swap(cmp_, other.cmp_);
  }

  // Moves every element with key >= `key` into `rest` (its old contents are
  // dropped). `rest` takes over this tree's resource and comparator. O(n): both
  // halves are rebuilt. If an allocation fails midway, some elements are lost
  // (both trees stay valid).
  template <class Q>
  void split(const Q& key, BTree& rest) {
    YASL_ASSERT(&rest != this);
    rest.clear();
    rest.res_ = res_;
    rest.cmp_ = cmp_;
    size_t left = rank(key);
    BTree head(res_, cmp_);
    Pos src = first_pos();
    auto take = [&](T* dst) {
      yasl::construct_at(dst, yasl::move(*src.leaf->slot(src.idx)));
      advance(src);
    };
    head.build(left, take);
    rest.build(size_ - left, take);
    clear();
    swap(head);
  }

  // Appends every element of `other`, whose keys must all be greater than ours;
  // `other` is left empty. O(n), see split() for the failure behaviour.
  void join(BTree& other) {
    YASL_ASSERT(&other != this);
    if (!other.root_) {
      return;
    }
    YASL_ASSERT(!root_ || cmp_(key_of(back()), key_of(other.front())));
    BTree merged(res_, cmp_);
    Pos a = first_pos();
    Pos b = other.first_pos();
    size_t na = size_;
    merged.build(size_ + other.size_, [&](T* dst) {
      if (na > 0) {
        yasl::construct_at(dst, yasl::move(*a.leaf->slot(a.idx)));
        advance(a);
        --na;
      } else {
        yasl::construct_at(dst, yasl::move(*b.leaf->slot(b.idx)));
        advance(b);
      }
    });
    clear();
    swap(merged);
    other.clear();
  }

  friend bool operator==(const BTree& a, const BTree& b)
    requires EqualityComparable<T>
  {
    if (a.size() != b.size()) {
      return false;
    }
    for (auto i = a.begin(), j = b.begin(); i != a.end(); ++i, ++j) {
      if (!(*i == *j)) {
        return false;
      }
    }
    return true;
  }

  // Checks every structural invariant. Meant for tests and debugging.
  bool validate() const noexcept {
    if (!root_) {
      return size_ == 0 && !first_leaf_ && !last_leaf_;
    }
    if (root_->parent) {
      return false;
    }
    size_t count = 0;
    int leaf_depth = -1;
    Leaf* expect_prev = nullptr;
    Leaf* cursor = first_leaf_;
    if (!validate_node(root_, nullptr, nullptr, 0, leaf_depth, count, cursor,
                       expect_prev)) {
      return false;
    }
    return count == size_ && cursor == nullptr && expect_prev == last_leaf_;
  }

 protected:
  static const key_type& key_of(const T& v) noexcept { return KeyOf{}(v); }

  static const key_type& key_at(Leaf* n, uint32 i) noexcept {
    return KeyOf{}(*n->slot(i));
  }

  static Inner* inner(Node* n) noexcept { return static_cast<Inner*>(n); }

  static Leaf* leaf(Node* n) noexcept { return static_cast<Leaf*>(n); }

  static void set_child(Inner* p, uint32 i, Node* c, size_t size) noexcept {
    p->kids[i] = c;
    p->sizes[i] = size;
    c->parent = p;
    c->pos = uint16(i);
  }

  // Moves *src into the raw slot dst and ends src's lifetime.
  template <class U>
  static void move_slot(U* dst, U* src) noexcept {
    if constexpr (TriviallyCopyable<U>) {
      yasl::mem_copy(static_cast<void*>(dst), static_cast<const void*>(src),
                     sizeof(U));
    } else {
      yasl::construct_at(dst, yasl::move(*src));
      yasl::destroy_at(src);
    }
  }

  // Opens a hole at `at` by shifting [at, count) one slot right.
  static void shift_right(Leaf* n, uint32 at) noexcept {
    for (uint32 j = n->count; j > at; --j) {
      move_slot(n->slot(j), n->slot(j - 1));
    }
  }

  // Closes the hole at `at` by shifting (at, count) one slot left.
  static void shift_left(Leaf* n, uint32 at) noexcept {
    for (uint32 j = at; j + 1 < n->count; ++j) {
      move_slot(n->slot(j), n->slot(j + 1));
    }
  }

  static void advance(Pos& p) noexcept {
    if (++p.idx == p.leaf->count) {
      p = {p.leaf->next, 0};
    }
  }

  Pos first_pos() const noexcept {
    return first_leaf_ ? Pos{first_leaf_, 0} : Pos{};
  }

  Pos last_pos() const noexcept {
    return last_leaf_ ? Pos{last_leaf_, uint32(last_leaf_->count - 1)} : Pos{};
  }

  Pos kth_pos(size_t i) const noexcept {
    if (i >= size_) {
      return {};
    }
    Node* n = root_;
    while (!n->leaf) {
      Inner* in = inner(n);
      uint32 j = 0;
      while (i >= in->sizes[j]) {
        i -= in->sizes[j];
        ++j;
      }
      n = in->kids[j];
    }
    return {leaf(n), uint32(i)};
  }

  // Child to descend into: first i with key < keys[i] (all keys[i - 1] <= key).
  template <class Q>
  uint32 child_index(Inner* in, const Q& key) const noexcept {
    uint32 lo = 0;
    uint32 hi = in->count;
    while (lo < hi) {
      uint32 mid = (lo + hi) / 2;
      if (cmp_(key, *in->key(mid))) {
        hi = mid;
      } else {
        lo = mid + 1;
      }
    }
    return lo;
  }

  template <class Q>
  Leaf* find_leaf(const Q& key) const noexcept {
    Node* n = root_;
    while (!n->leaf) {
      Inner* in = inner(n);
      n = in->kids[child_index(in, key)];
    }
    return leaf(n);
  }

  // First index in n with !(key_at(i) < key).
  template <class Q>
  uint32 lower_index(Leaf* n, const Q& key) const noexcept {
    uint32 lo = 0;
    uint32 hi = n->count;
    while (lo < hi) {
      uint32 mid = (lo + hi) / 2;
      if (cmp_(key_at(n, mid), key)) {
        lo = mid + 1;
      } else {
        hi = mid;
      }
    }
    return lo;
  }

  // First index in n with key < key_at(i).
  template <class Q>
  uint32 upper_index(Leaf* n, const Q& key) const noexcept {
    uint32 lo = 0;
    uint32 hi = n->count;
    while (lo < hi) {
      uint32 mid = (lo + hi) / 2;
      if (cmp_(key, key_at(n, mid))) {
        hi = mid;
      } else {
        lo = mid + 1;
      }
    }
    return lo;
  }

  template <class Q>
  Pos find_pos(const Q& key) const noexcept {
    if (!root_) {
      return {};
    }
    Leaf* l = find_leaf(key);
    uint32 i = lower_index(l, key);
    if (i < l->count && !cmp_(key, key_at(l, i))) {
      return {l, i};
    }
    return {};
  }

  // First element with key >= `key` (or > `key` if `upper`).
  template <class Q>
  Pos bound_pos(const Q& key, bool upper) const noexcept {
    if (!root_) {
      return {};
    }
    Leaf* l = find_leaf(key);
    uint32 i = upper ? upper_index(l, key) : lower_index(l, key);
    if (i == l->count) {
      return l->next ? Pos{l->next, 0} : Pos{};
    }
    return {l, i};
  }

  Leaf* new_leaf() { return res_->template create<Leaf>(); }

  Inner* new_inner() { return res_->template create<Inner>(); }

  void free_leaf(Leaf* l) noexcept {
    yasl::destroy_n(l->slot(0), l->count);
    res_->destroy(l);
  }

  void free_inner(Inner* in) noexcept {
    for (uint32 j = 0; j < in->count; ++j) {
      yasl::destroy_at(in->key(j));
    }
    res_->destroy(in);
  }

  void free_node(Node* n) noexcept {
    if (n->leaf) {
      free_leaf(leaf(n));
    } else {
      free_inner(inner(n));
    }
  }

  void destroy_subtree(Node* n) noexcept {
    if (!n->leaf) {
      for (uint32 j = 0; j <= n->count; ++j) {
        if (Node* kid = inner(n)->kids[j]) {
          destroy_subtree(kid);
        }
      }
    }
    free_node(n);
  }

  // Adds `delta` to the subtree sizes on the path from n to the root.
  static void bump(Node* n, ptrdiff_t delta) noexcept {
    for (; n->parent; n = n->parent) {
      n->parent->sizes[n->pos] += size_t(delta);
    }
  }

  // Inserts `right` as child j + 1 of p (new separator `sep`), child j now
  // holds cur_size elements. p may end up with kFan keys.
  static void insert_after(Inner* p, uint32 j, key_type&& sep, Node* right,
                           size_t cur_size, size_t right_size) noexcept {
    for (uint32 i = p->count; i > j; --i) {
      move_slot(p->key(i), p->key(i - 1));
    }
    for (uint32 i = p->count + 1; i > j + 1; --i) {
      set_child(p, i, p->kids[i - 1], p->sizes[i - 1]);
    }
    yasl::construct_at(p->key(j), yasl::move(sep));
    set_child(p, j + 1, right, right_size);
    p->sizes[j] = cur_size;
    ++p->count;
  }

  // Inserts a value whose key is known to be absent.
  Pos insert_value(T&& v) {
    const key_type& key = key_of(v);
    if (!root_) {
      Leaf* l = new_leaf();
      yasl::construct_at(l->slot(0), yasl::move(v));
      l->count = 1;
      root_ = first_leaf_ = last_leaf_ = l;
      return {l, 0};
    }
    Leaf* l = find_leaf(key);
    uint32 idx = lower_index(l, key);
    if (l->count < kLeafCap) {
      shift_right(l, idx);
      yasl::construct_at(l->slot(idx), yasl::move(v));
      ++l->count;
      bump(l, 1);
      return {l, idx};
    }

    // The leaf is full: split it, and the full ancestors above it. Everything
    // that can throw (the separator copy, the allocations) happens before the
    // first change.
    constexpr uint32 h = kLeafCap / 2;
    key_type sep(key_at(l, h));
    uint32 inner_splits = 0;
    Inner* top = l->parent;
    while (top && top->count == kFan - 1) {
      ++inner_splits;
      top = top->parent;
    }
    bool new_root = top == nullptr;
    YASL_ASSERT(inner_splits + 2 < kMaxDepth);
    Node* fresh[kMaxDepth];
    uint32 made = 0;
    try {
      fresh[made++] = new_leaf();
      for (uint32 i = 0; i < inner_splits + (new_root ? 1 : 0); ++i) {
        fresh[made++] = new_inner();
      }
    } catch (...) {
      while (made > 0) {
        free_node(fresh[--made]);
      }
      throw;
    }
    uint32 next_fresh = 0;

    bump(l, 1);
    Leaf* r = leaf(fresh[next_fresh++]);
    for (uint32 i = h; i < kLeafCap; ++i) {
      move_slot(r->slot(i - h), l->slot(i));
    }
    r->count = uint16(kLeafCap - h);
    l->count = uint16(h);
    Pos result;
    if (idx <= h) {
      shift_right(l, idx);
      yasl::construct_at(l->slot(idx), yasl::move(v));
      ++l->count;
      result = {l, idx};
    } else {
      shift_right(r, idx - h);
      yasl::construct_at(r->slot(idx - h), yasl::move(v));
      ++r->count;
      result = {r, idx - h};
    }
    r->prev = l;
    r->next = l->next;
    if (l->next) {
      l->next->prev = r;
    } else {
      last_leaf_ = r;
    }
    l->next = r;

    Node* cur = l;
    Node* right = r;
    size_t cur_size = l->count;
    size_t right_size = r->count;
    for (;;) {
      Inner* p = cur->parent;
      if (!p) {
        Inner* nr = inner(fresh[next_fresh++]);
        set_child(nr, 0, cur, cur_size);
        set_child(nr, 1, right, right_size);
        yasl::construct_at(nr->key(0), yasl::move(sep));
        nr->count = 1;
        root_ = nr;
        break;
      }
      insert_after(p, cur->pos, yasl::move(sep), right, cur_size, right_size);
      if (p->count < kFan) {
        break;
      }
      // p has kFan keys and kFan + 1 children: the first m stay, the key m - 1
      // moves up.
      constexpr uint32 m = (kFan + 1) / 2;
      Inner* q = inner(fresh[next_fresh++]);
      for (uint32 i = 0; i < kFan - m; ++i) {
        move_slot(q->key(i), p->key(m + i));
      }
      right_size = 0;
      for (uint32 i = 0; i <= kFan - m; ++i) {
        set_child(q, i, p->kids[m + i], p->sizes[m + i]);
        right_size += p->sizes[m + i];
      }
      q->count = uint16(kFan - m);
      cur_size = 0;
      for (uint32 i = 0; i < m; ++i) {
        cur_size += p->sizes[i];
      }
      sep = key_type(yasl::move(*p->key(m - 1)));
      yasl::destroy_at(p->key(m - 1));
      p->count = uint16(m - 1);
      cur = p;
      right = q;
    }
    return result;
  }

  // Removes the element at p; nodes are rebalanced, the successor is not
  // tracked.
  void erase_at(Pos p) noexcept {
    Leaf* l = p.leaf;
    yasl::destroy_at(l->slot(p.idx));
    shift_left(l, p.idx);
    --l->count;
    bump(l, -1);
    if (l == root_) {
      if (l->count == 0) {
        free_leaf(l);
        root_ = first_leaf_ = last_leaf_ = nullptr;
      }
      return;
    }
    if (l->count >= kLeafMin) {
      return;
    }
    rebalance_leaf(l);
  }

  void rebalance_leaf(Leaf* l) noexcept {
    Inner* p = l->parent;
    uint32 pos = l->pos;
    Leaf* left = pos > 0 ? leaf(p->kids[pos - 1]) : nullptr;
    Leaf* right = pos < p->count ? leaf(p->kids[pos + 1]) : nullptr;
    bool done = false;
    if (left && left->count > kLeafMin) {
      done = borrow_from_left(p, pos, l, left);
    } else if (right && right->count > kLeafMin) {
      done = borrow_from_right(p, pos, l, right);
    } else {
      merge_leaves(p, left ? pos - 1 : pos);
      rebalance_inner(p);
      return;
    }
    // The new separator could not be copied (out of memory): the leaf stays
    // underfull, which is harmless unless it is empty.
    if (!done && l->count == 0) {
      merge_leaves(p, left ? pos - 1 : pos);
      rebalance_inner(p);
    }
  }

  bool borrow_from_left(Inner* p, uint32 pos, Leaf* l, Leaf* left) noexcept {
    alignas(key_type) unsigned char buf[sizeof(key_type)];
    key_type* sep;
    try {
      sep = yasl::construct_at(reinterpret_cast<key_type*>(buf),
                               key_at(left, left->count - 1));
    } catch (...) {
      return false;
    }
    shift_right(l, 0);
    move_slot(l->slot(0), left->slot(left->count - 1));
    --left->count;
    ++l->count;
    --p->sizes[pos - 1];
    ++p->sizes[pos];
    yasl::destroy_at(p->key(pos - 1));
    move_slot(p->key(pos - 1), sep);
    return true;
  }

  bool borrow_from_right(Inner* p, uint32 pos, Leaf* l, Leaf* right) noexcept {
    alignas(key_type) unsigned char buf[sizeof(key_type)];
    key_type* sep;
    try {
      sep = yasl::construct_at(reinterpret_cast<key_type*>(buf),
                               key_at(right, 1));
    } catch (...) {
      return false;
    }
    move_slot(l->slot(l->count), right->slot(0));
    shift_left(right, 0);
    --right->count;
    ++l->count;
    ++p->sizes[pos];
    --p->sizes[pos + 1];
    yasl::destroy_at(p->key(pos));
    move_slot(p->key(pos), sep);
    return true;
  }

  // Removes child s + 1 of p together with separator s; child s absorbs its
  // size. `key_live`: whether key s still holds an object.
  static void remove_child_after(Inner* p, uint32 s, bool key_live) noexcept {
    if (key_live) {
      yasl::destroy_at(p->key(s));
    }
    p->sizes[s] += p->sizes[s + 1];
    for (uint32 i = s; i + 1 < p->count; ++i) {
      move_slot(p->key(i), p->key(i + 1));
    }
    for (uint32 i = s + 1; i < p->count; ++i) {
      set_child(p, i, p->kids[i + 1], p->sizes[i + 1]);
    }
    --p->count;
  }

  // Merges leaf s + 1 of p into leaf s (their elements must fit into one leaf).
  void merge_leaves(Inner* p, uint32 s) noexcept {
    Leaf* a = leaf(p->kids[s]);
    Leaf* b = leaf(p->kids[s + 1]);
    for (uint32 j = 0; j < b->count; ++j) {
      move_slot(a->slot(a->count + j), b->slot(j));
    }
    a->count = uint16(a->count + b->count);
    b->count = 0;
    a->next = b->next;
    if (b->next) {
      b->next->prev = a;
    } else {
      last_leaf_ = a;
    }
    remove_child_after(p, s, true);
    res_->destroy(b);
  }

  void rebalance_inner(Inner* n) noexcept {
    for (;;) {
      if (n == root_) {
        if (n->count == 0) {
          root_ = n->kids[0];
          root_->parent = nullptr;
          root_->pos = 0;
          free_inner(n);
        }
        return;
      }
      if (n->count + 1u >= kFanMin) {
        return;
      }
      Inner* p = n->parent;
      uint32 pos = n->pos;
      Inner* left = pos > 0 ? inner(p->kids[pos - 1]) : nullptr;
      Inner* right = pos < p->count ? inner(p->kids[pos + 1]) : nullptr;
      if (left && left->count + 1u > kFanMin) {
        rotate_right(p, pos - 1);
        return;
      }
      if (right && right->count + 1u > kFanMin) {
        rotate_left(p, pos);
        return;
      }
      merge_inner(p, left ? pos - 1 : pos);
      n = p;
    }
  }

  // Moves the last child of kids[s] through separator s to the front of kids[s
  // + 1].
  void rotate_right(Inner* p, uint32 s) noexcept {
    Inner* a = inner(p->kids[s]);
    Inner* b = inner(p->kids[s + 1]);
    for (uint32 i = b->count; i > 0; --i) {
      move_slot(b->key(i), b->key(i - 1));
    }
    for (uint32 i = b->count + 1; i > 0; --i) {
      set_child(b, i, b->kids[i - 1], b->sizes[i - 1]);
    }
    move_slot(b->key(0), p->key(s));
    move_slot(p->key(s), a->key(a->count - 1));
    Node* moved = a->kids[a->count];
    size_t moved_size = a->sizes[a->count];
    set_child(b, 0, moved, moved_size);
    --a->count;
    ++b->count;
    p->sizes[s] -= moved_size;
    p->sizes[s + 1] += moved_size;
  }

  // Moves the first child of kids[s + 1] through separator s to the end of
  // kids[s].
  void rotate_left(Inner* p, uint32 s) noexcept {
    Inner* a = inner(p->kids[s]);
    Inner* b = inner(p->kids[s + 1]);
    move_slot(a->key(a->count), p->key(s));
    move_slot(p->key(s), b->key(0));
    Node* moved = b->kids[0];
    size_t moved_size = b->sizes[0];
    set_child(a, a->count + 1u, moved, moved_size);
    for (uint32 i = 0; i + 1 < b->count; ++i) {
      move_slot(b->key(i), b->key(i + 1));
    }
    for (uint32 i = 0; i < b->count; ++i) {
      set_child(b, i, b->kids[i + 1], b->sizes[i + 1]);
    }
    ++a->count;
    --b->count;
    p->sizes[s] += moved_size;
    p->sizes[s + 1] -= moved_size;
  }

  // Merges kids[s], separator s and kids[s + 1] into kids[s].
  void merge_inner(Inner* p, uint32 s) noexcept {
    Inner* a = inner(p->kids[s]);
    Inner* b = inner(p->kids[s + 1]);
    uint32 base = a->count;
    move_slot(a->key(base), p->key(s));
    for (uint32 j = 0; j < b->count; ++j) {
      move_slot(a->key(base + 1 + j), b->key(j));
    }
    for (uint32 j = 0; j <= b->count; ++j) {
      set_child(a, base + 1 + j, b->kids[j], b->sizes[j]);
    }
    a->count = uint16(base + 1 + b->count);
    b->count = 0;
    remove_child_after(p, s, false);
    res_->destroy(b);
  }

  // Fills the empty tree bottom-up with n elements produced in order by put(T*
  // raw_slot): leaves are packed evenly, every level is built from the one
  // below. O(n). On a throw everything built so far is freed and the tree stays
  // empty.
  template <class Put>
  void build(size_t n, Put&& put) {
    YASL_ASSERT(!root_);
    if (n == 0) {
      return;
    }
    size_t count = (n + kLeafCap - 1) / kLeafCap;
    Node** nodes = nullptr;
    size_t* sizes = nullptr;
    size_t consumed =
        0;  // nodes[consumed, count) are not yet attached to a parent
    size_t built = 0;  // nodes[0, built) are finished parents of the next level
    Node* partial = nullptr;
    nodes = res_->template allocate_array<Node*>(count);
    try {
      sizes = res_->template allocate_array<size_t>(count);
    } catch (...) {
      res_->deallocate_array(nodes, count);
      throw;
    }
    size_t alloc_count = count;
    Leaf* prev = nullptr;
    bool leaves = true;
    try {
      size_t base = n / count;
      size_t extra = n % count;
      for (size_t i = 0; i < count; ++i) {
        Leaf* l = new_leaf();
        partial = l;
        size_t c = base + (i < extra ? 1 : 0);
        for (size_t k = 0; k < c; ++k) {
          put(l->slot(k));
          ++l->count;
        }
        l->prev = prev;
        if (prev) {
          prev->next = l;
        }
        prev = l;
        nodes[i] = l;
        sizes[i] = c;
        partial = nullptr;
        built = i + 1;
      }
      first_leaf_ = leaf(nodes[0]);
      last_leaf_ = leaf(nodes[count - 1]);
      leaves = false;
      while (count > 1) {
        size_t groups = (count + kFan - 1) / kFan;
        size_t gbase = count / groups;
        size_t gextra = count % groups;
        size_t at = 0;
        consumed = 0;
        built = 0;
        for (size_t g = 0; g < groups; ++g) {
          size_t kids = gbase + (g < gextra ? 1 : 0);
          Inner* in = new_inner();
          partial = in;
          size_t total = 0;
          for (size_t j = 0; j < kids; ++j) {
            if (j > 0) {
              yasl::construct_at(in->key(j - 1),
                                 key_of(*leftmost(nodes[at + j])->slot(0)));
              ++in->count;
            }
            set_child(in, uint32(j), nodes[at + j], sizes[at + j]);
            total += sizes[at + j];
          }
          at += kids;
          consumed = at;
          nodes[g] = in;
          sizes[g] = total;
          partial = nullptr;
          built = g + 1;
        }
        count = groups;
        consumed = 0;
      }
    } catch (...) {
      if (leaves) {
        for (size_t i = 0; i < built; ++i) {
          free_leaf(leaf(nodes[i]));
        }
        if (partial) {
          free_leaf(leaf(partial));
        }
      } else {
        for (size_t i = 0; i < built; ++i) {
          destroy_subtree(nodes[i]);
        }
        if (partial) {
          destroy_subtree(partial);
        }
        for (size_t i = consumed; i < count; ++i) {
          destroy_subtree(nodes[i]);
        }
      }
      first_leaf_ = last_leaf_ = nullptr;
      res_->deallocate_array(nodes, alloc_count);
      res_->deallocate_array(sizes, alloc_count);
      throw;
    }
    root_ = nodes[0];
    size_ = n;
    res_->deallocate_array(nodes, alloc_count);
    res_->deallocate_array(sizes, alloc_count);
  }

  static Leaf* leftmost(Node* n) noexcept {
    while (!n->leaf) {
      n = inner(n)->kids[0];
    }
    return leaf(n);
  }

  bool validate_node(Node* n, const key_type* lo, const key_type* hi, int depth,
                     int& leaf_depth, size_t& count, Leaf*& cursor,
                     Leaf*& prev_leaf) const noexcept {
    if (n->leaf) {
      Leaf* l = leaf(n);
      if (l != cursor || l->prev != prev_leaf) {
        return false;
      }
      if (l->count == 0 || l->count > kLeafCap ||
          (l != root_ && l->count < kLeafMin)) {
        return false;
      }
      for (uint32 j = 0; j < l->count; ++j) {
        const key_type& k = key_at(l, j);
        if (j > 0 && !cmp_(key_at(l, j - 1), k)) {
          return false;
        }
        if ((lo && cmp_(k, *lo)) || (hi && !cmp_(k, *hi))) {
          return false;
        }
      }
      count += l->count;
      prev_leaf = l;
      cursor = l->next;
      if (leaf_depth < 0) {
        leaf_depth = depth;
      }
      return leaf_depth == depth;
    }
    Inner* in = inner(n);
    if (in->count + 1u > kFan ||
        (in == root_ ? in->count < 1u : in->count + 1u < kFanMin)) {
      return false;
    }
    for (uint32 j = 0; j < in->count; ++j) {
      if (j > 0 && !cmp_(*in->key(j - 1), *in->key(j))) {
        return false;
      }
    }
    for (uint32 j = 0; j <= in->count; ++j) {
      Node* kid = in->kids[j];
      if (!kid || kid->parent != in || kid->pos != j) {
        return false;
      }
      const key_type* klo = j > 0 ? in->key(j - 1) : lo;
      const key_type* khi = j < in->count ? in->key(j) : hi;
      size_t before = count;
      if (!validate_node(kid, klo, khi, depth + 1, leaf_depth, count, cursor,
                         prev_leaf)) {
        return false;
      }
      if (in->sizes[j] != count - before) {
        return false;
      }
    }
    return true;
  }

  Node* root_ = nullptr;
  Leaf* first_leaf_ = nullptr;
  Leaf* last_leaf_ = nullptr;
  size_t size_ = 0;
  mem::Resource* res_;
  [[no_unique_address]] Cmp cmp_;
};
}  // namespace yasl
