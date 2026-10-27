#pragma once

#include <yasl/str/num.h>
#include <yasl/str/view.h>
#include <yasl/sys/traits.h>
#include <yasl/sys/utility.h>

namespace yasl::io {
    class Output;

    // Parsed "{:...}" replacement field: [[fill]align][+][#][0][width][.precision][type]
    //   align: '<' left, '>' right, '^' center
    //   type:  integers d x X o b c; floats f e (default: shortest round-trip); strings s
    struct FormatSpec {
        char fill = ' ';
        char align = 0;
        bool plus = false;
        bool alt = false;
        bool zero = false;
        uint32 width = 0;
        int32 precision = -1;
        char type = 0;
    };

    // Writes the formatted value; used by operator<< and format().
    void format_integer(Output& out, uint64 magnitude, bool negative, const FormatSpec& spec);
    void format_floating(Output& out, double value, bool single, const FormatSpec& spec);
    void format_string(Output& out, StringView s, const FormatSpec& spec);
    void format_pointer(Output& out, const void* p, const FormatSpec& spec);

    namespace detail {
        struct FormatArg {
            const void* value;
            void (*fn)(Output&, const void*, const FormatSpec&);
        };

        void vformat(Output& out, StringView fmt, const FormatArg* args, size_t count);

        template <class T>
        void format_one(Output& out, const T& v, const FormatSpec& spec);

        template <class T>
        void format_thunk(Output& out, const void* p, const FormatSpec& spec) {
            format_one(out, *static_cast<const T*>(p), spec);
        }

        template <class T>
        FormatArg make_arg(const T& v) noexcept {
            return {yasl::addressof(v), &format_thunk<T>};
        }
    }

    // Buffered output stream.
    //
    // The base class owns the fast path: writes land in the window [pos_, end_)
    // provided by the derived class; only when it is full does the virtual
    // overflow() run. Derived classes decide where bytes go (file, string, memory).
    //
    //     out << "x = " << 42 << '\n';
    //     out.format("{:>8} | {:.3f}\n", name, ratio);
    class Output {
    public:
        Output() noexcept = default;
        virtual ~Output() = default;

        Output(const Output&) = delete;
        Output& operator=(const Output&) = delete;

        void write(const char* data, size_t n) {
            if (n <= size_t(end_ - pos_)) {
                if (n) {
                    yasl::mem_copy(pos_, data, n);
                    pos_ += n;
                }
            } else {
                overflow(data, n);
            }
        }

        void write(StringView s) {
            write(s.data(), s.size());
        }

        void put(char c) {
            if (pos_ != end_) {
                *pos_++ = c;
            } else {
                overflow(&c, 1);
            }
        }

        void put(char c, size_t n);

        void flush() {
            do_flush();
        }

        Output& operator<<(StringView s) {
            write(s);
            return *this;
        }

        Output& operator<<(const char* s) {
            write(StringView(s));
            return *this;
        }

        Output& operator<<(char c) {
            put(c);
            return *this;
        }

        // Templates, so that string literals never silently convert to bool.
        template <class T>
            requires SameAs<T, bool>
        Output& operator<<(T v) {
            write(v ? StringView("true") : StringView("false"));
            return *this;
        }

        template <Integral T>
            requires(!SameAs<T, bool> && !SameAs<T, char>)
        Output& operator<<(T v) {
            char buf[num::kMaxIntChars];
            size_t n;
            if constexpr (SignedIntegral<T>) {
                n = num::format_int(buf, int64(v));
            } else {
                n = num::format_uint(buf, uint64(v));
            }
            write(buf, n);
            return *this;
        }

        template <FloatingPoint T>
        Output& operator<<(T v) {
            char buf[num::kMaxFloatChars];
            size_t n = SameAs<T, float> ? num::format_float(buf, float(v)) : num::format_double(buf, double(v));
            write(buf, n);
            return *this;
        }

        Output& operator<<(const void* p) {
            format_pointer(*this, p, {});
            return *this;
        }

        Output& operator<<(Output& (*manip)(Output&)) {
            return manip(*this);
        }

