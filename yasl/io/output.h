#pragma once

#include <yasl/core/traits.h>
#include <yasl/core/utility.h>
#include <yasl/str/num.h>
#include <yasl/str/view.h>

namespace yasl::io {
class Output;

// Parsed "{:...}" replacement field:
// [[fill]align][+][#][0][width][.precision][type]
//   align: '<' left, '>' right, '^' center
//   type:  integers d x X o b c; floats f e (default: shortest round-trip);
//   strings s
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
void format_integer(Output& out, uint64 magnitude, bool negative,
                    const FormatSpec& spec);
void format_floating(Output& out, double value, bool single,
                     const FormatSpec& spec);
void format_string(Output& out, StringView s, const FormatSpec& spec);
void format_pointer(Output& out, const void* p, const FormatSpec& spec);

namespace detail {
// What can be wrong with a format string. The parser below is constexpr:
// vformat() turns an error into FormatError, FormatString reports it as a
// compile error.
enum class FormatErr : uint8 {
  None,
  UnmatchedBrace,
  BadField,
  MissingPrecision,
  NumberTooLarge,
  NotEnoughArgs,
  BadType,
};

constexpr bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }

constexpr bool is_align(char c) noexcept {
  return c == '<' || c == '>' || c == '^';
}

constexpr FormatErr parse_number(const char* p, size_t n, size_t& i,
                                 uint32& v) noexcept {
  v = 0;
  while (i < n && is_digit(p[i])) {
    v = v * 10 + uint32(p[i] - '0');
    if (v > 100000) {
      return FormatErr::NumberTooLarge;
    }
    ++i;
  }
  return FormatErr::None;
}

// [[fill]align][+][#][0][width][.precision][type]; i points after the ':'.
constexpr FormatErr parse_spec(const char* p, size_t n, size_t& i,
                               FormatSpec& spec) noexcept {
  if (i + 1 < n && is_align(p[i + 1]) && p[i] != '}') {
    spec.fill = p[i];
    spec.align = p[i + 1];
    i += 2;
  } else if (i < n && is_align(p[i])) {
    spec.align = p[i++];
  }
  if (i < n && p[i] == '+') {
    spec.plus = true;
    ++i;
  }
  if (i < n && p[i] == '#') {
    spec.alt = true;
    ++i;
  }
  if (i < n && p[i] == '0') {
    spec.zero = true;
    ++i;
  }
  if (FormatErr e = parse_number(p, n, i, spec.width); e != FormatErr::None) {
    return e;
  }
  if (i < n && p[i] == '.') {
    ++i;
    if (i == n || !is_digit(p[i])) {
      return FormatErr::MissingPrecision;
    }
    uint32 prec = 0;
    if (FormatErr e = parse_number(p, n, i, prec); e != FormatErr::None) {
      return e;
    }
    spec.precision = int32(prec);
  }
  if (i < n && p[i] != '}') {
    spec.type = p[i++];
  }
  return FormatErr::None;
}

// Walks a format string. The visitor gets literal(const char*, size_t) for text
// (escaped braces are already collapsed) and field(index, spec) -> FormatErr.
template <class Visitor>
constexpr FormatErr walk_format(const char* p, size_t n, Visitor& vis) {
  size_t next = 0;
  size_t lit = 0;
  size_t i = 0;
  while (i < n) {
    char c = p[i];
    if (c != '{' && c != '}') {
      ++i;
      continue;
    }
    if (i + 1 < n && p[i + 1] == c) {
      vis.literal(p + lit, i + 1 - lit);
      i += 2;
      lit = i;
      continue;
    }
    if (c == '}') {
      return FormatErr::UnmatchedBrace;
    }
    vis.literal(p + lit, i - lit);
    ++i;
    size_t index = next;
    if (i < n && is_digit(p[i])) {
      uint32 idx = 0;
      if (FormatErr e = parse_number(p, n, i, idx); e != FormatErr::None) {
        return e;
      }
      index = idx;
    } else {
      ++next;
    }
    FormatSpec spec;
    if (i < n && p[i] == ':') {
      ++i;
      if (FormatErr e = parse_spec(p, n, i, spec); e != FormatErr::None) {
        return e;
      }
    }
    if (i >= n || p[i] != '}') {
      return FormatErr::BadField;
    }
    ++i;
    lit = i;
    if (FormatErr e = vis.field(index, spec); e != FormatErr::None) {
      return e;
    }
  }
  vis.literal(p + lit, n - lit);
  return FormatErr::None;
}

// How an argument can be formatted, to check "{:type}" at compile time.
enum class ArgKind : uint8 {
  Integer,  // d x X o b c
  Char,     // integer types plus s
  Float,    // f F e E
  String,   // s
  Any,      // custom formatters, pointers, bool: the type letter is theirs to
            // interpret
};

constexpr FormatErr check_type(ArgKind kind, char type) noexcept {
  if (type == 0) {
    return FormatErr::None;
  }
  bool ok = true;
  switch (kind) {
    case ArgKind::Integer:
      ok = type == 'd' || type == 'x' || type == 'X' || type == 'o' ||
           type == 'b' || type == 'c';
      break;
    case ArgKind::Char:
      ok = type == 'd' || type == 'x' || type == 'X' || type == 'o' ||
           type == 'b' || type == 'c' || type == 's';
      break;
    case ArgKind::Float:
      ok = type == 'f' || type == 'F' || type == 'e' || type == 'E';
      break;
    case ArgKind::String:
      ok = type == 's';
      break;
    case ArgKind::Any:
      break;
  }
  return ok ? FormatErr::None : FormatErr::BadType;
}

template <class T>
constexpr ArgKind arg_kind() noexcept {
  if constexpr (requires(Output& o, const T& x, const FormatSpec& s) {
                  format_value(o, x, s);
                }) {
    return ArgKind::Any;
  } else if constexpr (SameAs<T, bool>) {
    return ArgKind::Any;
  } else if constexpr (SameAs<T, char>) {
    return ArgKind::Char;
  } else if constexpr (Integral<T>) {
    return ArgKind::Integer;
  } else if constexpr (FloatingPoint<T>) {
    return ArgKind::Float;
  } else if constexpr (ConvertibleTo<const T&, StringView>) {
    return ArgKind::String;
  } else {
    return ArgKind::Any;
  }
}

struct CheckVisitor {
  const ArgKind* kinds;
  size_t count;

