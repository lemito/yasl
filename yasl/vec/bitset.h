#pragma once

#include "vector.h"

#include <yasl/sys/bits.h>

namespace yasl {
    // Growable set of bits (Java BitSet). Bits past size() read as 0; set() grows as needed.
    //
    //     BitSet s;
    //     s.set(3); s.set(70);
    //     for (size_t i : s) ...          // 3, 70
    class BitSet {
    public:
        static constexpr size_t npos = size_t(-1);

        BitSet() = default;

        explicit BitSet(mem::Resource* res)
            : words_(res)
        {
        }

        // Starts with n zero bits.
        explicit BitSet(size_t n, mem::Resource* res = mem::default_resource())
            : words_(res)
            , size_(n)
        {
            words_.resize((n + 63) / 64);
        }

        size_t size() const noexcept {
            return size_;
        }

        bool test(size_t i) const noexcept {
            return i < size_ && ((words_[i / 64] >> (i % 64)) & 1) != 0;
        }

        // Sets bit i to value, growing the set if i >= size().
        void set(size_t i, bool value = true) {
            if (i >= size_) {
                if (!value) {
                    return;
                }
                resize(i + 1);
            }
            uint64 mask = uint64(1) << (i % 64);
            if (value) {
                words_[i / 64] |= mask;
            } else {
                words_[i / 64] &= ~mask;
            }
        }

        void reset(size_t i) {
            set(i, false);
        }

        void flip(size_t i) {
            set(i, !test(i));
        }

        void resize(size_t n) {
            if (n < size_) {
                words_.resize((n + 63) / 64);
                if (n % 64 != 0) {
                    words_.back() &= (uint64(1) << (n % 64)) - 1;
                }
            } else {
                words_.resize((n + 63) / 64);
            }
            size_ = n;
        }

        void clear() noexcept {
            words_.clear();
            size_ = 0;
        }

        // Number of set bits.
        size_t count() const noexcept {
            size_t n = 0;
            for (uint64 w : words_) {
                n += size_t(popcount(w));
            }
            return n;
        }

        bool any() const noexcept {
            for (uint64 w : words_) {
                if (w) {
                    return true;
                }
            }
            return false;
        }

        // Index of the first set bit at or after `from`, npos if none.
        size_t find_first(size_t from = 0) const noexcept {
            if (from >= size_) {
                return npos;
            }
            size_t w = from / 64;
            uint64 cur = words_[w] & (~uint64(0) << (from % 64));
            for (;;) {
                if (cur) {
                    return w * 64 + size_t(countr_zero(cur));
                }
                if (++w == words_.size()) {
                    return npos;
                }
                cur = words_[w];
            }
        }

        BitSet& operator&=(const BitSet& o) {
            for (size_t i = 0; i < words_.size(); ++i) {
                words_[i] &= i < o.words_.size() ? o.words_[i] : 0;
            }
            return *this;
        }

        BitSet& operator|=(const BitSet& o) {
            if (o.size_ > size_) {
                resize(o.size_);
            }
            for (size_t i = 0; i < o.words_.size(); ++i) {
                words_[i] |= o.words_[i];
            }
            return *this;
        }

        BitSet& operator^=(const BitSet& o) {
            if (o.size_ > size_) {
                resize(o.size_);
            }
            for (size_t i = 0; i < o.words_.size(); ++i) {
                words_[i] ^= o.words_[i];
            }
            return *this;
        }

        friend bool operator==(const BitSet& a, const BitSet& b) noexcept {
            size_t n = a.words_.size() > b.words_.size() ? a.words_.size() : b.words_.size();
            for (size_t i = 0; i < n; ++i) {
                uint64 x = i < a.words_.size() ? a.words_[i] : 0;
                uint64 y = i < b.words_.size() ? b.words_[i] : 0;
                if (x != y) {
                    return false;
                }
            }
            return true;
        }

        // Iterates over the indices of set bits, ascending.
        class Iter {
        public:
            struct Sentinel {};

            size_t operator*() const noexcept {
                return i_;
            }

            Iter& operator++() noexcept {
                i_ = s_->find_first(i_ + 1);
                return *this;
            }

            friend bool operator==(const Iter& it, Sentinel) noexcept {
                return it.i_ == npos;
            }

        private:
            friend class BitSet;

            explicit Iter(const BitSet* s) noexcept
                : s_(s)
                , i_(s->find_first())
            {
            }

            const BitSet* s_;
            size_t i_;
        };

        Iter begin() const noexcept {
            return Iter(this);
        }

        Iter::Sentinel end() const noexcept {
            return {};
        }

    private:
        Vector<uint64> words_;
        size_t size_ = 0;
    };
}
