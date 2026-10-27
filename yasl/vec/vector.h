#pragma once

#include <yasl/mem/resource.h>
#include <yasl/sys/algo.h>
#include <yasl/sys/check.h>
#include <yasl/sys/exception.h>
#include <yasl/sys/span.h>
#include <yasl/sys/utility.h>

#include <initializer_list> // language support for braced lists

namespace yasl {
    // An iterator/sentinel pair we can walk once.
    template <class It, class S>
    concept IteratorRange = requires(It it, S s) {
        *it;
        ++it;
        { it != s } -> ConvertibleTo<bool>;
    };

    namespace detail {
        // Moves n objects from src into raw storage at dst and destroys the originals.
        // On exception dst is cleaned up and src is left untouched.
        template <class T>
        void relocate(T* src, size_t n, T* dst) {
            if constexpr (TriviallyCopyable<T>) {
                if (n) {
                    yasl::mem_copy(static_cast<void*>(dst), static_cast<const void*>(src), n * sizeof(T));
                }
            } else {
                size_t i = 0;
                try {
                    for (; i < n; ++i) {
                        if constexpr (NothrowMoveConstructible<T> || !CopyConstructible<T>) {
                            yasl::construct_at(dst + i, yasl::move(src[i]));
                        } else {
                            yasl::construct_at(dst + i, static_cast<const T&>(src[i]));
                        }
                    }
                } catch (...) {
                    yasl::destroy_n(dst, i);
                    throw;
                }
                yasl::destroy_n(src, n);
            }
        }
    }

    // Dynamic array that allocates from a mem::Resource.
    //
    //   * the resource is a runtime value, not a template parameter;
    //   * copies use the source's resource, moves carry the resource along;
    //   * uses the slack reported by allocate_at_least();
    //   * extras: pop(), erase_if(), append(range), span().
    template <class T>
    class Vector {
    public:
        using value_type = T;
        using size_type = size_t;
        using difference_type = ptrdiff_t;
        using reference = T&;
        using const_reference = const T&;
        using pointer = T*;
        using const_pointer = const T*;
        using iterator = T*;
        using const_iterator = const T*;

        Vector() noexcept
            : res_(mem::default_resource())
        {
        }

        explicit Vector(mem::Resource* res) noexcept
            : res_(res)
        {
        }

        explicit Vector(size_t n, mem::Resource* res = mem::default_resource())
            : Vector(res)
        {
            resize(n);
        }

        Vector(size_t n, const T& value, mem::Resource* res = mem::default_resource())
            : Vector(res)
        {
            resize(n, value);
        }

        template <class It, class S>
            requires(IteratorRange<It, S> && !Integral<It>)
        Vector(It first, S last, mem::Resource* res = mem::default_resource())
            : Vector(res)
        {
            append(yasl::move(first), yasl::move(last));
        }

        Vector(std::initializer_list<T> il, mem::Resource* res = mem::default_resource())
            : Vector(res)
        {
            append(il.begin(), il.end());
        }

        Vector(const Vector& other)
            : Vector(other, other.res_)
        {
        }

        Vector(const Vector& other, mem::Resource* res)
            : Vector(res)
        {
            append(other.begin(), other.end());
        }

        Vector(Vector&& other) noexcept
            : data_(yasl::exchange(other.data_, nullptr))
            , size_(yasl::exchange(other.size_, size_t(0)))
            , cap_(yasl::exchange(other.cap_, size_t(0)))
            , res_(other.res_)
        {
        }

        ~Vector() {
            reset();
        }

        Vector& operator=(const Vector& other) {
            if (this != &other) {
                assign(other.begin(), other.end());
            }
            return *this;
        }

        Vector& operator=(Vector&& other) noexcept {
            if (this != &other) {
                reset();
                data_ = yasl::exchange(other.data_, nullptr);
                size_ = yasl::exchange(other.size_, size_t(0));
                cap_ = yasl::exchange(other.cap_, size_t(0));
                res_ = other.res_;
            }
            return *this;
        }

        Vector& operator=(std::initializer_list<T> il) {
            assign(il.begin(), il.end());
            return *this;
        }

        template <class It, class S>
            requires IteratorRange<It, S>
        void assign(It first, S last) {
            clear();
            append(yasl::move(first), yasl::move(last));
        }

