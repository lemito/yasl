#pragma once

#include <yasl/mem/resource.h>
#include <yasl/sys/algo.h>
#include <yasl/sys/check.h>
#include <yasl/sys/exception.h>
#include <yasl/sys/optional.h>
#include <yasl/sys/span.h>
#include <yasl/sys/utility.h>
#include <yasl/vec/ops.h>

#include <initializer_list> // language support for braced lists

namespace yasl {
    // Dynamic array that keeps its first N elements inside the object and only allocates from
    // the mem::Resource when it outgrows them (LLVM SmallVector). Once it moved to the heap it
    // stays there; the inline space is not reused until clear_and_shrink().
    //
    //     SmallVector<int, 8> v;          // no allocation for up to 8 elements
    //
    // Moving takes the heap buffer over when there is one, otherwise it moves element by
    // element (references stay valid only for heap buffers).
    template <class T, size_t N>
    class SmallVector {
        static_assert(N > 0);

    public:
        using value_type = T;
        using iterator = T*;
        using const_iterator = const T*;

        SmallVector() noexcept
            : res_(mem::default_resource())
        {
        }

        explicit SmallVector(mem::Resource* res) noexcept
            : res_(res)
        {
        }

        SmallVector(std::initializer_list<T> il, mem::Resource* res = mem::default_resource())
            : res_(res)
        {
            append(il.begin(), il.end());
        }

        SmallVector(const SmallVector& other)
            : SmallVector(other, other.res_)
        {
        }

        SmallVector(const SmallVector& other, mem::Resource* res)
            : res_(res)
        {
            append(other.begin(), other.end());
        }

        SmallVector(SmallVector&& other) noexcept(NothrowMoveConstructible<T>)
            : res_(other.res_)
        {
            take(other);
        }

        ~SmallVector() {
            clear();
            release();
        }

        SmallVector& operator=(const SmallVector& other) {
            if (this != &other) {
                SmallVector tmp(other, res_);
                swap(tmp);
            }
            return *this;
        }

        SmallVector& operator=(SmallVector&& other) noexcept(NothrowMoveConstructible<T>) {
            if (this != &other) {
                clear();
                release();
                res_ = other.res_;
                take(other);
            }
            return *this;
        }

        T& operator[](size_t i) noexcept {
            YASL_ASSERT(i < size_);
            return data_[i];
        }

        const T& operator[](size_t i) const noexcept {
            YASL_ASSERT(i < size_);
            return data_[i];
        }

        T& at(size_t i) {
            if (i >= size_) {
                throw OutOfRange("yasl::SmallVector::at");
            }
            return data_[i];
        }

        const T& at(size_t i) const {
            if (i >= size_) {
                throw OutOfRange("yasl::SmallVector::at");
            }
            return data_[i];
        }

        T& front() noexcept {
            return (*this)[0];
        }

        const T& front() const noexcept {
            return (*this)[0];
        }

        T& back() noexcept {
            return (*this)[size_ - 1];
        }

        const T& back() const noexcept {
            return (*this)[size_ - 1];
        }

        T* data() noexcept {
            return data_;
        }

        const T* data() const noexcept {
            return data_;
        }

        Span<T> span() noexcept {
            return {data_, size_};
        }

        Span<const T> span() const noexcept {
            return {data_, size_};
        }

        T* begin() noexcept {
            return data_;
        }

        T* end() noexcept {
            return data_ + size_;
        }

        const T* begin() const noexcept {
            return data_;
        }

        const T* end() const noexcept {
            return data_ + size_;
        }

        bool empty() const noexcept {
            return size_ == 0;
        }

        size_t size() const noexcept {
            return size_;
        }

        size_t capacity() const noexcept {
            return cap_;
        }

        // True while the elements still live inside the object.
        bool is_inline() const noexcept {
            return data_ == inline_data();
        }

        mem::Resource* resource() const noexcept {
            return res_;
        }

        static constexpr size_t max_size() noexcept {
            return max_value<size_t>() / sizeof(T);
        }

        void reserve(size_t n) {
            if (n > cap_) {
                reallocate(n);
            }
        }

        void clear() noexcept {
            yasl::destroy_n(data_, size_);
            size_ = 0;
        }

        // Drops the elements and goes back to the inline buffer.
        void clear_and_shrink() noexcept {
            clear();
            release();
        }

        void push_back(const T& value) {
            emplace_back(value);
        }

        void push_back(T&& value) {
            emplace_back(yasl::move(value));
        }

        template <class... Args>
        T& emplace_back(Args&&... args) {
            if (size_ < cap_) [[likely]] {
                T* p = yasl::construct_at(data_ + size_, yasl::forward<Args>(args)...);
                ++size_;
                return *p;
            }
            // Build first: args may refer to an element that the reallocation moves.
            T tmp(yasl::forward<Args>(args)...);
            reallocate(grow_capacity(size_ + 1));
            T* p = yasl::construct_at(data_ + size_, yasl::move(tmp));
            ++size_;
            return *p;
        }

