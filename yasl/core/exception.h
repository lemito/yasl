#pragma once

namespace yasl {
// Root of every exception thrown by yasl. Messages are static strings, so
// throwing never allocates (io::Error keeps its own fixed buffer).
class Exception {
 public:
  explicit Exception(const char* message) noexcept : message_(message) {}

  virtual ~Exception() = default;

  virtual const char* what() const noexcept { return message_; }

 private:
  const char* message_;
};

class BadAlloc : public Exception {
 public:
  BadAlloc() noexcept : Exception("yasl: out of memory") {}
};

class OutOfRange : public Exception {
 public:
  using Exception::Exception;
};

class LengthError : public Exception {
 public:
  using Exception::Exception;
};

class BadAccess : public Exception {
 public:
  using Exception::Exception;
};

class FormatError : public Exception {
 public:
  using Exception::Exception;
};
}  // namespace yasl
