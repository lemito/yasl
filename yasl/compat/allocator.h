#pragma once

// Interop layer: this header is allowed to use the standard library.

#include <yasl/mem/resource.h>

#include <type_traits>

namespace yasl::mem {
    // Standard-conforming allocator over a yasl Resource, for regular std containers:
    //
    //     mem::Arena arena;
    //     std::vector<int, mem::Allocator<int>> v(&arena);
    //     std::map<int, int, std::less<>, mem::Allocator<std::pair<const int, int>>> m(&arena);
    //
    // A default-constructed allocator captures the current default_resource(), so
    // containers created inside a ScopedDefault use that resource automatically.
    //
    // Propagation follows the data: move-assignment and swap carry the allocator
    // along (no element-wise moves), copy-assignment keeps the target's allocator.
    template <class T>
    class Allocator {
    public:
        using value_type = T;
        using propagate_on_container_copy_assignment = std::false_type;
        using propagate_on_container_move_assignment = std::true_type;
        using propagate_on_container_swap = std::true_type;
        using is_always_equal = std::false_type;

        Allocator() noexcept
            : res_(default_resource())
        {
        }

        Allocator(Resource* res) noexcept
            : res_(res)
        {
        }

        template <class U>
        Allocator(const Allocator<U>& other) noexcept
            : res_(other.resource())
        {
        }

        T* allocate(size_t n) {
            return res_->allocate_array<T>(n);
        }

        void deallocate(T* p, size_t n) noexcept {
            res_->deallocate_array(p, n);
        }

        Resource* resource() const noexcept {
            return res_;
        }

        template <class U>
        friend bool operator==(const Allocator& a, const Allocator<U>& b) noexcept {
            return a.res_->is_equal(*b.resource());
        }

    private:
        Resource* res_;
    };
}
