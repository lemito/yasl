#pragma once

#include "optional.h"
#include "utility.h"

// Lazy iterator adapters in pipe style (Rust iterators, Kotlin sequences, C# LINQ).
// Nothing is computed until a terminal step (sum, collect, a range-for ...) walks the chain.
//
//     using namespace yasl::iter;
//     Vector<int> v{1, 2, 3, 4, 5, 6};
//     auto evens_squared = v | filter([](int x) { return x % 2 == 0; })
//                            | map([](int x) { return x * x; })
//                            | collect<Vector>();                 // {4, 16, 36}
//     for (auto [i, x] : v | enumerate()) ...
//     int total = range(1, 101) | sum();                           // 5050
//
// A source must be an lvalue range (begin()/end()), which the chain refers to, or the result of
// another adapter. Temporaries containers are rejected, they would dangle.

namespace yasl::iter {
    namespace detail {
        template <class T>
        concept IsView = requires { typename T::YaslView; };

        template <class R>
        concept Range = requires(R& r) {
            r.begin();
            r.end();
        };

        // Refers to a container that outlives the chain.
        template <class R>
        class RefView {
        public:
            using YaslView = void;

            explicit RefView(R* r) noexcept
                : r_(r)
            {
            }

            auto begin() const {
                return r_->begin();
            }

            auto end() const {
                return r_->end();
            }

        private:
            R* r_;
        };

        template <class R>
        auto all(R&& r) {
            using D = RemoveCVRef<R>;
            if constexpr (IsView<D>) {
                return D(yasl::forward<R>(r));
            } else {
                static_assert(!SameAs<R, D>, "iterator adapters need an lvalue range (a temporary would dangle)");
                return RefView<RemoveReference<R>>(&r);
            }
        }

        template <class V>
        using IterOf = decltype(declval<V&>().begin());
        template <class V>
        using SentOf = decltype(declval<V&>().end());
        template <class V>
        using ValueOf = RemoveCVRef<decltype(*declval<IterOf<V>&>())>;

        // End marker of an adapter: wraps the base end and is compared against the adapter's iterator.
        template <class S>
        struct Sent {
            S s;
        };

        // ---- map

        template <class V, class F>
        class MapView {
        public:
            using YaslView = void;

            MapView(V v, F f)
                : v_(yasl::move(v))
                , f_(yasl::move(f))
            {
            }

            class Iter {
            public:
                Iter(IterOf<V> it, F* f)
                    : it_(it)
                    , f_(f)
                {
                }

                decltype(auto) operator*() const {
                    return (*f_)(*it_);
                }

                Iter& operator++() {
                    ++it_;
                    return *this;
                }

                friend bool operator==(const Iter& a, const Sent<SentOf<V>>& s) {
                    return a.it_ == s.s;
                }

            private:
                IterOf<V> it_;
                F* f_;
            };

            Iter begin() {
                return {v_.begin(), &f_};
            }

            Sent<SentOf<V>> end() {
                return {v_.end()};
            }

        private:
            V v_;
            F f_;
        };

        // ---- filter

        template <class V, class P>
        class FilterView {
        public:
            using YaslView = void;

            FilterView(V v, P p)
                : v_(yasl::move(v))
                , p_(yasl::move(p))
            {
            }

            class Iter {
            public:
                Iter(IterOf<V> it, SentOf<V> end, P* p)
                    : it_(it)
                    , end_(end)
                    , p_(p)
                {
                    skip();
                }

                decltype(auto) operator*() const {
                    return *it_;
                }

                Iter& operator++() {
                    ++it_;
                    skip();
                    return *this;
                }

                friend bool operator==(const Iter& a, const Sent<SentOf<V>>& s) {
                    return a.it_ == s.s;
                }

            private:
                void skip() {
                    while (it_ != end_ && !(*p_)(*it_)) {
                        ++it_;
                    }
                }

                IterOf<V> it_;
                SentOf<V> end_;
                P* p_;
            };

            Iter begin() {
                return {v_.begin(), v_.end(), &p_};
            }

            Sent<SentOf<V>> end() {
                return {v_.end()};
            }

        private:
            V v_;
            P p_;
        };

        // ---- take / skip

        template <class V>
        class TakeView {
        public:
            using YaslView = void;

            TakeView(V v, size_t n)
                : v_(yasl::move(v))
                , n_(n)
            {
            }

            class Iter {
            public:
                Iter(IterOf<V> it, size_t left)
                    : it_(it)
                    , left_(left)
                {
                }

                decltype(auto) operator*() const {
                    return *it_;
                }

                Iter& operator++() {
                    ++it_;
                    --left_;
                    return *this;
                }

                friend bool operator==(const Iter& a, const Sent<SentOf<V>>& s) {
                    return a.left_ == 0 || a.it_ == s.s;
                }

            private:
                IterOf<V> it_;
                size_t left_;
            };

            Iter begin() {
                return {v_.begin(), n_};
            }

            Sent<SentOf<V>> end() {
                return {v_.end()};
            }

        private:
            V v_;
            size_t n_;
        };

