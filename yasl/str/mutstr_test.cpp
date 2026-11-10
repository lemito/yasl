#include "mutstr.h"

#include <yasl/cont/assoc/hash_map.h>
#include <yasl/cont/seq/vector.h>
#include <yasl/mem/arena.h>
#include <yasl/mem/tracking.h>
#include <yasl/rand/random.h>
#include <yasl/str/builder.h>
#include <yasl/test/test.h>

#include <algorithm>
#include <string>
#include <type_traits>
#include <unordered_map>

using namespace yasl;

namespace {
void same(const MutStr& s, const std::string& expected) {
  CHECK_EQ(s.size(), expected.size());
  CHECK_EQ(s.view(), StringView(expected));
  CHECK(s.capacity() >= s.size());
  CHECK_EQ(s.c_str()[s.size()], '\0');
}

// Verifies the actual Resource contract, including reported slack, alignment,
// resize(old_size, want, relocatable), and matching deallocation after moves.
class ProbeResource final : public mem::Resource {
 public:
  enum class Resize { Unsupported, InPlace, Moving };
  struct Record {
    size_t requested, reported, physical, align;
  };
  std::unordered_map<void*, Record> blocks;
  Resize mode = Resize::Unsupported;
  size_t allocations = 0, resizes = 0, frees = 0;
  bool fail = false, bad_contract = false;
  size_t fail_at = size_t(-1);

 private:
  mem::Block do_alloc(size_t n, size_t align) override {
    if (fail || allocations == fail_at) throw BadAlloc();
    const size_t reported = n + 7;
    const size_t physical =
        mode == Resize::InPlace ? reported + 4096 : reported;
    void* p = mem::system_resource()->allocate(physical, align);
    blocks[p] = {n, reported, physical, align};
    ++allocations;
    return {p, reported};
  }
  void do_free(void* p, size_t n, size_t align) noexcept override {
    auto it = blocks.find(p);
    if (it == blocks.end()) {
      bad_contract = true;
      return;
    }
    const auto record = it->second;
    if (n < record.requested || n > record.reported || align != record.align)
      bad_contract = true;
    mem::system_resource()->deallocate(p, record.physical, record.align);
    blocks.erase(it);
    ++frees;
  }
  mem::Block do_resize(void* p, size_t old, size_t want, size_t align,
                       bool relocatable) override {
    auto it = blocks.find(p);
    if (it == blocks.end()) {
      bad_contract = true;
      return {};
    }
    const auto record = it->second;
    if (old < record.requested || old > record.reported ||
        align != record.align || !relocatable)
      bad_contract = true;
    ++resizes;
    if (mode == Resize::Unsupported) return {};
    if (fail) throw BadAlloc();
    if (mode == Resize::InPlace) {
      CHECK(want + 7 <= record.physical);
      it->second.requested = want;
      it->second.reported = want + 7;
      return {p, want + 7};
    }
    auto next = do_alloc(want, align);
    yasl::mem_copy(next.ptr, p, yasl::min(old, want));
    do_free(p, old, align);
    return next;
  }
};

// Separate, interchangeable facades for an explicit-resource move.
class EqualResource final : public mem::Resource {
 public:
  explicit EqualResource(mem::Resource* upstream) : upstream_(upstream) {}

 private:
  mem::Block do_alloc(size_t n, size_t align) override {
    return upstream_->allocate_at_least(n, align);
  }
  void do_free(void* p, size_t n, size_t align) noexcept override {
    upstream_->deallocate(p, n, align);
  }
  bool do_is_equal(const mem::Resource& other) const noexcept override {
    const auto* facade = dynamic_cast<const EqualResource*>(&other);
    return facade && upstream_ == facade->upstream_;
  }
  mem::Resource* upstream_;
};

template <class F>
void fails_unchanged(MutStr& s, F&& f) {
  const std::string before(s.data(), s.size());
  const char* ptr = s.data();
  const size_t cap = s.capacity();
  auto* res = s.resource();
  CHECK_THROWS(f(), BadAlloc);
  same(s, before);
  CHECK(s.data() == ptr);
  CHECK_EQ(s.capacity(), cap);
  CHECK(s.resource() == res);
}
}  // namespace

