#pragma once

#include <yasl/core/check.h>
#include <yasl/core/hash.h>
#include <yasl/core/utility.h>
#include <yasl/mem/resource.h>
#include <yasl/rand/random.h>

#include <initializer_list>  // language support for braced lists

#include "map.h"

namespace yasl {
namespace detail {
static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__,
              "HashTable groups assume little-endian");

// Control bytes: 0..0x7F = slot is full (low 7 bits of the hash), kEmpty,
// kDeleted.
inline constexpr uint8 kEmpty = 0xFF;
inline constexpr uint8 kDeleted = 0x80;
inline constexpr uint64 kLo = 0x0101010101010101ull;
inline constexpr uint64 kHi = 0x8080808080808080ull;
inline constexpr size_t kGroup = 8;

inline uint64 load_group(const uint8* p) noexcept {
  uint64 g;
  yasl::mem_copy(&g, p, sizeof(g));
  return g;
}

// Bytes equal to b (may report a false positive above a real match; callers
// compare keys).
inline uint64 match_byte(uint64 g, uint8 b) noexcept {
  uint64 x = g ^ (kLo * b);
  return (x - kLo) & ~x & kHi;
}

inline uint64 match_empty(uint64 g) noexcept { return g & (g << 1) & kHi; }

inline uint64 match_free(uint64 g) noexcept { return g & kHi; }

inline uint64 match_full(uint64 g) noexcept { return ~g & kHi; }

inline size_t first_byte(uint64 mask) noexcept {
  return size_t(__builtin_ctzll(mask)) / 8;
}

constexpr size_t max_items(size_t cap) noexcept { return cap - cap / 8; }
}  // namespace detail

// Hash table with open addressing in the style of Swiss tables: one control
// byte per slot (7 bits of the hash or empty/deleted) probed eight at a time
// with plain 64-bit arithmetic, triangular probing, 7/8 maximum load. Slots and
// control bytes share one allocation.
//
// Iterators, pointers and references are invalidated by any insertion that
// rehashes; erasing never moves other elements. Iteration order is unspecified.
//
// The hash is mixed with a per-process random seed. That defeats collision
// floods aimed at the bucket index, not full 64-bit collisions of the hasher
// itself.
//
// The engine behind HashMap / HashSet; KeyOf extracts the key from T.
template <class T, class KeyOf = Identity, class Hasher = HashAny,
          class Eq = EqualTo>
class HashTable {
  static_assert(NothrowMoveConstructible<T>);

  static constexpr size_t kNpos = ~size_t(0);

  template <bool Const>
  class Iter {
    friend class HashTable;
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
    Iter(const Iter<C2>& other) noexcept : t_(other.t_), i_(other.i_) {}

    reference operator*() const noexcept { return *t_->slot(i_); }

    pointer operator->() const noexcept { return t_->slot(i_); }

    Iter& operator++() noexcept {
      i_ = t_->next_full(i_ + 1);
      return *this;
    }

    Iter operator++(int) noexcept {
      Iter copy = *this;
      ++*this;
      return copy;
    }

    friend bool operator==(const Iter& a, const Iter& b) noexcept {
      return a.i_ == b.i_;
    }

   private:
    Iter(const HashTable* t, size_t i) noexcept : t_(t), i_(i) {}

    const HashTable* t_ = nullptr;
    size_t i_ = 0;
  };

 public:
  using value_type = T;
  using key_type = RemoveCVRef<decltype(KeyOf{}(declval<const T&>()))>;
  using const_iterator = Iter<true>;
  using iterator =
      Conditional<SameAs<KeyOf, Identity>, Iter<true>, Iter<false>>;

  HashTable() noexcept : HashTable(mem::default_resource()) {}

  explicit HashTable(mem::Resource* res, Hasher hash = Hasher(),
                     Eq eq = Eq()) noexcept
      : res_(res), hash_(yasl::move(hash)), eq_(yasl::move(eq)) {}

  HashTable(std::initializer_list<T> il,
            mem::Resource* res = mem::default_resource())
      : HashTable(res) {
    reserve(il.size());
    for (const T& v : il) {
      try_emplace_with(key_of(v), v);
    }
  }

  HashTable(const HashTable& other) : HashTable(other, other.res_) {}

  HashTable(const HashTable& other, mem::Resource* res)
      : HashTable(res, other.hash_, other.eq_) {
    if (other.size_ == 0) {
      return;
    }
    allocate_table(other.cap_);
    // Same slot layout and control bytes (tombstones included), so probe chains
    // stay intact.
    size_t i = 0;
    try {
      for (; i < other.cap_; ++i) {
        if (other.ctrl_[i] < detail::kDeleted) {
          yasl::construct_at(slot(i), *other.slot(i));
        }
      }
    } catch (...) {
      for (size_t j = 0; j < i; ++j) {
        if (other.ctrl_[j] < detail::kDeleted) {
          yasl::destroy_at(slot(j));
        }
      }
      free_table();
      throw;
    }
    yasl::mem_copy(ctrl_, other.ctrl_, cap_ + detail::kGroup);
    size_ = other.size_;
    growth_left_ = other.growth_left_;
  }

