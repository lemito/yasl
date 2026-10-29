#pragma once

#include <yasl/core/span.h>
#include <yasl/str/str.h>
#include <yasl/str/view.h>
#include <yasl/vocab/optional.h>

namespace yasl::env {
// Value of an environment variable; the view points into the process
// environment, copy it if you keep it past a set_var / remove_var.
Optional<StringView> var(StringView name);

void set_var(StringView name, StringView value);
void remove_var(StringView name);

// Command-line arguments, program name first (Go os.Args). Read from
// /proc/self/cmdline (Linux) or _NSGetArgv (macOS), or taken from init()
// where neither is available.
Span<const String> args();

// Optional: hand over main's argc/argv explicitly.
void init(int argc, const char* const* argv);
}  // namespace yasl::env