  constexpr void literal(const char*, size_t) const noexcept {}

  constexpr FormatErr field(size_t index,
                            const FormatSpec& spec) const noexcept {
    if (index >= count) {
      return FormatErr::NotEnoughArgs;
    }
    return check_type(kinds[index], spec.type);
  }
};

// Called from consteval code only: the failed call is the compile error, its
// name the message.
void format_error_unmatched_brace();
void format_error_bad_replacement_field();
void format_error_missing_precision();
void format_error_number_too_large();
void format_error_not_enough_arguments();
void format_error_type_not_valid_for_argument();

constexpr void report_format_error(FormatErr e) {
  switch (e) {
    case FormatErr::None:
      break;
    case FormatErr::UnmatchedBrace:
      format_error_unmatched_brace();
      break;
    case FormatErr::BadField:
      format_error_bad_replacement_field();
      break;
    case FormatErr::MissingPrecision:
      format_error_missing_precision();
      break;
    case FormatErr::NumberTooLarge:
      format_error_number_too_large();
      break;
    case FormatErr::NotEnoughArgs:
      format_error_not_enough_arguments();
      break;
    case FormatErr::BadType:
      format_error_type_not_valid_for_argument();
      break;
  }
}
}  // namespace detail

// A format string checked against the argument types while compiling:
// braces, argument count, {N} indices and "{:type}" letters. A string literal
// converts implicitly; for strings only known at run time use
// io::runtime_format().
//
//     io::println("{:>5} {:.2f}", name, ratio);
//     io::println(io::runtime_format(user_text), x);   // checked when it runs
template <class... Args>
struct FormatString {
  StringView str;