        // Python/std::format-like formatting: "{}", "{0}", "{:>10.3f}", "{{" and "}}".
        // Throws FormatError on a malformed format string.
        template <class... Args>
        Output& format(StringView fmt, const Args&... args) {
            if constexpr (sizeof...(Args) == 0) {
                detail::vformat(*this, fmt, nullptr, 0);
            } else {
                const detail::FormatArg list[] = {detail::make_arg(args)...};
                detail::vformat(*this, fmt, list, sizeof...(Args));
            }
            return *this;
        }

        // Writes `s` padded to spec.width according to spec.fill/align.
        void write_padded(StringView s, const FormatSpec& spec, char default_align = '<');

    protected:
        // [pos_, end_) cannot take n more bytes: the derived class must consume `data`.
        virtual void overflow(const char* data, size_t n) = 0;

        virtual void do_flush() {
        }

        char* pos_ = nullptr;
        char* end_ = nullptr;
    };

    inline Output& endl(Output& out) {
        out.put('\n');
        out.flush();
        return out;
    }

    inline Output& flush(Output& out) {
        out.flush();
        return out;
    }

    // Discards everything, counting the bytes.
    class NullOutput final: public Output {
    public:
        size_t written() const noexcept {
            return written_;
        }

    protected:
        void overflow(const char*, size_t n) override {
            written_ += n;
        }

    private:
        size_t written_ = 0;
    };

    // Writes into a fixed caller-provided buffer, silently truncating.
    class ArrayOutput final: public Output {
    public:
        ArrayOutput(char* buf, size_t size) noexcept
            : begin_(buf)
        {
            pos_ = buf;
            end_ = buf + size;
        }

        StringView view() const noexcept {
            return {begin_, size_t(pos_ - begin_)};
        }

        bool truncated() const noexcept {
            return truncated_;
        }

    protected:
        void overflow(const char* data, size_t n) override {
            size_t room = size_t(end_ - pos_);
            yasl::mem_copy(pos_, data, room);
            pos_ += room;
            truncated_ |= n > room;
        }

    private:
        char* begin_;
        bool truncated_ = false;
    };

    namespace detail {
        // Collects into a growable buffer; used to measure/pad arbitrary values.
        class ScratchOutput final: public Output {
        public:
            ScratchOutput() noexcept {
                pos_ = buf_;
                end_ = buf_ + sizeof(buf_);
            }

            ~ScratchOutput() override;

            StringView view() const noexcept {
                return {heap_ ? heap_ : buf_, size_t(pos_ - (heap_ ? heap_ : buf_))};
            }

        protected:
            void overflow(const char* data, size_t n) override;

        private:
            char buf_[256];
            char* heap_ = nullptr;
            size_t cap_ = 0;
        };

        template <class T>
        constexpr bool kAlwaysFalse = false;

        template <class T>
        void format_one(Output& out, const T& v, const FormatSpec& spec) {
            if constexpr (requires(Output& o, const T& x, const FormatSpec& s) { format_value(o, x, s); }) {
                format_value(out, v, spec); // user hook found by ADL
            } else if constexpr (SameAs<T, bool>) {
                format_string(out, v ? "true" : "false", spec);
            } else if constexpr (SameAs<T, char>) {
                if (spec.type && spec.type != 'c') {
                    format_integer(out, uint64(uint8(v)), false, spec);
                } else {
                    format_string(out, StringView(&v, 1), spec);
                }
            } else if constexpr (Integral<T>) {
                if constexpr (SignedIntegral<T>) {
                    format_integer(out, v < 0 ? uint64(0) - uint64(v) : uint64(v), v < 0, spec);
                } else {
                    format_integer(out, uint64(v), false, spec);
                }
            } else if constexpr (FloatingPoint<T>) {
                format_floating(out, double(v), SameAs<T, float>, spec);
            } else if constexpr (ConvertibleTo<const T&, StringView>) {
                format_string(out, StringView(v), spec);
            } else if constexpr (Pointer<T>) {
                format_pointer(out, static_cast<const void*>(v), spec);
            } else if constexpr (requires(Output& o, const T& x) { o << x; }) {
                if (spec.width == 0) {
                    out << v;
                } else {
                    ScratchOutput tmp;
                    tmp << v;
                    out.write_padded(tmp.view(), spec);
                }
            } else {
                static_assert(kAlwaysFalse<T>, "type is not formattable: define format_value() or operator<<(io::Output&, const T&)");
            }
        }
    }
}
