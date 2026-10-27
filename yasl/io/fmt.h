#pragma once

#include "output.h"

// Number formatting helpers for streams: they carry a FormatSpec, so everything
// "{:...}" can do is available without a format string.
//
//     sb << "t=" << fmt::fixed(dt, 3) << " id=" << fmt::hex(id).width(8).zero_pad();
//     out << fmt::grouped(1000000) << ' ' << fmt::bytes(1536);   // 1_000_000 1.5 KiB
//
// The wrappers also work inside format("{}") (the spec of the field is merged in).

namespace yasl::fmt {
    // A number plus how to print it. Modifiers return a modified copy.
    template <Arithmetic T>
    struct Num {
        T v;
        io::FormatSpec spec;

        constexpr Num width(uint32 n) const noexcept {
            Num r = *this;
            r.spec.width = n;
            return r;
        }

        constexpr Num fill(char c) const noexcept {
            Num r = *this;
            r.spec.fill = c;
            return r;
        }

        constexpr Num left() const noexcept {
            Num r = *this;
            r.spec.align = '<';
            return r;
        }

        constexpr Num right() const noexcept {
            Num r = *this;
            r.spec.align = '>';
            return r;
        }

        constexpr Num center() const noexcept {
            Num r = *this;
            r.spec.align = '^';
            return r;
        }

        constexpr Num plus() const noexcept {
            Num r = *this;
            r.spec.plus = true;
            return r;
        }

        // Prefix 0x / 0b / 0 for hex, binary, octal.
        constexpr Num alt() const noexcept {
            Num r = *this;
            r.spec.alt = true;
            return r;
        }

        constexpr Num zero_pad() const noexcept {
            Num r = *this;
            r.spec.zero = true;
            return r;
        }

        constexpr Num precision(int32 n) const noexcept {
            Num r = *this;
            r.spec.precision = n;
            return r;
        }
    };

    namespace detail {
        template <class T>
        constexpr Num<T> make(T v, char type) noexcept {
            Num<T> n{v, {}};
            n.spec.type = type;
            return n;
        }

        // The wrapper's own spec wins; the field spec ("{:>10}") fills in what the wrapper left unset.
        inline io::FormatSpec merge(io::FormatSpec own, const io::FormatSpec& field) noexcept {
            if (own.width == 0) {
                own.width = field.width;
                own.fill = field.fill;
            }
            if (!own.align) {
                own.align = field.align;
            }
            own.plus |= field.plus;
            own.zero |= field.zero;
            if (own.precision < 0) {
                own.precision = field.precision;
            }
            return own;
        }

        template <class T>
        void write_num(io::Output& out, T v, const io::FormatSpec& spec) {
            if constexpr (FloatingPoint<T>) {
                io::format_floating(out, double(v), SameAs<RemoveCV<T>, float>, spec);
            } else if constexpr (SignedIntegral<T>) {
                io::format_integer(out, v < 0 ? uint64(0) - uint64(v) : uint64(v), v < 0, spec);
            } else {
                io::format_integer(out, uint64(v), false, spec);
            }
        }
    }

    template <Arithmetic T>
    constexpr Num<T> num(T x) noexcept {
        return detail::make(x, 0);
    }

    template <Integral T>
    constexpr Num<T> hex(T x) noexcept {
        return detail::make(x, 'x');
    }

    template <Integral T>
    constexpr Num<T> HEX(T x) noexcept {
        return detail::make(x, 'X');
    }

    template <Integral T>
    constexpr Num<T> oct(T x) noexcept {
        return detail::make(x, 'o');
    }

    template <Integral T>
    constexpr Num<T> bin(T x) noexcept {
        return detail::make(x, 'b');
    }

    template <FloatingPoint T>
    constexpr Num<T> fixed(T x, int32 prec = 6) noexcept {
        return detail::make(x, 'f').precision(prec);
    }

    template <FloatingPoint T>
    constexpr Num<T> sci(T x, int32 prec = 6) noexcept {
        return detail::make(x, 'e').precision(prec);
    }

