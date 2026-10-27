#pragma once

#include "vector.h"

#include <yasl/sys/algo.h>
#include <yasl/sys/optional.h>

namespace yasl {
    // Binary heap on a Vector (Rust BinaryHeap, Java PriorityQueue): the "largest" element by Cmp
    // comes out first. Cmp = Less gives a max-heap, Greater a min-heap.
    //
    //     PriorityQueue<int, Greater> pq;     // smallest first
    //     pq.push(5); pq.push(1);
    //     pq.top();                           // 1
    //     Optional<int> x = pq.pop();         // 1
    template <class T, class Cmp = Less>
    class PriorityQueue {
    public:
        PriorityQueue() = default;

        explicit PriorityQueue(mem::Resource* res, Cmp cmp = Cmp())
            : v_(res)
            , cmp_(yasl::move(cmp))
        {
        }

        // Heapifies a range in O(n).
        template <class It, class S>
            requires IteratorRange<It, S>
        PriorityQueue(It first, S last, mem::Resource* res = mem::default_resource(), Cmp cmp = Cmp())
            : v_(res)
            , cmp_(yasl::move(cmp))
        {
            v_.append(yasl::move(first), yasl::move(last));
            heapify();
        }

        PriorityQueue(std::initializer_list<T> il, mem::Resource* res = mem::default_resource(), Cmp cmp = Cmp())
            : PriorityQueue(il.begin(), il.end(), res, yasl::move(cmp))
        {
        }

        bool empty() const noexcept {
            return v_.empty();
        }

        size_t size() const noexcept {
            return v_.size();
        }

        mem::Resource* resource() const noexcept {
            return v_.resource();
        }

        void reserve(size_t n) {
            v_.reserve(n);
        }

        void clear() noexcept {
            v_.clear();
        }

        // The element that pop() would return. The queue must not be empty.
        const T& top() const noexcept {
            YASL_ASSERT(!v_.empty());
            return v_[0];
        }

        Optional<const T&> peek() const noexcept {
            return v_.empty() ? Optional<const T&>() : Optional<const T&>(v_[0]);
        }

        void push(const T& value) {
            emplace(value);
        }

        void push(T&& value) {
            emplace(yasl::move(value));
        }

        template <class... Args>
        void emplace(Args&&... args) {
            v_.emplace_back(yasl::forward<Args>(args)...);
            sift_up(v_.size() - 1);
        }

        // Removes and returns the top element, none if empty.
        Optional<T> pop() {
            if (v_.empty()) {
                return none;
            }
            T top(yasl::move(v_[0]));
            if (v_.size() > 1) {
                v_[0] = v_.pop();
                detail::sift_down(v_.begin(), 0, v_.size(), cmp_);
            } else {
                v_.pop_back();
            }
            return Optional<T>(yasl::move(top));
        }

        // The elements in ascending order by Cmp (largest last); consumes the queue.
        Vector<T> into_sorted() && {
            yasl::sort(v_.begin(), v_.end(), cmp_);
            return yasl::move(v_);
        }

        // The underlying heap array, in heap order.
        Vector<T> into_vec() && {
            return yasl::move(v_);
        }

        const Vector<T>& as_vec() const noexcept {
            return v_;
        }

    private:
        void sift_up(size_t i) {
            T value(yasl::move(v_[i]));
            while (i > 0) {
                size_t parent = (i - 1) / 2;
                if (!cmp_(v_[parent], value)) {
                    break;
                }
                v_[i] = yasl::move(v_[parent]);
                i = parent;
            }
            v_[i] = yasl::move(value);
        }

        void heapify() {
            for (size_t i = v_.size() / 2; i-- > 0;) {
                detail::sift_down(v_.begin(), i, v_.size(), cmp_);
            }
        }

        Vector<T> v_;
        [[no_unique_address]] Cmp cmp_;
    };
}
