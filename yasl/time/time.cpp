#include "time.h"

#include <time.h>

namespace yasl {
namespace {
int64 read_clock(clockid_t id) noexcept {
  timespec ts;
  ::clock_gettime(id, &ts);
  return int64(ts.tv_sec) * 1'000'000'000 + int64(ts.tv_nsec);
}

// Writes `value / unit` with up to `digits` fractional digits, trailing zeros
// dropped.
void put_fraction(io::Output& out, uint64 value, uint64 unit, int digits) {
  out << value / unit;
  uint64 frac = value % unit;
  if (frac == 0) {
    return;
  }
  char buf[16];
  int n = 0;
  for (; n < digits; ++n) {
    unit /= 10;
    buf[n] = char('0' + frac / unit);
    frac %= unit;
  }
  while (n > 0 && buf[n - 1] == '0') {
    --n;
  }
  if (n > 0) {
    out.put('.');
    out.write(buf, size_t(n));
  }
}
}  // namespace

Instant Instant::now() noexcept { return Instant(read_clock(CLOCK_MONOTONIC)); }

SystemTime SystemTime::now() noexcept {
  return SystemTime(read_clock(CLOCK_REALTIME));
}

io::Output& operator<<(io::Output& out, Duration d) {
  int64 ns = d.as_nanos();
  if (ns == 0) {
    out << "0s";
    return out;
  }
  uint64 u = ns < 0 ? uint64(0) - uint64(ns) : uint64(ns);
  if (ns < 0) {
    out.put('-');
  }
  if (u < 1'000) {
    out << u << "ns";
  } else if (u < 1'000'000) {
    put_fraction(out, u, 1'000, 3);
    out << "us";
  } else if (u < 1'000'000'000) {
    put_fraction(out, u, 1'000'000, 6);
    out << "ms";
  } else {
    uint64 secs = u / 1'000'000'000;
    uint64 rest = u % 1'000'000'000;
    uint64 h = secs / 3600;
    uint64 m = secs / 60 % 60;
    if (h > 0) {
      out << h << 'h';
    }
    if (h > 0 || m > 0) {
      out << m << 'm';
    }
    put_fraction(out, (secs % 60) * 1'000'000'000 + rest, 1'000'000'000, 9);
    out << 's';
  }
  return out;
}
}  // namespace yasl
