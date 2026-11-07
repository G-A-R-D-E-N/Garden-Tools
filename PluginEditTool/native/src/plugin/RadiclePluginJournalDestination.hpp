#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fo4ide::plugin::detail {

#if defined(FO4IDE_TEST_HOOKS) && defined(__linux__)
using JournalDestinationStageHook = void (*)();
void setJournalDestinationStageHook(JournalDestinationStageHook hook);
using JournalDirectoryEnumerationHook = void (*)();
void setJournalDirectoryEnumerationHook(JournalDirectoryEnumerationHook hook);
#endif

class JournalDirectoryLease;
#if defined(_WIN32)
class WindowsJournalDirectory;
class WindowsJournalSessionState;
struct WindowsJournalReplacementRequest;
struct WindowsJournalReplacementArtifacts;
struct WindowsJournalFileMetadata;
struct WindowsJournalFileSnapshot;
#endif

class JournalDirectoryAbsent : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct JournalDirectoryIdentity {
    std::uint64_t device{0};
    std::uint64_t inode{0};
    std::shared_ptr<JournalDirectoryLease> lease;
};

class JournalDestination {
public:
    JournalDestination(const std::filesystem::path& root,
                       const std::vector<std::string>& directories,
                       std::string file, bool createDirectories = false,
                       std::optional<JournalDirectoryIdentity> expectedRoot = std::nullopt);
    ~JournalDestination();
    JournalDestination(const JournalDestination&) = delete;
    JournalDestination& operator=(const JournalDestination&) = delete;
    std::optional<std::vector<std::byte>> read(std::size_t maxBytes) const;
#if defined(_WIN32)
    WindowsJournalFileMetadata metadata() const;
#endif
    void replace(bool originalExists, std::string_view originalHash, std::uint64_t originalSize,
                 std::span<const std::byte> bytes, std::string_view stageName);
    static JournalDirectoryIdentity captureRoot(const std::filesystem::path& root);
    static std::vector<std::string> listDirectory(const std::filesystem::path& root,
                                                   const std::vector<std::string>& directories,
                                                   const JournalDirectoryIdentity& identity);

private:
#if defined(_WIN32)
    friend class WindowsJournalDirectory;
    friend class WindowsJournalSessionState;
    WindowsJournalFileMetadata stagingMetadata() const;
    void* stagingParent() const;
    static WindowsJournalFileSnapshot stagingSnapshot(void* file, std::size_t maxBytes);
    WindowsJournalReplacementArtifacts replacementArtifacts(const WindowsJournalReplacementRequest& request,
        std::uint64_t originalSize, std::uint64_t finalSize, bool inheritedStage = false) const;
    void renameReplacementArtifact(const WindowsJournalReplacementRequest& request,
        const WindowsJournalReplacementArtifacts& expected, bool backupOriginal);
#endif
    JournalDestination(const std::filesystem::path& root, const std::vector<std::string>& directories,
                       std::string file, bool createDirectories,
                       std::optional<JournalDirectoryIdentity> expectedRoot, bool captureOnly);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
