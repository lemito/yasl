#include "file.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace yasl::io {
Error::Error(const char* op, StringView path, int code) noexcept
    : Exception("yasl::io::Error"), code_(code) {
  ArrayOutput out(message_, sizeof(message_) - 1);
  out << op << " '" << path << "': " << ::strerror(code);
  message_[out.view().size()] = '\0';
}

File File::open(StringView path, OpenMode mode, uint32 permissions) {
  String p(path, mem::system_resource());
  bool r = has(mode, OpenMode::Read);
  bool w = has(mode, OpenMode::Write) || has(mode, OpenMode::Append);
  int flags = O_CLOEXEC | (r && w ? O_RDWR : (w ? O_WRONLY : O_RDONLY));
  if (has(mode, OpenMode::Append)) {
    flags |= O_APPEND;
  }
  if (has(mode, OpenMode::Create)) {
    flags |= O_CREAT;
  }
  if (has(mode, OpenMode::Truncate)) {
    flags |= O_TRUNC;
  }
  if (has(mode, OpenMode::Exclusive)) {
    flags |= O_EXCL;
  }
  int fd;
  do {
    fd = ::open(p.c_str(), flags, mode_t(permissions));
  } while (fd < 0 && errno == EINTR);
  if (fd < 0) {
    throw Error("open", path, errno);
  }
  return File(Fd(fd));
}

size_t File::read(void* buf, size_t n) {
  for (;;) {
    ssize_t r = ::read(fd_.raw(), buf, n);
    if (r >= 0) {
      return size_t(r);
    }
    if (errno != EINTR) {
      throw Error("read", "fd", errno);
    }
  }
}

void File::write_all(const void* data, size_t n) {
  const char* p = static_cast<const char*>(data);
  while (n > 0) {
    ssize_t w = ::write(fd_.raw(), p, n);
    if (w < 0) {
      if (errno == EINTR) {
        continue;
      }
      throw Error("write", "fd", errno);
    }
    p += w;
    n -= size_t(w);
  }
}

uint64 File::size() const {
  struct stat st;
  if (::fstat(fd_.raw(), &st) != 0) {
    throw Error("fstat", "fd", errno);
  }
  return uint64(st.st_size);
}

void File::seek(uint64 offset) {
  if (::lseek(fd_.raw(), off_t(offset), SEEK_SET) < 0) {
    throw Error("lseek", "fd", errno);
  }
}

void File::sync() {
  if (::fsync(fd_.raw()) != 0) {
    throw Error("fsync", "fd", errno);
  }
}

void File::close() {
  if (fd_.is_open()) {
    if (!owned_) {
      fd_.release();
      return;
    }
    auto r = fd_.close();
    if (r.is_err()) {
      throw r.error();
    }
  }
}

void File::close_quietly() noexcept {
  if (!owned_) {
    fd_.release();
  }
  fd_ = Fd();
}

// FileOutput

struct FileOutput::Impl {
  Impl(File f, size_t buffer, mem::Resource* r)
      : file(yasl::move(f)), res(r), cap(buffer) {
    if (cap) {
      buf = static_cast<char*>(res->allocate(cap, 1));
    }
  }

  ~Impl() {
    if (buf) {
      res->deallocate(buf, cap, 1);
    }
  }

  File file;
  mem::Resource* res;
  char* buf = nullptr;
  size_t cap = 0;
};

FileOutput::FileOutput(StringView path, OpenMode mode)
    : FileOutput(File::open(path, mode)) {}

FileOutput::FileOutput(File file, size_t buffer, mem::Resource* res)
    : impl_(make_unique_in<Impl>(res, yasl::move(file), buffer, res)) {
  pos_ = impl_->buf;
  end_ = impl_->buf + impl_->cap;
}

FileOutput::~FileOutput() {
  try {
    drain();
  } catch (const Exception&) {
  }
}

File& FileOutput::file() noexcept { return impl_->file; }

void FileOutput::drain() {
  if (pos_ != impl_->buf) {
    size_t n = size_t(pos_ - impl_->buf);
    pos_ = impl_->buf;
    impl_->file.write_all(impl_->buf, n);
  }
}

void FileOutput::overflow(const char* data, size_t n) {
  drain();
  if (n >= impl_->cap) {
    impl_->file.write_all(data, n);
  } else {
    yasl::mem_copy(pos_, data, n);
    pos_ += n;
  }
}

void FileOutput::do_flush() { drain(); }

// FileInput

struct FileInput::Impl {
  Impl(File f, size_t buffer, mem::Resource* r)
      : file(yasl::move(f)),
        res(r),
        cap(yasl::max(buffer, size_t(1))),
        buf(static_cast<char*>(res->allocate(cap, 1))) {}

  ~Impl() { res->deallocate(buf, cap, 1); }

  File file;
  mem::Resource* res;
  size_t cap;
  char* buf;
};

FileInput::FileInput(StringView path)
    : FileInput(File::open(path, OpenMode::Read)) {}

FileInput::FileInput(File file, size_t buffer, mem::Resource* res)
    : impl_(make_unique_in<Impl>(res, yasl::move(file), buffer, res)) {
  pos_ = end_ = impl_->buf;
}

FileInput::~FileInput() = default;

File& FileInput::file() noexcept { return impl_->file; }

bool FileInput::underflow() {
  size_t n = impl_->file.read(impl_->buf, impl_->cap);
  pos_ = impl_->buf;
  end_ = impl_->buf + n;
  return n > 0;
}

// Standard streams: built in place and never destroyed, so they stay usable
// from other static destructors; out() is flushed by an atexit hook.

namespace {
template <class T, class... Args>
T* immortal(Args&&... args) {
  alignas(T) static unsigned char storage[sizeof(T)];
  return ::new (storage) T(yasl::forward<Args>(args)...);
}

void flush_stdout() {
  try {
    out().flush();
  } catch (const Exception&) {
  }
}
}  // namespace

SharedOutput& out() {
  static SharedOutput* stream = [] {
    SharedOutput* s = immortal<SharedOutput>(
        File(1, false), FileOutput::kDefaultBuffer, mem::system_resource());
    ::atexit(flush_stdout);
    return s;
  }();
  return *stream;
}

SharedOutput& err() {
  static SharedOutput* stream =
      immortal<SharedOutput>(File(2, false), 0, mem::system_resource());
  return *stream;
}

Input& in() {
  static Input* stream = [] {
    auto* s = immortal<FileInput>(File(0, false), FileInput::kDefaultBuffer,
                                  mem::system_resource());
    s->tie(&out());
    return s;
  }();
  return *stream;
}

String read_file(StringView path) {
  FileInput input(path);
  return input.read_all();
}

void write_file(StringView path, StringView data) {
  File f =
      File::open(path, OpenMode::Write | OpenMode::Create | OpenMode::Truncate);
  f.write_all(data.data(), data.size());
  f.close();
}
}  // namespace yasl::io
