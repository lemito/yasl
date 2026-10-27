#pragma once

#include <yasl/atomic.h>
#include <yasl/mem/resource.h>
#include <yasl/sys/check.h>
#include <yasl/sys/utility.h>

namespace yasl {
    // Atomically reference-counted shared pointer with a single allocation
    // (counter and object live in one block taken from a mem::Resource).
    //
    //     auto cfg = make_arc<Config>(...);
    //     auto copy = cfg;              // +1, no allocation
    //     copy.make_mut().port = 8080;  // clones only if shared (copy-on-write)
    template <class T>
    class Arc {
        struct Block {
            RefCount rc;
            mem::Resource* res;
            T value;

            template <class... Args>
            explicit Block(mem::Resource* r, Args&&... args)
                : res(r)
                , value(yasl::forward<Args>(args)...)
            {
            }
        };

    public:
        Arc() noexcept = default;

        Arc(decltype(nullptr)) noexcept {
        }

        Arc(const Arc& other) noexcept
            : b_(other.b_)
        {
            if (b_) {
                b_->rc.ref();
            }
        }

        Arc(Arc&& other) noexcept
            : b_(yasl::exchange(other.b_, nullptr))
        {
        }

        ~Arc() {
            release();
        }

        Arc& operator=(const Arc& other) noexcept {
            Arc(other).swap(*this);
            return *this;
        }

        Arc& operator=(Arc&& other) noexcept {
            Arc(yasl::move(other)).swap(*this);
            return *this;
        }

        template <class... Args>
        static Arc make_in(mem::Resource* res, Args&&... args) {
            return Arc(res->create<Block>(res, yasl::forward<Args>(args)...));
        }

        template <class... Args>
        static Arc make(Args&&... args) {
            return make_in(mem::default_resource(), yasl::forward<Args>(args)...);
        }

        T* get() const noexcept {
            return b_ ? &b_->value : nullptr;
        }

        T& operator*() const noexcept {
            YASL_ASSERT(b_);
            return b_->value;
        }

        T* operator->() const noexcept {
            YASL_ASSERT(b_);
            return &b_->value;
        }

        explicit operator bool() const noexcept {
            return b_ != nullptr;
        }

        size_t use_count() const noexcept {
            return b_ ? b_->rc.count() : 0;
        }

        bool unique() const noexcept {
            return b_ && b_->rc.unique();
        }

        // Mutable access with copy-on-write: clones the value if it is shared.
        T& make_mut()
            requires CopyConstructible<T>
        {
            YASL_ASSERT(b_);
            if (!b_->rc.unique()) {
                *this = make_in(b_->res, static_cast<const T&>(b_->value));
            }
            return b_->value;
        }

        void reset() noexcept {
            release();
            b_ = nullptr;
        }

        void swap(Arc& other) noexcept {
            yasl::swap(b_, other.b_);
        }

        friend bool operator==(const Arc& a, const Arc& b) noexcept {
            return a.b_ == b.b_;
        }

        friend bool operator==(const Arc& a, decltype(nullptr)) noexcept {
            return a.b_ == nullptr;
        }

    private:
        explicit Arc(Block* b) noexcept
            : b_(b)
        {
        }

        void release() noexcept {
            if (b_ && b_->rc.unref()) {
                b_->res->destroy(b_);
            }
        }

        Block* b_ = nullptr;
    };

    template <class T, class... Args>
    Arc<T> make_arc(Args&&... args) {
        return Arc<T>::make(yasl::forward<Args>(args)...);
    }

    template <class T, class... Args>
    Arc<T> make_arc_in(mem::Resource* res, Args&&... args) {
        return Arc<T>::make_in(res, yasl::forward<Args>(args)...);
    }
}
