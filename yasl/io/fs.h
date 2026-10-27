#pragma once

#include <yasl/cont/seq/vector.h>
#include <yasl/vocab/optional.h>
#include <yasl/vocab/result.h>

#include "file.h"

namespace yasl::io {
// A file system path ('/' separated) kept in a String. The accessors return
// views into it.
//
//     Path p = Path("/var/log") / "app.log";
//     p.file_name();   // "app.log"
//     p.extension();   // "log"
//     p.parent();      // "/var/log"
class Path {
 public:
  Path() = default;

  Path(StringView s, mem::Resource* res = mem::default_resource())
      : s_(s, res) {}

  Path(const char* s) : s_(s) {}

  StringView view() const noexcept { return s_.view(); }

  const char* c_str() const noexcept { return s_.c_str(); }

  bool empty() const noexcept { return s_.empty(); }

  bool is_absolute() const noexcept { return !s_.empty() && s_[0] == '/'; }

  // Last component ("" for "/" and for paths ending in '/').
  StringView file_name() const noexcept {
    StringView v = s_.view();
    size_t i = v.rfind('/');
    return i == StringView::npos ? v : v.drop_front(i + 1);
  }

  // file_name() without its last extension ("a.tar.gz" -> "a.tar"; ".bashrc"
  // stays).
  StringView stem() const noexcept {
    StringView n = file_name();
    size_t dot = n.rfind('.');
    return (dot == StringView::npos || dot == 0) ? n : n.first(dot);
  }

  // Text after the last '.' of the file name, none if there is none ("a." gives
  // "").
  Optional<StringView> extension() const noexcept {
    StringView n = file_name();
    size_t dot = n.rfind('.');
    if (dot == StringView::npos || dot == 0) {
      return none;
    }
    return n.drop_front(dot + 1);
  }

  // Everything before the last component, none for "" and "/".
  Optional<StringView> parent() const noexcept {
    StringView v = s_.view();
    while (v.size() > 1 && v.ends_with('/')) {
      v = v.drop_back(1);
    }
    if (v.empty() || v == "/") {
      return none;
    }
    size_t i = v.rfind('/');
    if (i == StringView::npos) {
      return StringView();
    }
    return i == 0 ? v.first(1) : v.first(i);
  }

  // Appends a component; an absolute `rel` replaces the whole path.
  Path join(StringView rel) const {
    if (rel.starts_with('/') || s_.empty()) {
      return Path(rel, s_.resource());
    }
    Path out(s_.view(), s_.resource());
    if (!out.s_.ends_with("/")) {
      out.s_.push_back('/');
    }
    out.s_.append(rel);
    return out;
  }

  friend Path operator/(const Path& a, StringView b) { return a.join(b); }

  friend bool operator==(const Path& a, const Path& b) noexcept {
    return a.s_ == b.s_;
  }

 private:
  String s_;
};

inline Output& operator<<(Output& out, const Path& p) {
  return out << p.view();
}
}  // namespace yasl::io

// Rust std::fs / Go os + filepath. Every failure is an io::Error in a Result.
namespace yasl::io::fs {
struct DirEntry {
  String name;
  bool is_dir;
};

bool exists(const Path& p) noexcept;
bool is_dir(const Path& p) noexcept;
bool is_file(const Path& p) noexcept;

Result<uint64, Error> file_size(const Path& p);

// Removes a file or an empty directory.
Result<void, Error> remove(const Path& p);

// One directory level; fails if it exists already.
Result<void, Error> create_dir(const Path& p);

// Like mkdir -p: succeeds if the directory exists.
Result<void, Error> create_dir_all(const Path& p);

Result<void, Error> rename(const Path& from, const Path& to);

// Entries of a directory (without "." and ".."), in no particular order.
Result<Vector<DirEntry>, Error> read_dir(const Path& p);
}  // namespace yasl::io::fs
