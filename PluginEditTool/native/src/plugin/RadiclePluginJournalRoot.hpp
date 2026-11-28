#pragma once

#include <filesystem>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fo4ide::plugin::detail {

inline constexpr std::size_t kJournalMaxDirectoryEntries = 256;
inline constexpr std::size_t kJournalMaxCleanupEntries = 4096;

#if defined(FO4IDE_TEST_HOOKS) && !defined(_WIN32)
using JournalRootSyncHook = int (*)();
void setJournalRootSyncHook(JournalRootSyncHook hook);
#endif

class JournalRoot {
public:
    explicit JournalRoot(const std::filesystem::path& path);
    ~JournalRoot();
    JournalRoot(const JournalRoot&) = delete;
    JournalRoot& operator=(const JournalRoot&) = delete;
    void writeFile(std::string_view name, std::span<const std::byte> bytes) const;
    std::vector<std::byte> readFile(std::string_view name, std::size_t maxBytes) const;
    std::vector<std::string> listFiles(std::size_t maxEntries = kJournalMaxDirectoryEntries) const;
    void publishFile(std::string_view staged, std::string_view published) const;
    void removeFile(std::string_view name) const;
#ifndef _WIN32
    int descriptor() const noexcept { return fd_; }
#else
    friend class JournalLock;
    void* nativeHandle() const noexcept { return handles_.back(); }
#endif

private:
#ifndef _WIN32
    int fd_{-1};
#else
    std::vector<void*> handles_;
#endif
};

}
