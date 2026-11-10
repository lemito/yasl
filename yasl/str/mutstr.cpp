#include "mutstr.h"

namespace yasl {
void MutStr::check_length(size_t n) {
  if (n > max_size()) throw LengthError("yasl::MutStr: too large");
}

MutStr MutStr::concat(StringView a, StringView b, mem::Resource* res) {
  if (a.size() > max_size() || b.size() > max_size() - a.size()) {
    throw LengthError("yasl::MutStr: too large");
  }
  MutStr result(res);
  result.reserve(a.size() + b.size());
  result.append(a);
  result.append(b);
  return result;
}

void MutStr::init_heap(StringView s) {
  check_length(s.size());
  mem::Block b = res_->allocate_at_least(s.size() + 1, alignof(char));
  char* p = static_cast<char*>(b.ptr);
  yasl::mem_copy(p, s.data(), s.size());
  p[s.size()] = '\0';
  storage_.heap = {p, yasl::min(b.size - 1, max_size())};
  size_and_tag_ = kHeapFlag | s.size();
}

void MutStr::take_storage(MutStr& other) noexcept {
  // Trivial union assignment starts the corresponding active member's lifetime.
  storage_ = other.storage_;
  size_and_tag_ = other.size_and_tag_;
  other.init_empty();
}

MutStr::MutStr(MutStr&& other) noexcept : res_(other.res_) {
  take_storage(other);
}

MutStr::MutStr(MutStr&& other, mem::Resource* res) : res_(res) {
  if (res_->is_equal(*other.res_)) {
    take_storage(other);
  } else {
    // Commit to consuming the source only after allocation/copy succeeds.
    assign(other.view());
    other.release();
    other.init_empty();
  }
}

MutStr& MutStr::operator=(MutStr&& other) noexcept {
  if (this != &other) {
    release();
    res_ = other.res_;
    take_storage(other);
  }
  return *this;
}

char& MutStr::at(size_t i) {
  if (i >= size()) throw OutOfRange("yasl::MutStr::at");
  return data()[i];
}

const char& MutStr::at(size_t i) const {
  if (i >= size()) throw OutOfRange("yasl::MutStr::at");
  return data()[i];
}

size_t MutStr::grown_capacity(size_t n) const noexcept {
  const size_t cap = capacity();
  const size_t doubled = cap <= max_size() / 2 ? cap * 2 : max_size();
  return yasl::max(n, doubled);
}

void MutStr::reallocate(size_t cap, bool try_resize) {
  const size_t n = size();
  if (try_resize && is_heap()) {
    mem::Block b = res_->resize(storage_.heap.data, capacity() + 1, cap + 1,
                                alignof(char), true);
    if (b.ptr) {
      storage_.heap = {static_cast<char*>(b.ptr),
                       yasl::min(b.size - 1, max_size())};
      return;
    }
  }
  mem::Block b = res_->allocate_at_least(cap + 1, alignof(char));
  char* p = static_cast<char*>(b.ptr);
  yasl::mem_copy(p, data(), n + 1);
  release();
  storage_.heap = {p, yasl::min(b.size - 1, max_size())};
  size_and_tag_ = kHeapFlag | n;
}

void MutStr::reserve(size_t n) {
  check_length(n);
  if (n > capacity()) reallocate(n, true);
}

void MutStr::shrink_to_fit() {
  if (!is_heap()) return;
  const size_t n = size();
  if (n <= inline_capacity) {
    const Heap old = storage_.heap;
    Storage next;
    yasl::mem_copy(next.small, old.data, n + 1);
    storage_ = next;
    size_and_tag_ = n;
    res_->deallocate(old.data, old.capacity + 1, alignof(char));
  } else if (n < capacity()) {
    reallocate(n, false);
  }
}

bool MutStr::aliases(StringView s) const noexcept {
  if (s.empty()) return false;
  const auto base = reinterpret_cast<uintptr_t>(data());
  const auto p = reinterpret_cast<uintptr_t>(s.data());
  return p >= base && p - base <= size();
}

MutStr& MutStr::assign(StringView s) {
  check_length(s.size());
  // Even a view containing the old terminator may need growth at the SSO
  // boundary; retain its offset across a moving resize/allocation.
  const bool self = aliases(s);
  const size_t offset = self ? size_t(s.data() - data()) : 0;
  reserve(s.size());
  const char* source = self ? data() + offset : s.data();
  if (!s.empty()) yasl::mem_move(data(), source, s.size());
  set_size(s.size());
  return *this;
}

MutStr& MutStr::assign(size_t n, char c) {
  check_length(n);
  reserve(n);
  if (n) yasl::mem_set(data(), c, n);
  set_size(n);
  return *this;
}

MutStr& MutStr::append(StringView s) {
  const size_t old = size();
  if (s.size() > max_size() - old) {
    throw LengthError("yasl::MutStr: too large");
  }
  if (s.empty()) return *this;
  const bool self = aliases(s);
  const size_t offset = self ? size_t(s.data() - data()) : 0;
  grow(old + s.size());
  const char* source = self ? data() + offset : s.data();
  yasl::mem_move(data() + old, source, s.size());
  set_size(old + s.size());
  return *this;
}

char* MutStr::append_uninitialized(size_t n) {
  const size_t old = size();
  if (n > max_size() - old) {
    throw LengthError("yasl::MutStr: too large");
  }
  grow(old + n);
  set_size(old + n);
  return data() + old;
}

MutStr& MutStr::append(size_t n, char c) {
  if (n) yasl::mem_set(append_uninitialized(n), c, n);
  return *this;
}

void MutStr::resize(size_t n, char c) {
  check_length(n);
  if (n > size())
    append(n - size(), c);
  else
    set_size(n);
}

MutStr& MutStr::erase(size_t pos, size_t n) {
  const size_t old = size();
  if (pos > old) throw OutOfRange("yasl::MutStr::erase");
  n = yasl::min(n, old - pos);
  yasl::mem_move(data() + pos, data() + pos + n, old - pos - n);
  set_size(old - n);
  return *this;
}

MutStr& MutStr::replace(size_t pos, size_t n, StringView s) {
  const size_t old = size();
  if (pos > old) throw OutOfRange("yasl::MutStr::replace");
  n = yasl::min(n, old - pos);
  const size_t kept = old - n;
  if (s.size() > max_size() - kept) {
    throw LengthError("yasl::MutStr: too large");
  }
  if (pos == old) return append(s);
  const size_t next = kept + s.size();
  const size_t tail = old - pos - n;
  if (next > capacity()) {
    // Copy all three ranges before releasing the old allocation. Aliased
    // replacement text remains valid, with one allocation and no snapshot.
    MutStr tmp(res_);
    tmp.reserve(grown_capacity(next));
    yasl::mem_copy(tmp.data(), data(), pos);
    if (!s.empty()) yasl::mem_copy(tmp.data() + pos, s.data(), s.size());
    yasl::mem_copy(tmp.data() + pos + s.size(), data() + pos + n, tail);
    tmp.set_size(next);
    swap(tmp);
    return *this;
  }
  if (aliases(s) && tail) {
    MutStr snapshot(s, res_);
    return replace(pos, n, snapshot.view());
  }
  // If there is no tail, memmove alone handles overlapping replacement text.
  if (tail) yasl::mem_move(data() + pos + s.size(), data() + pos + n, tail);
  if (!s.empty()) yasl::mem_move(data() + pos, s.data(), s.size());
  set_size(next);
  return *this;
}
}  // namespace yasl
