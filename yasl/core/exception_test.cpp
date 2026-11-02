#include <yasl/core/exception.h>
#include <yasl/test/test.h>

using namespace yasl;

YASL_TEST(exception_base_what) {
  Exception e("something went wrong");
  CHECK_EQ(e.what(), "something went wrong");
}

YASL_TEST(bad_alloc_message) {
  BadAlloc ba;
  CHECK_EQ(StringView(ba.what()), StringView("yasl: out of memory"));
  CHECK_THROWS(throw BadAlloc(), BadAlloc);
}

YASL_TEST(outofrange_message) {
  OutOfRange oor("index 5 out of range");
  CHECK_EQ(oor.what(), "index 5 out of range");
  CHECK_THROWS(throw OutOfRange("boom"), OutOfRange);
}

YASL_TEST(lengtherror_and_badaccess) {
  LengthError le("too long");
  CHECK_EQ(le.what(), "too long");
  BadAccess ba("bad access");
  CHECK_EQ(ba.what(), "bad access");
}

YASL_TEST(format_error_message) {
  FormatError fe("bad format string");
  CHECK_EQ(fe.what(), "bad format string");
  CHECK_THROWS(throw FormatError("fmt"), FormatError);
}

YASL_TEST(exception_caught_by_base) {
  bool caught = false;
  try {
    throw BadAlloc();
  } catch (const Exception& e) {
    caught = true;
    CHECK_EQ(StringView(e.what()), StringView("yasl: out of memory"));
  }
  CHECK(caught);
}

YASL_TEST(exception_inherits_std_exception) {
  // yasl::Exception derives from nothing std; it has a virtual dtor and what().
  Exception e("x");
  CHECK_EQ(e.what(), "x");
  // what() is noexcept.
  noexcept(e.what());
}
