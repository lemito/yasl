#include "fmt.h"

namespace yasl::fmt {
    io::Output& operator<<(io::Output& out, Bytes b) {
        static const char* const kUnits[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB"};
        if (b.n < 1024) {
            out << b.n << " B";
            return out;
        }
        // Tenths of the unit n / 2^(10 * u), rounded to nearest, in 64-bit arithmetic.
        auto tenths_in = [&](size_t u) {
            uint64 whole = b.n >> (10 * u);
            uint64 rest = b.n & ((uint64(1) << (10 * u)) - 1);
            uint64 half = uint64(1) << (10 * u - 1);
            // rest < 2^60, so rest * 10 fits into 64 bits
            return whole * 10 + ((rest * 10 + half) >> (10 * u));
        };
        size_t unit = 1;
        while (unit < 6 && (b.n >> (10 * (unit + 1))) != 0) {
            ++unit;
        }
        uint64 tenths = tenths_in(unit);
        if (tenths >= 10240 && unit < 6) {
            ++unit;
            tenths = tenths_in(unit);
        }
        out << tenths / 10;
        if (tenths % 10 != 0) {
            out << '.' << tenths % 10;
        }
        out << ' ' << kUnits[unit];
        return out;
    }

    io::Output& operator<<(io::Output& out, Percent p) {
        io::FormatSpec spec;
        spec.type = 'f';
        spec.precision = p.prec;
        io::format_floating(out, p.x * 100.0, false, spec);
        out.put('%');
        return out;
    }
}
