#include "resource.h"

#include <yasl/core/check.h>

#include "sys.h"
#include "thread_local_pool.h"

namespace yasl::mem {
namespace {
class SystemResource final : public Resource {
 protected:
  Block do_alloc(size_t size, size_t align) override {
    YASL_ASSERT(is_pow2(align));
    Block b = sys::alloc(size, align);
    if (!b.ptr) {
      throw BadAlloc();
    }
    return b;
  }

  void do_free(void* ptr, size_t size, size_t align) noexcept override {
    sys::free(ptr, size, align);
  }

  Block do_resize(void* ptr, size_t old_size, size_t want, size_t align,
                  bool relocatable) override {
    return relocatable ? sys::resize(ptr, old_size, want, align) : Block{};
  }
};

class NullResource final : public Resource {
 protected:
  Block do_alloc(size_t, size_t) override { throw BadAlloc(); }

  void do_free(void*, size_t, size_t) noexcept override {
    YASL_ASSERT(!"null_resource never allocates");
  }
};

// Constructed in place and never destroyed, so containers living in other
// static objects can still free memory during program shutdown.
template <class T>
T* immortal() noexcept {
  alignas(T) static unsigned char storage[sizeof(T)];
  static T* instance = ::new (storage) T();
  return instance;
}

thread_local Resource* t_default = nullptr;
}  // namespace

Resource* system_resource() noexcept { return immortal<SystemResource>(); }

Resource* null_resource() noexcept { return immortal<NullResource>(); }

Resource* default_resource() noexcept {
  Resource* res = t_default;
#ifdef YASL_DEFAULT_ALLOC_CHAIN
  return res ? res : chain_resource();
#else
  return res ? res : system_resource();
#endif
}

Resource* set_default_resource(Resource* res) noexcept {
  Resource* prev = default_resource();
  t_default = res;
  return prev;
}
}  // namespace yasl::mem