        template <class V>
        class SkipView {
        public:
            using YaslView = void;

            SkipView(V v, size_t n)
                : v_(yasl::move(v))
                , n_(n)
            {
            }

            IterOf<V> begin() {
                auto it = v_.begin();
                auto e = v_.end();
                for (size_t i = 0; i < n_ && it != e; ++i) {
                    ++it;
                }
                return it;
            }

            SentOf<V> end() {
                return v_.end();
            }

        private:
            V v_;
            size_t n_;
        };

        // ---- enumerate

        template <class V>
        class EnumerateView {
        public:
            using YaslView = void;

            explicit EnumerateView(V v)
                : v_(yasl::move(v))
            {
            }

            class Iter {
            public:
                explicit Iter(IterOf<V> it)
                    : it_(it)
                {
                }

                auto operator*() const {
                    return Pair<size_t, decltype(*it_)>{i_, *it_};
                }

                Iter& operator++() {
                    ++it_;
                    ++i_;
                    return *this;
                }

                friend bool operator==(const Iter& a, const Sent<SentOf<V>>& s) {
                    return a.it_ == s.s;
                }

            private:
                IterOf<V> it_;
                size_t i_ = 0;
            };

            Iter begin() {
                return Iter(v_.begin());
            }

            Sent<SentOf<V>> end() {
                return {v_.end()};
            }

        private:
            V v_;
        };

        // ---- zip

        template <class A, class B>
        class ZipView {
        public:
            using YaslView = void;

            ZipView(A a, B b)
                : a_(yasl::move(a))
                , b_(yasl::move(b))
            {
            }

            struct End {
                SentOf<A> a;
                SentOf<B> b;
            };

            class Iter {
            public:
                Iter(IterOf<A> a, IterOf<B> b)
                    : a_(a)
                    , b_(b)
                {
                }

                auto operator*() const {
                    return Pair<decltype(*a_), decltype(*b_)>{*a_, *b_};
                }

                Iter& operator++() {
                    ++a_;
                    ++b_;
                    return *this;
                }

                friend bool operator==(const Iter& it, const End& e) {
                    return it.a_ == e.a || it.b_ == e.b;
                }

            private:
                IterOf<A> a_;
                IterOf<B> b_;
            };

            Iter begin() {
                return {a_.begin(), b_.begin()};
            }

            End end() {
                return {a_.end(), b_.end()};
            }

        private:
            A a_;
            B b_;
        };

        // ---- reverse (bidirectional ranges whose begin() and end() have the same type)

        template <class V>
        class ReverseView {
        public:
            using YaslView = void;

            explicit ReverseView(V v)
                : v_(yasl::move(v))
            {
                static_assert(SameAs<IterOf<V>, SentOf<V>>, "reverse needs a common range");
            }

            class Iter {
            public:
                explicit Iter(IterOf<V> it)
                    : it_(it)
                {
                }

                decltype(auto) operator*() const {
                    auto t = it_;
                    --t;
                    return *t;
                }

                Iter& operator++() {
                    --it_;
                    return *this;
                }

                friend bool operator==(const Iter& a, const Iter& b) {
                    return a.it_ == b.it_;
                }

            private:
                IterOf<V> it_;
            };

            Iter begin() {
                return Iter(v_.end());
            }

            Iter end() {
                return Iter(v_.begin());
            }

        private:
            V v_;
        };

        // ---- integer range

        template <class T>
        class IotaView {
        public:
            using YaslView = void;

            IotaView(T lo, T hi)
                : lo_(lo)
                , hi_(hi)
            {
            }

            class Iter {
            public:
                explicit Iter(T v)
                    : v_(v)
                {
                }

                T operator*() const {
                    return v_;
                }

                Iter& operator++() {
                    ++v_;
                    return *this;
                }

                friend bool operator==(const Iter& a, const Iter& b) {
                    return a.v_ == b.v_;
                }

            private:
                T v_;
            };

            Iter begin() const {
                return Iter(lo_);
            }

            Iter end() const {
                return Iter(hi_ < lo_ ? lo_ : hi_);
            }

        private:
            T lo_;
            T hi_;
        };

        // A step that can be written after `|`.
        template <class Fn>
        struct Pipe {
            Fn fn;
        };

        template <class Fn>
        Pipe(Fn) -> Pipe<Fn>;
    }

    template <class R, class Fn>
        requires(detail::Range<RemoveReference<R>> || detail::IsView<RemoveCVRef<R>>)
    decltype(auto) operator|(R&& r, detail::Pipe<Fn> p) {
        return p.fn(yasl::forward<R>(r));
    }

    // ---- sources

    // The integers lo, lo + 1, ..., hi - 1.
    template <Integral T>
    detail::IotaView<T> range(T lo, T hi) {
        return {lo, hi};
    }

    template <Integral T>
    detail::IotaView<T> range(T hi) {
        return {T(0), hi};
    }

    // ---- adapters

