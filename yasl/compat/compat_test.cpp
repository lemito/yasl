#include <yasl/compat/std.h>
#include <yasl/test/test.h>

#include <format>
#include <sstream>
#include <string>
#include <unordered_map>

using namespace yasl;
using namespace yasl::literals;

YASL_TEST(compat_std_interop) {
  std::unordered_map<String, int> m;
  m[String("one")] = 1;
  CHECK_EQ(m.at(String("one")), 1);

  std::string s = to_std_string("abc"_sv);
  CHECK(s == "abc");
  StringView back = s;
  CHECK_EQ(back, "abc"_sv);

  std::ostringstream os;
  os << String("str") << '/' << "view"_sv;
  CHECK(os.str() == "str/view");

  CHECK(std::format("[{:>5}]", String("ab")) == "[   ab]");

  StringBuilder b;
  b << std::string("from std");
  CHECK_EQ(b.view(), "from std"_sv);
}

YASL_TEST(compat_hash_smart_pointers) {
  auto u = yasl::make_unique<int>(1);
  CHECK_EQ(std::hash<yasl::Unique<int>>()(u), std::hash<int*>()(u.get()));
  auto s = yasl::make_shared<int>(2);
  auto s2 = s;
  CHECK_EQ(std::hash<yasl::Shared<int>>()(s),
           std::hash<yasl::Shared<int>>()(s2));
  std::unordered_map<yasl::Shared<int>, int> m;
  m[s] = 5;
  CHECK_EQ(m.at(s2), 5);
}
