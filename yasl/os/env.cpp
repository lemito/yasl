#include "env.h"

#include <stdlib.h>
#include <yasl/cont/seq/vector.h>
#include <yasl/io/file.h>

namespace yasl::env {
namespace {
// Process-lifetime storage that is never destroyed (usable from static
// destructors).
Vector<String>& storage() {
  alignas(Vector<String>) static unsigned char buf[sizeof(Vector<String>)];
  static Vector<String>* v = ::new (buf) Vector<String>(mem::system_resource());
  return *v;
}

bool g_ready = false;
}  // namespace

Optional<StringView> var(StringView name) {
  String key(name, mem::system_resource());
  const char* v = ::getenv(key.c_str());
  if (!v) {
    return none;
  }
  return StringView(v);
}

void set_var(StringView name, StringView value) {
  String k(name, mem::system_resource());
  String v(value, mem::system_resource());
  ::setenv(k.c_str(), v.c_str(), 1);
}

void remove_var(StringView name) {
  String k(name, mem::system_resource());
  ::unsetenv(k.c_str());
}

void init(int argc, const char* const* argv) {
  Vector<String>& out = storage();
  out.clear();
  for (int i = 0; i < argc; ++i) {
    out.push_back(String(argv[i], mem::system_resource()));
  }
  g_ready = true;
}

Span<const String> args() {
  if (!g_ready) {
    Vector<String>& out = storage();
    try {
      String raw = io::read_file("/proc/self/cmdline");
      for (StringView part : raw.view().split('\0')) {
        out.push_back(String(part, mem::system_resource()));
      }
      if (!out.empty() && out.back().empty()) {
        out.pop_back();  // the data ends with a NUL
      }
    } catch (const Exception&) {
    }
    g_ready = true;
  }
  return Span<const String>(storage());
}
}  // namespace yasl::env