    template <Arithmetic T>
    constexpr Num<T> pad(T x, uint32 width, char fill = ' ') noexcept {
        return detail::make(x, 0).width(width).fill(fill);
    }

    template <Arithmetic T>
    io::Output& operator<<(io::Output& out, const Num<T>& n) {
        detail::write_num(out, n.v, n.spec);
        return out;
    }

    template <Arithmetic T>
    void format_value(io::Output& out, const Num<T>& n, const io::FormatSpec& field) {
        detail::write_num(out, n.v, detail::merge(n.spec, field));
    }

    // Integer with a digit separator every three digits: 1_000_000.
    template <Integral T>
    struct Grouped {
        T v;
        char sep;
        io::FormatSpec spec;

        constexpr Grouped width(uint32 n) const noexcept {
            Grouped r = *this;
            r.spec.width = n;
            return r;
        }

        constexpr Grouped fill(char c) const noexcept {
            Grouped r = *this;
            r.spec.fill = c;
            return r;
        }

        constexpr Grouped left() const noexcept {
            Grouped r = *this;
            r.spec.align = '<';
            return r;
        }

        constexpr Grouped plus() const noexcept {
            Grouped r = *this;
            r.spec.plus = true;
            return r;
        }
    };

    template <Integral T>
    constexpr Grouped<T> grouped(T x, char sep = '_') noexcept {
        return {x, sep, {}};
    }

    namespace detail {
        template <class T>
        void write_grouped(io::Output& out, T v, char sep, const io::FormatSpec& spec) {
            bool negative = false;
            uint64 mag = uint64(v);
            if constexpr (SignedIntegral<T>) {
                negative = v < 0;
                mag = negative ? uint64(0) - uint64(v) : uint64(v);
            }
            char digits[num::kMaxIntChars];
            size_t n = num::format_uint(digits, mag);
            char buf[num::kMaxIntChars * 2 + 2];
            size_t len = 0;
            if (negative) {
                buf[len++] = '-';
            } else if (spec.plus) {
                buf[len++] = '+';
            }
            for (size_t i = 0; i < n; ++i) {
                if (i > 0 && (n - i) % 3 == 0) {
                    buf[len++] = sep;
                }
                buf[len++] = digits[i];
            }
            out.write_padded(StringView(buf, len), spec, '>');
        }
    }

    template <Integral T>
    io::Output& operator<<(io::Output& out, const Grouped<T>& g) {
        detail::write_grouped(out, g.v, g.sep, g.spec);
        return out;
    }

    template <Integral T>
    void format_value(io::Output& out, const Grouped<T>& g, const io::FormatSpec& field) {
        detail::write_grouped(out, g.v, g.sep, detail::merge(g.spec, field));
    }

    // Human-readable size with binary units: 512 B, 1.5 KiB, 2 MiB.
    struct Bytes {
        uint64 n;
    };

    constexpr Bytes bytes(uint64 n) noexcept {
        return {n};
    }

    io::Output& operator<<(io::Output& out, Bytes b);

    inline void format_value(io::Output& out, Bytes b, const io::FormatSpec& field) {
        if (field.width == 0) {
            out << b;
        } else {
            io::detail::ScratchOutput tmp;
            tmp << b;
            out.write_padded(tmp.view(), field, '>');
        }
    }

    // A fraction as a percentage: percent(0.256, 1) prints 25.6%.
    struct Percent {
        double x;
        int32 prec;
    };

    constexpr Percent percent(double x, int32 prec = 0) noexcept {
        return {x, prec};
    }

    io::Output& operator<<(io::Output& out, Percent p);

    inline void format_value(io::Output& out, Percent p, const io::FormatSpec& field) {
        if (field.width == 0) {
            out << p;
        } else {
            io::detail::ScratchOutput tmp;
            tmp << p;
            out.write_padded(tmp.view(), field, '>');
        }
    }
}
