#include <fcntl.h>
#include <unistd.h>
#include <yasl/os/fd.h>
#include <yasl/test/test.h>

using namespace yasl;

YASL_TEST(fd_pipe_roundtrip) {
  Pipe p = *Pipe::create();
  CHECK(p.read.is_open());
  CHECK(p.write.is_open());
  CHECK_EQ(*p.write.write("hello", 5), 5u);
  char buf[16];
  CHECK_EQ(*p.read.read(buf, sizeof(buf)), 5u);
  CHECK_EQ(StringView(buf, 5), "hello");
  CHECK(p.write.close().is_ok());
  CHECK(!p.write.is_open());
  CHECK_EQ(*p.read.read(buf, sizeof(buf)),
           0u);  // end of file after the writer closed
}

YASL_TEST(fd_move_release_and_close) {
  Pipe p = *Pipe::create();
  int raw = p.read.raw();
  Fd moved = yasl::move(p.read);
  CHECK(!p.read.is_open());
  CHECK_EQ(moved.raw(), raw);
  int released = moved.release();
  CHECK(!moved.is_open());
  CHECK_EQ(released, raw);
  Fd again(released);  // takes ownership back
  CHECK(again);
}

YASL_TEST(fd_dup_and_flags) {
  Pipe p = *Pipe::create();
  Fd copy = *p.write.dup();
  CHECK(copy.raw() != p.write.raw());
  CHECK_EQ(*copy.write("x", 1), 1u);
  char c;
  CHECK_EQ(*p.read.read(&c, 1), 1u);
  CHECK_EQ(c, 'x');

  CHECK(p.read.set_nonblocking().is_ok());
  auto r = p.read.read(&c, 1);
  CHECK(r.is_err());
  CHECK_EQ(r.error().code(), EAGAIN);
  CHECK(p.read.set_nonblocking(false).is_ok());

  CHECK(p.read.set_cloexec(false).is_ok());
  CHECK((fcntl(p.read.raw(), F_GETFD) & FD_CLOEXEC) == 0);
  CHECK(p.read.set_cloexec().is_ok());
  CHECK((fcntl(p.read.raw(), F_GETFD) & FD_CLOEXEC) != 0);
}

YASL_TEST(fd_errors) {
  Fd bad;
  char c;
  auto r = bad.read(&c, 1);
  CHECK(r.is_err());
  CHECK_EQ(r.error().code(), EBADF);
  CHECK(bad.close().is_ok());  // closing an empty handle is a no-op
}

YASL_TEST(fd_memfd) {
  Fd f = *create_memfd("test");
  CHECK_EQ(*f.write("abcdef", 6), 6u);
  CHECK(lseek(f.raw(), 0, SEEK_SET) == 0);
  char buf[8];
  CHECK_EQ(*f.read(buf, sizeof(buf)), 6u);
  CHECK_EQ(StringView(buf, 6), "abcdef");
}
