#pragma once

#include "map.h"
#include "trie.h"

namespace yasl {
// Value type of sets built on maps.
struct Empty {
  friend bool operator==(Empty, Empty) noexcept { return true; }
};

// Ordered map from strings to V on a prefix tree. The interface matches
// BTreeMap and TreapMap (try_emplace, insert_or_assign, find, get, operator[],
// erase, ...), except that the key is not stored whole, so iteration yields a
// proxy {StringView key, V& value} and `key` is valid only until the iterator
// moves.
//
//     StringMap<int> m;
//     m["apple"] = 1;
//     m["apply"] = 2;
//     for (auto& [key, value] : m) { ... }               // lexicographic order
//     for (auto& [key, value] : m.with_prefix("appl")) { ... }
template <class V>
class StringMap : public MapOps<StringMap<V>, V> {
 public:
  using key_type = StringView;
  using mapped_type = V;
  using iterator = typename Trie<V>::iterator;
  using const_iterator = typename Trie<V>::const_iterator;

  StringMap() = default;
  explicit StringMap(mem::Resource* res) : t_(res) {}

  StringMap(std::initializer_list<Pair<StringView, V>> il,
            mem::Resource* res = mem::default_resource())
      : t_(il, res) {}

  StringMap(const StringMap&) = default;
  StringMap(const StringMap& other, mem::Resource* res) : t_(other.t_, res) {}
  StringMap(StringMap&&) noexcept = default;
  StringMap& operator=(const StringMap&) = default;
  StringMap& operator=(StringMap&&) noexcept = default;

  size_t size() const noexcept { return t_.size(); }
  bool empty() const noexcept { return t_.empty(); }
  mem::Resource* resource() const noexcept { return t_.resource(); }

  iterator begin() { return t_.begin(); }
  iterator end() { return t_.end(); }
  const_iterator begin() const { return t_.begin(); }
  const_iterator end() const { return t_.end(); }

  iterator find(StringView key) { return t_.find_iter(key); }
  const_iterator find(StringView key) const { return t_.find_iter(key); }

  bool contains(StringView key) const noexcept { return t_.contains(key); }

  // Used by MapOps: `lookup` is the key, `ignored` is the same key in the
  // caller's type.
  template <class KK, class... Args>
  Pair<iterator, bool> try_emplace_with(StringView lookup, KK&&,
                                        Args&&... args) {
    auto r = t_.try_emplace(lookup, yasl::forward<Args>(args)...);
    return {t_.find_iter(lookup), r.second};
  }

  bool erase(StringView key) { return t_.erase(key); }
  void clear() noexcept { t_.clear(); }

  bool has_prefix(StringView prefix) const noexcept {
    return t_.has_prefix(prefix);
  }
  size_t count_with_prefix(StringView prefix) const {
    return t_.count_with_prefix(prefix);
  }

  auto with_prefix(StringView prefix) { return t_.with_prefix(prefix); }
  auto with_prefix(StringView prefix) const { return t_.with_prefix(prefix); }

  // The longest key that is a prefix of `text`, with its length.
  auto longest_prefix_of(StringView text) { return t_.longest_prefix_of(text); }
  auto longest_prefix_of(StringView text) const {
    return t_.longest_prefix_of(text);
  }

  void swap(StringMap& other) noexcept { t_.swap(other.t_); }
  friend void swap(StringMap& a, StringMap& b) noexcept { a.swap(b); }

 private:
  Trie<V> t_;
};

// Ordered set of strings; iteration yields the same {key, value} proxy as
// StringMap.
class StringSet {
 public:
  using iterator = StringMap<Empty>::const_iterator;

  StringSet() = default;
  explicit StringSet(mem::Resource* res) : m_(res) {}

  StringSet(std::initializer_list<StringView> il,
            mem::Resource* res = mem::default_resource())
      : m_(res) {
    for (StringView s : il) {
      insert(s);
    }
  }

  size_t size() const noexcept { return m_.size(); }
  bool empty() const noexcept { return m_.empty(); }

  // True if the key was new.
  bool insert(StringView key) { return m_.try_emplace(key).second; }
  bool contains(StringView key) const noexcept { return m_.contains(key); }
  bool erase(StringView key) { return m_.erase(key); }
  void clear() noexcept { m_.clear(); }

  bool has_prefix(StringView prefix) const noexcept {
    return m_.has_prefix(prefix);
  }
  size_t count_with_prefix(StringView prefix) const {
    return m_.count_with_prefix(prefix);
  }

  iterator begin() const { return m_.begin(); }
  iterator end() const { return m_.end(); }
  auto with_prefix(StringView prefix) const { return m_.with_prefix(prefix); }

 private:
  StringMap<Empty> m_;
};
}  // namespace yasl
