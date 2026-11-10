#pragma once

#include <yasl/core/span.h>
#include <yasl/mem/resource.h>

#include "view.h"

namespace yasl {
// Mutable value string with SSO and a uniquely owned, NUL-terminated buffer.
// No refcount or heap header. Allocation goes exclusively through res_.
//
// Resource rules match Vector: copy construction uses the source resource;
// copy assignment keeps the destination resource; ordinary moves and swap
// carry the resource with the buffer. Explicit-resource constructors choose
// their destination resource. Every referenced resource must outlive its
// strings.
//
// Copies never invalidate pointers into the source. Capacity-changing
// operations invalidate pointers/views/iterators; other edits may move the text
// they refer to. Move/swap may invalidate pointers into inline storage. Mutable
// access is to [0, size()) only: preserve the trailing NUL at data()[size()].
//
// Allocation failures and invalid lengths/positions leave the string unchanged.
// Concurrent access follows ordinary value semantics: shared reads are safe;
// concurrent mutation of the same object requires external synchronization.
class MutStr {
 public:
  using value_type = char;
  using size_type = size_t;
  using difference_type = ptrdiff_t;
  using reference = char&;
  using const_reference = const char&;
  using pointer = char*;
  using const_pointer = const char*;
  using iterator = char*;
  using const_iterator = const char*;

  static constexpr size_t npos = StringView::npos;
  static constexpr size_t inline_capacity = 15;

  MutStr() noexcept : res_(mem::default_resource()) {}
  explicit MutStr(mem::Resource* res) noexcept : res_(res) {}
  MutStr(const char* s, mem::Resource* res = mem::default_resource())
      : MutStr(StringView(s), res) {}
  MutStr(const char* s, size_t n, mem::Resource* res = mem::default_resource())
      : MutStr(StringView(s, n), res) {}
  explicit MutStr(StringView s, mem::Resource* res = mem::default_resource())
      : res_(res) {
    if (s.size() <= inline_capacity) [[likely]] {
      if (!s.empty()) yasl::mem_copy(storage_.small, s.data(), s.size());
      set_size(s.size());
    } else {
      init_heap(s);
    }
  }
  MutStr(size_t n, char c, mem::Resource* res = mem::default_resource())
      : res_(res) {
    if (n > inline_capacity) reserve(n);
    if (n) yasl::mem_set(data(), c, n);
    set_size(n);
  }

  MutStr(const MutStr& other) : MutStr(other, other.res_) {}
  MutStr(const MutStr& other, mem::Resource* res) : res_(res) {
    if (other.is_heap()) {
      init_heap(other.view());
    } else {
      storage_ = other.storage_;
      size_and_tag_ = other.size_and_tag_;
    }
  }
  MutStr(MutStr&& other) noexcept;
  // Steals for equal resources; otherwise copies before emptying other.
  // Unlike the ordinary move constructor, this overload may allocate/throw.
  MutStr(MutStr&& other, mem::Resource* res);
  ~MutStr() { release(); }

  MutStr& operator=(const MutStr& other) {
    if (this != &other) assign(other.view());
    return *this;
  }
  MutStr& operator=(MutStr&& other) noexcept;
  MutStr& operator=(StringView s) { return assign(s); }
  MutStr& operator=(const char* s) { return assign(StringView(s)); }

  char* data() noexcept {
    return is_heap() ? storage_.heap.data : storage_.small;
  }
  const char* data() const noexcept {
    return is_heap() ? storage_.heap.data : storage_.small;
  }
  char* mut_data() noexcept { return data(); }
  const char* c_str() const noexcept { return data(); }
  size_t size() const noexcept { return size_and_tag_ & ~kHeapFlag; }
  size_t length() const noexcept { return size(); }
  bool empty() const noexcept { return size() == 0; }
  size_t capacity() const noexcept {
    return is_heap() ? storage_.heap.capacity : inline_capacity;
  }
  // Leave room for the terminator and keep pointer differences representable.
  static constexpr size_t max_size() noexcept { return kHeapFlag - 2; }
  mem::Resource* resource() const noexcept { return res_; }
  StringView view() const noexcept { return {data(), size()}; }
  Span<char> span() noexcept { return {data(), size()}; }
  Span<const char> span() const noexcept { return {data(), size()}; }

  char* begin() noexcept { return data(); }
  char* end() noexcept { return data() + size(); }
  const char* begin() const noexcept { return data(); }
  const char* end() const noexcept { return data() + size(); }
  const char* cbegin() const noexcept { return begin(); }
  const char* cend() const noexcept { return end(); }
  char& operator[](size_t i) noexcept {
    YASL_ASSERT(i < size());
    return data()[i];
  }
  const char& operator[](size_t i) const noexcept {
    YASL_ASSERT(i < size());
    return data()[i];
  }
  char& at(size_t i);
  const char& at(size_t i) const;
  char& front() noexcept { return (*this)[0]; }
  const char& front() const noexcept { return (*this)[0]; }
  char& back() noexcept { return (*this)[size() - 1]; }
  const char& back() const noexcept { return (*this)[size() - 1]; }
  void set(size_t i, char c) noexcept { (*this)[i] = c; }

  void reserve(size_t n);
  void shrink_to_fit();
  void resize(size_t n, char c = '\0');
  // Retains capacity, including heap storage.
  void clear() noexcept { set_size(0); }

