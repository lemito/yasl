#include <yasl/map/map.h>
#include <yasl/mem/arena.h>
#include <yasl/mem/tracking.h>
#include <yasl/str/str.h>
#include <yasl/test/test.h>
#include <yasl/vec/vector.h>

#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

using namespace yasl;
using namespace yasl::literals;

// Every scenario runs on both engines.

namespace {
    struct Big {
        int key;
        char pad[200];

        bool operator<(const Big& o) const {
            return key < o.key;
        }

        friend bool operator<(const Big& a, int b) {
            return a.key < b;
        }

        friend bool operator<(int a, const Big& b) {
            return a < b.key;
        }

        bool operator==(const Big& o) const {
            return key == o.key;
        }
    };

    template <template <class, class, class> class M>
    void map_basic_scenario() {
        M<String, int, Less> m;
        m["apple"] = 3;
        m["banana"] = 5;
        CHECK(m.insert("cherry", 7).second);
        CHECK(!m.insert("cherry", 8).second);
        CHECK_EQ(m.at("cherry"), 7);
        m.insert_or_assign("cherry", 8);
        CHECK_EQ(m.at("cherry"), 8);

        CHECK_EQ(*m.get("apple"_sv), 3);
        CHECK(m.get("durian") == nullptr);
        CHECK_EQ(m.get_or("durian", -1), -1);
        CHECK_THROWS(m.at("durian"), OutOfRange);

        std::vector<std::string> keys;
        for (auto& [k, v] : m) {
            keys.emplace_back(k.data(), k.size());
            v += 100;
        }
        CHECK((keys == std::vector<std::string>{"apple", "banana", "cherry"}));
        CHECK_EQ(m["banana"], 105);

        auto it = m.find("banana");
        it = m.erase(it);
        CHECK_EQ(it->key, "cherry");
        CHECK_EQ(m.size(), size_t(2));
        CHECK_EQ(m.erase("nope"), size_t(0));
        CHECK(m.validate());

        M<int, String, Less> init{{2, "two"}, {1, "one"}};
        CHECK_EQ(init.begin()->value, "one");
        CHECK_EQ(init.front().key, 1);
        CHECK_EQ(init.back().key, 2);
    }

    template <template <class, class, class> class M>
    void map_random_scenario(uint64 seed, int keys) {
        mem::Tracking t;
        {
            std::mt19937_64 rng(seed);
            M<int, int, Less> m(&t);
            std::map<int, int> ref;
            for (int step = 0; step < 40000; ++step) {
                int k = int(rng() % uint64(keys));
                int op = int(rng() % 5);
                if (op == 0) {
                    CHECK_EQ(m.insert(k, step).second, ref.emplace(k, step).second);
                } else if (op == 1) {
                    CHECK_EQ(m.erase(k), ref.erase(k));
                } else if (op == 2) {
                    m[k] += 1;
                    ref[k] += 1;
                } else if (op == 3) {
                    // erase through an iterator and check the returned successor
                    auto a = m.lower_bound(k);
                    auto b = ref.lower_bound(k);
                    CHECK_EQ(a == m.end(), b == ref.end());
                    if (b != ref.end()) {
                        a = m.erase(a);
                        b = ref.erase(b);
                        CHECK_EQ(a == m.end(), b == ref.end());
                        if (b != ref.end()) {
                            CHECK_EQ(a->key, b->first);
                        }
                    }
                } else {
                    auto a = m.upper_bound(k);
                    auto b = ref.upper_bound(k);
                    CHECK_EQ(a == m.end(), b == ref.end());
                    if (b != ref.end()) {
                        CHECK_EQ(a->key, b->first);
                        CHECK_EQ(a->value, b->second);
                    }
                }
                if (step % 4000 == 0) {
                    CHECK(m.validate());
                }
            }
            CHECK(m.validate());
            CHECK_EQ(m.size(), ref.size());
            auto it = ref.begin();
            for (const auto& [k, v] : m) {
                CHECK_EQ(k, it->first);
                CHECK_EQ(v, it->second);
                ++it;
            }
            auto rit = ref.rbegin();
            for (auto mit = m.end(); mit != m.begin();) {
                --mit;
                CHECK_EQ(mit->key, rit->first);
                ++rit;
            }

            auto copy = m;
            CHECK(copy == m);
            CHECK(copy.validate());
            for (auto& [k, v] : ref) {
                CHECK_EQ(m.erase(k), size_t(1));
            }
            CHECK(m.empty());
            CHECK(m.validate());
        }
        CHECK_EQ(t.stats().live(), size_t(0));
    }

