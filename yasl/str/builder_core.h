#pragma once

#include "str.h"

namespace yasl {
// Allocation-only string builder.  This type deliberately has no dependency
// on io::Output or formatting; include builder.h when stream insertion and
// format strings are needed.
class StringBuilderCore {
 public:
  StringBuilderCore() noexcept : s_(mem::default_resource()) {}
  explicit StringBuilderCore(mem::Resource* res) noexcept : s_(res) {}
  explicit StringBuilderCore(size_t reserve,
                             mem::Resource* res = mem::default_resource())
      : s_(res) {
    s_.reserve(reserve);
  }
  explicit StringBuilderCore(String initial) noexcept : s_(yasl::move(initial)) {}

  StringBuilderCore& append(StringView text) {
    s_.append(text);
    return *this;
  }
  StringBuilderCore& append(size_t n, char c) {
    for (size_t i = 0; i < n; ++i) {
      s_.push_back(c);
    }
    return *this;
  }
  StringBuilderCore& push_back(char c) {
    s_.push_back(c);
    return *this;
  }
  StringBuilderCore& push_char(char32_t cp) {
    char buf[4];
    return append(StringView(buf, utf8::encode(cp, buf)));
  }

  StringView view() const noexcept { return s_.view(); }
  const char* data() const noexcept { return s_.data(); }
  const char* c_str() const noexcept { return s_.c_str(); }
  size_t size() const noexcept { return s_.size(); }
  bool empty() const noexcept { return s_.empty(); }
  void reserve(size_t n) { s_.reserve(n); }
  void clear() noexcept { s_.clear(); }
  String str() const noexcept { return s_; }
  String take() noexcept { return yasl::exchange(s_, String(s_.resource())); }

 private:
  String s_;
};
}  // namespace yasl
