#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace fo4ide::papyrus {

struct SnapshotSource {
    std::filesystem::path relativePath{};
    std::string text{};
};

std::vector<SnapshotSource> snapshotPapyrusSources(std::intptr_t authorizedRoot,
                                                   std::size_t maxEntries,
                                                   std::size_t maxFileBytes,
                                                   std::size_t maxTotalBytes);

#if defined(FO4IDE_TEST_HOOKS) && (defined(__linux__) || defined(_WIN32))
using SnapshotReadHook = void (*)();
void setSnapshotReadHook(SnapshotReadHook hook);
void setSnapshotBeforeOpenHook(SnapshotReadHook hook);
#ifdef _WIN32
void setSnapshotAfterDirectoryOpenHook(SnapshotReadHook hook);
#endif
#endif

}
