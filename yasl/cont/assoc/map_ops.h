#pragma once

#include <yasl/core/exception.h>
#include <yasl/core/utility.h>

namespace yasl {
// Map element with named fields instead of first/second.
// Works with structured bindings: for (auto& [key, value] : map).
template <class K, class V>
struct MapEntry {
  const K key;
  V value;

  template <class KK, class... Args>
    requires(!SameAs<RemoveCVRef<KK>, MapEntry>)
  MapEntry(KK&& k, Args&&... args)
      : key(yasl::forward<KK>(k)), value(yasl::forward<Args>(args)...) {}

  friend bool operator==(const MapEntry&, const MapEntry&) = default;
};

struct EntryKey {
  template <class E>
  constexpr const auto& operator()(const E& e) const noexcept {
    return e.key;
  }
};

// Map API shared by every engine (CRTP over Treap / BTree).
template <class Derived, class V>
class MapOps {
 public:
  template <class KK, class... Args>
  auto try_emplace(KK&& key, Args&&... args) {
    return self().try_emplace_with(key, yasl::forward<KK>(key),
                                   yasl::forward<Args>(args)...);
  }

  // Inserts only if the key is absent.
  template <class KK, class VV>
  auto insert(KK&& key, VV&& value) {
    return try_emplace(yasl::forward<KK>(key), yasl::forward<VV>(value));
  }

  template <class KK, class VV>
  auto insert_or_assign(KK&& key, VV&& value) {
    auto r = try_emplace(yasl::forward<KK>(key), yasl::forward<VV>(value));
    if (!r.second) {
      r.first->value = yasl::forward<VV>(value);
    }
    return r;
  }

  template <class KK>
  V& operator[](KK&& key) {
    return try_emplace(yasl::forward<KK>(key)).first->value;
  }

  // Pointer to the value, nullptr if absent.
  template <class Q>
  V* get(const Q& key) {
    auto it = self().find(key);
    return it == self().end() ? nullptr : &it->value;
  }

  template <class Q>
  const V* get(const Q& key) const {
    auto it = self().find(key);
    return it == self().end() ? nullptr : &it->value;
  }

  template <class Q>
  V get_or(const Q& key, V fallback) const {
    const V* v = get(key);
    return v ? *v : yasl::move(fallback);
  }

  template <class Q>
  V& at(const Q& key) {
    if (V* v = get(key)) {
      return *v;
    }
    throw OutOfRange("yasl::Map::at");
  }

  template <class Q>
  const V& at(const Q& key) const {
    if (const V* v = get(key)) {
      return *v;
    }
    throw OutOfRange("yasl::Map::at");
  }

 private:
  Derived& self() noexcept { return static_cast<Derived&>(*this); }

  const Derived& self() const noexcept {
    return static_cast<const Derived&>(*this);
  }
};

template <class Derived, class K>
class SetOps {
 public:
  template <class Q>
  auto insert(Q&& key) {
    return self().try_emplace_with(key, yasl::forward<Q>(key));
  }

  template <class... Args>
  auto emplace(Args&&... args) {
    K key(yasl::forward<Args>(args)...);
    return self().try_emplace_with(key, yasl::move(key));
  }

 private:
  Derived& self() noexcept { return static_cast<Derived&>(*this); }
};

}  // namespace yasl
