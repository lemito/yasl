#include <string.h>
#include <yasl/bench/harness.h>

int main(int argc, char** argv) {
  bool csv = false;
  const char* filter = nullptr;
  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--csv") == 0) {
      csv = true;
    } else {
      filter = argv[i];
    }
  }

  bench::Bencher b(filter, csv);
  if (csv) {
    printf("\"group\",\"name\",\"ns_per_op\"\n");
  }
  bench::alloc_benches(b);
  bench::cont_benches(b);
  bench::str_benches(b);
  return 0;
}
