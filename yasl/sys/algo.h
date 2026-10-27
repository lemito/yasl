#pragma once

#include "utility.h"

#include <compare> // operator<=> result types: language support

namespace yasl {
    // First position in [first, last) where !cmp(*it, value).
    template <class It, class V, class Cmp = Less>
    constexpr It lower_bound(It first, It last, const V& value, Cmp cmp = Cmp()) {
        auto n = last - first;
        while (n > 0) {
            auto half = n / 2;
            It mid = first + half;
            if (cmp(*mid, value)) {
                first = mid + 1;
                n -= half + 1;
            } else {
                n = half;
            }
        }
        return first;
    }

    // First position in [first, last) where cmp(value, *it).
    template <class It, class V, class Cmp = Less>
    constexpr It upper_bound(It first, It last, const V& value, Cmp cmp = Cmp()) {
        auto n = last - first;
        while (n > 0) {
            auto half = n / 2;
            It mid = first + half;
            if (!cmp(value, *mid)) {
                first = mid + 1;
                n -= half + 1;
            } else {
                n = half;
            }
        }
        return first;
    }

    template <class T>
    constexpr void reverse(T* first, T* last) {
        while (first < last) {
            --last;
            yasl::swap(*first, *last);
            ++first;
        }
    }

    // Rotates [first, last) so that `mid` becomes the first element.
    template <class T>
    constexpr void rotate(T* first, T* mid, T* last) {
        yasl::reverse(first, mid);
        yasl::reverse(mid, last);
        yasl::reverse(first, last);
    }

    template <class A, class B>
    constexpr bool equal(const A* a, size_t na, const B* b, size_t nb) {
        if (na != nb) {
            return false;
        }
        for (size_t i = 0; i < na; ++i) {
            if (!(a[i] == b[i])) {
                return false;
            }
        }
        return true;
    }

    // Lexicographic three-way comparison of two sequences.
    template <class It1, class It2>
    constexpr auto compare3(It1 a, It1 a_end, It2 b, It2 b_end) -> decltype(*a <=> *b) {
        for (; a != a_end && b != b_end; ++a, ++b) {
            if (auto c = *a <=> *b; c != 0) {
                return c;
            }
        }
        if (a != a_end) {
            return std::strong_ordering::greater;
        }
        if (b != b_end) {
            return std::strong_ordering::less;
        }
        return std::strong_ordering::equal;
    }
}
