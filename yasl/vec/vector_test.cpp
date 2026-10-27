#include <yasl/mem/arena.h>
#include <yasl/mem/tracking.h>
#include <yasl/test/test.h>
#include <yasl/vec/vector.h>

#include <random>
#include <stdexcept>
#include <string>
#include <vector>

using namespace yasl;

namespace {
    // Counts live instances and can be told to throw on the N-th copy.
    struct Probe {
        static inline int live = 0;
        static inline int copies_until_throw = -1;

        int value;

        Probe(int v = 0)
            : value(v)
        {
            ++live;
        }

        Probe(const Probe& o)
            : value(o.value)
        {
            if (copies_until_throw >= 0 && copies_until_throw-- == 0) {
                throw std::runtime_error("copy failed");
            }
            ++live;
        }

        Probe& operator=(const Probe&) = default;

        ~Probe() {
            --live;
        }

    };
}

YASL_TEST(vector_basic) {
    Vector<int> v;
    CHECK(v.empty());
    for (int i = 0; i < 100; ++i) {
        v.push_back(i);
    }
    CHECK_EQ(v.size(), size_t(100));
    CHECK_EQ(v.front(), 0);
    CHECK_EQ(v.back(), 99);
    CHECK_EQ(v[42], 42);
    CHECK_EQ(v.pop(), 99);
    CHECK_EQ(v.size(), size_t(99));
    CHECK_THROWS(v.at(1000), OutOfRange);

    int sum = 0;
    for (int x : v) {
        sum += x;
    }
    CHECK_EQ(sum, 99 * 98 / 2);

    Vector<int> w{1, 2, 3};
    CHECK_EQ(w.size(), size_t(3));
    CHECK(w == (Vector<int>{1, 2, 3}));
    CHECK(w < (Vector<int>{1, 2, 4}));
    Span<const int> s = w;
    CHECK_EQ(s[2], 3);
}

YASL_TEST(vector_insert_erase) {
    Vector<std::string> v{"a", "b", "c"};
    v.insert(v.begin() + 1, "x");
    CHECK(v == (Vector<std::string>{"a", "x", "b", "c"}));
    v.insert(v.end(), {"y", "z"});
    v.insert(v.begin(), {"0"});
    CHECK(v == (Vector<std::string>{"0", "a", "x", "b", "c", "y", "z"}));
    v.erase(v.begin() + 1, v.begin() + 3);
    CHECK(v == (Vector<std::string>{"0", "b", "c", "y", "z"}));
    v.erase(v.begin());
    CHECK(v == (Vector<std::string>{"b", "c", "y", "z"}));
    CHECK_EQ(v.erase_if([](const std::string& s) { return s < "d"; }), size_t(2));
    CHECK(v == (Vector<std::string>{"y", "z"}));
}

YASL_TEST(vector_self_aliasing) {
    Vector<std::string> v;
    v.push_back("hello, a long string to defeat small string optimization");
    for (int i = 0; i < 10; ++i) {
        v.shrink_to_fit();
        v.push_back(v[0]);
    }
    for (const auto& s : v) {
        CHECK_EQ(s, v[0]);
    }
    v.shrink_to_fit();
    v.insert(v.begin(), v.back());
    CHECK_EQ(v[0], v[1]);

    Vector<int> n{1, 2, 3};
    n.shrink_to_fit();
    n.append(n.begin(), n.end());
    CHECK(n == (Vector<int>{1, 2, 3, 1, 2, 3}));

    n.resize(100, n[0]);
    CHECK_EQ(n[99], 1);
}

YASL_TEST(vector_resize_reserve) {
    Vector<int> v(5);
    CHECK_EQ(v.size(), size_t(5));
    CHECK_EQ(v[4], 0);
    v.resize(10, 7);
    CHECK_EQ(v[9], 7);
    v.resize(2);
    CHECK_EQ(v.size(), size_t(2));
    v.reserve(1000);
    CHECK(v.capacity() >= 1000);
    v.shrink_to_fit();
    CHECK_EQ(v.capacity(), size_t(2));
    v.clear();
    v.shrink_to_fit();
    CHECK_EQ(v.capacity(), size_t(0));
}

