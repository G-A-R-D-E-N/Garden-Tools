#include "../../src/plugin/RadiclePluginBa2Source.hpp"
#include <catch2/catch_test_macros.hpp>

#ifdef __linux__
#include <array>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
using fo4ide::plugin::detail::Ba2ReadRoot;
struct Ba2ReadSourceTest {
    std::filesystem::path path;
    Ba2ReadSourceTest() {
        char name[] = "/tmp/ba2-source-XXXXXX";
        const auto result = ::mkdtemp(name);
        REQUIRE(result != nullptr);
        path = result;
        std::filesystem::create_directory(path / "data");
        std::ofstream(path / "data/archive.ba2") << "original";
    }
    ~Ba2ReadSourceTest() {
        ::chmod((path / "data").c_str(), 0755);
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

// Detects: a retained read-only BA2 source must reject reads and opens beyond the authorized size.
TEST_CASE_METHOD(Ba2ReadSourceTest, "RetainsReadOnlyAuthorityAndBoundsReads", "[ba2][held-source]") {
    REQUIRE(::chmod((path / "data/archive.ba2").c_str(), 0444) == 0);
    auto root = Ba2ReadRoot::authorize(path);
    auto source = root.open("data/archive.ba2", 8);
    std::array<std::byte, 8> bytes{};
    REQUIRE_NOTHROW(source->read(0, bytes));
    REQUIRE(std::string(reinterpret_cast<char*>(bytes.data()), bytes.size()) == "original");
    REQUIRE_THROWS_AS(source->read(1, bytes), std::exception);
    REQUIRE_THROWS_AS(root.open("data/archive.ba2", 7), std::exception);
}

// Detects: held BA2 reads must reject replaced files and renamed ancestor directories.
TEST_CASE_METHOD(Ba2ReadSourceTest, "RejectsReplacementAndAncestorRename", "[ba2][held-source]") {
    auto root = Ba2ReadRoot::authorize(path);
    auto source = root.open("data/archive.ba2", 8);
    std::filesystem::rename(path / "data", path / "old");
    std::filesystem::create_directory(path / "data");
    std::ofstream(path / "data/archive.ba2") << "replaced";
    std::array<std::byte, 8> bytes{};
    REQUIRE_THROWS_AS(source->read(0, bytes), std::exception);
    std::filesystem::rename(path / "data/archive.ba2", path / "data/old.ba2");
    auto replacement = root.open("data/old.ba2", 8);
    std::filesystem::remove(path / "data/old.ba2");
    std::ofstream(path / "data/old.ba2") << "original";
    REQUIRE_THROWS_AS(replacement->read(0, bytes), std::exception);
}

// Detects: BA2 source opens must reject symlinks, hardlinks and path traversal.
TEST_CASE_METHOD(Ba2ReadSourceTest, "RejectsSymlinksHardlinksAndTraversal", "[ba2][held-source]") {
    auto root = Ba2ReadRoot::authorize(path);
    std::filesystem::create_symlink("data/archive.ba2", path / "link");
    REQUIRE_THROWS_AS(root.open("link", 8), std::exception);
    std::filesystem::create_directory_symlink("data", path / "alias");
    REQUIRE_THROWS_AS(root.open("alias/archive.ba2", 8), std::exception);
    REQUIRE_THROWS_AS(root.open("data/../data/archive.ba2", 8), std::exception);
    std::filesystem::create_hard_link(path / "data/archive.ba2", path / "hard");
    REQUIRE_THROWS_AS(root.open("hard", 8), std::exception);
}

// Detects: held BA2 reads must reject in-place mutation and truncation.
TEST_CASE_METHOD(Ba2ReadSourceTest, "RejectsInPlaceMutationAndTruncation", "[ba2][held-source]") {
    auto root = Ba2ReadRoot::authorize(path);
    auto source = root.open("data/archive.ba2", 8);
    std::ofstream(path / "data/archive.ba2") << "mutated!";
    std::array<std::byte, 8> bytes{};
    REQUIRE_THROWS_AS(source->read(0, bytes), std::exception);
    source = root.open("data/archive.ba2", 8);
    std::filesystem::resize_file(path / "data/archive.ba2", 1);
    REQUIRE_THROWS_AS(source->read(0, bytes), std::exception);
}

// Detects: a retained BA2 read authority must reject replacement of its authorized root.
TEST_CASE_METHOD(Ba2ReadSourceTest, "RejectsAuthorizedRootReplacement", "[ba2][held-source]") {
    auto root = Ba2ReadRoot::authorize(path / "data");
    std::filesystem::rename(path / "data", path / "old");
    std::filesystem::create_directory(path / "data");
    std::ofstream(path / "data/archive.ba2") << "original";
    REQUIRE_THROWS_AS(root.open("archive.ba2", 8), std::exception);
}

// Detects: inventory additions, removals and replacements must not reuse stale directory offsets or files.
TEST_CASE_METHOD(Ba2ReadSourceTest, "Provider inventory reflects additions removals and replacements", "[ba2][held-source]") {
    auto root = Ba2ReadRoot::authorize(path);
    REQUIRE(root.list("data", 2) == std::vector<std::string>{"archive.ba2"});
    auto old = root.open("data/archive.ba2", 8);
    std::ofstream(path / "data/added.ba2") << "added";
    REQUIRE(root.list("data", 2) == std::vector<std::string>{"added.ba2", "archive.ba2"});
    std::filesystem::remove(path / "data/archive.ba2");
    REQUIRE(root.list("data", 2) == std::vector<std::string>{"added.ba2"});
    std::ofstream(path / "data/archive.ba2") << "replaced";
    REQUIRE(root.list("data", 2) == std::vector<std::string>{"added.ba2", "archive.ba2"});
    std::array<std::byte, 8> bytes{};
    REQUIRE_THROWS_AS(old->read(0, bytes), std::exception);
}

// Detects: unrelated entries must count against the inventory bound without being followed or opened.
TEST_CASE_METHOD(Ba2ReadSourceTest, "Provider inventory counts all entry types", "[ba2][held-source]") {
    auto root = Ba2ReadRoot::authorize(path / "data");
    std::filesystem::create_directory(path / "data/meshes");
    std::filesystem::create_symlink("missing", path / "data/alias.ba2");
    std::ofstream(path / "data/unreadable.txt") << "unrelated";
    REQUIRE(::chmod((path / "data/unreadable.txt").c_str(), 0000) == 0);
    REQUIRE(root.list({}, 4) == std::vector<std::string>{"alias.ba2", "archive.ba2", "meshes", "unreadable.txt"});
    REQUIRE_THROWS_AS(root.list({}, 3), std::exception);
    REQUIRE_THROWS_AS(root.open("alias.ba2", 8), std::exception);
    REQUIRE(root.list("meshes", 0).empty());
    REQUIRE_THROWS_AS(root.list({}, 0), std::exception);
}

// Detects: replacement of an authorized ancestor and symlink directory paths must fail inventory reads.
TEST_CASE_METHOD(Ba2ReadSourceTest, "Provider inventory rejects replaced roots and invalid directories", "[ba2][held-source]") {
    auto root = Ba2ReadRoot::authorize(path);
    auto data = Ba2ReadRoot::authorize(path / "data");
    std::filesystem::create_directory_symlink("data", path / "alias");
    REQUIRE_THROWS_AS(root.list("alias", 8), std::exception);
    REQUIRE_THROWS_AS(root.list("missing", 8), std::exception);
    REQUIRE_THROWS_AS(root.list("data/archive.ba2", 8), std::exception);
    REQUIRE_THROWS_AS(root.list("data/..", 8), std::exception);
    std::filesystem::rename(path / "data", path / "old");
    std::filesystem::create_directory(path / "data");
    REQUIRE_THROWS_AS(data.list({}, 8), std::exception);
}

// Detects: an absent descendant must be distinguishable from a replaced authorized read root.
TEST_CASE_METHOD(Ba2ReadSourceTest, "Held read directory absence does not authorize root replacement", "[ba2][held-source]") {
    auto root = Ba2ReadRoot::authorize(path / "data");
    CHECK_FALSE(root.listOptional("Strings", 8).has_value());
    std::filesystem::rename(path / "data", path / "old");
    std::filesystem::create_directory(path / "data");
    CHECK_THROWS(root.listOptional("Strings", 8));
}

// Detects: inaccessible inventory directories must report an error instead of an empty provider list.
TEST_CASE_METHOD(Ba2ReadSourceTest, "Provider inventory fails on unreadable directories", "[ba2][held-source]") {
    REQUIRE(::chmod(path.c_str(), 0755) == 0);
    REQUIRE(::chmod((path / "data").c_str(), 0111) == 0);
    const auto child = ::fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        if (::geteuid() == 0 && (::setgid(65534) || ::setuid(65534))) ::_exit(2);
        try {
            auto root = Ba2ReadRoot::authorize(path);
            try { (void)root.list("data", 8); } catch (...) { ::_exit(0); }
            ::_exit(3);
        } catch (...) { ::_exit(4); }
    }
    int status = 0;
    REQUIRE(::waitpid(child, &status, 0) == child);
    REQUIRE(::chmod((path / "data").c_str(), 0755) == 0);
    REQUIRE(WIFEXITED(status));
    REQUIRE(WEXITSTATUS(status) == 0);
}

// Detects: a read-only BA2 authority must permit safe protected root-owned archives without write access.
TEST_CASE_METHOD(Ba2ReadSourceTest, "UnprivilegedReaderCanReadRootOwnedProtectedArchive", "[ba2][held-source]") {
    if (::geteuid() != 0) { WARN("requires root to drop privileges in child"); return; }
    REQUIRE(::chmod(path.c_str(), 0755) == 0);
    REQUIRE(::chmod((path / "data/archive.ba2").c_str(), 0444) == 0);
    const auto child = ::fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        if (::setgid(65534) || ::setuid(65534)) ::_exit(2);
        try {
            auto source = Ba2ReadRoot::authorize(path).open("data/archive.ba2", 8);
            std::array<std::byte, 8> bytes{};
            source->read(0, bytes);
            ::_exit(::access((path / "data/archive.ba2").c_str(), W_OK) == -1 ? 0 : 3);
        } catch (...) { ::_exit(4); }
    }
    int status = 0;
    REQUIRE(::waitpid(child, &status, 0) == child);
    REQUIRE(WIFEXITED(status));
    REQUIRE(WEXITSTATUS(status) == 0);
}
}
#endif

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <array>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <string>
#include <vector>
#include "../helpers/TempRoot.hpp"

