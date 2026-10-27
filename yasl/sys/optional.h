#pragma once

#include "check.h"
#include "exception.h"
#include "utility.h"

namespace yasl {
    struct None {
        explicit constexpr None(int) noexcept {
        }
    };

    inline constexpr None none{0};

    // A value or nothing.
    template <class T>
    class Optional {
    public:
        using value_type = T;

        Optional() noexcept {
        }

        Optional(None) noexcept {
        }

        template <class U = T>
            requires(ConstructibleFrom<T, U &&> && !SameAs<RemoveCVRef<U>, Optional> && !SameAs<RemoveCVRef<U>, None>)
        Optional(U&& value) {
            yasl::construct_at(yasl::addressof(value_), yasl::forward<U>(value));
            has_ = true;
        }

        Optional(const Optional& other) {
            if (other.has_) {
                yasl::construct_at(yasl::addressof(value_), other.value_);
                has_ = true;
            }
        }

        Optional(Optional&& other) noexcept(NothrowMoveConstructible<T>) {
            if (other.has_) {
                yasl::construct_at(yasl::addressof(value_), yasl::move(other.value_));
                has_ = true;
            }
        }

        ~Optional() {
            reset();
        }

        Optional& operator=(None) noexcept {
            reset();
            return *this;
        }

        Optional& operator=(const Optional& other) {
            if (this != &other) {
                if (other.has_) {
                    assign(other.value_);
                } else {
                    reset();
                }
            }
            return *this;
        }

        Optional& operator=(Optional&& other) noexcept(NothrowMoveConstructible<T>) {
            if (this != &other) {
                if (other.has_) {
                    assign(yasl::move(other.value_));
                } else {
                    reset();
                }
            }
            return *this;
        }

        template <class U = T>
            requires(ConstructibleFrom<T, U &&> && !SameAs<RemoveCVRef<U>, Optional> && !SameAs<RemoveCVRef<U>, None>)
        Optional& operator=(U&& value) {
            assign(yasl::forward<U>(value));
            return *this;
        }

        template <class... Args>
        T& emplace(Args&&... args) {
            reset();
            yasl::construct_at(yasl::addressof(value_), yasl::forward<Args>(args)...);
            has_ = true;
            return value_;
        }

        void reset() noexcept {
            if (has_) {
                yasl::destroy_at(yasl::addressof(value_));
                has_ = false;
            }
        }

        bool has_value() const noexcept {
            return has_;
        }

        explicit operator bool() const noexcept {
            return has_;
        }

        T& operator*() & noexcept {
            YASL_ASSERT(has_);
            return value_;
        }

        const T& operator*() const& noexcept {
            YASL_ASSERT(has_);
            return value_;
        }

        T&& operator*() && noexcept {
            YASL_ASSERT(has_);
            return yasl::move(value_);
        }

        T* operator->() noexcept {
            YASL_ASSERT(has_);
            return yasl::addressof(value_);
        }

        const T* operator->() const noexcept {
            YASL_ASSERT(has_);
            return yasl::addressof(value_);
        }

        T& value() & {
            check();
            return value_;
        }

        const T& value() const& {
            check();
            return value_;
        }

        T&& value() && {
            check();
            return yasl::move(value_);
        }

        template <class U>
        T value_or(U&& fallback) const& {
            return has_ ? value_ : T(yasl::forward<U>(fallback));
        }

        // nullptr if empty.
        T* get() noexcept {
            return has_ ? yasl::addressof(value_) : nullptr;
        }

        const T* get() const noexcept {
            return has_ ? yasl::addressof(value_) : nullptr;
        }

        friend bool operator==(const Optional& a, const Optional& b)
            requires EqualityComparable<T>
        {
            return a.has_ == b.has_ && (!a.has_ || a.value_ == b.value_);
        }

        friend bool operator==(const Optional& a, None) noexcept {
            return !a.has_;
        }

        template <class U>
            requires(!SameAs<U, Optional> && !SameAs<U, None> && EqualityComparableWith<T, U>)
        friend bool operator==(const Optional& a, const U& b) {
            return a.has_ && a.value_ == b;
        }

    private:
        template <class U>
        void assign(U&& v) {
            if (has_) {
                value_ = yasl::forward<U>(v);
            } else {
                yasl::construct_at(yasl::addressof(value_), yasl::forward<U>(v));
                has_ = true;
            }
        }

        void check() const {
            if (!has_) {
                throw BadAccess("yasl::Optional: no value");
            }
        }

        union {
            T value_;
        };
        bool has_ = false;
    };
}
