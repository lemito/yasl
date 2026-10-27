#include "fd.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <atomic>

namespace yasl {
namespace {
io::Error fail(const char* op) { return io::Error(op, "fd", errno); }
}  // namespace

void Fd::reset(Handle fd) noexcept {
  if (fd_ != kInvalid) {
    ::close(fd_);
  }
  fd_ = fd;
}

Result<void, io::Error> Fd::close() {
  Handle h = release();
  if (h != kInvalid && ::close(h) != 0 && errno != EINTR) {
    return err(fail("close"));
  }
  return ok();
}

Result<Fd, io::Error> Fd::dup() const {
  int copy = ::fcntl(fd_, F_DUPFD_CLOEXEC, 0);
  if (copy < 0) {
    return err(fail("dup"));
  }
  return ok(Fd(copy));
}

Result<void, io::Error> Fd::set_nonblocking(bool on) {
  int flags = ::fcntl(fd_, F_GETFL);
  if (flags < 0) {
    return err(fail("fcntl"));
  }
  int wanted = on ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
  if (wanted != flags && ::fcntl(fd_, F_SETFL, wanted) < 0) {
    return err(fail("fcntl"));
  }
  return ok();
}

Result<void, io::Error> Fd::set_cloexec(bool on) {
  int flags = ::fcntl(fd_, F_GETFD);
  if (flags < 0) {
    return err(fail("fcntl"));
  }
  int wanted = on ? (flags | FD_CLOEXEC) : (flags & ~FD_CLOEXEC);
  if (wanted != flags && ::fcntl(fd_, F_SETFD, wanted) < 0) {
    return err(fail("fcntl"));
  }
  return ok();
}

Result<size_t, io::Error> Fd::read(void* buf, size_t n) {
  for (;;) {
    ssize_t r = ::read(fd_, buf, n);
    if (r >= 0) {
      return ok(size_t(r));
    }
    if (errno != EINTR) {
      return err(fail("read"));
    }
  }
}

Result<size_t, io::Error> Fd::write(const void* data, size_t n) {
  for (;;) {
    ssize_t w = ::write(fd_, data, n);
    if (w >= 0) {
      return ok(size_t(w));
    }
    if (errno != EINTR) {
      return err(fail("write"));
    }
  }
}

Result<Pipe, io::Error> Pipe::create() {
  int fds[2];
#if defined(__linux__)
  if (::pipe2(fds, O_CLOEXEC) != 0) {
    return err(fail("pipe"));
  }
#else
  if (::pipe(fds) != 0) {
    return err(fail("pipe"));
  }
  Pipe p{Fd(fds[0]), Fd(fds[1])};
  YASL_TRY_VOID(p.read.set_cloexec());
  YASL_TRY_VOID(p.write.set_cloexec());
  return ok(yasl::move(p));
#endif
  return ok(Pipe{Fd(fds[0]), Fd(fds[1])});
}

Result<Fd, io::Error> create_memfd(StringView name) {
  char buf[256];
  size_t n = name.size() < sizeof(buf) - 16 ? name.size() : sizeof(buf) - 16;
#if defined(__linux__)
  yasl::mem_copy(buf, name.data(), n);
  buf[n] = '\0';
  int fd = ::memfd_create(buf, MFD_CLOEXEC);
  if (fd < 0) {
    return err(fail("memfd_create"));
  }
  return ok(Fd(fd));
#else
  // shm_open needs a unique "/name"; unlink it right away so only the
  // descriptor remains.
  static std::atomic<unsigned> counter{0};
  buf[0] = '/';
  yasl::mem_copy(buf + 1, name.data(), n);
  unsigned id = counter.fetch_add(1) + 1;
  size_t len = 1 + n;
  buf[len++] = '-';
  char digits[12];
  int d = 0;
  while (id) {
    digits[d++] = char('0' + id % 10);
    id /= 10;
  }
  while (d) {
    buf[len++] = digits[--d];
  }
  buf[len] = '\0';
  int fd = ::shm_open(buf, O_RDWR | O_CREAT | O_EXCL, 0600);
  if (fd < 0) {
    return err(fail("shm_open"));
  }
  ::shm_unlink(buf);
  Fd out(fd);
  YASL_TRY_VOID(out.set_cloexec());
  return ok(yasl::move(out));
#endif
}
}  // namespace yasl
