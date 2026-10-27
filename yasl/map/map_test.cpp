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
