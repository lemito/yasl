#pragma once

#include <yasl/sys/check.h>
#include <yasl/sys/hash.h>
#include <yasl/sys/optional.h>
#include <yasl/sys/utility.h>

#include <compare> // operator<=> result types: language support

namespace yasl {
    class SplitRange;
    class LinesRange;
    class WordsRange;

    enum class ParseErrorKind : uint8 {
        Empty,        // nothing to parse
        InvalidDigit, // a character that does not belong to a number
        Overflow,     // well-formed, but does not fit into the type
    };

    // Why StringView::try_parse failed.
    class ParseError {
    public:
        constexpr explicit ParseError(ParseErrorKind kind) noexcept
            : kind_(kind)
        {
        }

        constexpr ParseErrorKind kind() const noexcept {
            return kind_;
        }

        constexpr const char* what() const noexcept {
            switch (kind_) {
                case ParseErrorKind::Empty:
                    return "cannot parse a number from an empty string";
                case ParseErrorKind::InvalidDigit:
                    return "invalid character in number";
                case ParseErrorKind::Overflow:
                    return "number does not fit into the type";
            }
            return "";
        }

        friend constexpr bool operator==(ParseError, ParseError) noexcept = default;

    private:
        ParseErrorKind kind_;
    };

    // Non-owning view of a char range.
    //
    // substr() and friends clamp instead of throwing, and the helpers that usually
    // get hand-written are built in: trim, split, split_once, parse<T>.
    //
    // Implicitly constructible from anything with data() and size() returning chars
    // (String, StringBuilder, Vector<char>, and std::string / std::string_view too).
    class StringView {
    public:
        static constexpr size_t npos = size_t(-1);

        using value_type = char;
        using size_type = size_t;
        using const_iterator = const char*;
        using iterator = const_iterator;

        constexpr StringView() noexcept = default;

        constexpr StringView(const char* s) noexcept
            : p_(s ? s : "")
            , n_(s ? str_length(s) : 0)
        {
        }

        constexpr StringView(const char* s, size_t n) noexcept
            : p_(n ? s : "")
            , n_(n)
        {
        }

        template <class S>
            requires(!SameAs<RemoveCVRef<S>, StringView> && !ConvertibleTo<const S&, const char*> && requires(const S& s) {
                { s.data() } -> ConvertibleTo<const char*>;
                { s.size() } -> ConvertibleTo<size_t>;
            })
        constexpr StringView(const S& s) noexcept
            : StringView(s.data(), s.size())
        {
        }

        constexpr const char* data() const noexcept {
            return p_;
        }

        constexpr size_t size() const noexcept {
            return n_;
        }

        constexpr size_t length() const noexcept {
            return n_;
        }

        constexpr bool empty() const noexcept {
            return n_ == 0;
        }

        constexpr const char* begin() const noexcept {
            return p_;
        }

        constexpr const char* end() const noexcept {
            return p_ + n_;
        }

        constexpr const char& operator[](size_t i) const noexcept {
            YASL_ASSERT(i < n_);
            return p_[i];
        }

        constexpr char front() const noexcept {
            YASL_ASSERT(n_);
            return p_[0];
        }

        constexpr char back() const noexcept {
            YASL_ASSERT(n_);
            return p_[n_ - 1];
        }

        // Slicing: all of these clamp out-of-range arguments.

        constexpr StringView substr(size_t pos, size_t n = npos) const noexcept {
            pos = pos < n_ ? pos : n_;
            size_t rest = n_ - pos;
            return {p_ + pos, n < rest ? n : rest};
        }

        constexpr StringView first(size_t n) const noexcept {
            return substr(0, n);
        }

        constexpr StringView last(size_t n) const noexcept {
            return n < n_ ? StringView(p_ + n_ - n, n) : *this;
        }

        constexpr StringView drop_front(size_t n) const noexcept {
            return substr(n);
        }

        constexpr StringView drop_back(size_t n) const noexcept {
            return first(n < n_ ? n_ - n : 0);
        }

        // Search

        constexpr bool starts_with(StringView s) const noexcept {
            return s.n_ <= n_ && mem_compare(p_, s.p_, s.n_) == 0;
        }

        constexpr bool starts_with(char c) const noexcept {
            return n_ && p_[0] == c;
        }

        constexpr bool ends_with(StringView s) const noexcept {
            return s.n_ <= n_ && mem_compare(p_ + n_ - s.n_, s.p_, s.n_) == 0;
        }