        void assign(size_t n, const T& value) {
            T tmp(value);
            clear();
            resize(n, tmp);
        }

        // Element access

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
                throw OutOfRange("yasl::Vector::at");
            }
            return data_[i];
        }

        const T& at(size_t i) const {
            if (i >= size_) {
                throw OutOfRange("yasl::Vector::at");
            }
            return data_[i];
        }

        T& front() noexcept {
            YASL_ASSERT(size_);
            return data_[0];
        }

        const T& front() const noexcept {
            YASL_ASSERT(size_);
            return data_[0];
        }

        T& back() noexcept {
            YASL_ASSERT(size_);
            return data_[size_ - 1];
        }

        const T& back() const noexcept {
            YASL_ASSERT(size_);
            return data_[size_ - 1];
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

        // Iterators

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

        // Capacity

        bool empty() const noexcept {
            return size_ == 0;
        }

        size_t size() const noexcept {
            return size_;
        }

        size_t capacity() const noexcept {
            return cap_;
        }

        static constexpr size_t max_size() noexcept {
            return max_value<size_t>() / sizeof(T);
        }

        void reserve(size_t n) {
            if (n > cap_) {
                reallocate(n);
            }
        }

        void shrink_to_fit() {
            if (size_ == cap_) {
                return;
            }
            if (size_ == 0) {
                deallocate(data_, cap_);
                data_ = nullptr;
                cap_ = 0;
                return;
            }
            reallocate(size_);
        }

        mem::Resource* resource() const noexcept {
            return res_;
        }

        // Modifiers

        void clear() noexcept {
            yasl::destroy_n(data_, size_);
            size_ = 0;
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
            // The new element is built before the old ones move, so args may alias them.
            grow_and_construct(grow_capacity(size_ + 1), 1, [&](T* p) {
                yasl::construct_at(p, yasl::forward<Args>(args)...);
            });
            return data_[size_ - 1];
        }

        void pop_back() noexcept {
            YASL_ASSERT(size_);
            yasl::destroy_at(data_ + --size_);
        }

        // Removes the last element and returns it.
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
                reserve(grow_capacity(size_ + 1));
            }
            yasl::construct_at(data_ + size_, yasl::move(data_[size_ - 1]));
            ++size_;
            for (size_t i = size_ - 2; i > idx; --i) {
                data_[i] = yasl::move(data_[i - 1]);
            }
            data_[idx] = yasl::move(tmp);
            return data_ + idx;
        }

        T* insert(const T* pos, const T& value) {
            return emplace(pos, value);
        }

        T* insert(const T* pos, T&& value) {
            return emplace(pos, yasl::move(value));
        }

        template <class It, class S>
            requires IteratorRange<It, S>
        T* insert(const T* pos, It first, S last) {
            size_t idx = size_t(pos - data_);
            size_t old = size_;
            YASL_ASSERT(idx <= size_);
            append(yasl::move(first), yasl::move(last));
            yasl::rotate(data_ + idx, data_ + old, data_ + size_);
            return data_ + idx;
        }

        T* insert(const T* pos, std::initializer_list<T> il) {
            return insert(pos, il.begin(), il.end());
        }

        T* erase(const T* pos) {
            return erase(pos, pos + 1);
        }

        T* erase(const T* first, const T* last) {
            T* f = data_ + (first - data_);
            T* l = data_ + (last - data_);
            YASL_ASSERT(data_ <= f && f <= l && l <= data_ + size_);
            if (f != l) {
                T* out = f;
                for (T* in = l; in != end(); ++in, ++out) {
                    *out = yasl::move(*in);
                }
                yasl::destroy(out, end());
                size_ -= size_t(l - f);
            }
            return f;
        }

        // Removes every element matching pred, returns how many were removed.
        template <class Pred>
        size_t erase_if(Pred pred) {
            T* out = data_;
            for (T* in = data_; in != end(); ++in) {
                if (!pred(*in)) {
                    if (out != in) {
                        *out = yasl::move(*in);
                    }
                    ++out;
                }
            }
            size_t n = size_t(end() - out);
            erase(out, end());
            return n;
        }

        template <class It, class S>
            requires IteratorRange<It, S>
        void append(It first, S last) {
            if constexpr (requires(It a, S b) { size_t(b - a); }) {
                size_t n = size_t(last - first);
                if (n > max_size() - size_) {
                    throw LengthError("yasl::Vector: too large");
                }
                if (size_ + n > cap_) {
                    // Copy before relocating, so the range may point into *this.
                    grow_and_construct(grow_capacity(size_ + n), n, [&](T* p) {
                        copy_construct(first, last, p);
                    });
                } else {
                    copy_construct(first, last, data_ + size_);
                    size_ += n;
                }
            } else {
                for (; first != last; ++first) {
                    emplace_back(*first);
                }
            }
        }

        void append(std::initializer_list<T> il) {
            append(il.begin(), il.end());
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
            size_t extra = n - size_;
            if (n > cap_) {
                grow_and_construct(n, extra, [&](T* p) {
                    fill_construct(p, extra, value);
                });
            } else {
                fill_construct(data_ + size_, extra, value);
                size_ = n;
            }
        }

        void swap(Vector& other) noexcept {
            yasl::swap(data_, other.data_);
            yasl::swap(size_, other.size_);
            yasl::swap(cap_, other.cap_);
            yasl::swap(res_, other.res_);
        }

        friend void swap(Vector& a, Vector& b) noexcept {
            a.swap(b);
        }

        friend bool operator==(const Vector& a, const Vector& b)
            requires EqualityComparable<T>
        {
            return yasl::equal(a.data_, a.size_, b.data_, b.size_);
        }

        friend auto operator<=>(const Vector& a, const Vector& b)
            requires ThreeWayComparable<T>
        {
            return yasl::compare3(a.begin(), a.end(), b.begin(), b.end());
        }

    private:
        struct Buffer {
            T* ptr;
            size_t cap;
        };

        Buffer allocate(size_t n) {
            if (n > max_size()) {
                throw LengthError("yasl::Vector: too large");
            }
            mem::Block b = res_->allocate_at_least(n * sizeof(T), alignof(T));
            return {static_cast<T*>(b.ptr), b.size / sizeof(T)};
        }

        void deallocate(T* p, size_t cap) noexcept {
            if (p) {
                res_->deallocate(p, cap * sizeof(T), alignof(T));
            }
        }

        size_t grow_capacity(size_t need) const {
            if (need > max_size()) {
                throw LengthError("yasl::Vector: too large");
            }
            size_t doubled = cap_ > max_size() / 2 ? max_size() : cap_ * 2;
            return yasl::max(yasl::max(need, doubled), size_t(4));
        }

        void reallocate(size_t n) {
            Buffer b = allocate(n);
            try {
                detail::relocate(data_, size_, b.ptr);
            } catch (...) {
                deallocate(b.ptr, b.cap);
                throw;
            }
            deallocate(data_, cap_);
            data_ = b.ptr;
            cap_ = b.cap;
        }

        // Allocates a new buffer, lets `construct` build `count` elements at its tail,
        // then relocates the existing elements in front of them.
        template <class F>
        void grow_and_construct(size_t new_cap, size_t count, F&& construct) {
            Buffer b = allocate(new_cap);
            try {
                construct(b.ptr + size_);
            } catch (...) {
                deallocate(b.ptr, b.cap);
                throw;
            }
            try {
                detail::relocate(data_, size_, b.ptr);
            } catch (...) {
                yasl::destroy_n(b.ptr + size_, count);
                deallocate(b.ptr, b.cap);
                throw;
            }
            deallocate(data_, cap_);
            data_ = b.ptr;
            cap_ = b.cap;
            size_ += count;
        }

        template <class It, class S>
        static void copy_construct(It first, S last, T* dst) {
            T* cur = dst;
            try {
                for (; first != last; ++first, ++cur) {
                    yasl::construct_at(cur, *first);
                }
            } catch (...) {
                yasl::destroy(dst, cur);
                throw;
            }
        }

        static void fill_construct(T* dst, size_t n, const T& value) {
            size_t i = 0;
            try {
                for (; i < n; ++i) {
                    yasl::construct_at(dst + i, value);
                }
            } catch (...) {
                yasl::destroy_n(dst, i);
                throw;
            }
        }

        void reset() noexcept {
            clear();
            deallocate(data_, cap_);
            data_ = nullptr;
            cap_ = 0;
        }

        T* data_ = nullptr;
        size_t size_ = 0;
        size_t cap_ = 0;
        mem::Resource* res_;
    };
}