    template <template <class, class> class S>
    void set_scenario() {
        S<int, Less> s{5, 1, 4, 1, 3};
        CHECK_EQ(s.size(), size_t(4));
        CHECK((std::vector<int>(s.begin(), s.end()) == std::vector<int>{1, 3, 4, 5}));
        CHECK(s.insert(2).second);
        CHECK(!s.insert(2).second);
        CHECK(s.emplace(10).second);
        CHECK(s.contains(2));
        CHECK_EQ(*s.lower_bound(2), 2);
        CHECK_EQ(*s.upper_bound(2), 3);
        CHECK(s.lower_bound(11) == s.end());
        CHECK_EQ(s.erase(3), size_t(1));
        CHECK_EQ(s.erase(3), size_t(0));
        CHECK_EQ(s.front(), 1);
        CHECK_EQ(s.back(), 10);
        CHECK(s.validate());

        S<int, Less> big;
        for (int i = 0; i < 100000; ++i) {
            big.insert(i);
        }
        CHECK(big.validate());
        for (int i = 0; i < 100000; i += 2) {
            big.erase(i);
        }
        CHECK(big.validate());
        CHECK_EQ(big.size(), size_t(50000));
        CHECK_EQ(*big.begin(), 1);
    }
}

YASL_TEST(treap_map_basic) {
    map_basic_scenario<TreapMap>();
}

YASL_TEST(btree_map_basic) {
    map_basic_scenario<BTreeMap>();
}

YASL_TEST(treap_map_random) {
    map_random_scenario<TreapMap>(1, 2000);
}

YASL_TEST(btree_map_random) {
    map_random_scenario<BTreeMap>(1, 2000);
    map_random_scenario<BTreeMap>(2, 50);
    map_random_scenario<BTreeMap>(3, 100000);
}

YASL_TEST(treap_set) {
    set_scenario<TreapSet>();
}

YASL_TEST(btree_set) {
    set_scenario<BTreeSet>();
}

YASL_TEST(btree_big_elements) {
    // Large values get narrow nodes; exercise them too.
    BTreeSet<Big> s;
    std::set<int> ref;
    std::mt19937 rng(11);
    for (int i = 0; i < 20000; ++i) {
        int k = int(rng() % 3000);
        if (rng() % 3) {
            s.insert(Big{k, {}});
            ref.insert(k);
        } else {
            s.erase(k);
            ref.erase(k);
        }
    }
    CHECK(s.validate());
    CHECK_EQ(s.size(), ref.size());
}

YASL_TEST(treap_iterators_stable) {
    TreapMap<int, int> m;
    for (int i = 0; i < 100; ++i) {
        m[i] = i;
    }
    auto it = m.find(50);
    int* value = &it->value;
    for (int i = 100; i < 10000; ++i) {
        m[i] = i;
    }
    for (int i = 0; i < 50; ++i) {
        m.erase(i);
    }
    CHECK_EQ(it->key, 50);
    CHECK_EQ(value, &m.find(50)->value);
}

