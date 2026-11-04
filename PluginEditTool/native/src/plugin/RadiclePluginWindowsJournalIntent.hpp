#pragma once

#ifdef _WIN32
#include "RadiclePluginWindowsJournalDirectory.hpp"
#include <cstdint>
#include <string>

namespace fo4ide::plugin::detail {

inline constexpr std::uint64_t kWindowsReplacementMaxFileBytes = 512ull * 1024 * 1024;

struct WindowsJournalReplacementRequest {
    std::string transaction{};
    std::uint32_t entry{};
    std::string destination{};
    std::string staged{};
    std::string backup{};
};

struct WindowsJournalReplacementExpectation {
    WindowsJournalFileIdentity directory{};
    bool originalExists{};
    std::uint64_t originalSize{};
    std::string originalHash{};
    std::uint64_t finalSize{};
    std::string finalHash{};
    std::optional<WindowsJournalFileMetadata> metadata{};
};

struct WindowsJournalReplacementArtifacts {
    WindowsJournalFileIdentity directory{};
    std::optional<WindowsJournalFileSnapshot> destination{};
    std::optional<WindowsJournalFileSnapshot> staged{};
    std::optional<WindowsJournalFileSnapshot> backup{};
};

enum class WindowsJournalReplacementState { prepared, backedUp, applied };

#ifdef FO4IDE_TEST_HOOKS
enum class WindowsJournalIntentStep { intentWritten, intentPublished, doneWritten, donePublished, replacementPublished, originalBackedUp };
using WindowsJournalIntentHook = void (*)(WindowsJournalIntentStep);
void setWindowsJournalIntentHook(WindowsJournalIntentHook hook);
#endif

}
#endif