        void pop_back() noexcept {
            YASL_ASSERT(size_);
            yasl::destroy_at(data_ + --size_);
        }

        T pop() {
            YASL_ASSERT(size_);
            T value(yasl::move(data_[size_ - 1]));
            pop_back();
            return value;
        }

        template <class... Args>
        T* emplace(const T* pos, Args&&... args) {
            size_t idx = size_t(pos - data_);
            YASL_ASSERT(idx <= size_);
            if (idx == size_) {
                emplace_back(yasl::forward<Args>(args)...);
                return data_ + idx;
            }
            T tmp(yasl::forward<Args>(args)...);
            if (size_ == cap_) {
                reallocate(grow_capacity(size_ + 1));
            }
            detail::insert_inside(data_, size_, idx, yasl::move(tmp));
            return data_ + idx;
        }

        T* insert(const T* pos, const T& value) {
            return emplace(pos, value);
        }

        T* insert(const T* pos, T&& value) {
            return emplace(pos, yasl::move(value));
        }

        T* erase(const T* pos) {
            return erase(pos, pos + 1);
        }

        T* erase(const T* first, const T* last) {
            size_t f = size_t(first - data_);
            detail::erase_range(data_, size_, f, size_t(last - data_));
            return data_ + f;
        }

        template <class Pred>
        size_t erase_if(Pred pred) {
            return detail::erase_if(data_, size_, pred);
        }

        template <class Pred>
        void retain(Pred pred) {
            erase_if([&](T& v) { return !pred(v); });
        }

        template <class It, class S>
            requires IteratorRange<It, S>
        void append(It first, S last) {
            for (; first != last; ++first) {
                emplace_back(*first);
            }
        }

        void resize(size_t n) {
            if (n <= size_) {
                yasl::destroy(data_ + n, data_ + size_);
                size_ = n;
                return;
            }
            reserve(n);
            for (; size_ < n; ++size_) {
                yasl::construct_at(data_ + size_);
            }
        }

        void resize(size_t n, const T& value) {
            if (n <= size_) {
                yasl::destroy(data_ + n, data_ + size_);
                size_ = n;
                return;
            }
            T copy(value);
            reserve(n);
            detail::fill_construct(data_ + size_, n - size_, static_cast<const T&>(copy));
            size_ = n;
        }

        void swap(SmallVector& other) {
            if (this == &other) {
                return;
            }
            if (!is_inline() && !other.is_inline()) {
                yasl::swap(data_, other.data_);
                yasl::swap(size_, other.size_);
                yasl::swap(cap_, other.cap_);
                yasl::swap(res_, other.res_);
                return;
            }
            SmallVector tmp(yasl::move(other));
            other = yasl::move(*this);
            *this = yasl::move(tmp);
        }

        friend bool operator==(const SmallVector& a, const SmallVector& b)
            requires EqualityComparable<T>
        {
            return yasl::equal(a.data_, a.size_, b.data_, b.size_);
        }

    private:
        T* inline_data() noexcept {
            return yasl::launder(reinterpret_cast<T*>(inline_));
        }

        const T* inline_data() const noexcept {
            return yasl::launder(reinterpret_cast<const T*>(inline_));
        }

        size_t grow_capacity(size_t need) const {
            if (need > max_size()) {
                throw LengthError("yasl::SmallVector: too large");
            }
            size_t doubled = cap_ > max_size() / 2 ? max_size() : cap_ * 2;
            return yasl::max(need, doubled);
        }

        // Moves everything into a heap buffer of exactly n elements.
        void reallocate(size_t n) {
            if (n > max_size()) {
                throw LengthError("yasl::SmallVector: too large");
            }
            T* fresh = static_cast<T*>(res_->allocate(n * sizeof(T), alignof(T)));
            try {
                detail::relocate(data_, size_, fresh);
            } catch (...) {
                res_->deallocate(fresh, n * sizeof(T), alignof(T));
                throw;
            }
            free_heap();
            data_ = fresh;
            cap_ = n;
        }

        void free_heap() noexcept {
            if (!is_inline()) {
                res_->deallocate(data_, cap_ * sizeof(T), alignof(T));
            }
        }

        // Back to the (empty) inline buffer. Elements must already be destroyed.
        void release() noexcept {
            free_heap();
            data_ = inline_data();
            cap_ = N;
        }

        // Adopts other's elements; *this is empty and inline.
        void take(SmallVector& other) noexcept(NothrowMoveConstructible<T>) {
            if (!other.is_inline()) {
                data_ = yasl::exchange(other.data_, other.inline_data());
                size_ = yasl::exchange(other.size_, size_t(0));
                cap_ = yasl::exchange(other.cap_, N);
            } else {
                for (size_t i = 0; i < other.size_; ++i) {
                    yasl::construct_at(data_ + i, yasl::move(other.data_[i]));
                }
                size_ = other.size_;
                other.clear();
            }
        }

        T* data_ = inline_data();
        size_t size_ = 0;
        size_t cap_ = N;
        mem::Resource* res_;
        alignas(T) unsigned char inline_[N * sizeof(T)];
    };
}
