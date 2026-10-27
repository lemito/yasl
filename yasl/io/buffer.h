#pragma once

#include <yasl/core/check.h>
#include <yasl/core/utility.h>
#include <yasl/mem/resource.h>
#include <yasl/str/view.h>

namespace yasl::io {
// Growable byte buffer with a read position, for parsers and framing: append at
// the back, look at readable(), consume(n) from the front. Consumed space is
// reused.
//
//     Buffer buf;
//     buf.append(chunk);
//     while (auto line = next_line(buf.readable())) {
//         buf.consume(line->size() + 1);
//     }
class Buffer {
 public:
  Buffer() noexcept : res_(mem::default_resource()) {}
  explicit Buffer(mem::Resource* res) noexcept : res_(res) {}

  Buffer(const Buffer&) = delete;
  Buffer& operator=(const Buffer&) = delete;

  Buffer(Buffer&& other) noexcept
      : data_(yasl::exchange(other.data_, nullptr)),
        cap_(yasl::exchange(other.cap_, size_t(0))),
        begin_(yasl::exchange(other.begin_, size_t(0))),
        end_(yasl::exchange(other.end_, size_t(0))),
        res_(other.res_) {}

  Buffer& operator=(Buffer&& other) noexcept {
    if (this != &other) {
      free_storage();
      data_ = yasl::exchange(other.data_, nullptr);
      cap_ = yasl::exchange(other.cap_, size_t(0));
      begin_ = yasl::exchange(other.begin_, size_t(0));
      end_ = yasl::exchange(other.end_, size_t(0));
      res_ = other.res_;
    }
    return *this;
  }

  ~Buffer() { free_storage(); }

  size_t size() const noexcept { return end_ - begin_; }
  bool empty() const noexcept { return begin_ == end_; }
  size_t capacity() const noexcept { return cap_; }
  mem::Resource* resource() const noexcept { return res_; }

  // The unread bytes. Valid until the next append/reserve.
  StringView readable() const noexcept { return {data_ + begin_, size()}; }

  // Makes room for n more bytes without further allocation.
  void reserve(size_t extra);

  void append(const char* data, size_t n) {
    if (n == 0) {
      return;
    }
    reserve(n);
    yasl::mem_copy(data_ + end_, data, n);
    end_ += n;
  }

  void append(StringView s) { append(s.data(), s.size()); }

  // Drops n bytes from the front.
  void consume(size_t n) noexcept {
    YASL_ASSERT(n <= size());
    begin_ += n;
    if (begin_ == end_) {
      begin_ = end_ = 0;
    }
  }

  void clear() noexcept { begin_ = end_ = 0; }

 private:
  void free_storage() noexcept {
    if (data_) {
      res_->deallocate(data_, cap_, 1);
    }
  }

  char* data_ = nullptr;
  size_t cap_ = 0;
  size_t begin_ = 0;
  size_t end_ = 0;
  mem::Resource* res_;
};
}  // namespace yasl::io
