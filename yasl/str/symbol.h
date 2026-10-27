#pragma once

#include <yasl/cont/assoc/hash_map.h>
#include <yasl/mem/obj_pool.h>
#include <yasl/str/view.h>

namespace yasl {
// An interned string: equal strings give identical symbols, so comparing and
// hashing a Symbol costs one pointer operation. Valid as long as its
// SymbolTable lives.
class Symbol {
 public:
  Symbol() noexcept = default;

  StringView view() const noexcept { return {p_, n_}; }
  const char* c_str() const noexcept { return p_ ? p_ : ""; }
  size_t size() const noexcept { return n_; }
  bool empty() const noexcept { return n_ == 0; }

  // False only for a default-constructed Symbol, not for the empty string.
  explicit operator bool() const noexcept { return p_ != nullptr; }

  friend bool operator==(Symbol a, Symbol b) noexcept { return a.p_ == b.p_; }

  friend uint64 hash_value(Symbol s) noexcept {
    return mix64(uint64(reinterpret_cast<uintptr_t>(s.p_)));
  }

 private:
  friend class SymbolTable;
  Symbol(const char* p, size_t n) noexcept : p_(p), n_(n) {}

  const char* p_ = nullptr;
  size_t n_ = 0;
};

// Interning table: each distinct string is stored once in a pool and handed out
// as a Symbol. Not thread-safe.
//
//     SymbolTable names;
//     Symbol a = names.intern("foo");
//     Symbol b = names.intern(String("fo") + "o");
//     a == b;                          // true, a pointer comparison
class SymbolTable {
 public:
  explicit SymbolTable(mem::Resource* res = mem::default_resource())
      : pool_(res), set_(res) {}

  SymbolTable(const SymbolTable&) = delete;
  SymbolTable& operator=(const SymbolTable&) = delete;

  Symbol intern(StringView s) {
    auto it = set_.find(s);
    if (it != set_.end()) {
      return Symbol(it->data(), it->size());
    }
    StringView stored = pool_.copy_str(s);
    set_.insert(stored);
    return Symbol(stored.data(), stored.size());
  }

  // The symbol for s if it was interned before.
  Optional<Symbol> find(StringView s) const {
    auto it = set_.find(s);
    if (it != set_.end()) {
      return Symbol(it->data(), it->size());
    }
    return none;
  }

  size_t size() const noexcept { return set_.size(); }

 private:
  mem::ObjPool pool_;
  HashSet<StringView> set_;
};
}  // namespace yasl
