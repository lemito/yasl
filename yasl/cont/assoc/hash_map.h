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

// Bookkeeping at the front of the control bytes of every table block. The
// object itself holds just a pointer to the control bytes; the slots lie
// before the header.
struct TableHeader {
  size_t cap;          // slots, a power of two, or 0 for an empty table
  size_t size;         // full slots
  size_t growth_left;  // empty slots that may still be filled before a rehash
  mem::Resource* res;  // the block's resource; null = default one, no block
};

// What every table without a block points to: no allocation for an empty
// table. Never written to.
struct EmptyBlock {
  TableHeader header;
  uint8 ctrl[kGroup];
};

inline EmptyBlock kEmptyBlock = {
    {0, 0, 0, nullptr},
    {kEmpty, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty}};
}  // namespace detail

// Hash table with open addressing in the style of Swiss tables: one control
// byte per slot (7 bits of the hash or empty/deleted) probed eight at a time
// with plain 64-bit arithmetic, triangular probing, 7/8 maximum load. Slots,
// bookkeeping and control bytes share one allocation, and the object is a
// single pointer (an empty table points to a static block and allocates
// nothing).
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

  HashTable() noexcept : ctrl_(empty_ctrl()) {}

  // A table on a non-default resource keeps the resource in a block of its own
  // (so the constructor may throw); on the default resource it allocates
  // nothing.
  explicit HashTable(mem::Resource* res, Hasher hash = Hasher(), Eq eq = Eq())
      : ctrl_(empty_ctrl()), hash_(yasl::move(hash)), eq_(yasl::move(eq)) {
    if (res != mem::default_resource()) {
      ctrl_ = allocate_block(0, res);
    }
  }

  HashTable(std::initializer_list<T> il,
            mem::Resource* res = mem::default_resource())
      : HashTable(res) {
    reserve(il.size());
    for (const T& v : il) {
      try_emplace_with(key_of(v), v);
    }
  }

  HashTable(const HashTable& other) : HashTable(other, other.resource()) {}

  HashTable(const HashTable& other, mem::Resource* res)
      : HashTable(res, other.hash_, other.eq_) {
    const size_t other_cap = other.hdr().cap;
    if (other.hdr().size == 0) {
      return;
    }
    free_table();
    ctrl_ = allocate_block(other.hdr().cap, res);
    // Same slot layout and control bytes (tombstones included), so probe chains
    // stay intact.
    size_t i = 0;
    try {
      for (; i < other_cap; ++i) {
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
    yasl::mem_copy(ctrl_, other.ctrl_, other_cap + detail::kGroup);
    hdr().size = other.hdr().size;
    hdr().growth_left = other.hdr().growth_left;
  }

  // The moved-from table is empty and on the default resource.
  HashTable(HashTable&& other) noexcept
      : ctrl_(yasl::exchange(other.ctrl_, empty_ctrl())),
        hash_(yasl::move(other.hash_)),
        eq_(yasl::move(other.eq_)) {}

  ~HashTable() {
    destroy_all();
    free_table();
  }

  HashTable& operator=(const HashTable& other) {
    if (this != &other) {
      HashTable tmp(other, resource());
      swap(tmp);
    }
    return *this;
  }

  HashTable& operator=(HashTable&& other) noexcept {
    if (this != &other) {
      destroy_all();
      free_table();
      ctrl_ = yasl::exchange(other.ctrl_, empty_ctrl());
      hash_ = yasl::move(other.hash_);
      eq_ = yasl::move(other.eq_);
    }
    return *this;
  }

  size_t size() const noexcept { return hdr().size; }

  bool empty() const noexcept { return hdr().size == 0; }

  mem::Resource* resource() const noexcept {
    return hdr().res ? hdr().res : mem::default_resource();
  }

  // Number of slots (a power of two, or 0 before the first insertion).
  size_t bucket_count() const noexcept { return hdr().cap; }

  double load_factor() const noexcept {
    return hdr().cap ? double(hdr().size) / double(hdr().cap) : 0.0;
  }

  iterator begin() noexcept { return {this, next_full(0)}; }

  iterator end() noexcept { return {this, hdr().cap}; }

  const_iterator begin() const noexcept { return {this, next_full(0)}; }

  const_iterator end() const noexcept { return {this, hdr().cap}; }

  template <class Q>
  iterator find(const Q& key) noexcept {
    size_t i = find_index(key, hash_of(key));
    return {this, i == kNpos ? hdr().cap : i};
  }

  template <class Q>
  const_iterator find(const Q& key) const noexcept {
    size_t i = find_index(key, hash_of(key));
    return {this, i == kNpos ? hdr().cap : i};
  }

  template <class Q>
  bool contains(const Q& key) const noexcept {
    return find_index(key, hash_of(key)) != kNpos;
  }

  // Makes room for n elements without rehashing.
  void reserve(size_t n) {
    if (n <= hdr().size + hdr().growth_left) {
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
    ++hdr().size;
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
    for (size_t i = next_full(0); i < hdr().cap; i = next_full(i + 1)) {
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
    if (hdr().cap) {
      yasl::mem_set(ctrl_, detail::kEmpty, hdr().cap + detail::kGroup);
      hdr().growth_left = detail::max_items(hdr().cap);
      hdr().size = 0;
    }
  }

  void swap(HashTable& other) noexcept {
    yasl::swap(ctrl_, other.ctrl_);
    yasl::swap(hash_, other.hash_);
    yasl::swap(eq_, other.eq_);
  }

  // Same elements, regardless of order.
  friend bool operator==(const HashTable& a, const HashTable& b)
    requires EqualityComparable<T>
  {
    if (a.hdr().size != b.hdr().size) {
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
    if (hdr().cap == 0) {
      return hdr().size == 0;
    }
    if ((hdr().cap & (hdr().cap - 1)) != 0 || hdr().cap < detail::kGroup) {
      return false;
    }
    size_t full = 0;
    size_t deleted = 0;
    for (size_t i = 0; i < hdr().cap + detail::kGroup; ++i) {
      size_t home = i < hdr().cap ? i : i - hdr().cap;
      if (ctrl_[i] != ctrl_[home]) {
        return false;
      }
    }
    for (size_t i = 0; i < hdr().cap; ++i) {
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
    return full == hdr().size &&
           hdr().growth_left == detail::max_items(hdr().cap) - full - deleted;
  }

 protected:
  static const key_type& key_of(const T& v) noexcept { return KeyOf{}(v); }

  T* slot(size_t i) const noexcept {
    return yasl::launder(reinterpret_cast<T*>(slots_base()) + i);
  }

  template <class Q>
  uint64 hash_of(const Q& key) const noexcept {
    return mix64(uint64(hash_(key)) ^ process_seed());
  }

  void set_ctrl(size_t i, uint8 c) noexcept {
    ctrl_[i] = c;
    // The first group is mirrored after the end so that unaligned group loads
    // wrap around.
    ctrl_[((i - detail::kGroup) & (hdr().cap - 1)) + detail::kGroup] = c;
  }

  // First full slot at index >= i, or hdr().cap.
  size_t next_full(size_t i) const noexcept {
    while (i < hdr().cap) {
      uint64 m = detail::match_full(detail::load_group(ctrl_ + i));
      if (m) {
        size_t r = i + detail::first_byte(m);
        return r < hdr().cap ? r : hdr().cap;
      }
      i += detail::kGroup;
    }
    return hdr().cap;
  }

  template <class Q>
  size_t find_index(const Q& key, uint64 h) const noexcept {
    if (hdr().cap == 0) {
      return kNpos;
    }
    const size_t mask = hdr().cap - 1;
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
    const size_t mask = hdr().cap - 1;
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
    if (hdr().cap == 0) {
      rehash(detail::kGroup);
    }
    size_t i = find_free(h);
    if (ctrl_[i] == detail::kEmpty && hdr().growth_left == 0) {
      // Out of room: drop tombstones if they are most of the load, else double.
      rehash(hdr().size * 2 >= detail::max_items(hdr().cap) ? hdr().cap * 2
                                                            : hdr().cap);
      i = find_free(h);
    }
    if (ctrl_[i] == detail::kEmpty) {
      --hdr().growth_left;
    }
    set_ctrl(i, uint8(h >> 57));
    return i;
  }

  void erase_index(size_t i) noexcept {
    yasl::destroy_at(slot(i));
    --hdr().size;
    // A probe sequence can only have skipped this slot if a whole group around
    // it is occupied; then it must stay a tombstone.
    uint64 before = detail::match_empty(
        detail::load_group(ctrl_ + ((i - detail::kGroup) & (hdr().cap - 1))));
    uint64 after = detail::match_empty(detail::load_group(ctrl_ + i));
    size_t lead = before ? size_t(__builtin_clzll(before)) / 8 : detail::kGroup;
    size_t trail = after ? detail::first_byte(after) : detail::kGroup;
    if (lead + trail >= detail::kGroup) {
      set_ctrl(i, detail::kDeleted);
    } else {
      set_ctrl(i, detail::kEmpty);
      ++hdr().growth_left;
    }
  }

  // Allocates a block with an empty table of `cap` slots; returns its control
  // bytes.
  static uint8* allocate_block(size_t cap, mem::Resource* res) {
    if (cap > max_value<size_t>() / 2 / (sizeof(T) + 1)) {
      throw LengthError("yasl::HashTable: too large");
    }
    size_t slot_bytes = cap * sizeof(T);
    auto* mem = static_cast<unsigned char*>(res->allocate(
        slot_bytes + sizeof(detail::TableHeader) + cap + detail::kGroup,
        table_align()));
    uint8* ctrl = mem + slot_bytes + sizeof(detail::TableHeader);
    ::new (ctrl - sizeof(detail::TableHeader))
        detail::TableHeader{cap, 0, detail::max_items(cap), res};
    yasl::mem_set(ctrl, detail::kEmpty, cap + detail::kGroup);
    return ctrl;
  }

  static constexpr size_t table_align() noexcept {
    return alignof(T) > alignof(uint64) ? alignof(T) : alignof(uint64);
  }

  static uint8* empty_ctrl() noexcept { return detail::kEmptyBlock.ctrl; }

  bool has_block() const noexcept { return ctrl_ != empty_ctrl(); }

  detail::TableHeader& hdr() const noexcept {
    return *reinterpret_cast<detail::TableHeader*>(ctrl_ -
                                                   sizeof(detail::TableHeader));
  }

  unsigned char* slots_base() const noexcept {
    return ctrl_ - sizeof(detail::TableHeader) - hdr().cap * sizeof(T);
  }

  // Releases the block (the elements must already be gone) and goes back to
  // the shared empty table.
  void free_table() noexcept {
    if (has_block()) {
      const detail::TableHeader h = hdr();
      h.res->deallocate(slots_base(),
                        h.cap * sizeof(T) + sizeof(detail::TableHeader) +
                            h.cap + detail::kGroup,
                        table_align());
      ctrl_ = empty_ctrl();
    }
  }

  void destroy_all() noexcept {
    if constexpr (!TriviallyDestructible<T>) {
      for (size_t i = next_full(0); i < hdr().cap; i = next_full(i + 1)) {
        yasl::destroy_at(slot(i));
      }
    }
  }

  void rehash(size_t new_cap) {
    HashTable fresh(hash_, eq_);
    fresh.ctrl_ = allocate_block(new_cap, resource());
    for (size_t i = next_full(0); i < hdr().cap; i = next_full(i + 1)) {
      uint64 h = hash_of(key_of(*slot(i)));
      size_t j = fresh.find_free(h);
      fresh.set_ctrl(j, uint8(h >> 57));
      fresh.move_into(j, slot(i));
    }
    fresh.hdr().size = hdr().size;
    fresh.hdr().growth_left = detail::max_items(new_cap) - hdr().size;
    swap(fresh);
    // `fresh` now holds the old block whose elements were moved out.
    fresh.free_table();
  }

  void move_into(size_t j, T* src) noexcept {
    yasl::construct_at(slot(j), yasl::move(*src));
    yasl::destroy_at(src);
  }

  HashTable(Hasher hash, Eq eq) noexcept
      : ctrl_(empty_ctrl()), hash_(yasl::move(hash)), eq_(yasl::move(eq)) {}

  uint8* ctrl_;
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
