#include <yasl/os/env.h>
#include <yasl/test/test.h>

using namespace yasl;
using namespace yasl::literals;

YASL_TEST(env_vars) {
  CHECK(!env::var("YASL_TEST_SURELY_UNSET").has_value());
  env::set_var("YASL_TEST_VAR", "hello");
  CHECK_EQ(*env::var("YASL_TEST_VAR"), "hello");
  env::set_var("YASL_TEST_VAR", "");
  CHECK_EQ(env::var("YASL_TEST_VAR")->size(), size_t(0));
  env::remove_var("YASL_TEST_VAR");
  CHECK(!env::var("YASL_TEST_VAR").has_value());
  CHECK(env::var("PATH").has_value());
}

YASL_TEST(env_args) {
  Span<const String> a = env::args();
  CHECK(a.size() >= 1);
  CHECK(!a[0].empty());
  const char* fake[] = {"prog", "-x", "file"};
  env::init(3, fake);
  CHECK_EQ(env::args().size(), size_t(3));
  CHECK_EQ(env::args()[1], "-x");
}
