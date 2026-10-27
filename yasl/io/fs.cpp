#include "fs.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

namespace yasl::io::fs {
    namespace {
        bool stat_path(const Path& p, struct stat& st) noexcept {
            return ::stat(p.c_str(), &st) == 0;
        }

        Error fail(const char* op, const Path& p) {
            return Error(op, p.view(), errno);
        }
    }

    bool exists(const Path& p) noexcept {
        struct stat st;
        return stat_path(p, st);
    }

    bool is_dir(const Path& p) noexcept {
        struct stat st;
        return stat_path(p, st) && S_ISDIR(st.st_mode);
    }

    bool is_file(const Path& p) noexcept {
        struct stat st;
        return stat_path(p, st) && S_ISREG(st.st_mode);
    }

    Result<uint64, Error> file_size(const Path& p) {
        struct stat st;
        if (!stat_path(p, st)) {
            return err(fail("stat", p));
        }
        return ok(uint64(st.st_size));
    }

    Result<void, Error> remove(const Path& p) {
        if (::unlink(p.c_str()) == 0) {
            return ok();
        }
        if (errno == EISDIR || errno == EPERM) {
            if (::rmdir(p.c_str()) == 0) {
                return ok();
            }
        }
        return err(fail("remove", p));
    }

    Result<void, Error> create_dir(const Path& p) {
        if (::mkdir(p.c_str(), 0777) != 0) {
            return err(fail("mkdir", p));
        }
        return ok();
    }

    Result<void, Error> create_dir_all(const Path& p) {
        if (p.empty() || is_dir(p)) {
            return ok();
        }
        if (Optional<StringView> parent = p.parent(); parent && !parent->empty()) {
            auto r = create_dir_all(Path(*parent));
            if (!r) {
                return r;
            }
        }
        if (::mkdir(p.c_str(), 0777) != 0 && !(errno == EEXIST && is_dir(p))) {
            return err(fail("mkdir", p));
        }
        return ok();
    }

    Result<void, Error> rename(const Path& from, const Path& to) {
        if (::rename(from.c_str(), to.c_str()) != 0) {
            return err(fail("rename", from));
        }
        return ok();
    }

    Result<Vector<DirEntry>, Error> read_dir(const Path& p) {
        DIR* d = ::opendir(p.c_str());
        if (!d) {
            return err(fail("opendir", p));
        }
        Vector<DirEntry> out;
        try {
            while (dirent* e = ::readdir(d)) {
                StringView name(e->d_name);
                if (name == "." || name == "..") {
                    continue;
                }
                bool dir = e->d_type == DT_DIR;
                if (e->d_type == DT_UNKNOWN) {
                    dir = is_dir(p / name);
                }
                out.push_back(DirEntry{String(name), dir});
            }
        } catch (...) {
            ::closedir(d);
            throw;
        }
        ::closedir(d);
        return ok(yasl::move(out));
    }
}
