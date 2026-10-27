#include "resource.h"

#include <yasl/sys/check.h>

#include <stdlib.h>

namespace yasl::mem {
    namespace {
        class SystemResource final: public Resource {
        protected:
            Block do_alloc(size_t size, size_t align) override {
                YASL_ASSERT(is_pow2(align));
                if (size == 0) {
                    size = 1;
                }
                void* p = nullptr;
                if (align <= kMaxAlign) {
                    p = ::malloc(size);
                } else if (::posix_memalign(&p, align, size) != 0) {
                    p = nullptr;
                }
                if (!p) {
                    throw BadAlloc();
                }
                return {p, size};
            }

            void do_free(void* ptr, size_t, size_t) noexcept override {
                ::free(ptr);
            }
        };

        class NullResource final: public Resource {
        protected:
            Block do_alloc(size_t, size_t) override {
                throw BadAlloc();
            }

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
    }

    Resource* system_resource() noexcept {
        return immortal<SystemResource>();
    }

    Resource* null_resource() noexcept {
        return immortal<NullResource>();
    }

    Resource* default_resource() noexcept {
        Resource* res = t_default;
        return res ? res : system_resource();
    }

    Resource* set_default_resource(Resource* res) noexcept {
        Resource* prev = default_resource();
        t_default = res;
        return prev;
    }
}
