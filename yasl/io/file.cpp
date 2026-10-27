#include "file.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace yasl::io {
    Error::Error(const char* op, StringView path, int code) noexcept
        : Exception("yasl::io::Error")
        , code_(code)
    {
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
        return File(fd, true);
    }

    size_t File::read(void* buf, size_t n) {
        for (;;) {
            ssize_t r = ::read(fd_, buf, n);
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
            ssize_t w = ::write(fd_, p, n);
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
        if (::fstat(fd_, &st) != 0) {
            throw Error("fstat", "fd", errno);
        }
        return uint64(st.st_size);
    }

    void File::seek(uint64 offset) {
        if (::lseek(fd_, off_t(offset), SEEK_SET) < 0) {
            throw Error("lseek", "fd", errno);
        }
    }

    void File::sync() {
        if (::fsync(fd_) != 0) {
            throw Error("fsync", "fd", errno);
        }
    }

    void File::close() {
        if (fd_ >= 0) {
            int fd = yasl::exchange(fd_, -1);
            if (owned_ && ::close(fd) != 0 && errno != EINTR) {
                throw Error("close", "fd", errno);
            }
        }
    }

    void File::close_quietly() noexcept {
        if (fd_ >= 0 && owned_) {
            ::close(fd_);
        }
        fd_ = -1;
    }

    // FileOutput

    FileOutput::FileOutput(StringView path, OpenMode mode)
        : FileOutput(File::open(path, mode))
    {
    }

    FileOutput::FileOutput(File file, size_t buffer, mem::Resource* res)
        : file_(yasl::move(file))
        , res_(res)
        , cap_(buffer)
    {
        if (cap_) {
            buf_ = static_cast<char*>(res_->allocate(cap_, 1));
        }
        pos_ = buf_;
        end_ = buf_ + cap_;
    }

    FileOutput::~FileOutput() {
        try {
            drain();
        } catch (const Exception&) {
        }
        if (buf_) {
            res_->deallocate(buf_, cap_, 1);
        }
    }

    void FileOutput::drain() {
        if (pos_ != buf_) {
            size_t n = size_t(pos_ - buf_);
            pos_ = buf_;
            file_.write_all(buf_, n);
        }
    }

    void FileOutput::overflow(const char* data, size_t n) {
        drain();
        if (n >= cap_) {
            file_.write_all(data, n);
        } else {
            yasl::mem_copy(pos_, data, n);
            pos_ += n;
        }
    }

    void FileOutput::do_flush() {
        drain();
    }

    // FileInput

    FileInput::FileInput(StringView path)
        : FileInput(File::open(path, OpenMode::Read))
    {
    }

    FileInput::FileInput(File file, size_t buffer, mem::Resource* res)
        : file_(yasl::move(file))
        , res_(res)
        , cap_(yasl::max(buffer, size_t(1)))
    {
        buf_ = static_cast<char*>(res_->allocate(cap_, 1));
        pos_ = end_ = buf_;
    }

    FileInput::~FileInput() {
        res_->deallocate(buf_, cap_, 1);
    }

    bool FileInput::underflow() {
        size_t n = file_.read(buf_, cap_);
        pos_ = buf_;
        end_ = buf_ + n;
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
    }

    Output& out() {
        static Output* stream = [] {
            Output* s = immortal<FileOutput>(File(1, false), FileOutput::kDefaultBuffer, mem::system_resource());
            ::atexit(flush_stdout);
            return s;
        }();
        return *stream;
    }

    Output& err() {
        static Output* stream = immortal<FileOutput>(File(2, false), 0, mem::system_resource());
        return *stream;
    }

    Input& in() {
        static Input* stream = [] {
            auto* s = immortal<FileInput>(File(0, false), FileInput::kDefaultBuffer, mem::system_resource());
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
        File f = File::open(path, OpenMode::Write | OpenMode::Create | OpenMode::Truncate);
        f.write_all(data.data(), data.size());
        f.close();
    }
}
