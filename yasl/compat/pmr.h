#pragma once

// Interop layer: this header is allowed to use the standard library.

#include <yasl/mem/resource.h>

#include <memory_resource>

namespace yasl::mem {
    // yasl Resource -> std::pmr::memory_resource.
    //
    //     mem::Arena arena;
    //     mem::PmrAdapter pmr(&arena);
    //     std::pmr::vector<int> v(&pmr);
    class PmrAdapter final: public std::pmr::memory_resource {
    public:
        explicit PmrAdapter(Resource* res) noexcept
            : res_(res)
        {
        }

        Resource* resource() const noexcept {
            return res_;
        }

    private:
        void* do_allocate(size_t size, size_t align) override {
            return res_->allocate(size, align);
        }

        void do_deallocate(void* p, size_t size, size_t align) override {
            res_->deallocate(p, size, align);
        }

        bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
            auto* o = dynamic_cast<const PmrAdapter*>(&other);
            return o && res_->is_equal(*o->res_);
        }

        Resource* res_;
    };

    // std::pmr::memory_resource -> yasl Resource.
    class FromPmr final: public Resource {
    public:
        explicit FromPmr(std::pmr::memory_resource* upstream) noexcept
            : upstream_(upstream)
        {
        }

    protected:
        Block do_alloc(size_t size, size_t align) override {
            return {upstream_->allocate(size, align), size};
        }

        void do_free(void* ptr, size_t size, size_t align) noexcept override {
            upstream_->deallocate(ptr, size, align);
        }

    private:
        std::pmr::memory_resource* upstream_;
    };
}