  MutStr& assign(StringView s);
  MutStr& assign(size_t n, char c);
  // Views supplied to modifiers may alias this string, including its NUL.
  MutStr& append(StringView s);
  MutStr& append(size_t n, char c);
  void push_back(char c) {
    const size_t n = size();
    if (n < capacity()) [[likely]] {
      data()[n] = c;
      set_size(n + 1);
    } else {
      append(1, c);
    }
  }
  void pop_back() noexcept {
    YASL_ASSERT(!empty());
    set_size(size() - 1);
  }
  MutStr& insert(size_t pos, StringView s) { return replace(pos, 0, s); }
  MutStr& erase(size_t pos = 0, size_t n = npos);
  MutStr& replace(size_t pos, size_t n, StringView s);
  // Adds n uninitialized bytes; fill all of them before reading the string.
  char* append_uninitialized(size_t n);
  MutStr& operator+=(StringView s) { return append(s); }
  MutStr& operator+=(char c) {
    push_back(c);
    return *this;
  }

  size_t find(StringView s, size_t pos = 0) const noexcept {
    return view().find(s, pos);
  }
  size_t find(char c, size_t pos = 0) const noexcept {
    return view().find(c, pos);
  }
  size_t rfind(StringView s, size_t pos = npos) const noexcept {
    return view().rfind(s, pos);
  }
  size_t rfind(char c, size_t pos = npos) const noexcept {
    return view().rfind(c, pos);
  }
  bool contains(StringView s) const noexcept { return view().contains(s); }
  bool contains(char c) const noexcept { return view().contains(c); }
  bool starts_with(StringView s) const noexcept {
    return view().starts_with(s);
  }
  bool ends_with(StringView s) const noexcept { return view().ends_with(s); }
  // Clamps the range like StringView::substr; result uses this resource.
  MutStr substr(size_t pos, size_t n = npos) const {
    return MutStr(view().substr(pos, n), res_);
  }

  void swap(MutStr& other) noexcept {
    yasl::swap(storage_, other.storage_);
    yasl::swap(size_and_tag_, other.size_and_tag_);
    yasl::swap(res_, other.res_);
  }
  friend void swap(MutStr& a, MutStr& b) noexcept { a.swap(b); }

  friend bool operator==(const MutStr& a, const MutStr& b) noexcept {
    return a.view() == b.view();
  }
  friend std::strong_ordering operator<=>(const MutStr& a,
                                          const MutStr& b) noexcept {
    return a.view() <=> b.view();
  }
  // Exact string-like arguments avoid ambiguous comparisons with COW String.
  template <class S>
    requires ConvertibleTo<const S&, StringView>
  friend bool operator==(const MutStr& a, const S& b) noexcept {
    return a.view() == StringView(b);
  }
  template <class S>
    requires ConvertibleTo<const S&, StringView>
  friend std::strong_ordering operator<=>(const MutStr& a,
                                          const S& b) noexcept {
    return a.view() <=> StringView(b);
  }
  template <class S>
    requires ConvertibleTo<const S&, StringView>
  friend MutStr operator+(const MutStr& a, const S& b) {
    return concat(a.view(), StringView(b), a.res_);
  }
  template <class S>
    requires ConvertibleTo<const S&, StringView>
  friend MutStr operator+(MutStr&& a, const S& b) {
    a.append(StringView(b));
    return yasl::move(a);
  }
  template <class S>
    requires(!SameAs<RemoveCVRef<S>, MutStr> &&
             ConvertibleTo<const S&, StringView>)
  friend MutStr operator+(const S& a, const MutStr& b) {
    return concat(StringView(a), b.view(), b.res_);
  }
  friend MutStr operator+(MutStr a, char c) {
    a.push_back(c);
    return a;
  }
  friend MutStr operator+(char c, const MutStr& b) {
    return concat(StringView(&c, 1), b.view(), b.res_);
  }

  friend uint64 hash_value(const MutStr& s) noexcept {
    return hash_value(s.view());
  }

 private:
  struct Heap {
    char* data;
    size_t capacity;
  };
  // A separate tag in the size avoids endianness-dependent union inspection.
  static constexpr size_t kHeapFlag = max_value<size_t>() / 2 + 1;
  union Storage {
    char small[inline_capacity + 1];
    Heap heap;
    Storage() noexcept : small{} {}
  } storage_;
  size_t size_and_tag_ = 0;
  mem::Resource* res_;

  bool is_heap() const noexcept { return (size_and_tag_ & kHeapFlag) != 0; }
  void set_size(size_t n) noexcept {
    size_and_tag_ = (size_and_tag_ & kHeapFlag) | n;
    data()[n] = '\0';
  }
  void init_empty() noexcept {
    storage_.small[0] = '\0';
    size_and_tag_ = 0;
  }
  static MutStr concat(StringView a, StringView b, mem::Resource* res);
  void init_heap(StringView s);
  void release() noexcept {
    if (is_heap()) {
      res_->deallocate(storage_.heap.data, storage_.heap.capacity + 1,
                       alignof(char));
    }
  }
  void take_storage(MutStr& other) noexcept;
  bool aliases(StringView s) const noexcept;
  static void check_length(size_t n);
  size_t grown_capacity(size_t n) const noexcept;
  // reserve uses resize(relocatable=true); shrink deliberately allocates anew.
  void reallocate(size_t capacity, bool try_resize);
  void grow(size_t n) {
    if (n > capacity()) reallocate(grown_capacity(n), true);
  }
};

// No pointer into the object's own SSO storage is stored in the representation.
template <>
inline constexpr bool IsTriviallyRelocatable<MutStr> = true;
}  // namespace yasl