    template <class F>
    auto map(F f) {
        return detail::Pipe{[f = yasl::move(f)](auto&& r) {
            return detail::MapView<decltype(detail::all(yasl::forward<decltype(r)>(r))), F>(
                detail::all(yasl::forward<decltype(r)>(r)), f);
        }};
    }

    template <class P>
    auto filter(P p) {
        return detail::Pipe{[p = yasl::move(p)](auto&& r) {
            return detail::FilterView<decltype(detail::all(yasl::forward<decltype(r)>(r))), P>(
                detail::all(yasl::forward<decltype(r)>(r)), p);
        }};
    }

    inline auto take(size_t n) {
        return detail::Pipe{[n](auto&& r) {
            return detail::TakeView<decltype(detail::all(yasl::forward<decltype(r)>(r)))>(
                detail::all(yasl::forward<decltype(r)>(r)), n);
        }};
    }

    inline auto skip(size_t n) {
        return detail::Pipe{[n](auto&& r) {
            return detail::SkipView<decltype(detail::all(yasl::forward<decltype(r)>(r)))>(
                detail::all(yasl::forward<decltype(r)>(r)), n);
        }};
    }

    // Pairs (index, element).
    inline auto enumerate() {
        return detail::Pipe{[](auto&& r) {
            return detail::EnumerateView<decltype(detail::all(yasl::forward<decltype(r)>(r)))>(
                detail::all(yasl::forward<decltype(r)>(r)));
        }};
    }

    // Pairs (a, b) until the shorter range ends; `other` is an lvalue range or an adapter result.
    template <class Other>
    auto zip(Other&& other) {
        return detail::Pipe{[&other](auto&& r) {
            using A = decltype(detail::all(yasl::forward<decltype(r)>(r)));
            using B = decltype(detail::all(yasl::forward<Other>(other)));
            return detail::ZipView<A, B>(detail::all(yasl::forward<decltype(r)>(r)), detail::all(yasl::forward<Other>(other)));
        }};
    }

    inline auto reverse() {
        return detail::Pipe{[](auto&& r) {
            return detail::ReverseView<decltype(detail::all(yasl::forward<decltype(r)>(r)))>(
                detail::all(yasl::forward<decltype(r)>(r)));
        }};
    }

    // ---- terminal steps

    inline auto count() {
        return detail::Pipe{[](auto&& r) {
            auto v = detail::all(yasl::forward<decltype(r)>(r));
            size_t n = 0;
            for (auto it = v.begin(); !(it == v.end()); ++it) {
                ++n;
            }
            return n;
        }};
    }

    // Sum of the elements, starting from a value-initialized one.
    inline auto sum() {
        return detail::Pipe{[](auto&& r) {
            auto v = detail::all(yasl::forward<decltype(r)>(r));
            detail::ValueOf<decltype(v)> total{};
            for (auto it = v.begin(); !(it == v.end()); ++it) {
                total = total + *it;
            }
            return total;
        }};
    }

    template <class Acc, class F>
    auto fold(Acc init, F f) {
        return detail::Pipe{[init = yasl::move(init), f = yasl::move(f)](auto&& r) {
            auto v = detail::all(yasl::forward<decltype(r)>(r));
            Acc acc = init;
            for (auto it = v.begin(); !(it == v.end()); ++it) {
                acc = f(yasl::move(acc), *it);
            }
            return acc;
        }};
    }

    template <class P>
    auto any(P p) {
        return detail::Pipe{[p = yasl::move(p)](auto&& r) {
            auto v = detail::all(yasl::forward<decltype(r)>(r));
            for (auto it = v.begin(); !(it == v.end()); ++it) {
                if (p(*it)) {
                    return true;
                }
            }
            return false;
        }};
    }

    template <class P>
    auto all(P p) {
        return detail::Pipe{[p = yasl::move(p)](auto&& r) {
            auto v = detail::all(yasl::forward<decltype(r)>(r));
            for (auto it = v.begin(); !(it == v.end()); ++it) {
                if (!p(*it)) {
                    return false;
                }
            }
            return true;
        }};
    }

    // First element satisfying p: Optional<T&> for references, Optional<T> for computed values.
    template <class P>
    auto find(P p) {
        return detail::Pipe{[p = yasl::move(p)](auto&& r) {
            auto v = detail::all(yasl::forward<decltype(r)>(r));
            using Ref = decltype(*v.begin());
            using Out = Conditional<__is_reference(Ref), Optional<Ref>, Optional<RemoveCVRef<Ref>>>;
            for (auto it = v.begin(); !(it == v.end()); ++it) {
                if (p(*it)) {
                    return Out(*it);
                }
            }
            return Out();
        }};
    }

    // Collects into a container: `| collect<Vector>()`, `| collect<Deque>()`.
    template <template <class...> class C>
    auto collect() {
        return detail::Pipe{[](auto&& r) {
            auto v = detail::all(yasl::forward<decltype(r)>(r));
            C<detail::ValueOf<decltype(v)>> out;
            for (auto it = v.begin(); !(it == v.end()); ++it) {
                out.push_back(*it);
            }
            return out;
        }};
    }
}
