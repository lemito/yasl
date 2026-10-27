#pragma once

#include <yasl/mem/resource.h>
#include <yasl/sys/check.h>
#include <yasl/sys/exception.h>
#include <yasl/sys/utility.h>

#include <initializer_list> // language support for braced lists

namespace yasl {
    // Double-ended queue on a growable ring buffer (Rust VecDeque, Java ArrayDeque): O(1)
    // push/pop at both ends, O(1) indexing, random-access iterators. Elements are contiguous
    // only in up to two pieces. Growing moves the elements; any push may invalidate
    // iterators and references, pop/erase only the ones at or after the removed position.
    //
    //     Deque<int> d;
    //     d.push_back(1); d.push_front(0);
    //     d[1];  d.pop_front();
    template <class T>
    class Deque {
        template <bool Const>
        class Iter {
            friend class Deque;
            template <bool>
            friend class Iter;

        public:
            using value_type = T;
            using difference_type = ptrdiff_t;
            using reference = Conditional<Const, const T&, T&>;
            using pointer = Conditional<Const, const T*, T*>;

            Iter() noexcept = default;

            template <bool C2>
                requires(Const && !C2)
            Iter(const Iter<C2>& other) noexcept
                : d_(other.d_)
                , i_(other.i_)
            {
            }

            reference operator*() const noexcept {
                return (*d_)[i_];
            }

            pointer operator->() const noexcept {
                return &(*d_)[i_];
            }

            reference operator[](ptrdiff_t n) const noexcept {
                return (*d_)[size_t(ptrdiff_t(i_) + n)];
            }

            Iter& operator++() noexcept {
                ++i_;
                return *this;
            }

            Iter operator++(int) noexcept {
                Iter c = *this;
                ++i_;
                return c;
            }

            Iter& operator--() noexcept {
                --i_;
                return *this;
            }

            Iter operator--(int) noexcept {
                Iter c = *this;
                --i_;
                return c;
            }

            Iter& operator+=(ptrdiff_t n) noexcept {
                i_ = size_t(ptrdiff_t(i_) + n);
                return *this;
            }

            Iter& operator-=(ptrdiff_t n) noexcept {
                i_ = size_t(ptrdiff_t(i_) - n);
                return *this;
            }

            friend Iter operator+(Iter it, ptrdiff_t n) noexcept {
                return it += n;
            }

            friend Iter operator+(ptrdiff_t n, Iter it) noexcept {
                return it += n;
            }

            friend Iter operator-(Iter it, ptrdiff_t n) noexcept {
                return it -= n;
            }

            friend ptrdiff_t operator-(const Iter& a, const Iter& b) noexcept {
                return ptrdiff_t(a.i_) - ptrdiff_t(b.i_);
            }

            friend bool operator==(const Iter& a, const Iter& b) noexcept {
                return a.i_ == b.i_;
            }

            friend auto operator<=>(const Iter& a, const Iter& b) noexcept {
                return a.i_ <=> b.i_;
            }

        private:
            using Owner = Conditional<Const, const Deque, Deque>;

            Iter(Owner* d, size_t i) noexcept
                : d_(d)
                , i_(i)
            {
            }

            Owner* d_ = nullptr;
            size_t i_ = 0;
        };

    public:
        using value_type = T;
        using iterator = Iter<false>;
        using const_iterator = Iter<true>;

        Deque() noexcept
            : res_(mem::default_resource())
        {
        }

        explicit Deque(mem::Resource* res) noexcept
            : res_(res)
        {
        }

        Deque(std::initializer_list<T> il, mem::Resource* res = mem::default_resource())
            : res_(res)
        {
            reserve(il.size());
            for (const T& v : il) {
                push_back(v);
            }
        }

        Deque(const Deque& other)
            : Deque(other, other.res_)
        {
        }

        Deque(const Deque& other, mem::Resource* res)
            : res_(res)
        {
            reserve(other.size_);
            for (size_t i = 0; i < other.size_; ++i) {
                emplace_back(other[i]);
            }
        }

        Deque(Deque&& other) noexcept
            : buf_(yasl::exchange(other.buf_, nullptr))
            , cap_(yasl::exchange(other.cap_, size_t(0)))
            , head_(yasl::exchange(other.head_, size_t(0)))
            , size_(yasl::exchange(other.size_, size_t(0)))
            , res_(other.res_)
        {
        }

