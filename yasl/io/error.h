#pragma once

#include <yasl/core/exception.h>
#include <yasl/str/view.h>

namespace yasl::io {
// I/O failure with the OS error code and a ready message ("open 'x': No such
// file or directory").
class Error : public Exception {
 public:
  Error(const char* op, StringView path, int code) noexcept;

  const char* what() const noexcept override { return message_; }

  int code() const noexcept { return code_; }

 private:
  char message_[256];
  int code_;
};
}  // namespace yasl::io
