#include "num.h"

#include <yasl/core/check.h>
#include <yasl/core/math.h>

namespace yasl::num {
namespace {
constexpr char kLower[] = "0123456789abcdefghijklmnopqrstuvwxyz";
constexpr char kUpper[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

constexpr uint32 kPow10[] = {1,      10,      100,      1000,      10000,
                             100000, 1000000, 10000000, 100000000, 1000000000};

// Fixed-capacity unsigned big integer, little-endian 32-bit words.
// 6144 bits cover every intermediate value of the algorithms below.
class Big {
 public:
  static constexpr int kCap = 192;

  Big() noexcept = default;

  Big(const Big& o) noexcept : n_(o.n_) {
    yasl::mem_copy(w_, o.w_, size_t(n_) * 4);
  }

  Big& operator=(const Big& o) noexcept {
    n_ = o.n_;
    yasl::mem_copy(w_, o.w_, size_t(n_) * 4);
    return *this;
  }

  void set(uint64 v) noexcept {
    n_ = 0;
    while (v) {
      w_[n_++] = uint32(v);
      v >>= 32;
    }
  }

  bool zero() const noexcept { return n_ == 0; }

  int bitlen() const noexcept {
    return n_ == 0 ? 0 : (n_ - 1) * 32 + bit_width(w_[n_ - 1]);
  }

  void mul_small(uint32 m) noexcept {
    uint64 carry = 0;
    for (int i = 0; i < n_; ++i) {
      uint64 t = uint64(w_[i]) * m + carry;
      w_[i] = uint32(t);
      carry = t >> 32;
    }
    if (carry) {
      push(uint32(carry));
    }
  }

  void add_small(uint32 a) noexcept {
    uint64 carry = a;
    for (int i = 0; carry && i < n_; ++i) {
      uint64 t = uint64(w_[i]) + carry;
      w_[i] = uint32(t);
      carry = t >> 32;
    }
    if (carry) {
      push(uint32(carry));
    }
  }

  void mul_pow10(int e) noexcept {
    for (; e >= 9; e -= 9) {
      mul_small(kPow10[9]);
    }
    if (e > 0) {
      mul_small(kPow10[e]);
    }
  }

  void shl(int bits) noexcept {
    if (n_ == 0 || bits == 0) {
      return;
    }
    int words = bits / 32;
    int b = bits % 32;
    YASL_ASSERT(n_ + words + 1 <= kCap);
    uint32 top = b ? w_[n_ - 1] >> (32 - b) : 0;
    for (int i = n_ - 1; i >= 0; --i) {
      uint32 lo = (b && i > 0) ? w_[i - 1] >> (32 - b) : 0;
      w_[i + words] = (w_[i] << b) | lo;
    }
    for (int i = 0; i < words; ++i) {
      w_[i] = 0;
    }
    n_ += words;
    if (top) {
      w_[n_++] = top;
    }
  }

  void shr(int bits) noexcept {
    int words = bits / 32;
    int b = bits % 32;
    if (words >= n_) {
      n_ = 0;
      return;
    }
    int m = n_ - words;
    for (int i = 0; i < m; ++i) {
      uint32 hi = (b && i + words + 1 < n_) ? w_[i + words + 1] << (32 - b) : 0;
      w_[i] = (w_[i + words] >> b) | hi;
    }
    n_ = m;
    trim();
  }

  // Keeps only the lowest `bits` bits.
  void mask(int bits) noexcept {
    int words = bits / 32;
    int b = bits % 32;
    if (words >= n_) {
      return;
    }
    if (b) {
      w_[words] &= (uint32(1) << b) - 1;
      n_ = words + 1;
    } else {
      n_ = words;
    }
    trim();
  }

  bool bit(int i) const noexcept {
    int word = i / 32;
    return word < n_ && (w_[word] >> (i % 32)) & 1;
  }

  void add(const Big& o) noexcept {
    int m = yasl::max(n_, o.n_);
    uint64 carry = 0;
    for (int i = 0; i < m; ++i) {
      uint64 t = uint64(i < n_ ? w_[i] : 0) + (i < o.n_ ? o.w_[i] : 0) + carry;
      w_[i] = uint32(t);
      carry = t >> 32;
    }
    n_ = m;
    if (carry) {
      push(uint32(carry));
    }
  }

  // *this -= o; requires *this >= o.
  void sub(const Big& o) noexcept {
    uint64 borrow = 0;
    for (int i = 0; i < n_; ++i) {
      uint64 sub = uint64(i < o.n_ ? o.w_[i] : 0) + borrow;
      uint64 cur = w_[i];
      borrow = cur < sub;
      w_[i] = uint32(cur + (borrow << 32) - sub);
    }
    trim();
  }

  // *this /= d, returns the remainder.
  uint32 divmod_small(uint32 d) noexcept {
    uint64 rem = 0;
    for (int i = n_ - 1; i >= 0; --i) {
      uint64 cur = (rem << 32) | w_[i];
      w_[i] = uint32(cur / d);
      rem = cur % d;
    }
    trim();
    return uint32(rem);
  }

  static int cmp(const Big& a, const Big& b) noexcept {
    if (a.n_ != b.n_) {
      return a.n_ < b.n_ ? -1 : 1;
    }
    for (int i = a.n_ - 1; i >= 0; --i) {
      if (a.w_[i] != b.w_[i]) {
        return a.w_[i] < b.w_[i] ? -1 : 1;
      }
    }
    return 0;
  }

  // Compares a + b with c.
  static int cmp_sum(const Big& a, const Big& b, const Big& c) noexcept {
    Big t = a;
    t.add(b);
    return cmp(t, c);
  }

  // Compares 2a with b.
  static int cmp_twice(const Big& a, const Big& b) noexcept {
    Big t = a;
    t.shl(1);
    return cmp(t, b);
  }

 private:
  void push(uint32 v) noexcept {
    YASL_ASSERT(n_ < kCap);
    w_[n_++] = v;
  }

  void trim() noexcept {
    while (n_ > 0 && w_[n_ - 1] == 0) {
      --n_;
    }
  }

  uint32 w_[kCap];
  int n_ = 0;
};

// r -= d * s for the largest d <= 9 (r < 10s is guaranteed by the callers).
int next_digit(Big& r, const Big& s) noexcept {
  int d = 0;
  while (Big::cmp(r, s) >= 0) {
    r.sub(s);
    ++d;
  }
  return d;
}

// A lower bound for k = floor(log10(f * 2^e)) + 1 (off by at most a few;
// callers fix it up).
int estimate_k(uint64 f, int e) noexcept {
  int64 x = int64(e) + bit_width(f) - 1;
  return int((x * 78913) >> 18);  // 78913 / 2^18 ~ log10(2), slightly below
}

// value = f * 2^e (f > 0)
struct Decomposed {
  uint64 f;
  int e;
};

struct FloatFormat {
  int precision;  // significand bits including the hidden one
  int min_e;      // exponent of the subnormals
};

constexpr FloatFormat kDouble{53, -1074};
constexpr FloatFormat kFloat{24, -149};

// Shortest digits d1..dn with value ~ 0.d1..dn * 10^k that read back exactly
// (Steele & White / Burger & Dybvig free-format algorithm).
int shortest_digits(Decomposed v, FloatFormat fmt, char* digits,
                    int& k_out) noexcept {
  Big r;
  Big s;
  Big mp;
  Big mm;
  bool even = (v.f & 1) == 0;
  bool unequal = v.f == (uint64(1) << (fmt.precision - 1)) && v.e > fmt.min_e;
  if (v.e >= 0) {
    r.set(v.f);
    r.shl(v.e + (unequal ? 2 : 1));
    s.set(unequal ? 4 : 2);
    mp.set(1);
    mp.shl(v.e + (unequal ? 1 : 0));
    mm.set(1);
    mm.shl(v.e);
  } else {
    r.set(v.f);
    r.shl(unequal ? 2 : 1);
    s.set(1);
    s.shl(-v.e + (unequal ? 2 : 1));
    mp.set(unequal ? 2 : 1);
    mm.set(1);
  }

  int k = estimate_k(v.f, v.e);
  if (k >= 0) {
    s.mul_pow10(k);
  } else {
    r.mul_pow10(-k);
    mp.mul_pow10(-k);
    mm.mul_pow10(-k);
  }
  while (even ? Big::cmp_sum(r, mp, s) >= 0 : Big::cmp_sum(r, mp, s) > 0) {
    s.mul_small(10);
    ++k;
  }

  int n = 0;
  for (;;) {
    r.mul_small(10);
    mp.mul_small(10);
    mm.mul_small(10);
    int d = next_digit(r, s);
    int lo = Big::cmp(r, mm);
    int hi = Big::cmp_sum(r, mp, s);
    bool low_ok = even ? lo <= 0 : lo < 0;
    bool high_ok = even ? hi >= 0 : hi > 0;
    if (!low_ok && !high_ok) {
      digits[n++] = char('0' + d);
      continue;
    }
    if (low_ok && high_ok) {
      int c = Big::cmp_twice(r, s);
      if (c > 0 || (c == 0 && (d & 1))) {
        ++d;
      }
    } else if (high_ok) {
      ++d;
    }
    digits[n++] = char('0' + d);
    break;
  }
  k_out = k;
  return n;
}

// Rounds the digit string up by one unit in the last place. Returns true on
// carry-out (all nines), in which case digits become "100...0".
bool round_up(char* digits, int n) noexcept {
  int i = n - 1;
  while (i >= 0 && digits[i] == '9') {
    digits[i--] = '0';
  }
  if (i < 0) {
    digits[0] = '1';
    return true;
  }
  ++digits[i];
  return false;
}

// Exactly `count` significant digits, correctly rounded (half to even).
void precision_digits(Decomposed v, int count, char* digits,
                      int& k_out) noexcept {
  Big r;
  Big s;
  r.set(v.f);
  s.set(1);
  if (v.e >= 0) {
    r.shl(v.e);
  } else {
    s.shl(-v.e);
  }
  int k = estimate_k(v.f, v.e);
  if (k >= 0) {
    s.mul_pow10(k);
  } else {
    r.mul_pow10(-k);
  }
  while (Big::cmp(r, s) >= 0) {
    s.mul_small(10);
    ++k;
  }
  for (int i = 0; i < count; ++i) {
    r.mul_small(10);
    digits[i] = char('0' + next_digit(r, s));
  }
  int c = Big::cmp_twice(r, s);
  if ((c > 0 || (c == 0 && ((digits[count - 1] - '0') & 1))) &&
      round_up(digits, count)) {
    ++k;
  }
  k_out = k;
}

struct U128 {
  uint64 lo;
  uint64 hi;
};

U128 shift_right_round(U128 q, int sh) noexcept {
  int k = sh - 1;
  bool half = k < 64 ? (q.lo >> k) & 1 : (q.hi >> (k - 64)) & 1;
  bool sticky = k < 64
                    ? (q.lo & ((uint64(1) << k) - 1)) != 0
                    : q.lo != 0 || (q.hi & ((uint64(1) << (k - 64)) - 1)) != 0;
  if (sh < 64) {
    q.lo = (q.lo >> sh) | (q.hi << (64 - sh));
    q.hi >>= sh;
  } else {
    q.lo = q.hi >> (sh - 64);
    q.hi = 0;
  }
  if (half && (sticky || (q.lo & 1))) {
    q.hi += ++q.lo == 0;
  }
  return q;
}

uint32 div_1e9(U128& q) noexcept {
  constexpr uint64 k = 1000000000;
  uint64 limbs[4] = {q.hi >> 32, uint32(q.hi), q.lo >> 32, uint32(q.lo)};
  uint64 r = 0;
  for (uint64& l : limbs) {
    uint64 cur = (r << 32) | l;
    l = cur / k;
    r = cur % k;
  }
  q.hi = (limbs[0] << 32) | limbs[1];
  q.lo = (limbs[2] << 32) | limbs[3];
  return uint32(r);
}

bool fixed_digits_fast(Decomposed v, int frac, char* digits, int& n) noexcept {
  if (frac > 19 || v.e > 10 || v.e < -116) {
    return false;
  }
  uint64 p10 = 1;
  for (int i = 0; i < frac; ++i) {
    p10 *= 10;
  }
  WideProduct m = mul_wide(v.f, p10);
  U128 q{m.lo, m.hi};
  if (v.e > 0) {
    q.hi = (q.hi << v.e) | (q.lo >> (64 - v.e));
    q.lo <<= v.e;
  } else if (v.e < 0) {
    q = shift_right_round(q, -v.e);
  }
  char tmp[48];
  int k = 0;
  while (q.hi != 0) {
    uint32 chunk = div_1e9(q);
    for (int i = 0; i < 9; ++i) {
      tmp[k++] = char('0' + chunk % 10);
      chunk /= 10;
    }
  }
  uint64 low = q.lo;
  do {
    tmp[k++] = char('0' + low % 10);
    low /= 10;
  } while (low);
  for (int i = 0; i < k; ++i) {
    digits[i] = tmp[k - 1 - i];
  }
  n = k;
  return true;
}

// Decimal digits of round(value * 10^frac), no leading zeros ("0" for zero).
int fixed_digits(Decomposed v, int frac, char* digits) noexcept {
  if (int n; fixed_digits_fast(v, frac, digits, n)) {
    return n;
  }
  Big q;
  q.set(v.f);
  q.mul_pow10(frac);
  if (v.e >= 0) {
    q.shl(v.e);
  } else {
    int sh = -v.e;
    Big rem = q;
    rem.mask(sh);
    q.shr(sh);
    Big half;
    half.set(1);
    half.shl(sh - 1);
    int c = Big::cmp(rem, half);
    if (c > 0 || (c == 0 && q.bit(0))) {
      q.add_small(1);
    }
  }

  char tmp[kMaxFloatChars];
  int n = 0;
  while (!q.zero()) {
    uint32 chunk = q.divmod_small(kPow10[9]);
    for (int i = 0; i < 9; ++i) {
      tmp[n++] = char('0' + chunk % 10);
      chunk /= 10;
    }
  }
  while (n > 1 && tmp[n - 1] == '0') {
    --n;
  }
  if (n == 0) {
    tmp[n++] = '0';
  }
  for (int i = 0; i < n; ++i) {
    digits[i] = tmp[n - 1 - i];
  }
  return n;
}

char* put_exponent(char* p, int x) noexcept {
  *p++ = 'e';
  *p++ = x < 0 ? '-' : '+';
  unsigned u = unsigned(x < 0 ? -x : x);
  if (u < 10) {
    *p++ = '0';
  }
  char tmp[8];
  int n = 0;
  do {
    tmp[n++] = char('0' + u % 10);
    u /= 10;
  } while (u);
  while (n) {
    *p++ = tmp[--n];
  }
  return p;
}

char* put_chars(char* p, const char* s, int n) noexcept {
  yasl::mem_copy(p, s, size_t(n));
  return p + n;
}

char* put_zeros(char* p, int n) noexcept {
  for (int i = 0; i < n; ++i) {
    *p++ = '0';
  }
  return p;
}

// value = 0.d1..dn * 10^k
char* layout_shortest(char* p, const char* d, int n, int k) noexcept {
  int x = k - 1;
  if (x < -5 || x > 20) {
    *p++ = d[0];
    if (n > 1) {
      *p++ = '.';
      p = put_chars(p, d + 1, n - 1);
    }
    return put_exponent(p, x);
  }
  if (k <= 0) {
    *p++ = '0';
    *p++ = '.';
    p = put_zeros(p, -k);
    return put_chars(p, d, n);
  }
  if (k >= n) {
    p = put_chars(p, d, n);
    return put_zeros(p, k - n);
  }
  p = put_chars(p, d, k);
  *p++ = '.';
  return put_chars(p, d + k, n - k);
}

char* layout_fixed(char* p, const char* d, int n, int frac) noexcept {
  if (frac == 0) {
    return put_chars(p, d, n);
  }
  if (n <= frac) {
    *p++ = '0';
    *p++ = '.';
    p = put_zeros(p, frac - n);
    return put_chars(p, d, n);
  }
  p = put_chars(p, d, n - frac);
  *p++ = '.';
  return put_chars(p, d + n - frac, frac);
}

char* layout_scientific(char* p, const char* d, int n, int x) noexcept {
  *p++ = d[0];
  if (n > 1) {
    *p++ = '.';
    p = put_chars(p, d + 1, n - 1);
  }
  return put_exponent(p, x);
}

// Shared tail of format_double/format_float once the value is finite and
// non-zero.
size_t format_finite(char* buf, bool neg, Decomposed v,
                     FloatFormat shortest_fmt, Decomposed as_double,
                     FloatStyle style, int precision) noexcept {
  char* p = buf;
  if (neg) {
    *p++ = '-';
  }
  char digits[kMaxFloatChars];
  int k;
  switch (style) {
    case FloatStyle::Shortest: {
      int n = shortest_digits(v, shortest_fmt, digits, k);
      p = layout_shortest(p, digits, n, k);
      break;
    }
    case FloatStyle::Fixed: {
      int n = fixed_digits(as_double, precision, digits);
      p = layout_fixed(p, digits, n, precision);
      break;
    }
    case FloatStyle::Scientific: {
      precision_digits(as_double, precision + 1, digits, k);
      p = layout_scientific(p, digits, precision + 1, k - 1);
      break;
    }
  }
  return size_t(p - buf);
}

size_t format_special(char* buf, bool neg, bool nan, bool zero,
                      FloatStyle style, int precision) noexcept {
  char* p = buf;
  if (nan) {
    return size_t(put_chars(p, "nan", 3) - buf);
  }
  if (neg) {
    *p++ = '-';
  }
  if (!zero) {
    return size_t(put_chars(p, "inf", 3) - buf);
  }
  *p++ = '0';
  if (style != FloatStyle::Shortest && precision > 0) {
    *p++ = '.';
    p = put_zeros(p, precision);
  }
  if (style == FloatStyle::Scientific) {
    p = put_exponent(p, 0);
  }
  return size_t(p - buf);
}

Decomposed decompose_double(uint64 bits) noexcept {
  uint64 mant = bits & ((uint64(1) << 52) - 1);
  int be = int((bits >> 52) & 0x7FF);
  if (be == 0) {
    return {mant, -1074};
  }
  return {mant | (uint64(1) << 52), be - 1075};
}

// Decimal significand ("digits" without leading/trailing zeros) and exponent:
// value = digits * 10^exp10.
struct Decimal {
  static constexpr int kMaxDigits = 800;

  char digits[kMaxDigits + 1];
  int n = 0;
  int exp10 = 0;
  bool neg = false;
  bool inf = false;
  bool nan = false;
};

bool lower_equals(const char* s, size_t n, const char* word) noexcept {
  size_t m = str_length(word);
  if (n != m) {
    return false;
  }
  for (size_t i = 0; i < n; ++i) {
    char c = s[i];
    if (c >= 'A' && c <= 'Z') {
      c = char(c - 'A' + 'a');
    }
    if (c != word[i]) {
      return false;
    }
  }
  return true;
}

bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }

bool scan_decimal(StringView text, Decimal& d) noexcept {
  const char* s = text.data();
  size_t n = text.size();
  size_t i = 0;
  if (i < n && (s[i] == '+' || s[i] == '-')) {
    d.neg = s[i] == '-';
    ++i;
  }
  if (lower_equals(s + i, n - i, "inf") ||
      lower_equals(s + i, n - i, "infinity")) {
    d.inf = true;
    return true;
  }
  if (lower_equals(s + i, n - i, "nan")) {
    d.nan = true;
    return true;
  }

  bool any = false;
  bool seen = false;
  bool truncated = false;
  for (; i < n && is_digit(s[i]); ++i) {
    any = true;
    if (!seen && s[i] == '0') {
      continue;
    }
    seen = true;
    if (d.n < Decimal::kMaxDigits) {
      d.digits[d.n++] = s[i];
    } else {
      truncated |= s[i] != '0';
      ++d.exp10;
    }
  }
  if (i < n && s[i] == '.') {
    ++i;
    for (; i < n && is_digit(s[i]); ++i) {
      any = true;
      if (!seen && s[i] == '0') {
        --d.exp10;
        continue;
      }
      seen = true;
      if (d.n < Decimal::kMaxDigits) {
        d.digits[d.n++] = s[i];
        --d.exp10;
      } else {
        truncated |= s[i] != '0';
      }
    }
  }
  if (!any) {
    return false;
  }
  if (i < n && (s[i] == 'e' || s[i] == 'E')) {
    ++i;
    bool eneg = false;
    if (i < n && (s[i] == '+' || s[i] == '-')) {
      eneg = s[i] == '-';
      ++i;
    }
    if (i == n || !is_digit(s[i])) {
      return false;
    }
    int e = 0;
    for (; i < n && is_digit(s[i]); ++i) {
      if (e < 100000) {
        e = e * 10 + (s[i] - '0');
      }
    }
    d.exp10 += eneg ? -e : e;
  }
  if (i != n) {
    return false;
  }
  while (d.n > 0 && d.digits[d.n - 1] == '0') {
    --d.n;
    ++d.exp10;
  }
  if (truncated) {
    // Anything strictly between the kept prefix and the next value rounds the
    // same.
    d.digits[d.n++] = '1';
    --d.exp10;
  }
  return true;
}

struct BinaryFormat {
  int precision;  // significand bits including the hidden one
  int emin;       // exponent of the smallest normal
  int emax;
  int bias;
};

// Correctly rounded conversion. Returns the IEEE bit pattern without the sign.
uint64 decimal_to_bits(const Decimal& d, BinaryFormat f) noexcept {
  const uint64 inf_bits = uint64(f.emax + f.bias + 1) << (f.precision - 1);
  if (d.n == 0) {
    return 0;
  }
  int mag = d.n + d.exp10;  // value in [10^(mag-1), 10^mag)
  if (mag > 310) {
    return inf_bits;
  }
  if (mag < -330) {
    return 0;
  }

  Big num;
  Big den;
  for (int i = 0; i < d.n; i += 9) {
    int len = yasl::min(9, d.n - i);
    uint32 chunk = 0;
    for (int j = 0; j < len; ++j) {
      chunk = chunk * 10 + uint32(d.digits[i + j] - '0');
    }
    num.mul_small(kPow10[len]);
    num.add_small(chunk);
  }
  den.set(1);
  if (d.exp10 >= 0) {
    num.mul_pow10(d.exp10);
  } else {
    den.mul_pow10(-d.exp10);
  }

  // Scale so that the quotient has precision + 2 or + 3 bits.
  int shift = (f.precision + 2) - (num.bitlen() - den.bitlen());
  if (shift > 0) {
    num.shl(shift);
  } else if (shift < 0) {
    den.shl(-shift);
  }
  Big div = den;
  div.shl(f.precision + 2);
  uint64 q = 0;
  for (int i = f.precision + 2; i >= 0; --i) {
    if (Big::cmp(num, div) >= 0) {
      num.sub(div);
      q |= uint64(1) << i;
    }
    div.shr(1);
  }
  bool sticky = !num.zero();

  // value = (q + fraction) * 2^-shift
  int qbits = bit_width(q);
  int exp = qbits - 1 - shift;  // exponent of the leading bit
  int drop = qbits - f.precision;
  if (exp < f.emin) {
    drop += f.emin - exp;
  }
  if (drop > qbits) {
    return 0;
  }
  uint64 mant = q >> drop;
  uint64 rem = q & ((uint64(1) << drop) - 1);
  uint64 half = uint64(1) << (drop - 1);
  if (rem > half || (rem == half && (sticky || (mant & 1)))) {
    ++mant;
  }

  if (exp < f.emin) {
    // Subnormal; rounding up into the normal range sets the exponent bit by
    // itself.
    return mant;
  }
  if (mant == (uint64(1) << f.precision)) {
    mant >>= 1;
    ++exp;
  }
  if (exp > f.emax) {
    return inf_bits;
  }
  return (uint64(exp + f.bias) << (f.precision - 1)) |
         (mant & ((uint64(1) << (f.precision - 1)) - 1));
}

constexpr double kExactPow10[] = {
    1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,  1e10, 1e11,
    1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};
}  // namespace

namespace {
constexpr char kDigitPairs[] =
    "00010203040506070809101112131415161718192021222324252627282930313233343536"
    "373839"
    "40414243444546474849505152535455565758596061626364656667686970717273747576"
    "777879"
    "8081828384858687888990919293949596979899";

size_t decimal_length(uint64 v) noexcept {
  size_t n = 1;
  for (;;) {
    if (v < 10) return n;
    if (v < 100) return n + 1;
    if (v < 1000) return n + 2;
    if (v < 10000) return n + 3;
    v /= 10000;
    n += 4;
  }
}

size_t format_decimal(char* buf, uint64 v) noexcept {
  size_t n = decimal_length(v);
  char* p = buf + n;
  while (v >= 100) {
    size_t i = size_t(v % 100) * 2;
    v /= 100;
    p -= 2;
    p[0] = kDigitPairs[i];
    p[1] = kDigitPairs[i + 1];
  }
  if (v >= 10) {
    p -= 2;
    p[0] = kDigitPairs[v * 2];
    p[1] = kDigitPairs[v * 2 + 1];
  } else {
    *--p = char('0' + v);
  }
  return n;
}
}  // namespace

size_t format_uint(char* buf, uint64 value, uint32 base, bool upper) noexcept {
  YASL_ASSERT(base >= 2 && base <= 36);
  if (base == 10) {
    return format_decimal(buf, value);
  }
  const char* table = upper ? kUpper : kLower;
  char tmp[kMaxIntChars];
  size_t n = 0;
  do {
    tmp[n++] = table[value % base];
    value /= base;
  } while (value);
  for (size_t i = 0; i < n; ++i) {
    buf[i] = tmp[n - 1 - i];
  }
  return n;
}

size_t format_int(char* buf, int64 value, uint32 base, bool upper) noexcept {
  if (value < 0) {
    buf[0] = '-';
    return 1 + format_uint(buf + 1, uint64(0) - uint64(value), base, upper);
  }
  return format_uint(buf, uint64(value), base, upper);
}

size_t format_double(char* buf, double value, FloatStyle style,
                     int precision) noexcept {
  precision = yasl::clamp(precision, 0, kMaxPrecision);
  uint64 bits = bit_cast<uint64>(value);
  bool neg = bits >> 63;
  int be = int((bits >> 52) & 0x7FF);
  uint64 mant = bits & ((uint64(1) << 52) - 1);
  if (be == 0x7FF || (be == 0 && mant == 0)) {
    return format_special(buf, neg, be == 0x7FF && mant != 0, be == 0, style,
                          precision);
  }
  Decomposed v = decompose_double(bits);
  return format_finite(buf, neg, v, kDouble, v, style, precision);
}

size_t format_float(char* buf, float value, FloatStyle style,
                    int precision) noexcept {
  precision = yasl::clamp(precision, 0, kMaxPrecision);
  uint32 bits = bit_cast<uint32>(value);
  bool neg = bits >> 31;
  int be = int((bits >> 23) & 0xFF);
  uint32 mant = bits & ((uint32(1) << 23) - 1);
  if (be == 0xFF || (be == 0 && mant == 0)) {
    return format_special(buf, neg, be == 0xFF && mant != 0, be == 0, style,
                          precision);
  }
  Decomposed v = be == 0 ? Decomposed{mant, -149}
                         : Decomposed{mant | (uint32(1) << 23), be - 150};
  // float -> double is exact, so fixed/scientific reuse the double path.
  Decomposed wide =
      decompose_double(bit_cast<uint64>(double(value < 0 ? -value : value)));
  return format_finite(buf, neg, v, kFloat, wide, style, precision);
}

bool parse_uint64(StringView text, uint64& magnitude, bool& negative,
                  uint32 base) noexcept {
  YASL_ASSERT(base >= 2 && base <= 36);
  const char* s = text.data();
  size_t n = text.size();
  size_t i = 0;
  negative = false;
  if (i < n && (s[i] == '+' || s[i] == '-')) {
    negative = s[i] == '-';
    ++i;
  }
  if (i == n) {
    return false;
  }
  uint64 v = 0;
  for (; i < n; ++i) {
    char c = s[i];
    uint32 d;
    if (c >= '0' && c <= '9') {
      d = uint32(c - '0');
    } else if (c >= 'a' && c <= 'z') {
      d = uint32(c - 'a' + 10);
    } else if (c >= 'A' && c <= 'Z') {
      d = uint32(c - 'A' + 10);
    } else {
      return false;
    }
    if (d >= base) {
      return false;
    }
    Optional<uint64> next = checked_mul(v, uint64(base));
    if (next) {
      next = checked_add(*next, uint64(d));
    }
    if (!next) {
      return false;
    }
    v = *next;
  }
  magnitude = v;
  return true;
}

bool parse_double(StringView text, double& out) noexcept {
  Decimal d;
  if (!scan_decimal(text, d)) {
    return false;
  }
  uint64 sign = uint64(d.neg) << 63;
  if (d.nan) {
    out = bit_cast<double>(uint64(0x7FF8000000000000ull));
    return true;
  }
  if (d.inf) {
    out = bit_cast<double>(sign | 0x7FF0000000000000ull);
    return true;
  }
  // Clinger's fast path: both operands exact, so one IEEE operation rounds
  // correctly.
  if (d.n <= 15 && d.exp10 >= -22 && d.exp10 <= 22) {
    uint64 m = 0;
    for (int i = 0; i < d.n; ++i) {
      m = m * 10 + uint64(d.digits[i] - '0');
    }
    double v = double(m);
    v = d.exp10 >= 0 ? v * kExactPow10[d.exp10] : v / kExactPow10[-d.exp10];
    out = d.neg ? -v : v;
    return true;
  }
  out = bit_cast<double>(sign | decimal_to_bits(d, {53, -1022, 1023, 1023}));
  return true;
}

bool parse_float(StringView text, float& out) noexcept {
  Decimal d;
  if (!scan_decimal(text, d)) {
    return false;
  }
  uint32 sign = uint32(d.neg) << 31;
  if (d.nan) {
    out = bit_cast<float>(uint32(0x7FC00000u));
    return true;
  }
  if (d.inf) {
    out = bit_cast<float>(sign | 0x7F800000u);
    return true;
  }
  out =
      bit_cast<float>(sign | uint32(decimal_to_bits(d, {24, -126, 127, 127})));
  return true;
}

ParseErrorKind classify_parse_error(StringView text, bool floating,
                                    bool is_unsigned) noexcept {
  if (text.empty()) {
    return ParseErrorKind::Empty;
  }
  size_t i = 0;
  if (text[0] == '+' || text[0] == '-') {
    if (text[0] == '-' && is_unsigned) {
      return ParseErrorKind::InvalidDigit;
    }
    i = 1;
  }
  auto digits = [&] {
    size_t start = i;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
      ++i;
    }
    return i - start;
  };
  // Well-formed text that was still rejected does not fit.
  bool ok = digits() > 0;
  if (ok && floating) {
    if (i < text.size() && text[i] == '.') {
      ++i;
      digits();
    }
    if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
      ++i;
      if (i < text.size() && (text[i] == '+' || text[i] == '-')) {
        ++i;
      }
      ok = digits() > 0;
    }
  }
  return ok && i == text.size() ? ParseErrorKind::Overflow
                                : ParseErrorKind::InvalidDigit;
}
}  // namespace yasl::num
