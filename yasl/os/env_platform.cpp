#include "env_platform.h"

#include <yasl/io/file.h>

#if defined(__APPLE__)
#include <crt_externs.h>
#endif

namespace yasl::env::detail {
void append_process_args(Vector<String>& out) {
#if defined(__APPLE__)
  int argc = *::_NSGetArgc();
  char** argv = *::_NSGetArgv();
  for (int i = 0; i < argc; ++i) {
    out.push_back(String(argv[i], mem::system_resource()));
  }
#elif defined(_WIN32)
  // Windows callers should pass argc/argv through env::init.  The backend is
  // intentionally empty because the CRT command-line representation is not
  // equivalent to POSIX argv and requires an explicit parsing contract.
#else
  try {
    String raw = io::read_file("/proc/self/cmdline");
    for (StringView part : raw.view().split('\0')) {
      out.push_back(String(part, mem::system_resource()));
    }
    if (!out.empty() && out.back().empty()) {
      out.pop_back();
    }
  } catch (const Exception&) {
  }
#endif
}
}  // namespace yasl::env::detail
