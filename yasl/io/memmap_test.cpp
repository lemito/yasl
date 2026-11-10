#include "memmap.h"

#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <yasl/io/file.h>
#include <yasl/os/fd.h>
#include <yasl/test/test.h>

using namespace yasl;
using namespace yasl::io;

namespace {
File sample_file() {
  File f(*create_memfd("memmap-test"));
  f.write_all("0123456789", 10);
  return f;
}
}  // namespace

YASL_TEST(memmap_read_ranges_and_fd_lifetime) {
  File f = sample_file();
  auto whole = MemMap::map(f.fd());
  auto part = MemMap::map(f.fd(), MemMapMode::ReadOnly, 3, 4);
  auto tail = MemMap::map(f.fd(), MemMapMode::ReadOnly, 7);
  f.close();
  CHECK_EQ(whole.view(), "0123456789");
  CHECK_EQ(part.view(), "3456");
  CHECK_EQ(tail.view(), "789");
  CHECK_EQ(part.bytes()[1], '4');
  CHECK_THROWS(part.mut_data(), BadAccess);
  part.flush();
  part.close();
  part.close();
  CHECK(part.empty());
  CHECK(part.data() == nullptr);
}

YASL_TEST(memmap_shared_and_private_writes) {
  File f = sample_file();
  auto shared = MemMap::map(f.fd(), MemMapMode::ReadWrite, 3, 4);
  shared.mut_bytes()[0] = 'X';
  shared.flush();
  char buf[10];
  f.seek(0);
  CHECK_EQ(f.read(buf, sizeof(buf)), sizeof(buf));
  CHECK_EQ(StringView(buf, 10), "012X456789");
  auto priv = MemMap::map(f.fd(), MemMapMode::CopyOnWrite, 3, 4);
  priv.mut_data()[0] = 'Y';
  priv.flush();
  CHECK_EQ(priv.view(), "Y456");
  CHECK_EQ(shared.view(), "X456");
  priv.close();
  f.seek(0);
  CHECK_EQ(f.read(buf, sizeof(buf)), sizeof(buf));
  CHECK_EQ(buf[3], 'X');
}

YASL_TEST(memmap_empty_and_invalid_ranges) {
  File f(*create_memfd("empty-map"));
  auto empty = MemMap::map(f.fd());
  CHECK(empty.empty());
  CHECK(empty.data() == nullptr);
  empty.flush();
  empty.close();
  CHECK_THROWS(MemMap::map(f.fd(), MemMapMode::ReadOnly, 1), OutOfRange);
  f.write_all("abc", 3);
  auto eof = MemMap::map(f.fd(), MemMapMode::ReadWrite, 3);
  CHECK(eof.empty());
  CHECK(eof.mut_data() == nullptr);
  auto zero = MemMap::map(f.fd(), MemMapMode::ReadOnly, 1, 0);
  CHECK(zero.empty());
  CHECK_THROWS(MemMap::map(f.fd(), MemMapMode::ReadOnly, 2, 2), OutOfRange);
  CHECK_THROWS(MemMap::map(f.fd(), MemMapMode::ReadOnly, uint64(-2)),
               OutOfRange);
  CHECK_THROWS(MemMap::map(-1), Error);
  CHECK_THROWS(MemMap::map(f.fd(), MemMapMode(99)), Error);
  Pipe pipe = *Pipe::create();
  CHECK_THROWS(MemMap::map(pipe.read.raw()), Error);
}

YASL_TEST(memmap_move_ownership) {
  File f = sample_file();
  auto first = MemMap::map(f.fd());
  const char* ptr = first.data();
  MemMap second(yasl::move(first));
  CHECK(first.empty());
  CHECK(first.data() == nullptr);
  CHECK(second.data() == ptr);
  auto third = MemMap::map(f.fd(), MemMapMode::ReadOnly, 5);
  third = yasl::move(second);
  CHECK(second.empty());
  CHECK(third.data() == ptr);
  auto* self = &third;
  third = yasl::move(*self);
  CHECK_EQ(third.view(), "0123456789");
}

YASL_TEST(memmap_multiple_pages_and_unaligned_flush) {
  File f(*create_memfd("large-map"));
  const size_t page = size_t(sysconf(_SC_PAGESIZE));
  CHECK(ftruncate(f.fd(), off_t(page * 3)) == 0);
  auto map = MemMap::map(f.fd(), MemMapMode::ReadWrite, page - 1, page + 2);
  map.mut_data()[0] = 'a';
  map.mut_data()[page + 1] = 'z';
  map.flush();
  auto read = MemMap::map(f.fd());
  CHECK_EQ(read.data()[page - 1], 'a');
  CHECK_EQ(read.data()[page * 2], 'z');
}

YASL_TEST(memmap_path_and_access_errors) {
  char path[] = "/tmp/yasl-memmap-XXXXXX";
  Fd fd(mkstemp(path));
  CHECK(fd.is_open());
  // Unlink even if a check or mapping throws.
  struct Cleanup {
    const char* path;
    ~Cleanup() { unlink(path); }
  } cleanup{path};
  CHECK_EQ(*fd.write("hello", 5), 5u);
  MemMap read(path);
  CHECK_EQ(read.view(), "hello");
  MemMap write(path, MemMapMode::ReadWrite, 1, 2);
  write.mut_data()[0] = 'A';
  write.flush();
  CHECK_EQ(read.view(), "hAllo");
  File ro = File::open(path, OpenMode::Read);
  CHECK_THROWS(MemMap::map(ro.fd(), MemMapMode::ReadWrite), Error);
  auto priv = MemMap::map(ro.fd(), MemMapMode::CopyOnWrite);
  priv.mut_data()[0] = 'X';
  CHECK_EQ(read.view(), "hAllo");
  CHECK_THROWS(MemMap("/dev/null"), Error);
}
