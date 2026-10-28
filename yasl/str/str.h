#pragma once

#include <yasl/conc/ref_count.h>
#include <yasl/mem/resource.h>

#include "view.h"

namespace yasl {
// Immutable-by-default string with copy-on-write sharing.
//
//   * Copying is O(1): the buffer is shared through an atomic refcount.
//   * Any mutation first makes the buffer unique (copies only if it is shared).
//   * There is deliberately no mutable operator[] / iterator: that is what
//   broke
//     COW in old std::string. Write through set(i, c) or mut_data().
//   * Always NUL-terminated, c_str() is free.
//
// The buffer remembers the resource it came from, so sharing between strings
// that use different resources is fine.
class String {
 public:
  static constexpr size_t npos = StringView::npos;

  using value_type = char;
  using size_type = size_t;
  using const_iterator = const char*;
  using iterator = const_iterator;

  String() noexcept : res_(mem::default_resource()) {}

  explicit String(mem::Resource* res) noexcept : res_(res) {}

  String(const char* s, mem::Resource* res = mem::default_resource())
      : String(StringView(s), res) {}

  String(const char* s, size_t n, mem::Resource* res = mem::default_resource())
      : String(StringView(s, n), res) {}

  explicit String(StringView s, mem::Resource* res = mem::default_resource())
      : String(res) {
    append(s);
  }

  String(size_t n, char c, mem::Resource* res = mem::default_resource())
      : String(res) {
    append(n, c);
  }

  String(const String& other) noexcept : rep_(other.rep_), res_(other.res_) {
    if (rep_) {
      rep_->rc.ref();
    }
  }

  // Deep copy into another resource.
  String(const String& other, mem::Resource* res) : String(other.view(), res) {}

  String(String&& other) noexcept
      : rep_(yasl::exchange(other.rep_, nullptr)), res_(other.res_) {}

  ~String() { release(rep_); }

  String& operator=(const String& other) noexcept {
    if (rep_ != other.rep_) {
      if (other.rep_) {
        other.rep_->rc.ref();
      }
      release(rep_);
      rep_ = other.rep_;
    }
    return *this;
  }

  String& operator=(String&& other) noexcept {
    if (this != &other) {
      release(rep_);
      rep_ = yasl::exchange(other.rep_, nullptr);
      res_ = other.res_;
    }
    return *this;
  }

  String& operator=(StringView s) { return replace(0, size(), s); }

  String& operator=(const char* s) { return *this = StringView(s); }

  // Observers

  size_t size() const noexcept { return rep_ ? rep_->size : 0; }

  size_t length() const noexcept { return size(); }

  bool empty() const noexcept { return size() == 0; }

  size_t capacity() const noexcept { return rep_ ? rep_->cap : 0; }

  const char* data() const noexcept { return rep_ ? rep_->chars() : ""; }

  const char* c_str() const noexcept { return data(); }

  const char* begin() const noexcept { return data(); }

  const char* end() const noexcept { return data() + size(); }

  const char& operator[](size_t i) const noexcept {
    YASL_ASSERT(i < size());
    return data()[i];
  }

  char at(size_t i) const;

  char front() const noexcept { return view().front(); }

  char back() const noexcept { return view().back(); }

  StringView view() const noexcept { return {data(), size()}; }

  // True if the buffer is shared with another String.
  bool is_shared() const noexcept { return rep_ && !rep_->rc.unique(); }

  size_t use_count() const noexcept { return rep_ ? rep_->rc.count() : 0; }

  mem::Resource* resource() const noexcept { return res_; }

  // Search (forwarded to StringView)

  size_t find(StringView s, size_t pos = 0) const noexcept {
    return view().find(s, pos);
  }

  size_t find(char c, size_t pos = 0) const noexcept {
    return view().find(c, pos);
  }

  size_t rfind(StringView s, size_t pos = npos) const noexcept {
    return view().rfind(s, pos);
  }

  size_t rfind(char c, size_t pos = npos) const noexcept {
    return view().rfind(c, pos);
  }

  bool contains(StringView s) const noexcept { return view().contains(s); }

  bool starts_with(StringView s) const noexcept {
    return view().starts_with(s);
  }

  bool ends_with(StringView s) const noexcept { return view().ends_with(s); }

  String substr(size_t pos, size_t n = npos) const {
    return String(view().substr(pos, n), res_);
  }

  // Mutation

  // Writable pointer to the (now unique) buffer. Valid until the next copy or
  // mutation of this string - do not keep it around.
  char* mut_data();

  void set(size_t i, char c) {
    YASL_ASSERT(i < size());
    mut_data()[i] = c;
  }

  void reserve(size_t n);
  void resize(size_t n, char c = '\0');
  void clear() noexcept;

  String& append(StringView s) { return replace(size(), 0, s); }

  String& append(size_t n, char c);

  void push_back(char c) {
    if (rep_ && rep_->size < rep_->cap && rep_->rc.unique()) [[likely]] {
      char* d = rep_->chars();
      d[rep_->size++] = c;
      d[rep_->size] = '\0';
      return;
    }
    *splice(size(), 0, 1) = c;
  }

  void pop_back() {
    YASL_ASSERT(!empty());
    splice(size() - 1, 1, 0);
  }

