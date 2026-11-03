#include "../../src/plugin/RadiclePluginJournalDestination.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

#ifdef __linux__
namespace {
std::filesystem::path changedDirectory;
void addEntryDuringEnumeration() {
    const auto previous = std::filesystem::last_write_time(changedDirectory);
    std::ofstream(changedDirectory / "new.ba2").put('x');
    std::filesystem::last_write_time(changedDirectory, previous - std::chrono::seconds(10));
}
struct ResetHook {
    ~ResetHook() { fo4ide::plugin::detail::setJournalDirectoryEnumerationHook(nullptr); }
};
struct Cleanup {
    std::filesystem::path path;
    ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); }
};
}

// Detects: source directory enumeration must fail when entries change during its held-directory scan.
TEST_CASE("Journal source inventory rejects concurrent directory mutation") {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() / ("fo4ide_inventory_" + std::to_string(id));
    Cleanup cleanup{root}; std::filesystem::create_directories(root);
    std::ofstream(root / "existing.ba2").put('x');
    const auto identity = fo4ide::plugin::detail::JournalDestination::captureRoot(root);
    changedDirectory = root; ResetHook reset;
    fo4ide::plugin::detail::setJournalDirectoryEnumerationHook(&addEntryDuringEnumeration);
    CHECK_THROWS(fo4ide::plugin::detail::JournalDestination::listDirectory(root, {}, identity));
    fo4ide::plugin::detail::setJournalDirectoryEnumerationHook(nullptr);
    CHECK(std::filesystem::exists(root / "new.ba2"));
}
#endif
