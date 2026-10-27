#include "output.h"

#include <yasl/core/exception.h>
#include <yasl/mem/resource.h>

namespace yasl::io {
void Output::put(char c, size_t n) {
  char chunk[64];
  yasl::mem_set(chunk, c, sizeof(chunk));
  while (n > 0) {
    size_t step = yasl::min(n, sizeof(chunk));
    write(chunk, step);
    n -= step;
  }
}

void Output::write_padded(StringView s, const FormatSpec& spec,
                          char default_align) {
  if (s.size() >= spec.width) {
    write(s);
    return;
  }
  size_t pad = spec.width - s.size();
  char align = spec.align ? spec.align : default_align;
  size_t left = align == '>' ? pad : (align == '^' ? pad / 2 : 0);
  put(spec.fill, left);
  write(s);
  put(spec.fill, pad - left);
}

namespace {
// sign/prefix + digits, honoring the '0' flag (zeros go between them).
void write_number(Output& out, StringView prefix, StringView digits,
                  const FormatSpec& spec) {
  size_t len = prefix.size() + digits.size();
  if (spec.zero && !spec.align && spec.width > len) {
    out.write(prefix);
    out.put('0', spec.width - len);
    out.write(digits);
    return;
  }
  char buf[num::kMaxFloatChars + 8];
  yasl::mem_copy(buf, prefix.data(), prefix.size());
  yasl::mem_copy(buf + prefix.size(), digits.data(), digits.size());
  out.write_padded(StringView(buf, len), spec, '>');
}
}  // namespace

void format_integer(Output& out, uint64 magnitude, bool negative,
                    const FormatSpec& spec) {
  if (spec.type == 'c') {
    char c = char(magnitude);
    format_string(out, StringView(&c, 1), spec);
    return;
  }
  uint32 base = 10;
  const char* alt = "";
  switch (spec.type) {
    case 'x':
    case 'X':
      base = 16;
      alt = spec.type == 'x' ? "0x" : "0X";
      break;
    case 'o':
      base = 8;
      alt = "0";
      break;
    case 'b':
      base = 2;
      alt = "0b";
      break;
    default:
      break;
  }
  char prefix[4];
  size_t pn = 0;
  if (negative) {
    prefix[pn++] = '-';
  } else if (spec.plus) {
    prefix[pn++] = '+';
  }
  if (spec.alt) {
    for (const char* a = alt; *a; ++a) {
      prefix[pn++] = *a;
    }
  }
  char digits[num::kMaxIntChars];
  size_t dn = num::format_uint(digits, magnitude, base, spec.type == 'X');
  write_number(out, StringView(prefix, pn), StringView(digits, dn), spec);
}

void format_floating(Output& out, double value, bool single,
                     const FormatSpec& spec) {
  num::FloatStyle style = num::FloatStyle::Shortest;
  int precision = spec.precision;
  if (spec.type == 'f' || spec.type == 'F') {
    style = num::FloatStyle::Fixed;
  } else if (spec.type == 'e' || spec.type == 'E') {
    style = num::FloatStyle::Scientific;
  } else if (precision >= 0) {
    style = num::FloatStyle::Fixed;
  }
  if (style != num::FloatStyle::Shortest && precision < 0) {
    precision = 6;
  }
  char buf[num::kMaxFloatChars];
  size_t n = single ? num::format_float(buf, float(value), style, precision)
                    : num::format_double(buf, value, style, precision);
  StringView text(buf, n);
  StringView sign;
  if (text.starts_with('-')) {
    sign = "-";
    text = text.drop_front(1);
  } else if (spec.plus) {
    sign = "+";
  }
  write_number(out, sign, text, spec);
}

void format_string(Output& out, StringView s, const FormatSpec& spec) {
  if (spec.precision >= 0) {
    s = s.first(size_t(spec.precision));
  }
  out.write_padded(s, spec, '<');
}

void format_pointer(Output& out, const void* p, const FormatSpec& spec) {
  FormatSpec hex = spec;
  hex.type = 'x';
  hex.alt = true;
  format_integer(out, uint64(reinterpret_cast<uintptr_t>(p)), false, hex);
}

namespace detail {
ScratchOutput::~ScratchOutput() {
  if (heap_) {
    mem::system_resource()->deallocate(heap_, cap_);
  }
}

void ScratchOutput::overflow(const char* data, size_t n) {
  char* begin = heap_ ? heap_ : buf_;
  size_t used = size_t(pos_ - begin);
  size_t cap = yasl::max(used + n, (heap_ ? cap_ : sizeof(buf_)) * 2);
  char* fresh = static_cast<char*>(mem::system_resource()->allocate(cap, 1));
  yasl::mem_copy(fresh, begin, used);
  yasl::mem_copy(fresh + used, data, n);
  if (heap_) {
    mem::system_resource()->deallocate(heap_, cap_);
  }
  heap_ = fresh;
  cap_ = cap;
  pos_ = fresh + used + n;
  end_ = fresh + cap;
}

namespace {
[[noreturn]] void throw_format_error(FormatErr e) {
  switch (e) {
    case FormatErr::UnmatchedBrace:
      throw FormatError("yasl::format: unmatched '}'");
    case FormatErr::MissingPrecision:
      throw FormatError("yasl::format: missing precision");
    case FormatErr::NumberTooLarge:
      throw FormatError("yasl::format: number too large");
    case FormatErr::NotEnoughArgs:
      throw FormatError("yasl::format: not enough arguments");
    default:
      throw FormatError("yasl::format: bad replacement field");
  }
}

struct WriteVisitor {
  Output& out;
  const FormatArg* args;
  size_t count;

  void literal(const char* p, size_t n) { out.write(p, n); }

  FormatErr field(size_t index, const FormatSpec& spec) {
    if (index >= count) {
      return FormatErr::NotEnoughArgs;
    }
    args[index].fn(out, args[index].value, spec);
    return FormatErr::None;
  }
};
}  // namespace

void vformat(Output& out, StringView fmt, const FormatArg* args, size_t count) {
  WriteVisitor vis{out, args, count};
  if (FormatErr e = walk_format(fmt.data(), fmt.size(), vis);
      e != FormatErr::None) {
    throw_format_error(e);
  }
}
}  // namespace detail
}  // namespace yasl::io
