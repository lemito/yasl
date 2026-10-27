#include <yasl/io/buffer.h>
#include <yasl/mem/tracking.h>
#include <yasl/test/test.h>

using namespace yasl;

YASL_TEST(buffer_append_consume) {
  io::Buffer b;
  CHECK(b.empty());
  b.append("hello ");
  b.append(StringView("world"));
  CHECK_EQ(b.readable(), "hello world");
  b.consume(6);
  CHECK_EQ(b.readable(), "world");
  b.consume(5);
  CHECK(b.empty());
  b.append("again");
  CHECK_EQ(b.readable(), "again");
}

YASL_TEST(buffer_reuses_consumed_space) {
  mem::Tracking t;
  {
    io::Buffer b(&t);
    b.reserve(100);
    size_t cap = b.capacity();
    size_t allocs = t.stats().allocations;
    for (int i = 0; i < 1000; ++i) {
      b.append("0123456789");
      b.consume(10);
    }
    CHECK_EQ(b.capacity(), cap);
    CHECK_EQ(t.stats().allocations, allocs);

    // Keep a tail, then keep appending: the live bytes slide to the front.
    b.append("0123456789abcdef");
    b.consume(10);
    for (int i = 0; i < 1000; ++i) {
      b.append("xxxx");
      b.consume(4);
    }
    CHECK_EQ(b.readable(), "xxxxxx");
    CHECK_EQ(t.stats().allocations, allocs);
  }
  CHECK_EQ(t.stats().live(), 0u);
}

YASL_TEST(buffer_grows_and_moves) {
  mem::Tracking t;
  {
    io::Buffer b(&t);
    for (int i = 0; i < 10000; ++i) {
      b.append("abc");
    }
    CHECK_EQ(b.size(), 30000u);
    CHECK(b.readable().starts_with("abcabc"));
    io::Buffer c = yasl::move(b);
    CHECK(b.empty());
    CHECK_EQ(c.size(), 30000u);
    c.clear();
    CHECK(c.empty());
  }
  CHECK_EQ(t.stats().live(), 0u);
}
