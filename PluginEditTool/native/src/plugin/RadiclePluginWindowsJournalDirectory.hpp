#pragma once

#ifdef _WIN32
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fo4ide::plugin::detail {

class JournalDestination;
struct WindowsJournalReplacementArtifacts;
struct WindowsJournalReplacementRequest;
struct WindowsJournalReplacementExpectation;
enum class WindowsJournalReplacementState;
void validateWindowsJournalFileName(std::string_view name);

struct WindowsJournalFileIdentity {
    std::uint64_t volume{};
    std::array<std::byte, 16> file{};
    bool operator==(const WindowsJournalFileIdentity&) const = default;
};

struct WindowsJournalFileMetadata {
    std::string security{};
    std::uint32_t attributes{};
    std::int64_t creationTime{};
    bool operator==(const WindowsJournalFileMetadata&) const = default;
};

struct WindowsJournalFileSnapshot {
    WindowsJournalFileIdentity identity{};
    std::vector<std::byte> bytes{};
    std::optional<WindowsJournalFileMetadata> metadata{};
};

#ifdef FO4IDE_TEST_HOOKS
enum class WindowsJournalReplacementStep { validated, originalBackedUp, stagedPublished };
using WindowsJournalReplacementHook = void (*)(WindowsJournalReplacementStep);
void setWindowsJournalReplacementHook(WindowsJournalReplacementHook hook);
#endif

class WindowsJournalDirectory {
public:
    explicit WindowsJournalDirectory(void* directory);
    ~WindowsJournalDirectory();
    WindowsJournalDirectory(const WindowsJournalDirectory&) = delete;
    WindowsJournalDirectory& operator=(const WindowsJournalDirectory&) = delete;
    void writeFile(std::string_view name, std::span<const std::byte> bytes) const;
    std::vector<std::byte> readFile(std::string_view name, std::size_t maxBytes) const;
    WindowsJournalFileSnapshot snapshotFile(std::string_view name, std::size_t maxBytes) const;
    std::optional<WindowsJournalFileSnapshot> trySnapshotFile(std::string_view name, std::size_t maxBytes) const;
    WindowsJournalFileIdentity directoryIdentity() const;
    std::vector<std::string> listFiles(std::size_t maxEntries = 256) const;
    std::shared_ptr<void> lockFile(std::string_view name) const;
    void publishFile(std::string_view staged, std::string_view published) const;
    void publishSnapshot(std::string_view staged, const WindowsJournalFileSnapshot& expected, std::string_view published) const;
    void removeFile(std::string_view name) const;
    void replaceFileRecoverably(std::string_view destination, const WindowsJournalFileSnapshot& original,
                                std::string_view staged, const WindowsJournalFileSnapshot& replacement,
                                std::string_view backup) const;
    void persistReplacementIntent(const WindowsJournalReplacementRequest& request,
                                  const WindowsJournalFileSnapshot& original, const WindowsJournalFileSnapshot& replacement) const;
    void persistReplacementIntent(const WindowsJournalReplacementRequest& request, std::nullopt_t,
                                  const WindowsJournalFileSnapshot& replacement) const;
    WindowsJournalReplacementState inspectReplacement(const WindowsJournalReplacementRequest& request,
        const WindowsJournalReplacementExpectation& expected) const;
    void persistReplacementIntent(const WindowsJournalReplacementRequest& request,
        const WindowsJournalReplacementExpectation& expected, const JournalDestination& destination) const;
    WindowsJournalReplacementState inspectReplacement(const WindowsJournalReplacementRequest& request,
        const WindowsJournalReplacementExpectation& expected, const JournalDestination& destination) const;
    void resumeReplacement(const WindowsJournalReplacementRequest& request,
        const WindowsJournalReplacementExpectation& expected, JournalDestination& destination) const;
    void resumeReplacement(const WindowsJournalReplacementRequest& request) const;
    void resumeReplacement(const WindowsJournalReplacementRequest& request,
        const WindowsJournalReplacementExpectation& expected) const;

private:
    friend class WindowsJournalSessionState;
    void removeSnapshot(std::string_view name, const WindowsJournalFileSnapshot& expected) const;
    WindowsJournalReplacementState processReplacement(const WindowsJournalReplacementRequest& request,
        const WindowsJournalReplacementExpectation* expected, bool apply,
        const WindowsJournalReplacementArtifacts* artifacts = nullptr) const;
    std::shared_ptr<void> lockReplacement() const;
    void* directory_{};
};

}
#endif