        constexpr bool ends_with(char c) const noexcept {
            return n_ && p_[n_ - 1] == c;
        }

        constexpr bool contains(StringView s) const noexcept {
            return find(s) != npos;
        }

        constexpr bool contains(char c) const noexcept {
            return find(c) != npos;
        }

        constexpr size_t find(char c, size_t pos = 0) const noexcept {
            for (size_t i = pos; i < n_; ++i) {
                if (p_[i] == c) {
                    return i;
                }
            }
            return npos;
        }

        constexpr size_t find(StringView s, size_t pos = 0) const noexcept {
            if (s.n_ == 0) {
                return pos <= n_ ? pos : npos;
            }
            if (s.n_ > n_) {
                return npos;
            }
            for (size_t i = pos; i + s.n_ <= n_; ++i) {
                if (p_[i] == s.p_[0] && mem_compare(p_ + i, s.p_, s.n_) == 0) {
                    return i;
                }
            }
            return npos;
        }

        constexpr size_t rfind(char c, size_t pos = npos) const noexcept {
            if (n_ == 0) {
                return npos;
            }
            for (size_t i = pos < n_ ? pos + 1 : n_; i-- > 0;) {
                if (p_[i] == c) {
                    return i;
                }
            }
            return npos;
        }

        constexpr size_t rfind(StringView s, size_t pos = npos) const noexcept {
            if (s.n_ > n_) {
                return npos;
            }
            size_t start = n_ - s.n_;
            if (pos < start) {
                start = pos;
            }
            for (size_t i = start + 1; i-- > 0;) {
                if (mem_compare(p_ + i, s.p_, s.n_) == 0) {
                    return i;
                }
            }
            return npos;
        }

        constexpr size_t find_first_of(StringView set, size_t pos = 0) const noexcept {
            for (size_t i = pos; i < n_; ++i) {
                if (set.find(p_[i]) != npos) {
                    return i;
                }
            }
            return npos;
        }

        constexpr size_t find_first_not_of(StringView set, size_t pos = 0) const noexcept {
            for (size_t i = pos; i < n_; ++i) {
                if (set.find(p_[i]) == npos) {
                    return i;
                }
            }
            return npos;
        }

        constexpr size_t find_last_of(StringView set, size_t pos = npos) const noexcept {
            for (size_t i = pos < n_ ? pos + 1 : n_; i-- > 0;) {
                if (set.find(p_[i]) != npos) {
                    return i;
                }
            }
            return npos;
        }

        constexpr size_t find_last_not_of(StringView set, size_t pos = npos) const noexcept {
            for (size_t i = pos < n_ ? pos + 1 : n_; i-- > 0;) {
                if (set.find(p_[i]) == npos) {
                    return i;
                }
            }
            return npos;
        }

        // Trimming (ASCII whitespace by default)

        static constexpr const char* kWhitespace = " \t\n\v\f\r";

        constexpr StringView trim_left(StringView chars = kWhitespace) const noexcept {
            size_t i = find_first_not_of(chars);
            return i == npos ? StringView(p_ + n_, 0) : drop_front(i);
        }

        constexpr StringView trim_right(StringView chars = kWhitespace) const noexcept {
            size_t i = find_last_not_of(chars);
            return i == npos ? StringView(p_, 0) : first(i + 1);
        }

        constexpr StringView trim(StringView chars = kWhitespace) const noexcept {
            return trim_left(chars).trim_right(chars);
        }

        // Splitting

        // "key=value".split_once('=') -> {"key", "value"}; none if there is no separator.
        Optional<Pair<StringView, StringView>> split_once(StringView sep) const noexcept {
            size_t i = find(sep);
            if (i == npos) {
                return none;
            }
            return Pair<StringView, StringView>{first(i), drop_front(i + sep.size())};
        }

        Optional<Pair<StringView, StringView>> split_once(char sep) const noexcept {
            return split_once(char_view(sep));
        }

        Optional<Pair<StringView, StringView>> rsplit_once(StringView sep) const noexcept {
            size_t i = rfind(sep);
            if (i == npos) {
                return none;
            }
            return Pair<StringView, StringView>{first(i), drop_front(i + sep.size())};
        }

        Optional<Pair<StringView, StringView>> rsplit_once(char sep) const noexcept {
            return rsplit_once(char_view(sep));
        }

