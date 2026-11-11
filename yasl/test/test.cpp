#include "test.h"

#include <yasl/cont/seq/vector.h>
#include <yasl/io/file.h>

namespace yasl::test {
namespace {
struct Failure {
  String message;
};

Vector<Pair<const char*, Fn>>& registry() {
  static Vector<Pair<const char*, Fn>> tests(mem::system_resource());
  return tests;
}
}  // namespace

void register_test(const char* name, Fn fn) {
  registry().push_back({name, fn});
}

void fail(const char* file, int line, StringView message) {
  // Failure objects outlive the test body.  Copy diagnostics to the process
  // resource so a scoped test resource cannot dangle before the catch block.
  StringBuilder b(mem::system_resource());
  b.format("{}:{}: {}", file, line, message);
  throw Failure{b.take()};
}

// Usage: yasl_tests [substring-filter]
int run(int argc, char** argv) {
  StringView filter = argc > 1 ? argv[1] : "";
  io::Output& out = io::out();
  int passed = 0;
  int failed = 0;
  for (auto [name, fn] : registry()) {
    if (!StringView(name).contains(filter)) {
      continue;
    }
    try {
      fn();
      ++passed;
      out.format("[  OK  ] {}\n", name);
    } catch (const Failure& f) {
      ++failed;
      out.format("[ FAIL ] {}\n         {}\n", name, f.message);
    } catch (const Exception& e) {
      ++failed;
      out.format("[ FAIL ] {}\n         unexpected yasl exception: {}\n", name,
                 e.what());
    } catch (...) {
      ++failed;
      out.format("[ FAIL ] {}\n         unexpected exception\n", name);
    }
    out.flush();
  }
  out.format("\n{} passed, {} failed\n", passed, failed);
  out.flush();
  return failed == 0 ? 0 : 1;
}
}  // namespace yasl::test

int main(int argc, char** argv) { return yasl::test::run(argc, argv); }
