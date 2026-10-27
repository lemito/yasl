#pragma once

#include "input.h"
#include "output.h"

#include <yasl/mem/resource.h>
#include <yasl/str/str.h>
#include <yasl/sys/exception.h>

namespace yasl::io {
    // I/O failure with the OS error code and a ready message ("open 'x': No such file or directory").
    class Error: public Exception {
    public:
        Error(const char* op, StringView path, int code) noexcept;

        const char* what() const noexcept override {
            return message_;
        }

        int code() const noexcept {
            return code_;
        }

    private:
        char message_[256];
        int code_;
    };

    enum class OpenMode : uint32 {
        Read = 1,
        Write = 2,
        Append = 4,
        Create = 8,
        Truncate = 16,
        Exclusive = 32,
    };

    constexpr OpenMode operator|(OpenMode a, OpenMode b) noexcept {
        return OpenMode(uint32(a) | uint32(b));
    }

    constexpr bool has(OpenMode set, OpenMode flag) noexcept {
        return (uint32(set) & uint32(flag)) != 0;
    }

    // Owning (or borrowing) handle to an OS file descriptor. Move-only.
    class File {
    public:
        File() noexcept = default;

        explicit File(int fd, bool owned = true) noexcept
            : fd_(fd)
            , owned_(owned)
        {
        }

        File(File&& other) noexcept
            : fd_(yasl::exchange(other.fd_, -1))
            , owned_(other.owned_)
        {
        }

        File& operator=(File&& other) noexcept {
            if (this != &other) {
                close_quietly();
                fd_ = yasl::exchange(other.fd_, -1);
                owned_ = other.owned_;
            }
            return *this;
        }

        ~File() {
            close_quietly();
        }

        static File open(StringView path, OpenMode mode, uint32 permissions = 0644);

        // Returns 0 at end of file. Retries on EINTR.
        size_t read(void* buf, size_t n);

        void write_all(const void* data, size_t n);

        uint64 size() const;
        void seek(uint64 offset);
        void sync();
        void close();

        bool is_open() const noexcept {
            return fd_ >= 0;
        }

        int fd() const noexcept {
            return fd_;
        }

    private:
        void close_quietly() noexcept;

        int fd_ = -1;
        bool owned_ = true;
    };

    // Buffered writer to a file. Flushes on destruction (errors there are swallowed;
    // call flush() explicitly to observe them).
    class FileOutput final: public Output {
    public:
        static constexpr size_t kDefaultBuffer = 64 * 1024;

        explicit FileOutput(StringView path, OpenMode mode = OpenMode::Write | OpenMode::Create | OpenMode::Truncate);
        explicit FileOutput(File file, size_t buffer = kDefaultBuffer, mem::Resource* res = mem::default_resource());
        ~FileOutput() override;

        File& file() noexcept {
            return file_;
        }

    protected:
        void overflow(const char* data, size_t n) override;
        void do_flush() override;

    private:
        void drain();

        File file_;
        mem::Resource* res_;
        char* buf_ = nullptr;
        size_t cap_ = 0;
    };

    // Buffered reader from a file.
    class FileInput final: public Input {
    public:
        static constexpr size_t kDefaultBuffer = 64 * 1024;

        explicit FileInput(StringView path);
        explicit FileInput(File file, size_t buffer = kDefaultBuffer, mem::Resource* res = mem::default_resource());
        ~FileInput() override;

        File& file() noexcept {
            return file_;
        }

    protected:
        bool underflow() override;

    private:
        File file_;
        mem::Resource* res_;
        char* buf_ = nullptr;
        size_t cap_ = 0;
    };

    // Process-wide standard streams. out() is buffered and flushed at exit and
    // before in() reads; err() is unbuffered. Not synchronized between threads.
    Output& out();
    Output& err();
    Input& in();

    String read_file(StringView path);
    void write_file(StringView path, StringView data);

    template <class... Args>
    void print(FormatString<TypeIdentity<Args>...> fmt, const Args&... args) {
        out().format(fmt, args...);
    }

    template <class... Args>
    void println(FormatString<TypeIdentity<Args>...> fmt, const Args&... args) {
        out().format(fmt, args...).put('\n');
    }
}
