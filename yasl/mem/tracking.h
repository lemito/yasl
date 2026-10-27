#pragma once

#include "resource.h"

namespace yasl::mem {
    // Forwards to upstream and counts what goes through. Great for tests and
    // for finding out how much a subsystem really allocates. Not thread-safe.
    class Tracking final: public Resource {
    public:
        struct Stats {
            size_t allocations = 0;
            size_t deallocations = 0;
            size_t bytes_in_use = 0;
            size_t peak_bytes = 0;
            size_t total_bytes = 0;

            size_t live() const noexcept {
                return allocations - deallocations;
            }
        };

        explicit Tracking(Resource* upstream = default_resource()) noexcept
            : upstream_(upstream)
        {
        }

        const Stats& stats() const noexcept {
            return stats_;
        }

        Resource* upstream() const noexcept {
            return upstream_;
        }

    protected:
        Block do_alloc(size_t size, size_t align) override {
            Block b = upstream_->allocate_at_least(size, align);
            ++stats_.allocations;
            stats_.bytes_in_use += b.size;
            stats_.total_bytes += b.size;
            stats_.peak_bytes = yasl::max(stats_.peak_bytes, stats_.bytes_in_use);
            return b;
        }

        void do_free(void* ptr, size_t size, size_t align) noexcept override {
            ++stats_.deallocations;
            // The caller may pass anything between the requested and the reported size.
            stats_.bytes_in_use -= yasl::min(size, stats_.bytes_in_use);
            upstream_->deallocate(ptr, size, align);
        }

    private:
        Resource* upstream_;
        Stats stats_;
    };
}
