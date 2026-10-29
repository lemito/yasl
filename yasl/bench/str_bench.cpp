#include <absl/hash/hash.h>
#include <absl/strings/str_cat.h>
#include <absl/strings/str_format.h>
#include <absl/strings/str_split.h>
#include <stdio.h>
#include <stdlib.h>
#include <yasl/bench/harness.h>
#include <yasl/core/hash.h>
#include <yasl/io/fmt.h>
#include <yasl/str/builder.h>
#include <yasl/str/num.h>
#include <yasl/str/str.h>
#include <yasl/str/view.h>

#include <charconv>
#include <format>
#include <sstream>
#include <string>
#include <string_view>

namespace bench {
namespace {

using namespace yasl;

constexpr size_t kN = 1'000'000;
constexpr size_t kLines = 100'000;

std::string make_text(size_t n, char fill) {
  std::string s(n, fill);
  return s;
}

void construct_and_copy(Bencher& b) {
  constexpr char kShort[] = "hello, world";
  std::string_view short_text(kShort, sizeof(kShort) - 1);
  std::string long_text = make_text(200, 'x');

  std::string_view g = "construct from 12 bytes";
  b.run(g, "yasl::String", kN, [&] {
    for (size_t i = 0; i < kN; ++i) {
      String s(StringView(short_text.data(), short_text.size()));
      keep(s);
    }
  });
  b.run(g, "std::string", kN, [&] {
    for (size_t i = 0; i < kN; ++i) {
      std::string s(short_text);
      keep(s);
    }
  });

  g = "construct from 200 bytes";
  b.run(g, "yasl::String", kN, [&] {
    for (size_t i = 0; i < kN; ++i) {
      String s(StringView(long_text.data(), long_text.size()));
      keep(s);
    }
  });
  b.run(g, "std::string", kN, [&] {
    for (size_t i = 0; i < kN; ++i) {
      std::string s(long_text);
      keep(s);
    }
  });

  String ys(StringView(long_text.data(), long_text.size()));
  g = "copy-construct a 200-byte string (yasl shares buffer)";
  b.run(g, "yasl::String", kN, [&] {
    for (size_t i = 0; i < kN; ++i) {
      String t = ys;
      keep(t);
    }
  });
  b.run(g, "std::string", kN, [&] {
    for (size_t i = 0; i < kN; ++i) {
      std::string t = long_text;
      keep(t);
    }
  });

  g = "append 1M chars one by one";
  b.run(g, "yasl::String", kN, [&] {
    String s;
    for (size_t i = 0; i < kN; ++i) {
      s.push_back('a');
    }
    keep(s.size());
  });
  b.run(g, "std::string", kN, [&] {
    std::string s;
    for (size_t i = 0; i < kN; ++i) {
      s.push_back('a');
    }
    keep(s.size());
  });
}

void search_and_compare(Bencher& b) {
  std::string haystack = make_text(4096, 'a');
  haystack.replace(haystack.size() - 10, 10, "needle-xyz");
  constexpr size_t kSearches = 100'000;
  std::string_view g = "find \"needle-xyz\" in 4 KB";
  String yhay(StringView(haystack.data(), haystack.size()));
  b.run(g, "yasl::String::find", kSearches, [&] {
    size_t hits = 0;
    for (size_t i = 0; i < kSearches; ++i) {
      keep(yhay);
      hits += yhay.find("needle-xyz") != String::npos;
    }
    keep(hits);
  });
  b.run(g, "yasl::StringView::find", kSearches, [&] {
    StringView v(haystack.data(), haystack.size());
    size_t hits = 0;
    for (size_t i = 0; i < kSearches; ++i) {
      keep(v);
      hits += v.find("needle-xyz") != StringView::npos;
    }
    keep(hits);
  });
  b.run(g, "std::string::find", kSearches, [&] {
    size_t hits = 0;
    for (size_t i = 0; i < kSearches; ++i) {
      keep(haystack);
      hits += haystack.find("needle-xyz") != std::string::npos;
    }
    keep(hits);
  });
  b.run(g, "std::string_view::find", kSearches, [&] {
    std::string_view v(haystack);
    size_t hits = 0;
    for (size_t i = 0; i < kSearches; ++i) {
      keep(v);
      hits += v.find("needle-xyz") != std::string_view::npos;
    }
    keep(hits);
  });

  std::string same = make_text(200, 'q');
  String ya(StringView(same.data(), same.size()));
  String yb(StringView(same.data(), same.size()));
  std::string sa = same;
  std::string sb = same;
  g = "equality of two equal 200-byte strings";
  b.run(g, "yasl::String ==", kN, [&] {
    size_t eq = 0;
    for (size_t i = 0; i < kN; ++i) {
      keep(ya);
      eq += ya == yb;
    }
    keep(eq);
  });
  b.run(g, "std::string ==", kN, [&] {
    size_t eq = 0;
    for (size_t i = 0; i < kN; ++i) {
      keep(sa);
      eq += sa == sb;
    }
    keep(eq);
  });

  std::string_view sv(same);
  g = "hash 200 bytes";
  b.run(g, "yasl::hash_bytes", kN, [&] {
    uint64 h = 0;
    for (size_t i = 0; i < kN; ++i) {
      keep(sv);
      h += hash_bytes(sv.data(), sv.size(), i);
    }
    keep(h);
  });
  b.run(g, "std::hash<string_view>", kN, [&] {
    size_t h = 0;
    std::hash<std::string_view> hasher;
    for (size_t i = 0; i < kN; ++i) {
      keep(sv);
      h += hasher(sv);
    }
    keep(h);
  });
  b.run(g, "absl::HashOf", kN, [&] {
    size_t h = 0;
    for (size_t i = 0; i < kN; ++i) {
      keep(sv);
      h += absl::HashOf(sv);
    }
    keep(h);
  });
}

void integers_to_text(Bencher& b) {
  std::string_view g = "integer to decimal text";
  b.run(g, "yasl num::format_uint (no heap)", kN, [&] {
    char buf[32];
    size_t total = 0;
    for (size_t i = 0; i < kN; ++i) {
      total += num::format_uint(buf, i);
    }
    keep(total);
  });
  b.run(g, "std::to_chars (no heap)", kN, [&] {
    char buf[32];
    size_t total = 0;
    for (size_t i = 0; i < kN; ++i) {
      total += size_t(std::to_chars(buf, buf + sizeof(buf), i).ptr - buf);
    }
    keep(total);
  });
  b.run(g, "yasl StringBuilder << i", kN, [&] {
    size_t total = 0;
    for (size_t i = 0; i < kN; ++i) {
      StringBuilder sb;
      sb << i;
      total += sb.size();
    }
    keep(total);
  });
  b.run(g, "std::to_string", kN, [&] {
    size_t total = 0;
    for (size_t i = 0; i < kN; ++i) {
      total += std::to_string(i).size();
    }
    keep(total);
  });
  b.run(g, "absl::StrCat", kN, [&] {
    size_t total = 0;
    for (size_t i = 0; i < kN; ++i) {
      total += absl::StrCat(i).size();
    }
    keep(total);
  });
  b.run(g, "snprintf(\"%zu\")", kN, [&] {
    char buf[32];
    size_t total = 0;
    for (size_t i = 0; i < kN; ++i) {
      total += size_t(snprintf(buf, sizeof(buf), "%zu", i));
    }
    keep(total);
  });
}

void parse_numbers(Bencher& b) {
  std::vector<std::string> ints;
  ints.reserve(kN);
  for (size_t i = 0; i < kN; ++i) {
    ints.push_back(std::to_string(1'000'000 + i * 7));
  }
  std::string_view g = "parse 1M u64 (7 digits)";
  b.run(g, "yasl StringView::parse<uint64>", kN, [&] {
    uint64 s = 0;
    for (const auto& t : ints) {
      s += StringView(t.data(), t.size()).parse<uint64>().value_or(0);
    }
    keep(s);
  });
  b.run(g, "std::from_chars", kN, [&] {
    uint64 s = 0;
    for (const auto& t : ints) {
      uint64 v = 0;
      std::from_chars(t.data(), t.data() + t.size(), v);
      s += v;
    }
    keep(s);
  });
  b.run(g, "strtoull", kN, [&] {
    uint64 s = 0;
    for (const auto& t : ints) {
      s += strtoull(t.c_str(), nullptr, 10);
    }
    keep(s);
  });

  std::vector<std::string> doubles;
  doubles.reserve(kLines);
  for (size_t i = 0; i < kLines; ++i) {
    doubles.push_back(std::to_string(double(i) / 7.0));
  }
  g = "parse 100k doubles (e.g. 123.456789)";
  b.run(g, "yasl StringView::parse<double>", kLines, [&] {
    double s = 0;
    for (const auto& t : doubles) {
      s += StringView(t.data(), t.size()).parse<double>().value_or(0);
    }
    keep(s);
  });
  b.run(g, "std::from_chars(double)", kLines, [&] {
    double s = 0;
    for (const auto& t : doubles) {
      double v = 0;
      std::from_chars(t.data(), t.data() + t.size(), v);
      s += v;
    }
    keep(s);
  });
  b.run(g, "strtod", kLines, [&] {
    double s = 0;
    for (const auto& t : doubles) {
      s += strtod(t.c_str(), nullptr);
    }
    keep(s);
  });
}

void formatting(Bencher& b) {
  std::string_view g = "format \"{} {:.2f}\" (int, double)";
  b.run(g, "yasl format", kLines, [&] {
    size_t total = 0;
    for (size_t i = 0; i < kLines; ++i) {
      total += format("{} {:.2f}", i, double(i) / 7).size();
    }
    keep(total);
  });
  b.run(g, "std::format", kLines, [&] {
    size_t total = 0;
    for (size_t i = 0; i < kLines; ++i) {
      total += std::format("{} {:.2f}", i, double(i) / 7).size();
    }
    keep(total);
  });
  b.run(g, "absl::StrFormat", kLines, [&] {
    size_t total = 0;
    for (size_t i = 0; i < kLines; ++i) {
      total += absl::StrFormat("%d %.2f", int(i), double(i) / 7).size();
    }
    keep(total);
  });
  b.run(g, "snprintf(\"%zu %.2f\")", kLines, [&] {
    char buf[64];
    size_t total = 0;
    for (size_t i = 0; i < kLines; ++i) {
      total += size_t(snprintf(buf, sizeof(buf), "%zu %.2f", i, double(i) / 7));
    }
    keep(total);
  });
}

void building_and_splitting(Bencher& b) {
  constexpr size_t kPieces = 200000;
  std::string_view g = "build 200k \"id=<n> \" pieces";
  b.run(g, "yasl StringBuilder", kPieces, [&] {
    StringBuilder sb;
    for (size_t i = 0; i < kPieces; ++i) {
      sb << "id=" << i << ' ';
    }
    keep(sb.size());
  });
  b.run(g, "std::string += to_string", kPieces, [&] {
    std::string s;
    for (size_t i = 0; i < kPieces; ++i) {
      s += "id=";
      s += std::to_string(i);
      s += ' ';
    }
    keep(s.size());
  });
  b.run(g, "std::ostringstream", kPieces, [&] {
    std::ostringstream os;
    for (size_t i = 0; i < kPieces; ++i) {
      os << "id=" << i << ' ';
    }
    keep(os.str().size());
  });

  std::string line;
  for (int i = 0; i < 16; ++i) {
    line += "field-" + std::to_string(1000 + i) + (i == 15 ? "" : ",");
  }
  std::string_view v(line);
  g = "split \",\" 16 fields, 100k lines";
  b.run(g, "yasl StringView::split", kLines, [&] {
    size_t total = 0;
    for (size_t i = 0; i < kLines; ++i) {
      keep(line);
      for (StringView part : StringView(line.data(), line.size()).split(',')) {
        total += part.size();
      }
    }
    keep(total);
  });
  b.run(g, "std::string_view find loop", kLines, [&] {
    size_t total = 0;
    for (size_t i = 0; i < kLines; ++i) {
      keep(line);
      std::string_view rest = v;
      while (true) {
        size_t pos = rest.find(',');
        total +=
            (pos == std::string_view::npos ? rest : rest.substr(0, pos)).size();
        if (pos == std::string_view::npos) {
          break;
        }
        rest.remove_prefix(pos + 1);
      }
    }
    keep(total);
  });
  b.run(g, "absl::StrSplit", kLines, [&] {
    size_t total = 0;
    for (size_t i = 0; i < kLines; ++i) {
      keep(line);
      for (std::string_view part : absl::StrSplit(v, ',')) {
        total += part.size();
      }
    }
    keep(total);
  });
}

}  // namespace

void str_benches(Bencher& b) {
  construct_and_copy(b);
  search_and_compare(b);
  integers_to_text(b);
  parse_numbers(b);
  formatting(b);
  building_and_splitting(b);
}

}  // namespace bench
