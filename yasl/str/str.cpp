#include "str.h"

namespace yasl {
String::Rep* String::alloc_rep(mem::Resource* res, size_t cap) {
  if (cap > max_value<size_t>() - sizeof(Rep) - 1) {
    throw LengthError("yasl::String: too large");
  }
  mem::Block b = res->allocate_at_least(sizeof(Rep) + cap + 1, alignof(Rep));
  Rep* rep = ::new (b.ptr) Rep{RefCount(1), 0, b.size - sizeof(Rep) - 1, res};
  rep->chars()[0] = '\0';
  return rep;
}

void String::release(Rep* rep) noexcept {
  if (rep && rep->rc.unref()) {
    mem::Resource* res = rep->res;
    size_t bytes = sizeof(Rep) + rep->cap + 1;
    rep->~Rep();
    res->deallocate(rep, bytes, alignof(Rep));
  }
}

void String::init_heap(StringView s) {
  Rep* rep = alloc_rep(res_, s.size());
  char* d = rep->chars();
  yasl::mem_copy(d, s.data(), s.size());
  rep->size = s.size();
  d[s.size()] = '\0';
  set_heap(rep);
}

bool String::aliases(StringView s) const noexcept {
  if (s.empty()) {
    return false;
  }
  auto b = reinterpret_cast<uintptr_t>(data());
  auto p = reinterpret_cast<uintptr_t>(s.data());
  return b <= p && p <= b + capacity();
}

char String::at(size_t i) const {
  if (i >= size()) {
    throw OutOfRange("yasl::String::at");
  }
  return data()[i];
}

char* String::splice(size_t pos, size_t remove, size_t insert) {
  size_t old = size();
  YASL_ASSERT(pos <= old && remove <= old - pos);
  size_t kept = old - remove;
  if (insert > max_value<size_t>() - sizeof(Rep) - 1 - kept) {
    throw LengthError("yasl::String: too large");
  }
  size_t ns = kept + insert;
  size_t tail = old - pos - remove;

  if (!is_heap()) {
    if (ns <= inline_capacity) {
      yasl::mem_move(buf_ + pos + insert, buf_ + pos + remove, tail);
      set_inline_size(ns);
      return buf_ + pos;
    }
    Rep* nr = alloc_rep(res_, yasl::max(ns, inline_capacity * 2));
    char* nd = nr->chars();
    yasl::mem_copy(nd, buf_, pos);
    yasl::mem_copy(nd + pos + insert, buf_ + pos + remove, tail);
    nr->size = ns;
    nd[ns] = '\0';
    set_heap(nr);
    return nd + pos;
  }

  if (rep_->rc.unique() && ns > rep_->cap) {
    size_t cap = yasl::max(ns, rep_->cap * 2);
    mem::Block b = rep_->res->resize(rep_, sizeof(Rep) + rep_->cap + 1,
                                     sizeof(Rep) + cap + 1, alignof(Rep), true);
    if (b.ptr) {
      rep_ = static_cast<Rep*>(b.ptr);
      rep_->cap = b.size - sizeof(Rep) - 1;
    }
  }
  if (rep_->rc.unique() && ns <= rep_->cap) {
    char* d = rep_->chars();
    yasl::mem_move(d + pos + insert, d + pos + remove, tail);
    rep_->size = ns;
    d[ns] = '\0';
    return d + pos;
  }

  Rep* old_rep = rep_;
  const char* d = old_rep->chars();
  if (ns <= inline_capacity) {
    yasl::mem_copy(buf_, d, pos);
    yasl::mem_copy(buf_ + pos + insert, d + pos + remove, tail);
    set_inline_size(ns);
    release(old_rep);
    return buf_ + pos;
  }
  size_t cap = ns;
  if (ns > old_rep->cap) {
    cap = yasl::max(ns, old_rep->cap * 2);
  }
  Rep* nr = alloc_rep(res_, cap);
  char* nd = nr->chars();
  yasl::mem_copy(nd, d, pos);
  yasl::mem_copy(nd + pos + insert, d + pos + remove, tail);
  nr->size = ns;
  nd[ns] = '\0';
  release(old_rep);
  set_heap(nr);
  return nd + pos;
}

String& String::replace(size_t pos, size_t n, StringView s) {
  size_t sz = size();
  YASL_ASSERT(pos <= sz);
  n = yasl::min(n, sz - pos);
  if (aliases(s)) {
    // The in-place path moves bytes around; take a private copy of s first.
    String tmp(s, res_);
    return replace(pos, n, tmp.view());
  }
  char* gap = splice(pos, n, s.size());
  if (!s.empty()) {
    yasl::mem_copy(gap, s.data(), s.size());
  }
  return *this;
}

String& String::append(size_t n, char c) {
  if (n) {
    yasl::mem_set(splice(size(), 0, n), c, n);
  }
  return *this;
}

char* String::mut_data() {
  if (!is_heap()) {
    return buf_;
  }
  if (!rep_->rc.unique()) {
    reserve(rep_->size);
  }
  return rep_->chars();
}

void String::reserve(size_t n) {
  if (n <= capacity() && unique()) {
    return;
  }
  size_t sz = size();
  Rep* nr = alloc_rep(res_, yasl::max(n, sz));
  yasl::mem_copy(nr->chars(), data(), sz);
  nr->size = sz;
  nr->chars()[sz] = '\0';
  if (is_heap()) {
    release(rep_);
  }
  set_heap(nr);
}

void String::resize(size_t n, char c) {
  size_t sz = size();
  if (n <= sz) {
    splice(n, sz - n, 0);
  } else {
    append(n - sz, c);
  }
}

void String::clear() noexcept {
  if (!is_heap()) {
    init_empty();
  } else if (rep_->rc.unique()) {
    rep_->size = 0;
    rep_->chars()[0] = '\0';
  } else {
    release(rep_);
    init_empty();
  }
}

String replace_all(StringView text, StringView from, StringView to,
                   mem::Resource* res) {
  String out(res);
  if (from.empty()) {
    out.append(text);
    return out;
  }
  size_t pos = 0;
  for (size_t i = text.find(from); i != StringView::npos;
       i = text.find(from, pos)) {
    out.append(text.substr(pos, i - pos));
    out.append(to);
    pos = i + from.size();
  }
  out.append(text.substr(pos));
  return out;
}

String repeat(StringView text, size_t n, mem::Resource* res) {
  String out(res);
  if (n != 0 && text.size() > max_value<size_t>() / n) {
    throw LengthError("yasl::repeat: too long");
  }
  out.reserve(text.size() * n);
  for (size_t i = 0; i < n; ++i) {
    out.append(text);
  }
  return out;
}

String to_upper(StringView text, mem::Resource* res) {
  String out(res);
  char* p = out.append_uninitialized(text.size());
  for (size_t i = 0; i < text.size(); ++i) {
    char c = text[i];
    p[i] = (c >= 'a' && c <= 'z') ? char(c - 'a' + 'A') : c;
  }
  return out;
}

String to_lower(StringView text, mem::Resource* res) {
  String out(res);
  char* p = out.append_uninitialized(text.size());
  for (size_t i = 0; i < text.size(); ++i) {
    char c = text[i];
    p[i] = (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c;
  }
  return out;
}
}  // namespace yasl
