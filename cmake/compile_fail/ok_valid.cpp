#include <yasl/str/builder.h>

void f() { yasl::format("{} {:>5.2f} {1} {{x}} {:x}", 1, 2.5, 3); }
