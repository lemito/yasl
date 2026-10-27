#pragma once

#include <yasl/conc/mutex.h>
#include <yasl/core/exception.h>
#include <yasl/mem/resource.h>
#include <yasl/os/fd.h>
#include <yasl/ptr/unique.h>
#include <yasl/str/str.h>

#include "error.h"
#include "input.h"
#include "output.h"

namespace yasl::io {
enum class OpenMode : uint32 {
  Read = 1,
  Write = 2,
  Append = 4,
  Create = 8,
  Truncate = 16,
  Exclusive = 32,
};

constexpr OpenMode operator|(OpenMode a, OpenMode b) noexcept {
  return OpenMode(uint32(a) | uint32(b));
}

constexpr bool has(OpenMode set, OpenMode flag) noexcept {
  return (uint32(set) & uint32(flag)) != 0;
}

// Owning (or borrowing) handle to an OS file descriptor. Move-only.
class File {
 public:
  File() noexcept = default;

  explicit File(int fd, bool owned = true) noexcept : fd_(fd), owned_(owned) {}

  explicit File(Fd fd) noexcept : fd_(yasl::move(fd)) {}

  File(File&& other) noexcept
      : fd_(yasl::move(other.fd_)), owned_(other.owned_) {}

  File& operator=(File&& other) noexcept {
    if (this != &other) {
      close_quietly();
      fd_ = yasl::move(other.fd_);
      owned_ = other.owned_;
    }
    return *this;
  }

  ~File() { close_quietly(); }

  static File open(StringView path, OpenMode mode, uint32 permissions = 0644);

  // Returns 0 at end of file. Retries on EINTR.
  size_t read(void* buf, size_t n);

  void write_all(const void* data, size_t n);

  uint64 size() const;
  void seek(uint64 offset);
  void sync();
  void close();

  bool is_open() const noexcept { return fd_.is_open(); }

  int fd() const noexcept { return fd_.raw(); }

 private:
  void close_quietly() noexcept;

  Fd fd_;
  bool owned_ = true;
};

// Buffered writer to a file. Flushes on destruction (errors there are
// swallowed; call flush() explicitly to observe them).
class FileOutput : public Output {
 public:
  static constexpr size_t kDefaultBuffer = 64 * 1024;

  explicit FileOutput(StringView path, OpenMode mode = OpenMode::Write |
                                                       OpenMode::Create |
                                                       OpenMode::Truncate);
  explicit FileOutput(File file, size_t buffer = kDefaultBuffer,
                      mem::Resource* res = mem::default_resource());
  ~FileOutput() override;

  File& file() noexcept;

 protected:
  void overflow(const char* data, size_t n) override;
  void do_flush() override;

 private:
  struct Impl;

  void drain();

  Unique<Impl> impl_;
};

class SharedOutput;

// Holds a SharedOutput's lock for its lifetime, so a series of writes stays
// together:
//
//     io::Locked out = io::out().lock();
//     out << "a=" << a << ' ';
//     out << "b=" << b << '\n';       // no other thread's output in between
class Locked {
 public:
  Locked(const Locked&) = delete;
  Locked& operator=(const Locked&) = delete;

  Locked(Locked&& other) noexcept : s_(yasl::exchange(other.s_, nullptr)) {}

  ~Locked();

  template <class T>
  Locked& operator<<(const T& v) {
    stream() << v;
    return *this;
  }

  template <class... Args>
  Locked& format(FormatString<TypeIdentity<Args>...> fmt, const Args&... args) {
    stream().Output::format(fmt, args...);
    return *this;
  }

  void write(StringView s) { stream().write(s); }

  void put(char c) { stream().put(c); }

  void flush() { stream().flush(); }

 private:
  friend class SharedOutput;

  explicit Locked(SharedOutput* s) noexcept : s_(s) {}

  Output& stream() noexcept;

  SharedOutput* s_;
};

// A FileOutput several threads may share: format() and println() build the
// whole text first and write it in one piece under a mutex, so lines from
// different threads never interleave. Plain operator<< and write() are NOT
// synchronized; use lock() around those.
class SharedOutput final : public FileOutput {
 public:
  using FileOutput::FileOutput;

  template <class... Args>
  SharedOutput& format(FormatString<TypeIdentity<Args>...> fmt,
                       const Args&... args) {
    detail::ScratchOutput text;
    text.format(fmt, args...);
    write_locked(text.view());
    return *this;
  }

  template <class... Args>
  SharedOutput& println(FormatString<TypeIdentity<Args>...> fmt,
                        const Args&... args) {
    detail::ScratchOutput text;
    text.format(fmt, args...);
    text.put('\n');
    write_locked(text.view());
    return *this;
  }

  // Writes s as one piece.
  void write_locked(StringView s) {
    LockGuard<Mutex> g(mutex_);
    write(s);
  }

  Locked lock() {
    mutex_.lock();
    return Locked(this);
  }

 private:
  friend class Locked;

  Mutex mutex_;
};

inline Locked::~Locked() {
  if (s_) {
    s_->mutex_.unlock();
  }
}

inline Output& Locked::stream() noexcept { return *s_; }

// Buffered reader from a file.
class FileInput final : public Input {
 public:
  static constexpr size_t kDefaultBuffer = 64 * 1024;

  explicit FileInput(StringView path);
  explicit FileInput(File file, size_t buffer = kDefaultBuffer,
                     mem::Resource* res = mem::default_resource());
  ~FileInput() override;

  File& file() noexcept;

 protected:
  bool underflow() override;

 private:
  struct Impl;

  Unique<Impl> impl_;
};

// Process-wide standard streams. out() is buffered and flushed at exit and
// before in() reads; err() is unbuffered. Not synchronized between threads.
SharedOutput& out();
SharedOutput& err();
Input& in();

// One line from stdin without the newline, none at end of input.
inline Optional<String> read_line() {
  String line;
  if (!in().read_line(line)) {
    return none;
  }
  return Optional<String>(yasl::move(line));
}

String read_file(StringView path);
void write_file(StringView path, StringView data);

template <class... Args>
void print(FormatString<TypeIdentity<Args>...> fmt, const Args&... args) {
  out().format(fmt, args...);
}

template <class... Args>
void eprintln(FormatString<TypeIdentity<Args>...> fmt, const Args&... args) {
  err().println(fmt, args...);
}

template <class... Args>
void println(FormatString<TypeIdentity<Args>...> fmt, const Args&... args) {
  out().println(fmt, args...);
}
}  // namespace yasl::io