  template <size_t N>
  consteval FormatString(const char (&s)[N]) : str(s, N - 1) {
    check();
  }

  consteval FormatString(StringView s) : str(s) { check(); }

  // Unchecked: vformat() throws FormatError for malformed strings.
  constexpr FormatString(struct RuntimeFormat r) noexcept;

 private:
  consteval void check() const {
    detail::ArgKind kinds[sizeof...(Args) + 1] = {detail::arg_kind<Args>()...,
                                                  detail::ArgKind::Any};
    detail::CheckVisitor vis{kinds, sizeof...(Args)};
    detail::report_format_error(
        detail::walk_format(str.data(), str.size(), vis));
  }
};

struct RuntimeFormat {
  StringView str;
};

// Marks a format string that is only known at run time (no compile-time check).
constexpr RuntimeFormat runtime_format(StringView s) noexcept { return {s}; }

template <class... Args>
constexpr FormatString<Args...>::FormatString(RuntimeFormat r) noexcept
    : str(r.str) {}

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
}  // namespace detail

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

  void write(StringView s) { write(s.data(), s.size()); }

  void put(char c) {
    if (pos_ != end_) {
      *pos_++ = c;
    } else {
      overflow(&c, 1);
    }
  }

  void put(char c, size_t n);

  void flush() { do_flush(); }

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
    size_t n = SameAs<T, float> ? num::format_float(buf, float(v))
                                : num::format_double(buf, double(v));
    write(buf, n);
    return *this;
  }

  Output& operator<<(const void* p) {
    format_pointer(*this, p, {});
    return *this;
  }

  Output& operator<<(Output& (*manip)(Output&)) { return manip(*this); }

  // Python/std::format-like formatting: "{}", "{0}", "{:>10.3f}", "{{" and
  // "}}". A literal format string is checked at compile time;
  // io::runtime_format() strings throw FormatError.
  template <class... Args>
  Output& format(FormatString<TypeIdentity<Args>...> fmt, const Args&... args) {
    if constexpr (sizeof...(Args) == 0) {
      detail::vformat(*this, fmt.str, nullptr, 0);
    } else {
      const detail::FormatArg list[] = {detail::make_arg(args)...};
      detail::vformat(*this, fmt.str, list, sizeof...(Args));
    }
    return *this;
  }

  // Writes `s` padded to spec.width according to spec.fill/align.
  void write_padded(StringView s, const FormatSpec& spec,
                    char default_align = '<');

 protected:
  // [pos_, end_) cannot take n more bytes: the derived class must consume
  // `data`.
  virtual void overflow(const char* data, size_t n) = 0;

  virtual void do_flush() {}

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
class NullOutput final : public Output {
 public:
  size_t written() const noexcept { return written_; }

 protected:
  void overflow(const char*, size_t n) override { written_ += n; }

 private:
  size_t written_ = 0;
};

// Writes into a fixed caller-provided buffer, silently truncating.
class ArrayOutput final : public Output {
 public:
  ArrayOutput(char* buf, size_t size) noexcept : begin_(buf) {
    pos_ = buf;
    end_ = buf + size;
  }

  StringView view() const noexcept { return {begin_, size_t(pos_ - begin_)}; }

  bool truncated() const noexcept { return truncated_; }

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
class ScratchOutput final : public Output {
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
  if constexpr (requires(Output& o, const T& x, const FormatSpec& s) {
                  format_value(o, x, s);
                }) {
    format_value(out, v, spec);  // user hook found by ADL
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
      format_integer(out, v < 0 ? uint64(0) - uint64(v) : uint64(v), v < 0,
                     spec);
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
    static_assert(kAlwaysFalse<T>,
                  "type is not formattable: define format_value() or "
                  "operator<<(io::Output&, const T&)");
  }
}
}  // namespace detail
}  // namespace yasl::io
