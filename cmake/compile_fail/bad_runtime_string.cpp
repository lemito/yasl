#include <yasl/str/builder.h>

void f() {
    yasl::String s("{}"); yasl::format(s.view(), 1);
}
