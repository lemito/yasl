#include <yasl/algo/algo.h>
#include <yasl/cont/seq/vector.h>
#include <yasl/test/test.h>

#include <vector>

using namespace yasl;

YASL_TEST(algo_edge_sort_empty_single) {
  std::vector<int> empty;
  yasl::sort(empty.begin(), empty.end());
  CHECK(empty.empty());

  std::vector<int> one{42};
  yasl::sort(one.begin(), one.end());
  CHECK_EQ(one[0], 42);
}

YASL_TEST(algo_edge_sort_already_sorted) {
  std::vector<int> v{1, 2, 3, 4, 5};
  yasl::sort(v.begin(), v.end());
  for (int i = 0; i < 5; ++i) {
    CHECK_EQ(v[size_t(i)], i + 1);
  }
}

YASL_TEST(algo_edge_sort_reverse_sorted) {
  std::vector<int> v{5, 4, 3, 2, 1};
  yasl::sort(v.begin(), v.end());
  for (int i = 0; i < 5; ++i) {
    CHECK_EQ(v[size_t(i)], i + 1);
  }
}

YASL_TEST(algo_edge_sort_all_equal) {
  std::vector<int> v(100, 7);
  yasl::sort(v.begin(), v.end());
  for (int x : v) {
    CHECK_EQ(x, 7);
  }
}

YASL_TEST(algo_edge_lower_upper_bound_empty) {
  std::vector<int> empty;
  CHECK(yasl::lower_bound(empty.begin(), empty.end(), 5) == empty.end());
  CHECK(yasl::upper_bound(empty.begin(), empty.end(), 5) == empty.end());
}

YASL_TEST(algo_edge_lower_upper_bound_single) {
  std::vector<int> one{5};
  CHECK(yasl::lower_bound(one.begin(), one.end(), 5) == one.begin());
  CHECK(yasl::upper_bound(one.begin(), one.end(), 5) == one.end());
  CHECK(yasl::lower_bound(one.begin(), one.end(), 3) == one.begin());
  CHECK(yasl::upper_bound(one.begin(), one.end(), 3) == one.begin());
  CHECK(yasl::lower_bound(one.begin(), one.end(), 7) == one.end());
}

YASL_TEST(algo_edge_lower_upper_bound_duplicates) {
  std::vector<int> v{1, 2, 2, 2, 3};
  auto lb = yasl::lower_bound(v.begin(), v.end(), 2);
  auto ub = yasl::upper_bound(v.begin(), v.end(), 2);
  CHECK_EQ(lb - v.begin(), ptrdiff_t(1));
  CHECK_EQ(ub - v.begin(), ptrdiff_t(4));
}

YASL_TEST(algo_edge_binary_search_boundaries) {
  std::vector<int> v{1, 3, 5, 7, 9};
  CHECK_EQ(*yasl::binary_search(v.begin(), v.end(), 1), size_t(0));
  CHECK_EQ(*yasl::binary_search(v.begin(), v.end(), 9), size_t(4));
  CHECK(!yasl::binary_search(v.begin(), v.end(), 0));
  CHECK(!yasl::binary_search(v.begin(), v.end(), 10));
  CHECK(!yasl::binary_search(v.begin(), v.end(), 2));
  std::vector<int> empty;
  CHECK(!yasl::binary_search(empty.begin(), empty.end(), 1));
}

YASL_TEST(algo_edge_rotate_empty_and_no_op) {
  std::vector<int> empty;
  yasl::rotate(empty.data(), empty.data(), empty.data());

  std::vector<int> one{1};
  yasl::rotate(one.data(), one.data(), one.data() + 1);
  CHECK_EQ(one[0], 1);

  std::vector<int> v{1, 2, 3, 4, 5};
  yasl::rotate(v.data(), v.data(), v.data() + 5);
  for (int i = 0; i < 5; ++i) {
    CHECK_EQ(v[size_t(i)], i + 1);
  }
  yasl::rotate(v.data(), v.data() + 5, v.data() + 5);
  for (int i = 0; i < 5; ++i) {
    CHECK_EQ(v[size_t(i)], i + 1);
  }
}

YASL_TEST(algo_edge_rotate_full) {
  std::vector<int> v{1, 2, 3, 4, 5};
  yasl::rotate(v.data(), v.data() + 2, v.data() + 5);
  CHECK_EQ(v[0], 3);
  CHECK_EQ(v[1], 4);
  CHECK_EQ(v[2], 5);
  CHECK_EQ(v[3], 1);
  CHECK_EQ(v[4], 2);

  yasl::rotate(v.data(), v.data() + 3, v.data() + 5);
  CHECK_EQ(v[0], 1);
  CHECK_EQ(v[1], 2);
  CHECK_EQ(v[2], 3);
  CHECK_EQ(v[3], 4);
  CHECK_EQ(v[4], 5);
}

