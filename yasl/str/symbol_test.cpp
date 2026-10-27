#include <yasl/cont/assoc/hash_map.h>
#include <yasl/mem/tracking.h>
#include <yasl/str/symbol.h>
#include <yasl/test/test.h>

using namespace yasl;

YASL_TEST(symbol_equal_strings_equal_symbols) {
  SymbolTable t;
  String built = String("fo") + "o";
  Symbol a = t.intern("foo");
  Symbol b = t.intern(built.view());
  Symbol c = t.intern("bar");
  CHECK(a == b);
  CHECK(!(a == c));
  CHECK_EQ(a.view(), "foo");
  CHECK_EQ(c.size(), 3u);
  CHECK_EQ(t.size(), 2u);
  CHECK(a.view().data() != built.data());  // the symbol owns its own copy
}

YASL_TEST(symbol_empty_string_and_default) {
  SymbolTable t;
  Symbol e = t.intern("");
  CHECK(e);
  CHECK(e.empty());
  CHECK(e == t.intern(""));
  Symbol none_yet;
  CHECK(!none_yet);
  CHECK(!(none_yet == e));
}

YASL_TEST(symbol_find) {
  SymbolTable t;
  CHECK(!t.find("x").has_value());
  Symbol x = t.intern("x");
  CHECK(t.find("x").has_value());
  CHECK(*t.find("x") == x);
  CHECK_EQ(t.size(), 1u);
}

YASL_TEST(symbol_as_hash_key) {
  SymbolTable t;
  HashMap<Symbol, int> counts;
  for (int i = 0; i < 3000; ++i) {
    String word = String("w") + to_string(i % 100);
    counts[t.intern(word.view())] += 1;
  }
  CHECK_EQ(counts.size(), 100u);
  CHECK_EQ(counts.at(t.intern("w7")), 30);
}

YASL_TEST(symbol_no_leaks) {
  mem::Tracking tr;
  {
    SymbolTable t(&tr);
    for (int i = 0; i < 2000; ++i) {
      t.intern(to_string(i).view());
    }
    CHECK_EQ(t.size(), 2000u);
  }
  CHECK_EQ(tr.stats().live(), 0u);
}