YASL_TEST(treap_order_statistics_split_join) {
    mem::Tracking t;
    {
        TreapSet<int> s(&t);
        for (int i = 0; i < 1000; ++i) {
            s.insert(i * 2);
        }
        CHECK_EQ(*s.kth(500), 1000);
        CHECK(s.kth(1000) == s.end());
        CHECK_EQ(s.rank(1001), size_t(501));
        CHECK_EQ(s.index_of(s.find(1998)), size_t(999));

        TreapSet<int> right;
        s.split(600, right);
        CHECK_EQ(s.size(), size_t(300));
        CHECK_EQ(right.front(), 600);
        CHECK_EQ(right.resource(), static_cast<mem::Resource*>(&t));
        CHECK(s.validate() && right.validate());
        s.join(right);
        CHECK_EQ(s.size(), size_t(1000));
        CHECK(right.empty() && s.validate());

        TreapSet<int> other(mem::system_resource());
        other.insert(5000);
        s.join(other);
        CHECK_EQ(s.back(), 5000);
        CHECK(s.validate());
    }
    CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(map_copy_resources) {
    mem::Tracking t;
    {
        Map<int, String> a(&t);
        for (int i = 0; i < 500; ++i) {
            a[i] = String("v", &t);
        }
        Map<int, String> b = a;
        CHECK(a == b);
        b[10] = "changed";
        CHECK(!(a == b));
        a = b;
        CHECK(a == b);
        Map<int, String> c = yasl::move(a);
        CHECK(a.empty());
        CHECK_EQ(c.size(), size_t(500));
    }
    CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(map_in_arena) {
    mem::Arena arena;
    Map<String, Vector<int>> m(&arena);
    m.try_emplace(String("a", &arena), &arena).first->value.push_back(1);
    m.try_emplace("a", &arena).first->value.push_back(2);
    CHECK_EQ(m.at("a").size(), size_t(2));
    CHECK_EQ(m.at("a").resource(), static_cast<mem::Resource*>(&arena));
}

namespace {
    // Random insert/erase/lookup/order-statistics against std::map.
    template <class V>
    void btree_random_vs_std(unsigned seed, int ops, int key_range) {
        mem::Tracking t;
        {
            BTreeMap<int, V> m(&t);
            std::map<int, int> ref;
            std::mt19937 rng(seed);
            for (int i = 0; i < ops; ++i) {
                int k = int(rng() % unsigned(key_range));
                switch (rng() % 5) {
                case 0:
                case 1:
                    m.insert_or_assign(k, V(i));
                    ref[k] = i;
                    break;
                case 2:
                    CHECK_EQ(m.erase(k), ref.erase(k));
                    break;
                case 3: {
                    auto it = m.lower_bound(k);
                    auto rit = ref.lower_bound(k);
                    CHECK_EQ(it == m.end(), rit == ref.end());
                    if (rit != ref.end()) {
                        CHECK_EQ(it->key, rit->first);
                    }
                    CHECK_EQ(m.rank(k), size_t(std::distance(ref.begin(), rit)));
                    break;
                }
                default: {
                    if (ref.empty()) {
                        break;
                    }
                    size_t idx = rng() % ref.size();
                    auto it = m.kth(idx);
                    CHECK_EQ(it->key, std::next(ref.begin(), long(idx))->first);
                    CHECK_EQ(m.index_of(it), idx);
                    break;
                }
                }
                if (i % 1024 == 0) {
                    CHECK(m.validate());
                }
            }
            CHECK(m.validate());
            CHECK_EQ(m.size(), ref.size());
            CHECK(m.kth(ref.size()) == m.end());
            // erase(it) hands back the successor
            while (!ref.empty()) {
                auto it = m.kth(ref.size() / 2);
                int k = it->key;
                auto next = m.erase(it);
                auto rnext = ref.erase(ref.find(k));
                CHECK_EQ(next == m.end(), rnext == ref.end());
                if (rnext != ref.end()) {
                    CHECK_EQ(next->key, rnext->first);
                }
            }
            CHECK(m.empty() && m.validate());
        }
        CHECK_EQ(t.stats().live(), size_t(0));
    }

    struct BigVal {
        int v;
        char pad[100];
        BigVal(int x = 0)
            : v(x)
        {
        }
    };
}

YASL_TEST(btree_plus_random_small_values) {
    btree_random_vs_std<int>(1, 100000, 500);
    btree_random_vs_std<int>(2, 100000, 100000);
}

YASL_TEST(btree_plus_random_big_values) {
    btree_random_vs_std<BigVal>(3, 60000, 2000);
}

YASL_TEST(btree_plus_sequential) {
    BTreeSet<int> s;
    for (int i = 0; i < 50000; ++i) {
        s.insert(i);
    }
    CHECK(s.validate());
    for (int i = 49999; i >= 0; --i) {
        CHECK_EQ(s.erase(i), size_t(1));
        if (i % 5000 == 0) {
            CHECK(s.validate());
        }
    }
    CHECK(s.empty());
    for (int i = 50000; i > 0; --i) {
        s.insert(i);
    }
    CHECK(s.validate());
    CHECK_EQ(s.rank(25000), size_t(24999));
}

YASL_TEST(btree_plus_move_only_values) {
    mem::Tracking t;
    {
        struct MoveOnly {
            explicit MoveOnly(int x)
                : p(new int(x))
            {
            }
            MoveOnly(MoveOnly&& o) noexcept
                : p(o.p)
            {
                o.p = nullptr;
            }
            MoveOnly& operator=(MoveOnly&& o) noexcept {
                delete p;
                p = o.p;
                o.p = nullptr;
                return *this;
            }
            ~MoveOnly() {
                delete p;
            }
            int* p;
        };
        BTreeMap<int, MoveOnly> m(&t);
        for (int i = 0; i < 5000; ++i) {
            m.try_emplace((i * 7919) % 5000, i);
        }
        CHECK(m.validate());
        for (int i = 0; i < 5000; i += 3) {
            m.erase(i);
        }
        CHECK(m.validate());
        BTreeMap<int, MoveOnly> other = yasl::move(m);
        CHECK(other.validate() && m.empty());
        for (auto& [k, v] : other) {
            CHECK(v.p != nullptr);
            (void)k;
        }
    }
    CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(btree_plus_from_sorted_and_range) {
    mem::Tracking t;
    {
        for (int n : {0, 1, 2, 31, 32, 33, 64, 1000, 12345}) {
            std::vector<int> v(static_cast<size_t>(n));
            for (int i = 0; i < n; ++i) {
                v[size_t(i)] = i * 2;
            }
            auto s = BTreeSet<int>::from_sorted(v.begin(), v.end(), &t);
            CHECK(s.validate());
            CHECK_EQ(s.size(), size_t(n));
            CHECK((std::vector<int>(s.begin(), s.end()) == v));
            if (n > 0) {
                CHECK_EQ(*s.kth(size_t(n) - 1), (n - 1) * 2);
            }
            // erase everything again: rebalancing must cope with the packed layout
            for (int i = 0; i < n; ++i) {
                CHECK_EQ(s.erase(i * 2), size_t(1));
            }
            CHECK(s.empty() && s.validate());
        }
        BTreeSet<int> s(&t);
        for (int i = 0; i < 100; ++i) {
            s.insert(i);
        }
        int sum = 0;
        for (int x : s.range(10, 20)) {
            sum += x;
        }
        CHECK_EQ(sum, 145);
        CHECK(s.range(200, 300).begin() == s.range(200, 300).end());
    }
    CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(btree_plus_split_join) {
    mem::Tracking t;
    {
        BTreeSet<int> s(&t);
        for (int i = 0; i < 5000; ++i) {
            s.insert(i * 2);
        }
        BTreeSet<int> right;
        s.split(3001, right);
        CHECK_EQ(s.size(), size_t(1501));
        CHECK_EQ(right.size(), size_t(3499));
        CHECK_EQ(s.back(), 3000);
        CHECK_EQ(right.front(), 3002);
        CHECK(s.validate() && right.validate());
        CHECK_EQ(right.resource(), static_cast<mem::Resource*>(&t));
        s.join(right);
        CHECK(right.empty() && right.validate());
        CHECK_EQ(s.size(), size_t(5000));
        CHECK(s.validate());
        CHECK_EQ(*s.kth(2500), 5000);
    }
    CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(btree_plus_string_keys_and_copy) {
    mem::Tracking t;
    {
        BTreeMap<String, int> m(&t);
        for (int i = 0; i < 3000; ++i) {
            m[String("key") + String(std::to_string(i).c_str())] = i;
        }
        CHECK(m.validate());
        auto copy = m;
        CHECK(copy == m && copy.validate());
        for (int i = 0; i < 3000; i += 2) {
            m.erase(String("key") + String(std::to_string(i).c_str()));
        }
        CHECK(m.validate());
        CHECK_EQ(m.size(), size_t(1500));
        CHECK_EQ(copy.size(), size_t(3000));
    }
    CHECK_EQ(t.stats().live(), size_t(0));
}