        ~Deque() {
            clear();
            free_buffer();
        }

        Deque& operator=(const Deque& other) {
            if (this != &other) {
                Deque tmp(other, res_);
                swap(tmp);
            }
            return *this;
        }

        Deque& operator=(Deque&& other) noexcept {
            if (this != &other) {
                clear();
                free_buffer();
                buf_ = yasl::exchange(other.buf_, nullptr);
                cap_ = yasl::exchange(other.cap_, size_t(0));
                head_ = yasl::exchange(other.head_, size_t(0));
                size_ = yasl::exchange(other.size_, size_t(0));
                res_ = other.res_;
            }
            return *this;
        }

        T& operator[](size_t i) noexcept {
            YASL_ASSERT(i < size_);
            return buf_[physical(i)];
        }

        const T& operator[](size_t i) const noexcept {
            YASL_ASSERT(i < size_);
            return buf_[physical(i)];
        }

        T& at(size_t i) {
            if (i >= size_) {
                throw OutOfRange("yasl::Deque::at");
            }
            return buf_[physical(i)];
        }

        const T& at(size_t i) const {
            if (i >= size_) {
                throw OutOfRange("yasl::Deque::at");
            }
            return buf_[physical(i)];
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

        iterator begin() noexcept {
            return {this, 0};
        }

        iterator end() noexcept {
            return {this, size_};
        }

        const_iterator begin() const noexcept {
            return {this, 0};
        }

        const_iterator end() const noexcept {
            return {this, size_};
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

        mem::Resource* resource() const noexcept {
            return res_;
        }

        void reserve(size_t n) {
            if (n > cap_) {
                reallocate(n);
            }
        }

        void clear() noexcept {
            for (size_t i = 0; i < size_; ++i) {
                yasl::destroy_at(buf_ + physical(i));
            }
            head_ = 0;
            size_ = 0;
        }

        void push_back(const T& v) {
            emplace_back(v);
        }

        void push_back(T&& v) {
            emplace_back(yasl::move(v));
        }

        void push_front(const T& v) {
            emplace_front(v);
        }

        void push_front(T&& v) {
            emplace_front(yasl::move(v));
        }

        template <class... Args>
        T& emplace_back(Args&&... args) {
            if (size_ == cap_) {
                // args may refer to an element: build it before the buffer moves
                T tmp(yasl::forward<Args>(args)...);
                grow();
                T* p = yasl::construct_at(buf_ + physical(size_), yasl::move(tmp));
                ++size_;
                return *p;
            }
            T* p = yasl::construct_at(buf_ + physical(size_), yasl::forward<Args>(args)...);
            ++size_;
            return *p;
        }

        template <class... Args>
        T& emplace_front(Args&&... args) {
            if (size_ == cap_) {
                T tmp(yasl::forward<Args>(args)...);
                grow();
                return push_front_raw(yasl::move(tmp));
            }
            size_t h = head_ == 0 ? cap_ - 1 : head_ - 1;
            T* p = yasl::construct_at(buf_ + h, yasl::forward<Args>(args)...);
            head_ = h;
            ++size_;
            return *p;
        }

        void pop_back() noexcept {
            YASL_ASSERT(size_ > 0);
            yasl::destroy_at(buf_ + physical(size_ - 1));
            --size_;
        }

        void pop_front() noexcept {
            YASL_ASSERT(size_ > 0);
            yasl::destroy_at(buf_ + head_);
            head_ = (head_ + 1) % cap_;
            --size_;
        }

        // Removes and returns the first / last element.
        T take_front() {
            T v = yasl::move(front());
            pop_front();
            return v;
        }

        T take_back() {
            T v = yasl::move(back());
            pop_back();
            return v;
        }

        // Inserts before index `at` (0..size); moves the shorter side. Returns the new position.
        template <class... Args>
        size_t emplace_at(size_t at, Args&&... args) {
            YASL_ASSERT(at <= size_);
            if (at == size_) {
                emplace_back(yasl::forward<Args>(args)...);
                return at;
            }
            if (at == 0) {
                emplace_front(yasl::forward<Args>(args)...);
                return 0;
            }
            T tmp(yasl::forward<Args>(args)...);
            if (at < size_ / 2) {
                emplace_front(yasl::move(front()));
                for (size_t i = 1; i < at; ++i) {
                    (*this)[i] = yasl::move((*this)[i + 1]);
                }
                (*this)[at] = yasl::move(tmp);
            } else {
                emplace_back(yasl::move(back()));
                for (size_t i = size_ - 2; i > at; --i) {
                    (*this)[i] = yasl::move((*this)[i - 1]);
                }
                (*this)[at] = yasl::move(tmp);
            }
            return at;
        }

        iterator insert(const_iterator pos, const T& v) {
            return {this, emplace_at(pos.i_, v)};
        }

        iterator insert(const_iterator pos, T&& v) {
            return {this, emplace_at(pos.i_, yasl::move(v))};
        }

        // Removes the element at `at`, moving the shorter side. Returns the position of the next one.
        size_t erase_at(size_t at) noexcept {
            YASL_ASSERT(at < size_);
            if (at < size_ / 2) {
                for (size_t i = at; i > 0; --i) {
                    (*this)[i] = yasl::move((*this)[i - 1]);
                }
                pop_front();
            } else {
                for (size_t i = at; i + 1 < size_; ++i) {
                    (*this)[i] = yasl::move((*this)[i + 1]);
                }
                pop_back();
            }
            return at;
        }

        iterator erase(const_iterator pos) noexcept {
            return {this, erase_at(pos.i_)};
        }

        void swap(Deque& other) noexcept {
            yasl::swap(buf_, other.buf_);
            yasl::swap(cap_, other.cap_);
            yasl::swap(head_, other.head_);
            yasl::swap(size_, other.size_);
            yasl::swap(res_, other.res_);
        }

        friend bool operator==(const Deque& a, const Deque& b)
            requires EqualityComparable<T>
        {
            if (a.size_ != b.size_) {
                return false;
            }
            for (size_t i = 0; i < a.size_; ++i) {
                if (!(a[i] == b[i])) {
                    return false;
                }
            }
            return true;
        }

    private:
        size_t physical(size_t i) const noexcept {
            size_t p = head_ + i;
            return p >= cap_ ? p - cap_ : p;
        }

        T& push_front_raw(T&& v) {
            size_t h = head_ == 0 ? cap_ - 1 : head_ - 1;
            T* p = yasl::construct_at(buf_ + h, yasl::move(v));
            head_ = h;
            ++size_;
            return *p;
        }

        void grow() {
            if (cap_ > max_value<size_t>() / 2 / sizeof(T)) {
                throw LengthError("yasl::Deque: too large");
            }
            reallocate(cap_ == 0 ? 8 : cap_ * 2);
        }

        // Moves the elements into a fresh buffer with the head at 0. Strong guarantee: if a
        // throwing move/copy fails, the old buffer is untouched.
        void reallocate(size_t new_cap) {
            if (new_cap > max_value<size_t>() / sizeof(T)) {
                throw LengthError("yasl::Deque: too large");
            }
            T* fresh = static_cast<T*>(res_->allocate(new_cap * sizeof(T), alignof(T)));
            size_t i = 0;
            try {
                for (; i < size_; ++i) {
                    if constexpr (NothrowMoveConstructible<T> || !CopyConstructible<T>) {
                        yasl::construct_at(fresh + i, yasl::move((*this)[i]));
                    } else {
                        yasl::construct_at(fresh + i, static_cast<const T&>((*this)[i]));
                    }
                }
            } catch (...) {
                yasl::destroy_n(fresh, i);
                res_->deallocate(fresh, new_cap * sizeof(T), alignof(T));
                throw;
            }
            for (size_t k = 0; k < size_; ++k) {
                yasl::destroy_at(buf_ + physical(k));
            }
            free_buffer();
            buf_ = fresh;
            cap_ = new_cap;
            head_ = 0;
        }

        void free_buffer() noexcept {
            if (buf_) {
                res_->deallocate(buf_, cap_ * sizeof(T), alignof(T));
                buf_ = nullptr;
                cap_ = 0;
            }
        }

        T* buf_ = nullptr;
        size_t cap_ = 0;
        size_t head_ = 0;
        size_t size_ = 0;
        mem::Resource* res_;
    };
}
