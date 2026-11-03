#pragma once

#include "RadiclePluginJournalLock.hpp"
#include "RadiclePluginJournalManifest.hpp"
#include "RadiclePluginJournalRoot.hpp"
#include "RadiclePluginJournalPeers.hpp"

#include <stdexcept>
#include <system_error>
#include <functional>

namespace fo4ide::plugin::detail {

#ifdef FO4IDE_TEST_HOOKS
using JournalCommitHook = void (*)(std::size_t);
void setJournalCommitHook(JournalCommitHook hook);
using JournalTimestampHook = std::error_code (*)();
void setJournalTimestampHook(JournalTimestampHook hook);
#ifdef _WIN32
enum class WindowsJournalSessionStep { prepared, preflightComplete, stageCreated, stageReceipted, stageExposed,
    completionPublished, backupRemoved, stagingDirectoryRemoved, manifestRemoved, privateRecordRemoved };
using WindowsJournalSessionHook = void (*)(WindowsJournalSessionStep, std::size_t,
    const std::filesystem::path&, const std::filesystem::path&);
void setWindowsJournalSessionHook(WindowsJournalSessionHook hook);
#endif
#endif

struct JournalInput {
    std::string kind;
    std::string name;
    bool originalExists{false};
    std::string originalHash;
    std::uint64_t originalSize{0};
    std::vector<std::byte> finalBytes;
    std::filesystem::path rootSelector;
    std::vector<std::string> directories;
    bool verifyOnly{false};
};

std::vector<JournalArchiveReference> journalArchiveOrder(
    const std::shared_ptr<const Ba2ReadRoot>& dataRoot,
    const std::shared_ptr<const Ba2ReadRoot>& modRoot,
    const std::vector<std::string>& plugins, std::string_view ownPlugin);

class JournalRecoveryRequired : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class JournalRecoveryProfileInvalid : public JournalRecoveryRequired {
public:
    using JournalRecoveryRequired::JournalRecoveryRequired;
};

class JournalRecoveryReadRootChanged : public JournalRecoveryRequired {
public:
    using JournalRecoveryRequired::JournalRecoveryRequired;
};

struct JournalReadConfiguration {
    std::vector<JournalApprovedReadRoot> approvedReadRoots;
    std::vector<JournalArchiveReference> archiveDirectories;
    std::vector<JournalArchiveReference> archiveProviderOrder;
    std::vector<JournalPeerSnapshot> peers;
    std::vector<JournalArchiveReference> readStringDirectories;
};

using JournalPrepareRecovery = std::function<void(const JournalManifest&, const std::vector<std::byte>&, JournalReadConfiguration&)>;

class JournalSession {
public:
    explicit JournalSession(const std::filesystem::path& root);
    void publish(const JournalRoots& roots, const std::vector<JournalInput>& files,
                 const std::vector<JournalSourceSet>& sources = {},
                 const std::vector<JournalArchiveInventory>& archives = {},
                 const std::vector<JournalReadStringInventory>& readStrings = {});
    void recover(const std::vector<JournalRoots>& approved, const JournalPrepareRecovery& prepare = {});

private:
    void replay(std::string_view manifestName, const JournalRoots& roots,
                const std::vector<JournalPeerSnapshot>& peers = {});
    JournalRoot root_;
    JournalLock lock_;
};

}
