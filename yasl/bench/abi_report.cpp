#include <stdio.h>

#include <yasl/conc/thread_pool.h>
#include <yasl/mem/resource.h>
#include <yasl/ptr/shared.h>
#include <yasl/ptr/unique.h>
#include <yasl/str/mutstr.h>
#include <yasl/str/str.h>
#include <yasl/time/time.h>

namespace bench {
void abi_report() {
  // Keep this report outside the CSV stream so existing benchmark consumers
  // remain compatible. It is intended for release notes and ABI baselines.
  fprintf(stderr, "yasl ABI sizes (bytes):\n");
  fprintf(stderr, "  Duration=%zu Instant=%zu SystemTime=%zu\n",
          sizeof(yasl::Duration), sizeof(yasl::Instant),
          sizeof(yasl::SystemTime));
  fprintf(stderr, "  String=%zu MutStr=%zu StringView=%zu\n",
          sizeof(yasl::String), sizeof(yasl::MutStr), sizeof(yasl::StringView));
  fprintf(stderr, "  Unique<int>=%zu Shared<int>=%zu Resource*=%zu\n",
          sizeof(yasl::Unique<int>), sizeof(yasl::Shared<int>),
          sizeof(yasl::mem::Resource*));
}
}  // namespace bench