YASL_TEST(algo_edge_equal_empty_and_mismatched) {
  CHECK(yasl::equal(static_cast<int*>(nullptr), size_t(0),
                    static_cast<int*>(nullptr), size_t(0)));
  int a[] = {1, 2, 3};
  int b[] = {1, 2, 3};
  CHECK(yasl::equal(a, size_t(3), b, size_t(3)));
  b[2] = 4;
  CHECK(!yasl::equal(a, size_t(3), b, size_t(3)));
  CHECK(!yasl::equal(a, size_t(2), b, size_t(3)));
  CHECK(!yasl::equal(a, size_t(3), b, size_t(2)));
}

YASL_TEST(algo_edge_compare3_empty) {
  std::vector<int> a, b;
  CHECK(yasl::compare3(a.begin(), a.end(), b.begin(), b.end()) ==
        std::strong_ordering::equal);
  a.push_back(1);
  CHECK(yasl::compare3(a.begin(), a.end(), b.begin(), b.end()) ==
        std::strong_ordering::greater);
  CHECK(yasl::compare3(b.begin(), b.end(), a.begin(), a.end()) ==
        std::strong_ordering::less);
}

YASL_TEST(algo_edge_count_if_empty_and_all) {
  std::vector<int> empty;
  CHECK_EQ(yasl::count_if(empty.begin(), empty.end(), [](int) { return true; }),
           size_t(0));
  std::vector<int> v{1, 2, 3, 4, 5};
  CHECK_EQ(
      yasl::count_if(v.begin(), v.end(), [](int) { return true; }), size_t(5));
  CHECK_EQ(
      yasl::count_if(v.begin(), v.end(), [](int) { return false; }), size_t(0));
  CHECK_EQ(
      yasl::count_if(v.begin(), v.end(), [](int x) { return x % 2 == 0; }),
      size_t(2));
}

YASL_TEST(algo_edge_any_all_none_empty) {
  std::vector<int> empty;
  CHECK(!yasl::any_of(empty.begin(), empty.end(), [](int) { return true; }));
  CHECK(yasl::all_of(empty.begin(), empty.end(), [](int) { return false; }));
  CHECK(yasl::none_of(empty.begin(), empty.end(), [](int) { return true; }));
}

YASL_TEST(algo_edge_min_max_element_single) {
  std::vector<int> v{42};
  CHECK_EQ(*yasl::min_element(v.begin(), v.end()), 42);
  CHECK_EQ(*yasl::max_element(v.begin(), v.end()), 42);
}

YASL_TEST(algo_edge_min_max_element_all_equal) {
  std::vector<int> v(100, 5);
  CHECK_EQ(*yasl::min_element(v.begin(), v.end()), 5);
  CHECK_EQ(*yasl::max_element(v.begin(), v.end()), 5);
}

YASL_TEST(algo_edge_partition_empty_and_all_true_false) {
  std::vector<int> empty;
  auto mid = yasl::partition(empty.begin(), empty.end(), [](int) { return true; });
  CHECK(mid == empty.end());

  std::vector<int> v{1, 2, 3, 4, 5};
  auto all_true = yasl::partition(v.begin(), v.end(), [](int) { return true; });
  CHECK(all_true == v.end());

  std::vector<int> v2{1, 2, 3};
  auto all_false = yasl::partition(v2.begin(), v2.end(), [](int) { return false; });
  CHECK(all_false == v2.begin());
}

YASL_TEST(algo_edge_stable_sort_empty_single) {
  std::vector<int> empty;
  yasl::stable_sort(empty.begin(), empty.end());
  CHECK(empty.empty());

  std::vector<int> one{7};
  yasl::stable_sort(one.begin(), one.end());
  CHECK_EQ(one[0], 7);
}

YASL_TEST(algo_edge_nth_element_first_last) {
  std::vector<int> v{5, 3, 1, 4, 2};
  yasl::nth_element(v.begin(), v.begin(), v.end());
  CHECK_EQ(v[0], 1);

  std::vector<int> v2{5, 3, 1, 4, 2};
  yasl::nth_element(v2.begin(), v2.begin() + 4, v2.end());
  CHECK_EQ(v2[4], 5);
}

YASL_TEST(algo_edge_find_if_not_found) {
  std::vector<int> v{1, 3, 5, 7};
  auto it = yasl::find_if(v.begin(), v.end(), [](int x) { return x % 2 == 0; });
  CHECK(it == v.end());
  std::vector<int> empty;
  CHECK(yasl::find_if(empty.begin(), empty.end(), [](int) { return true; }) ==
        empty.end());
}

YASL_TEST(algo_edge_sort_with_custom_comparator) {
  std::vector<int> v{1, 2, 3, 4, 5};
  yasl::sort(v.begin(), v.end(), [](int a, int b) { return a > b; });
  for (int i = 0; i < 5; ++i) {
    CHECK_EQ(v[size_t(i)], 5 - i);
  }
}
