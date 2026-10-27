#include "str.h"


namespace yasl {
    namespace {
        // Returned by mut_data() for an empty string: there is nothing to write anyway.
        char g_empty[1] = {'\0'};
    }

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

    bool String::aliases(StringView s) const noexcept {
        if (!rep_ || s.empty()) {
            return false;
        }
        auto b = reinterpret_cast<uintptr_t>(rep_->chars());
        auto p = reinterpret_cast<uintptr_t>(s.data());
        return b <= p && p <= b + rep_->cap;
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

        if (unique() && ns <= rep_->cap) {
            char* d = rep_->chars();
            yasl::mem_move(d + pos + insert, d + pos + remove, tail);
            rep_->size = ns;
            d[ns] = '\0';
            return d + pos;
        }
        if (ns == 0) {
            release(rep_);
            rep_ = nullptr;
            return g_empty;
        }

        size_t cap = ns;
        if (rep_ && ns > rep_->cap) {
            cap = yasl::max(ns, rep_->cap * 2);
        }
        Rep* nr = alloc_rep(res_, cap);
        const char* d = data();
        char* nd = nr->chars();
        yasl::mem_copy(nd, d, pos);
        yasl::mem_copy(nd + pos + insert, d + pos + remove, tail);
        nr->size = ns;
        nd[ns] = '\0';
        release(rep_);
        rep_ = nr;
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
        if (!rep_) {
            return g_empty;
        }
        if (!rep_->rc.unique()) {
            reserve(rep_->size);
        }
        return rep_->chars();
    }

    void String::reserve(size_t n) {
        if (unique() && n <= rep_->cap) {
            return;
        }
        if (!rep_ && n == 0) {
            return;
        }
        size_t sz = size();
        Rep* nr = alloc_rep(res_, yasl::max(n, sz));
        yasl::mem_copy(nr->chars(), data(), sz);
        nr->size = sz;
        nr->chars()[sz] = '\0';
        release(rep_);
        rep_ = nr;
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
        if (unique()) {
            rep_->size = 0;
            rep_->chars()[0] = '\0';
        } else {
            release(rep_);
            rep_ = nullptr;
        }
    }
}
