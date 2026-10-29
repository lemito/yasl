#pragma once

#include <yasl/io/output.h>

#include "str.h"

namespace yasl {
// An output stream that accumulates text into a String.
//
//     StringBuilder b;
//     b << "id=" << 42 << ", ratio=" << 0.5;
//     b.format(" [{:>6}]", name);
//     String s = b.take();
//
// The stream window is the spare capacity of the String's own buffer, so
// writes are plain memcpy, take() is a move and str() is an O(1) COW snapshot.
class StringBuilder final : public io::Output {
 public:
  StringBuilder() noexcept : s_(mem::default_resource()) {}

  explicit StringBuilder(mem::Resource* res) noexcept : s_(res) {}

  explicit StringBuilder(size_t reserve,
                         mem::Resource* res = mem::default_resource())
      : s_(res) {
    this->reserve(reserve);
  }

  // Continues appending to an existing string.
  explicit StringBuilder(String initial) noexcept : s_(yasl::move(initial)) {}

  StringBuilder& append(StringView s) {
    write(s);
    return *this;
  }

  StringBuilder& append(size_t n, char c) {
    put(c, n);
    return *this;
  }

  // Appends a code point as UTF-8 (invalid ones as U+FFFD).
  StringBuilder& push_char(char32_t cp) {
    char buf[4];
    write(buf, utf8::encode(cp, buf));
    return *this;
  }

  // For generic back-inserting code.
  void push_back(char c) { put(c); }

  template <class... Args>
  StringBuilder& format(io::FormatString<TypeIdentity<Args>...> fmt,
                        const Args&... args) {
    io::Output::format(fmt, args...);
    return *this;
  }

  StringView view() const noexcept {
    sync();
    return s_.view();
  }

  const char* data() const noexcept { return view().data(); }

  const char* c_str() const noexcept {
    sync();
    return s_.c_str();
  }

  size_t size() const noexcept { return s_.size() + pending(); }

  bool empty() const noexcept { return size() == 0; }

  void reserve(size_t n) {
    sync();
    s_.reserve(n);
    attach();
  }

  void clear() noexcept {
    detach();
    s_.clear();
  }

  // Snapshot sharing the buffer (O(1)). The next write detaches the builder.
  String str() const noexcept {
    sync();
    const_cast<StringBuilder*>(this)->detach();
    return s_;
  }

  // Moves the accumulated text out; the builder is left empty.
  String take() noexcept {
    sync();
    detach();
    return yasl::exchange(s_, String(s_.resource()));
  }

 protected:
  void overflow(const char* data, size_t n) override {
    sync();
    size_t size = s_.size();
    s_.reserve(yasl::max(size + n, yasl::max(s_.capacity() * 2, size_t(64))));
    s_.append(StringView(data, n));
    attach();
  }

 private:
  size_t pending() const noexcept {
    return pos_ ? size_t(pos_ - (s_.data() + s_.size())) : 0;
  }

  // Commits bytes written into the window to the string's size.
  void sync() const noexcept {
    auto* self = const_cast<StringBuilder*>(this);
    if (size_t n = pending()) {
      // The string is unique and has the room: this only bumps size and writes
      // the NUL.
      self->s_.append_uninitialized(n);
    }
  }

  // Exposes the spare capacity as the write window (makes the buffer unique).
  void attach() {
    char* d = s_.mut_data();
    if (s_.capacity() == 0) {
      detach();
      return;
    }
    pos_ = d + s_.size();
    end_ = d + s_.capacity();
  }

  void detach() noexcept { pos_ = end_ = nullptr; }

  String s_;
};

// format() into a new String.
template <class... Args>
String format(io::FormatString<TypeIdentity<Args>...> fmt,
              const Args&... args) {
  io::detail::ScratchOutput out;
  out.format(fmt, args...);
  return String(out.view());
}

// Any printable value to a String.
template <class T>
String to_string(const T& value) {
  StringBuilder b;
  b << value;
  return b.take();
}
}  // namespace yasl