  HashTable(HashTable&& other) noexcept
      : slots_(yasl::exchange(other.slots_, nullptr)),
        ctrl_(yasl::exchange(other.ctrl_, nullptr)),
        cap_(yasl::exchange(other.cap_, size_t(0))),
        size_(yasl::exchange(other.size_, size_t(0))),
        growth_left_(yasl::exchange(other.growth_left_, size_t(0))),
        res_(other.res_),
        hash_(yasl::move(other.hash_)),
        eq_(yasl::move(other.eq_)) {}

  ~HashTable() {
    destroy_all();
    free_table();
  }

  HashTable& operator=(const HashTable& other) {
    if (this != &other) {
      HashTable tmp(other, res_);
      swap(tmp);
    }
    return *this;
  }

  HashTable& operator=(HashTable&& other) noexcept {
    if (this != &other) {
      destroy_all();
      free_table();
      slots_ = yasl::exchange(other.slots_, nullptr);
      ctrl_ = yasl::exchange(other.ctrl_, nullptr);
      cap_ = yasl::exchange(other.cap_, size_t(0));
      size_ = yasl::exchange(other.size_, size_t(0));
      growth_left_ = yasl::exchange(other.growth_left_, size_t(0));
      res_ = other.res_;
      hash_ = yasl::move(other.hash_);
      eq_ = yasl::move(other.eq_);
    }
    return *this;
  }

  size_t size() const noexcept { return size_; }

  bool empty() const noexcept { return size_ == 0; }

  mem::Resource* resource() const noexcept { return res_; }

  // Number of slots (a power of two, or 0 before the first insertion).
  size_t bucket_count() const noexcept { return cap_; }

  double load_factor() const noexcept {
    return cap_ ? double(size_) / double(cap_) : 0.0;
  }

  iterator begin() noexcept { return {this, next_full(0)}; }

  iterator end() noexcept { return {this, cap_}; }

  const_iterator begin() const noexcept { return {this, next_full(0)}; }

  const_iterator end() const noexcept { return {this, cap_}; }

  template <class Q>
  iterator find(const Q& key) noexcept {
    size_t i = find_index(key, hash_of(key));
    return {this, i == kNpos ? cap_ : i};
  }

  template <class Q>
  const_iterator find(const Q& key) const noexcept {
    size_t i = find_index(key, hash_of(key));
    return {this, i == kNpos ? cap_ : i};
  }

  template <class Q>
  bool contains(const Q& key) const noexcept {
    return find_index(key, hash_of(key)) != kNpos;
  }

  // Makes room for n elements without rehashing.
  void reserve(size_t n) {
    if (n <= size_ + growth_left_) {
      return;
    }
    size_t cap = detail::kGroup;
    while (detail::max_items(cap) < n) {
      if (cap > max_value<size_t>() / 2 / (sizeof(T) + 1)) {
        throw LengthError("yasl::HashTable: too large");
      }
      cap *= 2;
    }
    rehash(cap);
  }

  // If `key` is absent, constructs T(args...) and inserts it.
  template <class Q, class... Args>
  Pair<iterator, bool> try_emplace_with(const Q& key, Args&&... args) {
    uint64 h = hash_of(key);
    if (size_t i = find_index(key, h); i != kNpos) {
      return {iterator(this, i), false};
    }
    // Build the element first: if that throws, the table is untouched
    // (and args may refer to elements that a rehash would move).
    T tmp(yasl::forward<Args>(args)...);
    size_t i = prepare_insert(h);
    yasl::construct_at(slot(i), yasl::move(tmp));
    ++size_;
    return {iterator(this, i), true};
  }

  // Removes the element, returns the iterator to the next one.
  iterator erase(const_iterator it) noexcept {
    erase_index(it.i_);
    return {this, next_full(it.i_ + 1)};
  }

  template <class Q>
    requires(!ConvertibleTo<const Q&, const_iterator>)
  size_t erase(const Q& key) {
    size_t i = find_index(key, hash_of(key));
    if (i == kNpos) {
      return 0;
    }
    erase_index(i);
    return 1;
  }

  // Erases every element for which pred(element) holds; returns how many.
  template <class Pred>
  size_t erase_if(Pred pred) {
    size_t removed = 0;
    for (size_t i = next_full(0); i < cap_; i = next_full(i + 1)) {
      if (pred(*slot(i))) {
        erase_index(i);
        ++removed;
      }
    }
    return removed;
  }