namespace {
using fo4ide::plugin::detail::Ba2ReadRoot;
struct Ba2ReadSourceTest {
    fo4ide_test::TempRoot temp{"ba2-source"};
    std::filesystem::path path = temp.path();
    Ba2ReadSourceTest() {
        std::filesystem::create_directory(path / "data");
        std::ofstream(path / "data/archive.ba2") << "original";
    }
    ~Ba2ReadSourceTest() {
        ::SetFileAttributesW((path / "data/archive.ba2").c_str(), FILE_ATTRIBUTE_NORMAL);
        ::RemoveDirectoryW((path / "alias").c_str());
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

void makeJunction(const std::filesystem::path& link, const std::filesystem::path& target) {
    const auto command = "mklink /J \"" + link.string() + "\" \"" + target.string() + "\"";
    REQUIRE(std::system(command.c_str()) == 0);
}

// Invariant, detects: a hard-linked BA2, or a traversal, stream-suffix or drive-relative name, opened as a held source.
TEST_CASE_METHOD(Ba2ReadSourceTest, "Windows refuses held reads through hard links and unsafe names", "[ba2][held-source]") {
    auto root = Ba2ReadRoot::authorize(path);
    std::filesystem::create_hard_link(path / "data/archive.ba2", path / "hard.ba2");
    REQUIRE_THROWS_AS(root.open("data/archive.ba2", 8), std::exception);
    std::filesystem::remove(path / "hard.ba2");
    REQUIRE_NOTHROW(root.open("data/archive.ba2", 8));
    REQUIRE_THROWS_AS(root.open("data/../data/archive.ba2", 8), std::exception);
    REQUIRE_THROWS_AS(root.open("data/archive.ba2:stream", 8), std::exception);
    REQUIRE_THROWS_AS(root.open("C:archive.ba2", 8), std::exception);
}

// Invariant, detects: another process overwriting or deleting a BA2 while a held source reads it, or a held source reading or opening past its authorized size.
TEST_CASE_METHOD(Ba2ReadSourceTest, "Windows refuses writers and deleters of a held BA2", "[ba2][held-source]") {
    auto root = Ba2ReadRoot::authorize(path);
    auto source = root.open("data/archive.ba2", 8);
    REQUIRE_THROWS_AS(root.open("data/archive.ba2", 7), std::exception);
    std::ofstream writer(path / "data/archive.ba2");
    CHECK_FALSE(writer.is_open());
    CHECK_FALSE(::DeleteFileW((path / "data/archive.ba2").c_str()));
    std::array<std::byte, 8> bytes{};
    REQUIRE_NOTHROW(source->read(0, bytes));
    REQUIRE(std::string(reinterpret_cast<char*>(bytes.data()), bytes.size()) == "original");
    REQUIRE_THROWS_AS(source->read(1, bytes), std::exception);
}

// Invariant, detects: an authorized folder renamed away under a live root, so later reads reach a different folder or fail to notice the rename.
TEST_CASE_METHOD(Ba2ReadSourceTest, "Windows held source fails while its authorized directory is renamed away", "[ba2][held-source]") {
    auto root = Ba2ReadRoot::authorize(path / "data");
    std::filesystem::rename(path / "data", path / "old");
    REQUIRE_THROWS_AS(root.open("archive.ba2", 8), std::exception);
    std::filesystem::rename(path / "old", path / "data");
    REQUIRE_NOTHROW(root.open("archive.ba2", 8));
}

// Invariant, detects: a held authorized directory that refuses a write open from the same process, which would block the journal from creating a missing Strings folder at publish.
TEST_CASE_METHOD(Ba2ReadSourceTest, "Windows held directory accepts a write open from the same process", "[ba2][held-source]") {
    auto root = Ba2ReadRoot::authorize(path / "data");
    const HANDLE writer = ::CreateFileW((path / "data").c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    REQUIRE(writer != INVALID_HANDLE_VALUE);
    ::CloseHandle(writer);
    REQUIRE_NOTHROW(root.open("archive.ba2", 8));
}

// Invariant, detects: a held BA2 that another process can rename away while a held source still reads the old name.
TEST_CASE_METHOD(Ba2ReadSourceTest, "Windows refuses renaming a held BA2", "[ba2][held-source]") {
    auto root = Ba2ReadRoot::authorize(path);
    auto source = root.open("data/archive.ba2", 8);
    std::error_code error;
    std::filesystem::rename(path / "data/archive.ba2", path / "data/moved.ba2", error);
    CHECK(static_cast<bool>(error));
    std::array<std::byte, 8> bytes{};
    REQUIRE_NOTHROW(source->read(0, bytes));
    REQUIRE(std::string(reinterpret_cast<char*>(bytes.data()), bytes.size()) == "original");
}

// Invariant, detects: a string lease that blocks the journal's rename of its table, or that keeps reading the old object after a same-named replacement appears.
TEST_CASE_METHOD(Ba2ReadSourceTest, "Windows string lease lets its file be renamed away and detects a replacement", "[ba2][held-source]") {
    auto root = Ba2ReadRoot::authorize(path);
    auto source = root.open("data/archive.ba2", 8, true);
    std::array<std::byte, 8> bytes{};
    REQUIRE_NOTHROW(source->read(0, bytes));
    std::filesystem::rename(path / "data/archive.ba2", path / "data/backup.ba2");
    std::ofstream(path / "data/archive.ba2") << "replaced";
    REQUIRE_THROWS_AS(source->read(0, bytes), std::exception);
}

// Invariant, detects: a held source that keeps reading after an outside process changes the file's timestamps.
TEST_CASE_METHOD(Ba2ReadSourceTest, "Windows read fails after a held BA2 timestamp changes", "[ba2][held-source]") {
    auto root = Ba2ReadRoot::authorize(path);
    auto source = root.open("data/archive.ba2", 8);
    std::array<std::byte, 8> bytes{};
    REQUIRE_NOTHROW(source->read(0, bytes));
    const HANDLE file = ::CreateFileW((path / "data/archive.ba2").c_str(), FILE_WRITE_ATTRIBUTES,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    REQUIRE(file != INVALID_HANDLE_VALUE);
    SYSTEMTIME moment{};
    moment.wYear = 2001;
    moment.wMonth = 1;
    moment.wDay = 1;
    FILETIME written{};
    REQUIRE(::SystemTimeToFileTime(&moment, &written));
    const BOOL changed = ::SetFileTime(file, nullptr, nullptr, &written);
    ::CloseHandle(file);
    REQUIRE(changed);
    REQUIRE_THROWS_AS(source->read(0, bytes), std::exception);
}

// Invariant, detects: two roots at different depths compared as the same root.
TEST_CASE_METHOD(Ba2ReadSourceTest, "Windows sameRoot separates a directory from its parent", "[ba2][held-source]") {
    auto root = Ba2ReadRoot::authorize(path / "data");
    auto again = Ba2ReadRoot::authorize(path / "data");
    auto parent = Ba2ReadRoot::authorize(path);
    CHECK(root.sameRoot(again));
    CHECK_FALSE(root.sameRoot(parent));
}

// Invariant, detects: a directory junction on the authorized path followed by authorize, open or list, and a listing reused between calls so an added or removed entry is missed.
TEST_CASE_METHOD(Ba2ReadSourceTest, "Windows refuses reparse point directories in authorize open and list and tracks entries between calls", "[ba2][held-source]") {
    auto root = Ba2ReadRoot::authorize(path);
    REQUIRE(root.list("data", 2) == std::vector<std::string>{"archive.ba2"});
    std::ofstream(path / "data/added.ba2") << "added";
    REQUIRE(root.list("data", 3) == std::vector<std::string>{"added.ba2", "archive.ba2"});
    std::filesystem::remove(path / "data/archive.ba2");
    REQUIRE(root.list("data", 2) == std::vector<std::string>{"added.ba2"});
    makeJunction(path / "alias", path / "data");
    REQUIRE_THROWS_AS(Ba2ReadRoot::authorize(path / "alias"), std::exception);
    REQUIRE_THROWS_AS(root.open("alias/added.ba2", 8), std::exception);
    REQUIRE_THROWS_AS(root.list("alias", 8), std::exception);
}

// Invariant, detects: unsorted names, dropped directory or symlink entries, or an entry bound that is not enforced.
TEST_CASE_METHOD(Ba2ReadSourceTest, "Windows directory listing sorts names, keeps every entry type and enforces the entry bound", "[ba2][held-source]") {
    auto root = Ba2ReadRoot::authorize(path);
    std::ofstream(path / "data/B.ba2") << "b";
    std::ofstream(path / "data/a.ba2") << "a";
    std::filesystem::create_directory(path / "data/meshes");
    if (::CreateSymbolicLinkW((path / "data/alias.ba2").c_str(), L"missing.ba2", SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE) == 0) {
        if (::GetLastError() == ERROR_PRIVILEGE_NOT_HELD) SKIP("creating file symlinks requires Developer Mode or SeCreateSymbolicLinkPrivilege");
        FAIL("cannot create file symlink");
    }
    REQUIRE(root.list("data", 5) == std::vector<std::string>{"B.ba2", "a.ba2", "alias.ba2", "archive.ba2", "meshes"});
    REQUIRE_THROWS_AS(root.list("data", 4), std::exception);
    REQUIRE_THROWS_AS(root.open("data/alias.ba2", 8), std::exception);
}

// Invariant, detects: an absent directory reported as a hard error, or a present one reported as absent.
TEST_CASE_METHOD(Ba2ReadSourceTest, "Windows optional listing reports an absent directory as nullopt", "[ba2][held-source]") {
    auto root = Ba2ReadRoot::authorize(path);
    CHECK_FALSE(root.listOptional("Strings", 8).has_value());
    const auto present = root.listOptional("data", 8);
    REQUIRE(present.has_value());
    REQUIRE(*present == std::vector<std::string>{"archive.ba2"});
}

// Invariant, detects: a reparse point or a file in directory position reported as an absent directory.
TEST_CASE_METHOD(Ba2ReadSourceTest, "Windows optional listing still throws for a reparse point child or a file in directory position", "[ba2][held-source]") {
    auto root = Ba2ReadRoot::authorize(path);
    makeJunction(path / "alias", path / "data");
    REQUIRE_THROWS_AS(root.listOptional("alias", 8), std::exception);
    REQUIRE_THROWS_AS(root.listOptional("data/archive.ba2", 8), std::exception);
}

// Invariant, detects: UTF-16 names returned in another encoding, or unpaired surrogates silently replaced.
TEST_CASE_METHOD(Ba2ReadSourceTest, "Windows directory listing returns non-ASCII names as UTF-8 and refuses lone surrogates", "[ba2][held-source]") {
    auto root = Ba2ReadRoot::authorize(path);
    std::ofstream(path / "data" / std::wstring(L"caf\x00E9-\x0416-\xD83D\xDE00.ba2")) << "u";
    const auto listed = root.list("data", 3);
    REQUIRE(listed == std::vector<std::string>{"archive.ba2", "caf\xC3\xA9-\xD0\x96-\xF0\x9F\x98\x80.ba2"});
    const auto utf8 = std::u8string(reinterpret_cast<const char8_t*>(listed[1].data()), listed[1].size());
    REQUIRE_NOTHROW(root.open(std::filesystem::path("data") / std::filesystem::path(utf8), 8));
    std::ofstream bad(path / "data" / std::wstring(L"bad\xD800.ba2"));
    REQUIRE(bad.is_open());
    bad.close();
    REQUIRE_THROWS_AS(root.list("data", 3), std::exception);
}
}
#endif
