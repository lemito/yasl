#pragma once

#include "bits.h"
#include "optional.h"
#include "utility.h"

#include <yasl/mem/resource.h>

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

    namespace detail {
        template <class It, class Cmp>
        void insertion_sort(It first, It last, Cmp& cmp) {
            if (first == last) {
                return;
            }
            for (It i = first + 1; i != last; ++i) {
                auto v = yasl::move(*i);
                It j = i;
                while (j != first && cmp(v, *(j - 1))) {
                    *j = yasl::move(*(j - 1));
                    --j;
                }
                *j = yasl::move(v);
            }
        }

        template <class It, class Cmp>
        void sift_down(It first, size_t root, size_t n, Cmp& cmp) {
            auto v = yasl::move(first[root]);
            for (;;) {
                size_t child = 2 * root + 1;
                if (child >= n) {
                    break;
                }
                if (child + 1 < n && cmp(first[child], first[child + 1])) {
                    ++child;
                }
                if (!cmp(v, first[child])) {
                    break;
                }
                first[root] = yasl::move(first[child]);
                root = child;
            }
            first[root] = yasl::move(v);
        }

        template <class It, class Cmp>
        void heap_sort(It first, It last, Cmp& cmp) {
            size_t n = size_t(last - first);
            for (size_t i = n / 2; i-- > 0;) {
                sift_down(first, i, n, cmp);
            }
            for (size_t end = n; end-- > 1;) {
                yasl::swap(first[0], first[end]);
                sift_down(first, 0, end, cmp);
            }
        }

        // Moves the median of first[1], middle, last[-1] to *first and partitions around it.
        // Needs at least 3 elements. Returns the cut: [first, cut) <= pivot <= [cut, last).
        template <class It, class Cmp>
        It partition_pivot(It first, It last, Cmp& cmp) {
            It a = first + 1;
            It b = first + (last - first) / 2;
            It c = last - 1;
            if (cmp(*a, *b)) {
                if (cmp(*b, *c)) {
                    yasl::swap(*first, *b);
                } else if (cmp(*a, *c)) {
                    yasl::swap(*first, *c);
                } else {
                    yasl::swap(*first, *a);
                }
            } else if (cmp(*a, *c)) {
                yasl::swap(*first, *a);
            } else if (cmp(*b, *c)) {
                yasl::swap(*first, *c);
            } else {
                yasl::swap(*first, *b);
            }
            It lo = first + 1;
            It hi = last;
            for (;;) {
                while (cmp(*lo, *first)) {
                    ++lo;
                }
                --hi;
                while (cmp(*first, *hi)) {
                    --hi;
                }
                if (!(lo < hi)) {
                    return lo;
                }
                yasl::swap(*lo, *hi);
                ++lo;
            }
        }

        template <class It, class Cmp>
        void introsort_loop(It first, It last, int depth, Cmp& cmp) {
            while (last - first > 16) {
                if (depth == 0) {
                    heap_sort(first, last, cmp);
                    return;
                }
                --depth;
                It cut = partition_pivot(first, last, cmp);
                introsort_loop(cut, last, depth, cmp);
                last = cut;
            }
            insertion_sort(first, last, cmp);
        }

        template <class It, class Cmp, class V>
        void merge_sort(It first, It last, V* buf, Cmp& cmp) {
            size_t n = size_t(last - first);
            if (n <= 16) {
                insertion_sort(first, last, cmp);
                return;
            }
            It mid = first + n / 2;
            merge_sort(first, mid, buf, cmp);
            merge_sort(mid, last, buf, cmp);
            size_t n1 = size_t(mid - first);
            for (size_t i = 0; i < n1; ++i) {
                yasl::construct_at(buf + i, yasl::move(first[i]));
            }
            size_t i = 0;
            It j = mid;
            It out = first;
            while (i < n1 && j != last) {
                if (cmp(*j, buf[i])) {
                    *out = yasl::move(*j);
                    ++j;
                } else {
                    *out = yasl::move(buf[i]);
                    ++i;
                }
                ++out;
            }
            while (i < n1) {
                *out = yasl::move(buf[i]);
                ++i;
                ++out;
            }
            yasl::destroy_n(buf, n1);
        }
    }

    // Unstable O(n log n) sort (introsort: median-of-3 quicksort, heapsort fallback, insertion sort for small ranges).
    template <class It, class Cmp = Less>
    void sort(It first, It last, Cmp cmp = Cmp()) {
        size_t n = size_t(last - first);
        if (n < 2) {
            return;
        }
        detail::introsort_loop(first, last, 2 * (bit_width(uint64(n)) - 1), cmp);
    }

    // Stable O(n log n) merge sort; the temporary buffer (n/2 elements) comes from `res`.
    template <class It, class Cmp = Less>
    void stable_sort(It first, It last, Cmp cmp = Cmp(), mem::Resource* res = mem::default_resource()) {
        using V = RemoveCVRef<decltype(*first)>;
        size_t n = size_t(last - first);
        if (n < 2) {
            return;
        }
        if (n <= 16) {
            detail::insertion_sort(first, last, cmp);
            return;
        }
        V* buf = res->allocate_array<V>(n / 2);
        try {
            detail::merge_sort(first, last, buf, cmp);
        } catch (...) {
            res->deallocate_array(buf, n / 2);
            throw;
        }
        res->deallocate_array(buf, n / 2);
    }

    // Sorts by key(element) with "<"; key is evaluated on every comparison.
    template <class It, class Key>
    void sort_by_key(It first, It last, Key key) {
        yasl::sort(first, last, [&](const auto& a, const auto& b) { return key(a) < key(b); });
    }

    // Index of an element equivalent to `value` in the sorted range, or none.
    template <class It, class V, class Cmp = Less>
    Optional<size_t> binary_search(It first, It last, const V& value, Cmp cmp = Cmp()) {
        It it = yasl::lower_bound(first, last, value, cmp);
        if (it != last && !cmp(value, *it)) {
            return size_t(it - first);
        }
        return none;
    }

    // Moves elements satisfying pred to the front (not stable); returns the first one that does not.
    template <class It, class Pred>
    It partition(It first, It last, Pred pred) {
        for (;;) {
            while (first != last && pred(*first)) {
                ++first;
            }
            if (first == last) {
                return first;
            }
            --last;
            while (first != last && !pred(*last)) {
                --last;
            }
            if (first == last) {
                return first;
            }
            yasl::swap(*first, *last);
            ++first;
        }
    }

    // Puts the element that would be at `nth` in sorted order there; smaller ones before it, larger after.
    template <class It, class Cmp = Less>
    void nth_element(It first, It nth, It last, Cmp cmp = Cmp()) {
        if (first == last || nth == last) {
            return;
        }
        int depth = 2 * (bit_width(uint64(last - first)) - 1);
        while (last - first > 3) {
            if (depth == 0) {
                detail::heap_sort(first, last, cmp);
                return;
            }
            --depth;
            It cut = detail::partition_pivot(first, last, cmp);
            if (cut <= nth) {
                first = cut;
            } else {
                last = cut;
            }
        }
        detail::insertion_sort(first, last, cmp);
    }

    template <class It, class Cmp = Less>
    It min_element(It first, It last, Cmp cmp = Cmp()) {
        It best = first;
        if (first == last) {
            return last;
        }
        for (++first; first != last; ++first) {
            if (cmp(*first, *best)) {
                best = first;
            }
        }
        return best;
    }

    template <class It, class Cmp = Less>
    It max_element(It first, It last, Cmp cmp = Cmp()) {
        It best = first;
        if (first == last) {
            return last;
        }
        for (++first; first != last; ++first) {
            if (cmp(*best, *first)) {
                best = first;
            }
        }
        return best;
    }

    template <class It, class Pred>
    It find_if(It first, It last, Pred pred) {
        for (; first != last; ++first) {
            if (pred(*first)) {
                break;
            }
        }
        return first;
    }

    template <class It, class Pred>
    bool any_of(It first, It last, Pred pred) {
        return yasl::find_if(first, last, pred) != last;
    }

    template <class It, class Pred>
    bool all_of(It first, It last, Pred pred) {
        for (; first != last; ++first) {
            if (!pred(*first)) {
                return false;
            }
        }
        return true;
    }

    template <class It, class Pred>
    bool none_of(It first, It last, Pred pred) {
        return !yasl::any_of(first, last, pred);
    }

    template <class It, class Pred>
    size_t count_if(It first, It last, Pred pred) {
        size_t n = 0;
        for (; first != last; ++first) {
            n += pred(*first) ? 1 : 0;
        }
        return n;
    }
}