        // The text after / before `prefix` / `suffix`, none if it does not start / end with it.
        Optional<StringView> strip_prefix(StringView prefix) const noexcept {
            if (!starts_with(prefix)) {
                return none;
            }
            return drop_front(prefix.size());
        }

        Optional<StringView> strip_prefix(char c) const noexcept {
            if (!starts_with(c)) {
                return none;
            }
            return drop_front(1);
        }

        Optional<StringView> strip_suffix(StringView suffix) const noexcept {
            if (!ends_with(suffix)) {
                return none;
            }
            return drop_back(suffix.size());
        }

        Optional<StringView> strip_suffix(char c) const noexcept {
            if (!ends_with(c)) {
                return none;
            }
            return drop_back(1);
        }

        // Number of non-overlapping occurrences (0 for an empty `sub`).
        constexpr size_t count(StringView sub) const noexcept {
            size_t n = 0;
            if (sub.empty()) {
                return 0;
            }
            for (size_t i = find(sub); i != npos; i = find(sub, i + sub.size())) {
                ++n;
            }
            return n;
        }

        constexpr size_t count(char c) const noexcept {
            size_t n = 0;
            for (char x : *this) {
                n += x == c ? 1 : 0;
            }
            return n;
        }

        // Lines split at '\n', a trailing '\r' dropped; no empty piece after a final newline.
        constexpr LinesRange lines() const noexcept;

        // Words separated by runs of ASCII whitespace; never yields an empty piece.
        constexpr WordsRange split_whitespace() const noexcept;

        // Lazy split: for (StringView part : line.split(',')) ...
        // "a,,b" yields "a", "", "b"; an empty string yields one empty piece.
        constexpr SplitRange split(StringView sep) const noexcept;
        constexpr SplitRange split(char sep) const noexcept;

        // The whole view must be a number, otherwise none. Defined in num.h.
        template <class T>
            requires(Arithmetic<T> && !SameAs<T, bool>)
        Optional<T> parse() const noexcept;

        // Same, but says what was wrong. Defined in num.h.
        template <class T>
            requires(Arithmetic<T> && !SameAs<T, bool>)
        Result<T, ParseError> try_parse() const noexcept;

        // Comparison

        constexpr int compare(StringView other) const noexcept {
            size_t n = n_ < other.n_ ? n_ : other.n_;
            int c = mem_compare(p_, other.p_, n);
            if (c != 0) {
                return c;
            }
            return n_ == other.n_ ? 0 : (n_ < other.n_ ? -1 : 1);
        }

        friend constexpr bool operator==(StringView a, StringView b) noexcept {
            return a.n_ == b.n_ && mem_compare(a.p_, b.p_, a.n_) == 0;
        }

        friend constexpr std::strong_ordering operator<=>(StringView a, StringView b) noexcept {
            return a.compare(b) <=> 0;
        }

        friend uint64 hash_value(StringView v) noexcept {
            return hash_bytes(v.p_, v.n_);
        }

        // A one-character view for every byte value; lets char overloads reuse the string ones.
        static constexpr StringView char_view(char c) noexcept {
            return {&kAllChars[static_cast<unsigned char>(c)], 1};
        }

    private:
        static constexpr char kAllChars[256] = {
#define YASL_C16(x) char(x), char(x + 1), char(x + 2), char(x + 3), char(x + 4), char(x + 5), char(x + 6), char(x + 7), \
                    char(x + 8), char(x + 9), char(x + 10), char(x + 11), char(x + 12), char(x + 13), char(x + 14), char(x + 15)
            YASL_C16(0), YASL_C16(16), YASL_C16(32), YASL_C16(48), YASL_C16(64), YASL_C16(80), YASL_C16(96), YASL_C16(112),
            YASL_C16(128), YASL_C16(144), YASL_C16(160), YASL_C16(176), YASL_C16(192), YASL_C16(208), YASL_C16(224), YASL_C16(240),
#undef YASL_C16
        };

        const char* p_ = "";
        size_t n_ = 0;
    };

    struct DefaultSentinel {};

    class SplitRange {
    public:
        class iterator {
        public:
            using value_type = StringView;
            using difference_type = ptrdiff_t;

            constexpr iterator() noexcept = default;

            constexpr iterator(StringView text, StringView sep) noexcept
                : rest_(text)
                , sep_(sep)
            {
                advance();
            }

            constexpr StringView operator*() const noexcept {
                return cur_;
            }

            constexpr iterator& operator++() noexcept {
                advance();
                return *this;
            }