YASL_TEST(vector_copy_move_resource) {
    mem::Tracking a;
    mem::Tracking b;
    {
        Vector<int> v({1, 2, 3}, &a);
        Vector<int> copy = v;
        CHECK_EQ(copy.resource(), static_cast<mem::Resource*>(&a));
        Vector<int> other(v, &b);
        CHECK_EQ(other.resource(), static_cast<mem::Resource*>(&b));
        CHECK(other == v);

        Vector<int> moved = std::move(v);
        CHECK(v.empty());
        CHECK_EQ(moved.size(), size_t(3));

        other = moved;  // copy-assignment keeps other's resource
        CHECK_EQ(other.resource(), static_cast<mem::Resource*>(&b));
        other = std::move(moved);  // move-assignment takes the source's
        CHECK_EQ(other.resource(), static_cast<mem::Resource*>(&a));
    }
    CHECK_EQ(a.stats().live(), size_t(0));
    CHECK_EQ(b.stats().live(), size_t(0));
}

YASL_TEST(vector_exception_safety) {
    mem::Tracking t;
    Probe::live = 0;
    {
        Vector<Probe> v(&t);
        for (int i = 0; i < 10; ++i) {
            v.emplace_back(i);
        }
        v.shrink_to_fit();

        // Relocation copies (Probe's move is not noexcept); fail half-way through.
        Probe::copies_until_throw = 5;
        CHECK_THROWS(v.push_back(Probe(100)), std::runtime_error);
        Probe::copies_until_throw = -1;
        CHECK_EQ(v.size(), size_t(10));
        for (int i = 0; i < 10; ++i) {
            CHECK_EQ(v[i].value, i);
        }

        Vector<Probe> src(v.begin(), v.end(), &t);
        Probe::copies_until_throw = 3;
        CHECK_THROWS(Vector<Probe>(src.begin(), src.end(), &t), std::runtime_error);
        Probe::copies_until_throw = -1;
    }
    CHECK_EQ(Probe::live, 0);
    CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(vector_random_vs_std) {
    mem::Tracking t;
    {
        std::mt19937 rng(7);
        Vector<int> v(&t);
        std::vector<int> ref;
        for (int step = 0; step < 20000; ++step) {
            int op = int(rng() % 8);
            int x = int(rng() % 1000);
            if (op < 3) {
                v.push_back(x);
                ref.push_back(x);
            } else if (op == 3 && !ref.empty()) {
                v.pop_back();
                ref.pop_back();
            } else if (op == 4) {
                size_t pos = ref.empty() ? 0 : rng() % (ref.size() + 1);
                v.insert(v.begin() + pos, x);
                ref.insert(ref.begin() + long(pos), x);
            } else if (op == 5 && !ref.empty()) {
                size_t pos = rng() % ref.size();
                v.erase(v.begin() + pos);
                ref.erase(ref.begin() + long(pos));
            } else if (op == 6) {
                size_t n = rng() % 50;
                v.resize(n, x);
                ref.resize(n, x);
            }
            CHECK_EQ(v.size(), ref.size());
        }
        CHECK(std::equal(v.begin(), v.end(), ref.begin(), ref.end()));
    }
    CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(vector_in_arena) {
    mem::InlineArena<4096> arena(mem::null_resource());
    Vector<Vector<int>> vv(&arena);
    for (int i = 0; i < 10; ++i) {
        auto& inner = vv.emplace_back(&arena);
        for (int j = 0; j < i; ++j) {
            inner.push_back(j);
        }
    }
    CHECK_EQ(vv[9].size(), size_t(9));
    CHECK_EQ(vv[9].resource(), static_cast<mem::Resource*>(&arena));
}