  String& insert(size_t pos, StringView s) { return replace(pos, 0, s); }

  String& erase(size_t pos, size_t n = npos) { return replace(pos, n, {}); }

  // Replaces [pos, pos + n) with s. `s` may point into this string.
  String& replace(size_t pos, size_t n, StringView s);

  // New strings in this string's resource; see the free functions of the same
  // names.
  String replace_all(StringView from, StringView to) const;
  String repeat(size_t n) const;
  String to_upper() const;
  String to_lower() const;

  // Grows the string by n bytes and returns a pointer to them. The bytes are
  // uninitialized - the caller must fill them before reading.
  char* append_uninitialized(size_t n) { return splice(size(), 0, n); }

  String& operator+=(StringView s) { return append(s); }

  String& operator+=(char c) {
    push_back(c);
    return *this;
  }

  void swap(String& other) noexcept {
    yasl::swap(rep_, other.rep_);
    yasl::swap(res_, other.res_);
  }

  friend void swap(String& a, String& b) noexcept { a.swap(b); }

  friend String operator+(const String& a, StringView b) {
    String r(a.res_);
    r.reserve(a.size() + b.size());
    r.append(a.view());
    r.append(b);
    return r;
  }

  friend String operator+(String&& a, StringView b) {
    a.append(b);
    return yasl::move(a);
  }

  friend String operator+(const String& a, const String& b) {
    return a + b.view();
  }

  friend String operator+(String&& a, const String& b) {
    return yasl::move(a) + b.view();
  }

  friend String operator+(const String& a, const char* b) {
    return a + StringView(b);
  }

  friend String operator+(String&& a, const char* b) {
    return yasl::move(a) + StringView(b);
  }

  friend String operator+(const char* a, const String& b) {
    return StringView(a) + b;
  }

  friend String operator+(StringView a, const String& b) {
    String r(b.res_);
    r.reserve(a.size() + b.size());
    r.append(a);
    r.append(b.view());
    return r;
  }

  friend bool operator==(const String& a, const String& b) noexcept {
    return a.rep_ == b.rep_ || a.view() == b.view();
  }

  friend std::strong_ordering operator<=>(const String& a,
                                          const String& b) noexcept {
    return a.view() <=> b.view();
  }

  friend bool operator==(const String& a, StringView b) noexcept {
    return a.view() == b;
  }

  friend std::strong_ordering operator<=>(const String& a,
                                          StringView b) noexcept {
    return a.view() <=> b;
  }

  friend bool operator==(const String& a, const char* b) noexcept {
    return a.view() == StringView(b);
  }

  friend std::strong_ordering operator<=>(const String& a,
                                          const char* b) noexcept {
    return a.view() <=> StringView(b);
  }

  friend uint64 hash_value(const String& s) noexcept {
    return hash_value(s.view());
  }

 private:
  struct Rep {
    RefCount rc;
    size_t size;
    size_t cap;
    mem::Resource* res;

    char* chars() noexcept { return reinterpret_cast<char*>(this + 1); }

    const char* chars() const noexcept {
      return reinterpret_cast<const char*>(this + 1);
    }
  };

  static Rep* alloc_rep(mem::Resource* res, size_t cap);
  static void release(Rep* rep) noexcept;

  bool unique() const noexcept { return rep_ && rep_->rc.unique(); }

  bool aliases(StringView s) const noexcept;

  // Makes the buffer unique, removes `remove` bytes at `pos` and opens a gap of
  // `insert` bytes there. Returns a pointer to the gap.
  char* splice(size_t pos, size_t remove, size_t insert);

  Rep* rep_ = nullptr;
  mem::Resource* res_;
};

// Text utilities that build new strings.

// Every `from` in `text` replaced by `to` (non-overlapping, left to right).
String replace_all(StringView text, StringView from, StringView to,
                   mem::Resource* res = mem::default_resource());

String repeat(StringView text, size_t n,
              mem::Resource* res = mem::default_resource());

// ASCII case conversion; other bytes are left alone.
String to_upper(StringView text, mem::Resource* res = mem::default_resource());
String to_lower(StringView text, mem::Resource* res = mem::default_resource());

// The elements of a range (anything convertible to StringView) separated by
// `sep`.
//
//     join(", ", names)      // "ann, bob, cy"
template <class R>
  requires requires(const R& r) {
    r.begin();
    r.end();
  }
String join(StringView sep, const R& range,
            mem::Resource* res = mem::default_resource()) {
  String out(res);
  bool first = true;
  for (const auto& item : range) {
    if (!first) {
      out.append(sep);
    }
    first = false;
    out.append(StringView(item));
  }
  return out;
}

inline String String::replace_all(StringView from, StringView to) const {
  return yasl::replace_all(view(), from, to, res_);
}

inline String String::repeat(size_t n) const {
  return yasl::repeat(view(), n, res_);
}

inline String String::to_upper() const { return yasl::to_upper(view(), res_); }

inline String String::to_lower() const { return yasl::to_lower(view(), res_); }

template <>
inline constexpr bool IsTriviallyRelocatable<String> = true;

namespace literals {
inline String operator""_s(const char* s, size_t n) { return String(s, n); }
}  // namespace literals
}  // namespace yasl