  // Keeps only the elements for which pred(element) holds.
  template <class Pred>
  void retain(Pred pred) {
    erase_if([&](T& v) { return !pred(v); });
  }

  // Destroys the elements, keeps the memory.
  void clear() noexcept {
    destroy_all();
    if (cap_) {
      yasl::mem_set(ctrl_, detail::kEmpty, cap_ + detail::kGroup);
      growth_left_ = detail::max_items(cap_);
    }
    size_ = 0;
  }

  void swap(HashTable& other) noexcept {
    yasl::swap(slots_, other.slots_);
    yasl::swap(ctrl_, other.ctrl_);
    yasl::swap(cap_, other.cap_);
    yasl::swap(size_, other.size_);
    yasl::swap(growth_left_, other.growth_left_);
    yasl::swap(res_, other.res_);
    yasl::swap(hash_, other.hash_);
    yasl::swap(eq_, other.eq_);
  }

  // Same elements, regardless of order.
  friend bool operator==(const HashTable& a, const HashTable& b)
    requires EqualityComparable<T>
  {
    if (a.size_ != b.size_) {
      return false;
    }
    for (const T& v : a) {
      auto it = b.find(key_of(v));
      if (it == b.end() || !(*it == v)) {
        return false;
      }
    }
    return true;
  }

  // Checks every structural invariant. Meant for tests and debugging.
  bool validate() const noexcept {
    if (cap_ == 0) {
      return size_ == 0 && !ctrl_;
    }
    if ((cap_ & (cap_ - 1)) != 0 || cap_ < detail::kGroup) {
      return false;
    }
    size_t full = 0;
    size_t deleted = 0;
    for (size_t i = 0; i < cap_ + detail::kGroup; ++i) {
      size_t home = i < cap_ ? i : i - cap_;
      if (ctrl_[i] != ctrl_[home]) {
        return false;
      }
    }
    for (size_t i = 0; i < cap_; ++i) {
      uint8 c = ctrl_[i];
      if (c == detail::kDeleted) {
        ++deleted;
      } else if (c != detail::kEmpty) {
        if (c > 0x7F) {
          return false;
        }
        ++full;
        uint64 h = hash_of(key_of(*slot(i)));
        if (uint8(h >> 57) != c || find_index(key_of(*slot(i)), h) != i) {
          return false;
        }
      }
    }
    return full == size_ &&
           growth_left_ == detail::max_items(cap_) - full - deleted;
  }

 protected:
  static const key_type& key_of(const T& v) noexcept { return KeyOf{}(v); }

  T* slot(size_t i) const noexcept {
    return yasl::launder(reinterpret_cast<T*>(slots_) + i);
  }

  template <class Q>
  uint64 hash_of(const Q& key) const noexcept {
    return mix64(uint64(hash_(key)) ^ process_seed());
  }

  void set_ctrl(size_t i, uint8 c) noexcept {
    ctrl_[i] = c;
    // The first group is mirrored after the end so that unaligned group loads
    // wrap around.
    ctrl_[((i - detail::kGroup) & (cap_ - 1)) + detail::kGroup] = c;
  }

  // First full slot at index >= i, or cap_.
  size_t next_full(size_t i) const noexcept {
    while (i < cap_) {
      uint64 m = detail::match_full(detail::load_group(ctrl_ + i));
      if (m) {
        size_t r = i + detail::first_byte(m);
        return r < cap_ ? r : cap_;
      }
      i += detail::kGroup;
    }
    return cap_;
  }

  template <class Q>
  size_t find_index(const Q& key, uint64 h) const noexcept {
    if (cap_ == 0) {
      return kNpos;
    }
    const size_t mask = cap_ - 1;
    const uint8 h2 = uint8(h >> 57);
    size_t pos = size_t(h) & mask;
    size_t stride = 0;
    for (;;) {
      uint64 g = detail::load_group(ctrl_ + pos);
      for (uint64 m = detail::match_byte(g, h2); m; m &= m - 1) {
        size_t i = (pos + detail::first_byte(m)) & mask;
        if (eq_(key_of(*slot(i)), key)) {
          return i;
        }
      }
      if (detail::match_empty(g)) {
        return kNpos;
      }
      stride += detail::kGroup;
      pos = (pos + stride) & mask;
    }
  }

  // First empty or deleted slot on the probe sequence of h.
  size_t find_free(uint64 h) const noexcept {
    const size_t mask = cap_ - 1;
    size_t pos = size_t(h) & mask;
    size_t stride = 0;
    for (;;) {
      uint64 m = detail::match_free(detail::load_group(ctrl_ + pos));
      if (m) {
        return (pos + detail::first_byte(m)) & mask;
      }
      stride += detail::kGroup;
      pos = (pos + stride) & mask;
    }
  }

