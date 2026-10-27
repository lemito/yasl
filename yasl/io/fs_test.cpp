#include <yasl/io/fs.h>
#include <yasl/sys/algo.h>
#include <yasl/test/test.h>

#include <unistd.h>

using namespace yasl;
using namespace yasl::io;

YASL_TEST(path_components) {
    Path p("/var/log/app.tar.gz");
    CHECK_EQ(p.file_name(), "app.tar.gz");
    CHECK_EQ(p.stem(), "app.tar");
    CHECK_EQ(*p.extension(), "gz");
    CHECK_EQ(*p.parent(), "/var/log");
    CHECK(p.is_absolute());
    CHECK(!Path("rel/x").is_absolute());
    CHECK(!Path(".bashrc").extension().has_value());
    CHECK_EQ(Path(".bashrc").stem(), ".bashrc");
    CHECK(!Path("noext").extension().has_value());
    CHECK_EQ(*Path("a.").extension(), "");
    CHECK_EQ(*Path("/x").parent(), "/");
    CHECK(!Path("/").parent().has_value());
    CHECK(!Path("").parent().has_value());
    CHECK_EQ(*Path("name").parent(), "");
    CHECK_EQ(*Path("dir/sub/").parent(), "dir");
    CHECK_EQ(Path("a/b").file_name(), "b");
    CHECK_EQ(Path("a/b/").file_name(), "");
}

YASL_TEST(path_join) {
    CHECK((Path("/a") / "b").view() == "/a/b");
    CHECK((Path("/a/") / "b").view() == "/a/b");
    CHECK((Path("") / "b").view() == "b");
    CHECK((Path("/a") / "/etc").view() == "/etc");
    CHECK((Path("x").join("y/z")).view() == "x/y/z");
    CHECK(Path("q") == Path("q"));
}

YASL_TEST(fs_roundtrip) {
    Path base = Path("/tmp") / format("yasl_fs_test_{}", ::getpid());
    CHECK(!fs::exists(base));
    CHECK(fs::create_dir_all(base / "a/b/c"));
    CHECK(fs::create_dir_all(base / "a/b/c")); // idempotent
    CHECK(fs::is_dir(base / "a/b"));
    CHECK(fs::create_dir(base / "a/b/c").is_err());

    Path file = base / "a/hello.txt";
    write_file(file.view(), "hello world");
    CHECK(fs::is_file(file));
    CHECK(!fs::is_dir(file));
    CHECK_EQ(*fs::file_size(file), uint64(11));
    auto missing = fs::file_size(base / "nope");
    CHECK(missing.is_err());
    CHECK_EQ(missing.error().code(), ENOENT);

    auto entries = fs::read_dir(base / "a");
    CHECK(entries.is_ok());
    CHECK_EQ(entries->size(), size_t(2));
    sort_by_key(entries->begin(), entries->end(), [](const fs::DirEntry& e) { return e.name.view(); });
    CHECK_EQ((*entries)[0].name, "b");
    CHECK((*entries)[0].is_dir);
    CHECK_EQ((*entries)[1].name, "hello.txt");
    CHECK(!(*entries)[1].is_dir);
    CHECK(fs::read_dir(base / "none").is_err());

    CHECK(fs::rename(file, base / "a/moved.txt"));
    CHECK(!fs::exists(file));
    CHECK_EQ(read_file((base / "a/moved.txt").view()), "hello world");

    CHECK(fs::remove(base / "a/moved.txt"));
    CHECK(fs::remove(base / "a/b/c"));
    CHECK(fs::remove(base / "a/b"));
    CHECK(fs::remove(base / "a"));
    CHECK(fs::remove(base / "a").is_err());
    CHECK(fs::remove(base));
    CHECK(!fs::exists(base));
}
