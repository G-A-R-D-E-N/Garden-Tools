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
