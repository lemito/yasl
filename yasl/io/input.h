#pragma once

#include <yasl/core/check.h>
#include <yasl/str/num.h>
#include <yasl/str/str.h>

#include "output.h"

namespace yasl::io {
// Buffered input stream.
//
// The derived class exposes a window [pos_, end_) of unread bytes and refills
// it in underflow(). Parsing operators set a sticky failure flag instead of
// throwing; I/O errors in derived classes throw io::Error.
//
//     int n; String name;
//     if (in >> n >> name) ...
//     for (String line; in.read_line(line);) ...
class Input {
 public:
  Input() noexcept = default;
  virtual ~Input() = default;

  Input(const Input&) = delete;
  Input& operator=(const Input&) = delete;

  // Next byte or -1 at end of stream.
  int peek() {
    if (pos_ == end_ && !refill()) {
      return -1;
    }
    return static_cast<unsigned char>(*pos_);
  }

  int get() {
    if (pos_ == end_ && !refill()) {
      return -1;
    }
    return static_cast<unsigned char>(*pos_++);
  }

  bool eof() { return peek() == -1; }

  // Reads up to n bytes; fewer only at end of stream.
  size_t read(char* dst, size_t n);

  // Reads exactly n bytes. If the stream ends first, the stream is marked
  // failed (like a failed parse), the bytes read so far stay in dst, and false
  // is returned.
  bool read_exact(char* dst, size_t n);

  // Marks n buffered bytes as read. n must not exceed buffered().size().
  void consume(size_t n) noexcept {
    YASL_ASSERT(n <= size_t(end_ - pos_));
    pos_ += n;
  }

  // Reads the next line without its "\n" / "\r\n". False at end of stream.
  bool read_line(String& line);

  // Reads the next whitespace-separated word. False at end of stream.
  bool read_word(String& word);

  String read_all();

  void skip_whitespace();

  // Bytes already buffered but not consumed yet (for a StringInput: all of the
  // rest).
  StringView buffered() const noexcept { return {pos_, size_t(end_ - pos_)}; }

  bool ok() const noexcept { return !failed_; }

  explicit operator bool() const noexcept { return !failed_; }

  void clear_error() noexcept { failed_ = false; }

  // `out` is flushed before every refill (prompts appear before reading stdin).
  void tie(Output* out) noexcept { tied_ = out; }

  Input& operator>>(String& word) {
    if (!read_word(word)) {
      failed_ = true;
    }
    return *this;
  }

  Input& operator>>(char& c) {
    skip_whitespace();
    int v = get();
    if (v < 0) {
      failed_ = true;
    } else {
      c = char(v);
    }
    return *this;
  }

  template <class T>
    requires(Arithmetic<T> && !SameAs<T, bool> && !SameAs<T, char>)
  Input& operator>>(T& value) {
    char buf[512];
    size_t n;
    T parsed{};
    if (!read_token(buf, sizeof(buf), n) ||
        !num::parse(StringView(buf, n), parsed)) {
      failed_ = true;
    } else {
      value = parsed;
    }
    return *this;
  }

 protected:
  // Called when [pos_, end_) is empty. Make new bytes visible or return false
  // at EOF.
  virtual bool underflow() = 0;

  const char* pos_ = nullptr;
  const char* end_ = nullptr;

 private:
  bool refill() {
    if (tied_) {
      tied_->flush();
    }
    return underflow();
  }

  // Next word into a fixed buffer; false at EOF or if it does not fit.
  bool read_token(char* buf, size_t cap, size_t& n);

  Output* tied_ = nullptr;
  bool failed_ = false;
};

// Reads from memory. Zero-copy: the view is scanned in place.
class StringInput final : public Input {
 public:
  // `text` must outlive the stream.
  explicit StringInput(StringView text) noexcept {
    pos_ = text.data();
    end_ = text.data() + text.size();
  }

  // Keeps the (shared, copy-on-write) string alive.
  explicit StringInput(const String& text) noexcept : keep_(text) {
    pos_ = keep_.data();
    end_ = keep_.data() + keep_.size();
  }

 protected:
  bool underflow() override { return false; }

 private:
  String keep_;
};

// Moves everything from `in` to `out` and returns the byte count. Bytes go
// straight from the input's buffer to the output, with no intermediate buffer.
size_t copy(Input& in, Output& out);

// Reads at most `limit` bytes of another stream (HTTP bodies, length-prefixed
// records). Only what is actually read is taken from the underlying stream, so
// after the limit is reached (or this object is destroyed) the rest is still
// there. The underlying stream must outlive this object.
//
//     LimitedInput body(in, content_length);
//     String text = body.read_all();
class LimitedInput final : public Input {
 public:
  LimitedInput(Input& source, size_t limit) noexcept
      : src_(source), remaining_(limit) {}

  ~LimitedInput() override { sync(); }

  // Bytes that may still be read.
  size_t remaining() const noexcept {
    return remaining_ - (start_ ? size_t(pos_ - start_) : 0);
  }

 protected:
  bool underflow() override;

 private:
  // Hands the bytes read from the exposed window back to the source.
  void sync() noexcept;

  Input& src_;
  size_t remaining_;
  const char* start_ = nullptr;
};

// Writes everything to two streams (like `tee`). Unbuffered itself; both
// streams must outlive this object.
class TeeOutput final : public Output {
 public:
  TeeOutput(Output& first, Output& second) noexcept : a_(first), b_(second) {}

 protected:
  void overflow(const char* data, size_t n) override {
    a_.write(data, n);
    b_.write(data, n);
  }

  void do_flush() override {
    a_.flush();
    b_.flush();
  }

 private:
  Output& a_;
  Output& b_;
};
}  // namespace yasl::io
