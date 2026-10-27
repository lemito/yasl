#pragma once

#include <yasl/core/types.h>
#include <yasl/io/error.h>
#include <yasl/str/view.h>
#include <yasl/vocab/result.h>

namespace yasl {
// Owning handle to an OS file descriptor (RAII, move-only). All OS calls return
// a Result; the handle is a plain int, so a poller can be built on raw() later.
//
//     Fd fd = *Fd::open("data.bin", ...);
//     auto n = fd.read(buf, sizeof(buf));
//     if (!n) log(n.error());
class Fd {
 public:
  using Handle = int;
  static constexpr Handle kInvalid = -1;

  Fd() noexcept = default;
  explicit Fd(Handle fd) noexcept : fd_(fd) {}

  Fd(const Fd&) = delete;
  Fd& operator=(const Fd&) = delete;

  Fd(Fd&& other) noexcept : fd_(other.release()) {}

  Fd& operator=(Fd&& other) noexcept {
    if (this != &other) {
      reset(other.release());
    }
    return *this;
  }

  ~Fd() { reset(); }

  bool is_open() const noexcept { return fd_ != kInvalid; }
  explicit operator bool() const noexcept { return is_open(); }
  Handle raw() const noexcept { return fd_; }

  // Gives up ownership without closing.
  Handle release() noexcept {
    Handle h = fd_;
    fd_ = kInvalid;
    return h;
  }

  // Closes the current handle (errors ignored) and takes `fd`.
  void reset(Handle fd = kInvalid) noexcept;

  // Closes now and reports the error; the handle is gone either way.
  Result<void, io::Error> close();

  Result<Fd, io::Error> dup() const;
  Result<void, io::Error> set_nonblocking(bool on = true);
  Result<void, io::Error> set_cloexec(bool on = true);

  // Reads up to n bytes (0 = end of file); retries on EINTR.
  Result<size_t, io::Error> read(void* buf, size_t n);

  // Writes up to n bytes and returns how many went out; retries on EINTR.
  Result<size_t, io::Error> write(const void* data, size_t n);

 private:
  Handle fd_ = kInvalid;
};

// The two ends of an anonymous pipe (both close-on-exec).
//
//     Pipe p = *Pipe::create();
//     p.write.write("hi", 2);
//     p.read.read(buf, sizeof(buf));
struct Pipe {
  Fd read;
  Fd write;

  static Result<Pipe, io::Error> create();
};

// An anonymous file that lives in memory: memfd_create on Linux, an immediately
// unlinked shm_open object elsewhere. Open for reading and writing.
Result<Fd, io::Error> create_memfd(StringView name = "yasl");
}  // namespace yasl
