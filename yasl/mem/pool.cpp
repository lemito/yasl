#include "pool.h"

#include <yasl/sys/check.h>

namespace yasl::mem {
    Pool::Pool(Resource* upstream, Options opts) noexcept
        : upstream_(upstream)
        , opts_(opts)
    {
        size_t largest = kMinBlock << (kMaxClasses - 1);
        opts_.max_block = bit_ceil(yasl::clamp(opts_.max_block, kMinBlock, largest));
    }

    size_t Pool::class_of(size_t size, size_t align) const noexcept {
        if (align > kMaxAlign) {
            return kNoClass;
        }
        size_t need = yasl::max(yasl::max(size, align), kMinBlock);
        if (need > opts_.max_block) {
            return kNoClass;
        }
        return size_t(bit_width(uint64(need - 1))) - 3;
    }

    Block Pool::do_alloc(size_t size, size_t align) {
        YASL_ASSERT(is_pow2(align));
        size_t idx = class_of(size, align);
        if (idx == kNoClass) {
            return upstream_->allocate_at_least(size, align);
        }
        size_t block = kMinBlock << idx;
        SizeClass& c = classes_[idx];
        if (c.free) {
            FreeNode* n = c.free;
            c.free = n->next;
            return {n, block};
        }
        if (size_t(c.end - c.cur) < block) {
            refill(c, block);
        }
        void* p = c.cur;
        c.cur += block;
        return {p, block};
    }

    void Pool::do_free(void* ptr, size_t size, size_t align) noexcept {
        size_t idx = class_of(size, align);
        if (idx == kNoClass) {
            upstream_->deallocate(ptr, size, align);
            return;
        }
        SizeClass& c = classes_[idx];
        c.free = ::new (ptr) FreeNode{c.free};
    }

    void Pool::refill(SizeClass& c, size_t block) {
        size_t header = align_up(sizeof(Slab), kMaxAlign);
        size_t want = yasl::max(opts_.slab_size, header + block * 4);
        Block b = upstream_->allocate_at_least(want, kMaxAlign);
        slabs_ = ::new (b.ptr) Slab{slabs_, b.size};
        c.cur = static_cast<char*>(b.ptr) + header;
        c.end = c.cur + (b.size - header) / block * block;
    }

    void Pool::release() noexcept {
        while (slabs_) {
            Slab* next = slabs_->next;
            upstream_->deallocate(slabs_, slabs_->size, kMaxAlign);
            slabs_ = next;
        }
        for (SizeClass& c : classes_) {
            c = SizeClass{};
        }
    }
}