            constexpr iterator operator++(int) noexcept {
                iterator copy = *this;
                advance();
                return copy;
            }

            friend constexpr bool operator==(const iterator& it, DefaultSentinel) noexcept {
                return it.done_;
            }

        private:
            constexpr void advance() noexcept {
                if (last_) {
                    done_ = true;
                    return;
                }
                size_t i = rest_.find(sep_);
                if (i == StringView::npos) {
                    cur_ = rest_;
                    last_ = true;
                } else {
                    cur_ = rest_.first(i);
                    rest_ = rest_.drop_front(i + sep_.size());
                }
            }

            StringView rest_;
            StringView sep_;
            StringView cur_;
            bool last_ = false;
            bool done_ = false;
        };

        constexpr SplitRange(StringView text, StringView sep) noexcept
            : text_(text)
            , sep_(sep)
        {
            YASL_ASSERT(!sep.empty());
        }

        constexpr iterator begin() const noexcept {
            return {text_, sep_};
        }

        constexpr DefaultSentinel end() const noexcept {
            return {};
        }

    private:
        StringView text_;
        StringView sep_;
    };

    class LinesRange {
    public:
        class iterator {
        public:
            using value_type = StringView;
            using difference_type = ptrdiff_t;

            constexpr iterator() noexcept = default;

            explicit constexpr iterator(StringView text) noexcept
                : rest_(text)
            {
                advance();
            }

            constexpr StringView operator*() const noexcept {
                return cur_;
            }

            constexpr iterator& operator++() noexcept {
                advance();
                return *this;
            }

            friend constexpr bool operator==(const iterator& it, DefaultSentinel) noexcept {
                return it.done_;
            }

        private:
            constexpr void advance() noexcept {
                if (rest_.empty()) {
                    done_ = true;
                    return;
                }
                size_t i = rest_.find('\n');
                if (i == StringView::npos) {
                    cur_ = rest_;
                    rest_ = {};
                } else {
                    cur_ = rest_.first(i);
                    rest_ = rest_.drop_front(i + 1);
                }
                if (cur_.ends_with('\r')) {
                    cur_ = cur_.drop_back(1);
                }
            }

            StringView rest_;
            StringView cur_;
            bool done_ = false;
        };

        explicit constexpr LinesRange(StringView text) noexcept
            : text_(text)
        {
        }

        constexpr iterator begin() const noexcept {
            return iterator(text_);
        }

        constexpr DefaultSentinel end() const noexcept {
            return {};
        }

    private:
        StringView text_;
    };

    class WordsRange {
    public:
        class iterator {
        public:
            using value_type = StringView;
            using difference_type = ptrdiff_t;

            constexpr iterator() noexcept = default;

            explicit constexpr iterator(StringView text) noexcept
                : rest_(text)
            {
                advance();
            }

            constexpr StringView operator*() const noexcept {
                return cur_;
            }

            constexpr iterator& operator++() noexcept {
                advance();
                return *this;
            }

            friend constexpr bool operator==(const iterator& it, DefaultSentinel) noexcept {
                return it.done_;
            }

        private:
            constexpr void advance() noexcept {
                rest_ = rest_.trim_left();
                if (rest_.empty()) {
                    done_ = true;
                    return;
                }
                size_t i = rest_.find_first_of(StringView::kWhitespace);
                if (i == StringView::npos) {
                    cur_ = rest_;
                    rest_ = {};
                } else {
                    cur_ = rest_.first(i);
                    rest_ = rest_.drop_front(i);
                }
            }

            StringView rest_;
            StringView cur_;
            bool done_ = false;
        };

        explicit constexpr WordsRange(StringView text) noexcept
            : text_(text)
        {
        }

        constexpr iterator begin() const noexcept {
            return iterator(text_);
        }

        constexpr DefaultSentinel end() const noexcept {
            return {};
        }

    private:
        StringView text_;
    };

    constexpr LinesRange StringView::lines() const noexcept {
        return LinesRange(*this);
    }

    constexpr WordsRange StringView::split_whitespace() const noexcept {
        return WordsRange(*this);
    }

    constexpr SplitRange StringView::split(StringView sep) const noexcept {
        return {*this, sep};
    }

    constexpr SplitRange StringView::split(char sep) const noexcept {
        return {*this, char_view(sep)};
    }

    namespace literals {
        constexpr StringView operator""_sv(const char* s, size_t n) noexcept {
            return {s, n};
        }
    }
}

#include "num.h"