  // Picks (and marks as full) the slot for a new element with hash h, growing
  // if needed.
  size_t prepare_insert(uint64 h) {
    if (cap_ == 0) {
      rehash(detail::kGroup);
    }
    size_t i = find_free(h);
    if (ctrl_[i] == detail::kEmpty && growth_left_ == 0) {
      // Out of room: drop tombstones if they are most of the load, else double.
      rehash(size_ * 2 >= detail::max_items(cap_) ? cap_ * 2 : cap_);
      i = find_free(h);
    }
    if (ctrl_[i] == detail::kEmpty) {
      --growth_left_;
    }
    set_ctrl(i, uint8(h >> 57));
    return i;
  }

  void erase_index(size_t i) noexcept {
    yasl::destroy_at(slot(i));
    --size_;
    // A probe sequence can only have skipped this slot if a whole group around
    // it is occupied; then it must stay a tombstone.
    uint64 before = detail::match_empty(
        detail::load_group(ctrl_ + ((i - detail::kGroup) & (cap_ - 1))));
    uint64 after = detail::match_empty(detail::load_group(ctrl_ + i));
    size_t lead = before ? size_t(__builtin_clzll(before)) / 8 : detail::kGroup;
    size_t trail = after ? detail::first_byte(after) : detail::kGroup;
    if (lead + trail >= detail::kGroup) {
      set_ctrl(i, detail::kDeleted);
    } else {
      set_ctrl(i, detail::kEmpty);
      ++growth_left_;
    }
  }

  void allocate_table(size_t cap) {
    if (cap > max_value<size_t>() / 2 / (sizeof(T) + 1)) {
      throw LengthError("yasl::HashTable: too large");
    }
    size_t slot_bytes = cap * sizeof(T);
    void* mem =
        res_->allocate(slot_bytes + cap + detail::kGroup, table_align());
    slots_ = static_cast<unsigned char*>(mem);
    ctrl_ = slots_ + slot_bytes;
    cap_ = cap;
    yasl::mem_set(ctrl_, detail::kEmpty, cap + detail::kGroup);
    size_ = 0;
    growth_left_ = detail::max_items(cap);
  }

  static constexpr size_t table_align() noexcept {
    return alignof(T) > alignof(uint64) ? alignof(T) : alignof(uint64);
  }

  void free_table() noexcept {
    if (slots_) {
      res_->deallocate(slots_, cap_ * sizeof(T) + cap_ + detail::kGroup,
                       table_align());
      slots_ = nullptr;
      ctrl_ = nullptr;
      cap_ = 0;
      growth_left_ = 0;
    }
  }

  void destroy_all() noexcept {
    if constexpr (!TriviallyDestructible<T>) {
      for (size_t i = next_full(0); i < cap_; i = next_full(i + 1)) {
        yasl::destroy_at(slot(i));
      }
    }
  }

  void rehash(size_t new_cap) {
    HashTable fresh(res_, hash_, eq_);
    fresh.allocate_table(new_cap);
    for (size_t i = next_full(0); i < cap_; i = next_full(i + 1)) {
      uint64 h = hash_of(key_of(*slot(i)));
      size_t j = fresh.find_free(h);
      fresh.set_ctrl(j, uint8(h >> 57));
      fresh.move_into(j, slot(i));
    }
    fresh.size_ = size_;
    fresh.growth_left_ = detail::max_items(new_cap) - size_;
    size_ = 0;
    swap(fresh);
    // `fresh` now holds the old block whose elements were moved out.
    fresh.free_table();
  }

  void move_into(size_t j, T* src) noexcept {
    yasl::construct_at(slot(j), yasl::move(*src));
    yasl::destroy_at(src);
  }

  unsigned char* slots_ = nullptr;
  uint8* ctrl_ = nullptr;
  size_t cap_ = 0;
  size_t size_ = 0;
  size_t growth_left_ = 0;
  mem::Resource* res_;
  [[no_unique_address]] Hasher hash_;
  [[no_unique_address]] Eq eq_;
};

// Unordered map. Faster than the ordered maps for point lookups; no ordering,
// no lower_bound. Lookups are heterogeneous: map.get("key"_sv) does not build a
// String.
template <class K, class V, class Hasher = HashAny, class Eq = EqualTo>
class HashMap : public HashTable<MapEntry<K, V>, EntryKey, Hasher, Eq>,
                public MapOps<HashMap<K, V, Hasher, Eq>, V> {
 public:
  using key_type = K;
  using mapped_type = V;
  using HashTable<MapEntry<K, V>, EntryKey, Hasher, Eq>::HashTable;
};

template <class K, class Hasher = HashAny, class Eq = EqualTo>
class HashSet : public HashTable<K, Identity, Hasher, Eq>,
                public SetOps<HashSet<K, Hasher, Eq>, K> {
 public:
  using HashTable<K, Identity, Hasher, Eq>::HashTable;
};

}  // namespace yasl