YASL_TEST(mutstr_sso_and_mutable_access) {
  static_assert(sizeof(void*) != 8 || sizeof(MutStr) == 32);
  static_assert(std::is_nothrow_move_constructible_v<MutStr>);
  static_assert(std::is_nothrow_move_assignable_v<MutStr>);
  static_assert(std::is_nothrow_swappable_v<MutStr>);
  static_assert(std::is_same_v<decltype(MutStr().data()), char*>);
  static_assert(std::is_same_v<decltype(yasl::declval<const MutStr&>().data()),
                               const char*>);
  mem::Tracking t;
  {
    MutStr empty(&t);
    CHECK(empty.empty());
    CHECK(empty.data() != nullptr);
    same(empty, "");
    for (size_t n = 0; n <= MutStr::inline_capacity; ++n) {
      MutStr s(n, 'a', &t);
      CHECK_EQ(s.capacity(), MutStr::inline_capacity);
      CHECK_EQ(t.stats().allocations, size_t(0));
      if (n) {
        s[0] = 'b';
        s.front() = 'c';
        s.back() = 'd';
        s.at(0) = 'e';
        s.span()[0] = 'f';
        CHECK_EQ(s[0], 'f');
        CHECK_EQ(s.data(), s.mut_data());
        for (char& c : s) c = 'x';
        same(s, std::string(n, 'x'));
      }
      CHECK_THROWS(s.at(n), OutOfRange);
      const auto& cs = s;
      CHECK_THROWS(cs.at(n), OutOfRange);
      CHECK_EQ(cs.span().size(), n);
      CHECK_EQ(size_t(cs.cend() - cs.cbegin()), n);
    }
    MutStr s(15, 'a', &t);
    s.push_back('b');
    CHECK_EQ(t.stats().allocations, size_t(1));
    same(s, std::string(15, 'a') + 'b');
    auto* buffer = s.data();
    const auto capacity = s.capacity();
    s.clear();
    CHECK(s.data() == buffer);
    CHECK_EQ(s.capacity(), capacity);
    s.append("abc");
    s.shrink_to_fit();
    same(s, "abc");
    CHECK_EQ(s.capacity(), MutStr::inline_capacity);
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(mutstr_copy_is_unique_and_keeps_resource_on_assignment) {
  ProbeResource a, b;
  {
    MutStr source(80, 'a', &a);
    auto* source_ptr = source.data();
    char& stable = source[0];
    MutStr copy(source);
    CHECK(copy.resource() == &a);
    CHECK(copy.data() != source_ptr);
    copy[0] = 'b';
    stable = 'z';
    CHECK_EQ(copy[0], 'b');
    CHECK_EQ(source[0], 'z');
    CHECK(source.data() == source_ptr);
    MutStr selected(source, &b);
    CHECK(selected.resource() == &b);
    CHECK(selected == source);
    MutStr target(&b);
    target = source;
    CHECK(target.resource() == &b);
    CHECK(target == source);
    CHECK(target.data() != source.data());
    const auto cap = target.capacity();
    const auto count = b.allocations;
    target = copy;
    CHECK_EQ(b.allocations, count);  // existing capacity reused
    CHECK_EQ(target.capacity(), cap);
    auto* self = &target;
    target = *self;
    CHECK(target == copy);
  }
  CHECK(a.blocks.empty());
  CHECK(b.blocks.empty());
  CHECK(!a.bad_contract);
  CHECK(!b.bad_contract);
}

YASL_TEST(mutstr_move_swap_and_explicit_resources) {
  ProbeResource a, b;
  {
    MutStr source(80, 'a', &a);
    char* ptr = source.data();
    MutStr moved(yasl::move(source));
    CHECK(moved.data() == ptr);
    CHECK(moved.resource() == &a);
    same(source, "");
    CHECK(source.resource() == &a);
    source.append("reusable");
    MutStr target(40, 'b', &b);
    target = yasl::move(moved);
    CHECK(target.data() == ptr);
    CHECK(target.resource() == &a);
    same(moved, "");
    CHECK(b.blocks.empty());
    auto* self = &target;
    target = yasl::move(*self);
    CHECK(target.data() == ptr);
    MutStr tiny("tiny", &b);
    swap(tiny, target);
    CHECK(tiny.data() == ptr);
    CHECK(tiny.resource() == &a);
    same(target, "tiny");
    CHECK(target.resource() == &b);
    MutStr tiny2("other", &a);
    swap(tiny2, target);
    same(target, "other");
    same(tiny2, "tiny");
    MutStr explicit_move(yasl::move(tiny), &b);
    CHECK(explicit_move.resource() == &b);
    CHECK(explicit_move.data() != ptr);
    same(explicit_move, std::string(80, 'a'));
    same(tiny, "");
    MutStr heap2(60, 'q', &a);
    swap(explicit_move, heap2);
    CHECK(explicit_move.resource() == &a);
    CHECK(heap2.resource() == &b);
  }
  CHECK(a.blocks.empty());
  CHECK(b.blocks.empty());
  CHECK(!a.bad_contract);
  CHECK(!b.bad_contract);
  EqualResource left(&a), right(&a);
  {
    MutStr s(40, 'x', &left);
    char* ptr = s.data();
    const auto count = a.allocations;
    MutStr t(yasl::move(s), &right);
    CHECK(t.data() == ptr);
    CHECK_EQ(a.allocations, count);
    CHECK(t.resource() == &right);
    same(s, "");
  }
  CHECK(a.blocks.empty());
  CHECK(!a.bad_contract);
}

YASL_TEST(mutstr_allocate_at_least_and_resize) {
  for (auto mode :
       {ProbeResource::Resize::Unsupported, ProbeResource::Resize::InPlace,
        ProbeResource::Resize::Moving}) {
    ProbeResource r;
    r.mode = mode;
    {
      MutStr s(24, 'x', &r);
      CHECK_EQ(s.capacity(), size_t(31));
      CHECK_EQ(r.allocations, size_t(1));
      auto* ptr = s.data();
      s.reserve(28);
      CHECK(s.data() == ptr);
      CHECK_EQ(r.resizes, size_t(0));
      s.reserve(128);
      CHECK_EQ(r.resizes, size_t(1));
      CHECK_EQ(s.capacity(), size_t(135));
      CHECK((s.data() == ptr) == (mode == ProbeResource::Resize::InPlace));
      same(s, std::string(24, 'x'));
      s.resize(100, 'y');
      same(s, std::string(24, 'x') + std::string(76, 'y'));
      s.shrink_to_fit();
      CHECK_EQ(s.capacity(), size_t(107));
      CHECK_EQ(r.resizes,
               size_t(1));  // shrink copies; does not mutate old block
      s.resize(3);
      s.shrink_to_fit();
      CHECK(r.blocks.empty());
      same(s, "xxx");
    }
    CHECK(r.blocks.empty());
    CHECK(!r.bad_contract);
  }
}

YASL_TEST(mutstr_aliasing_and_embedded_nuls) {
  for (size_t n : {size_t(15), size_t(16), size_t(40)}) {
    for (auto mode :
         {ProbeResource::Resize::Unsupported, ProbeResource::Resize::Moving}) {
      ProbeResource r;
      r.mode = mode;
      {
        MutStr s(n, 'a', &r);
        s.back() = 'b';
        std::string expected(s.data(), s.size());
        auto part = s.view().substr(2);
        std::string snapshot(part.data(), part.size());
        s.append(part);
        expected += snapshot;
        same(s, expected);
        s.assign(s.view().substr(3));
        expected = expected.substr(3);
        same(s, expected);
        s.assign(n, 'x');
        // Include the old NUL, including growth through the SSO boundary.
        expected.assign(n, 'x');
        expected.push_back('\0');
        s.assign(StringView(s.data(), s.size() + 1));
        same(s, expected);
        s.append(StringView(s.data() + s.size(), 1));
        expected.push_back('\0');
        same(s, expected);
        s.replace(0, s.size(), s.view().substr(1));
        expected = expected.substr(1);
        same(s, expected);
        s.clear();
        s.shrink_to_fit();
      }
      CHECK(r.blocks.empty());
      CHECK(!r.bad_contract);
    }
  }
  MutStr s("a\0bc", 4);
  CHECK_EQ(s.size(), size_t(4));
  CHECK_EQ(s.find('\0'), size_t(1));
  s.append(s.view());
  same(s, std::string("a\0bca\0bc", 8));
  s.erase();
  char* p = s.append_uninitialized(3);
  yasl::mem_copy(p, "xyz", 3);
  same(s, "xyz");
}

YASL_TEST(mutstr_modifiers_differential_aliasing) {
  Random rng(0x12345678);
  for (size_t trial = 0; trial < 10000; ++trial) {
    const size_t n = rng.next_u64() % 97;
    std::string expected(n, 'a');
    for (char& c : expected) c = char(rng.next_u64() % 256);
    MutStr s(expected.data(), expected.size());
    if (trial % 2) s.reserve(256);
    const size_t offset = rng.next_u64() % (n + 1);
    const size_t len =
        rng.next_u64() % (n - offset + 2);  // may include the NUL
    const size_t pos = rng.next_u64() % (n + 1);
    const size_t count = rng.next_u64() % (n + 2);
    const StringView alias(s.data() + offset, len);
    const std::string snapshot(expected.data() + offset, len);
    switch (trial % 9) {
      case 0:
        s.replace(pos, count, alias);
        expected.replace(pos, count, snapshot);
        break;
      case 1:
        s.insert(pos, alias);
        expected.insert(pos, snapshot);
        break;
      case 2:
        s.append(alias);
        expected.append(snapshot);
        break;
      case 3:
        s.assign(alias);
        expected = snapshot;
        break;
      case 4:
        s.erase(pos, count);
        expected.erase(pos, count);
        break;
      case 5:
        s.resize(count, 'z');
        expected.resize(count, 'z');
        break;
      case 6:
        s.assign(count, 'q');
        expected.assign(count, 'q');
        break;
      case 7:
        s.append(count, '\0');
        expected.append(count, '\0');
        break;
      case 8:
        s.push_back('x');
        expected.push_back('x');
        s.pop_back();
        expected.pop_back();
        break;
    }
    same(s, expected);
  }
}

YASL_TEST(mutstr_allocation_failures_are_transactional) {
  ProbeResource r, source_resource;
  {
    MutStr s("small", &r);
    r.fail = true;
    fails_unchanged(s, [&] { s.reserve(100); });
    fails_unchanged(s, [&] { s.append(100, 'a'); });
    fails_unchanged(s, [&] { s.assign(100, 'a'); });
    fails_unchanged(s, [&] { s.resize(100); });
    fails_unchanged(s, [&] {
      s.replace(1, 1, StringView("a long replacement beyond sso"));
    });
    CHECK_THROWS(MutStr(100, 'a', &r), BadAlloc);
    MutStr source(50, 'z', &source_resource);
    fails_unchanged(s, [&] { s = source; });
    fails_unchanged(source, [&] { MutStr moved(yasl::move(source), &r); });
    r.fail = false;
    s.assign(60, 'a');
    s.reserve(200);
    r.fail = true;
    // In-place overlapping replace needs a snapshot; its failure preserves s.
    fails_unchanged(s, [&] { s.replace(2, 20, s.view().substr(10, 30)); });
    fails_unchanged(s, [&] { s.shrink_to_fit(); });
    fails_unchanged(s, [&] { s.append(300, 'b'); });
    r.mode = ProbeResource::Resize::Moving;
    fails_unchanged(s, [&] { s.reserve(400); });
    // Edits within reserved capacity continue to work without allocations.
    s.assign(s.view().substr(1));
    s[0] = 'x';
    s.push_back('q');
    s.pop_back();
    s.clear();
    s.shrink_to_fit();
  }
  CHECK(r.blocks.empty());
  CHECK(source_resource.blocks.empty());
  CHECK(!r.bad_contract);
  CHECK(!source_resource.bad_contract);
}

YASL_TEST(mutstr_bounds_and_overflow) {
  MutStr s("abc", mem::null_resource());
  CHECK_THROWS(s.reserve(MutStr::max_size() + 1), LengthError);
  CHECK_THROWS(s.resize(MutStr::npos), LengthError);
  CHECK_THROWS(s.append(MutStr::npos, 'x'), LengthError);
  CHECK_THROWS(s.append_uninitialized(MutStr::npos), LengthError);
  CHECK_THROWS(s.assign(MutStr::npos, 'x'), LengthError);
  const StringView huge("x", MutStr::npos);
  CHECK_THROWS(MutStr(huge), LengthError);
  CHECK_THROWS(s.assign(huge), LengthError);
  CHECK_THROWS(s.append(huge), LengthError);
  CHECK_THROWS(s.replace(0, 1, huge), LengthError);
  CHECK_THROWS(s.insert(4, "x"), OutOfRange);
  CHECK_THROWS(s.replace(4, 0, ""), OutOfRange);
  CHECK_THROWS(s.erase(4), OutOfRange);
  same(s, "abc");
  same(s.substr(100), "");
  s.erase(1, MutStr::npos);
  same(s, "a");
  s.append(StringView());
  s.append(0, 'z');
  s.reserve(0);
  same(s, "a");
}

YASL_TEST(mutstr_resource_defaults_and_integration) {
  mem::Tracking t;
  {
    mem::ScopedDefault scope(&t);
    MutStr s(40, 'x');
    CHECK(s.resource() == &t);
    MutStr from_view(s.view());
    CHECK(from_view.resource() == &t);
    Vector<MutStr> values;
    for (int i = 0; i < 100; ++i) values.push_back(MutStr("tiny"));
    values.push_back(yasl::move(s));
    for (int i = 0; i < 100; ++i) values.push_back(MutStr(40, 'z'));
    CHECK(values[100] == std::string(40, 'x'));
    values[0][0] = 'T';
    CHECK(values[0] == "Tiny");
    std::sort(values[0].begin(), values[0].end());
    CHECK_EQ(values[0], "Tiny");
    HashMap<MutStr, int> map;
    map.try_emplace(MutStr("key"), 7);
    CHECK(map.contains(StringView("key")));
    CHECK_EQ(*map.get("key"), 7);
    CHECK_EQ(hash_value(MutStr("abc")), hash_value(StringView("abc")));
    MutStr text("abc");
    String cow("abc");
    CHECK(text == cow);
    CHECK(cow == text);
    CHECK(text == StringView("abc"));
    CHECK(StringView("abc") == text);
    CHECK("abc" == text);
    CHECK(text < "abd");
    CHECK("abb" < text);
    CHECK_EQ(format("{} {:>5}", text, text), "abc   abc");
    CHECK_EQ(text.substr(1), "bc");
  }
  CHECK_EQ(t.stats().live(), size_t(0));
  mem::Arena arena;
  {
    MutStr s(40, 'a', &arena);
    s.append(200, 'b');
    same(s, std::string(40, 'a') + std::string(200, 'b'));
  }
}

YASL_TEST(mutstr_hash_map_rehash_and_copy_failure) {
  ProbeResource keys;
  mem::Tracking table;
  {
    HashMap<MutStr, int> map(&table);
    for (int i = 0; i < 80; ++i) {
      const std::string key = "a-long-mutable-string-key-" + std::to_string(i);
      map.try_emplace(MutStr(key.data(), key.size(), &keys), i);
    }
    CHECK(map.validate());
    const size_t count = map.size();
    // Fail after several rehash copies: partially constructed fresh entries
    // must be destroyed, and every entry in the old table must survive.
    keys.fail_at = keys.allocations + 3;
    const size_t live_keys = keys.blocks.size();
    CHECK_THROWS(map.reserve(1000), BadAlloc);
    CHECK_EQ(map.size(), count);
    CHECK_EQ(keys.blocks.size(), live_keys);
    CHECK(map.validate());
    keys.fail_at = size_t(-1);
    for (int i = 0; i < 80; ++i) {
      const std::string key = "a-long-mutable-string-key-" + std::to_string(i);
      CHECK_EQ(*map.get(StringView(key)), i);
    }
    // A failed entry copy must leave its slot empty. Retry the insertion
    // afterwards to check growth bookkeeping.
    MutStr key("another-long-mutable-string-key", &keys);
    keys.fail = true;
    CHECK_THROWS(map.try_emplace(yasl::move(key), 99), BadAlloc);
    CHECK_EQ(map.size(), count);
    CHECK(!map.contains("another-long-mutable-string-key"));
    CHECK(map.validate());
    keys.fail = false;
    map.try_emplace(MutStr("another-long-mutable-string-key", &keys), 99);
    map.reserve(1000);
    CHECK_EQ(map.at("another-long-mutable-string-key"), 99);
    CHECK(map.validate());
  }
  CHECK(keys.blocks.empty());
  CHECK(!keys.bad_contract);
  CHECK_EQ(table.stats().live(), size_t(0));
}

YASL_TEST(mutstr_concatenation_and_cross_type_ordering) {
  mem::Tracking t;
  {
    MutStr a("hello", &t), b("world", &t);
    same(a + b, "helloworld");
    same(a + "!", "hello!");
    same("<" + a, "<hello");
    same(a + '!', "hello!");
    same('>' + a, ">hello");
    same(a + std::string("!"), "hello!");
    String cow("hello");
    same(a + cow, "hellohello");
    same(cow + a, "hellohello");
    CHECK(a == cow);
    CHECK(cow == a);
    CHECK(a < String("world"));
    CHECK(String("abc") < a);
    MutStr lhs(50, 'x', &t);
    lhs.reserve(200);
    char* ptr = lhs.data();
    const auto count = t.stats().allocations;
    MutStr sum = yasl::move(lhs) + "!";
    CHECK(sum.data() == ptr);
    CHECK(sum.resource() == &t);
    CHECK_EQ(t.stats().allocations, count);
    same(lhs, "");
    same(sum, std::string(50, 'x') + "!");
    // The RHS may alias the moved LHS.
    MutStr doubled = yasl::move(sum) + sum;
    same(doubled, (std::string(50, 'x') + "!") + (std::string(50, 'x') + "!"));
    same(sum, "");
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}
