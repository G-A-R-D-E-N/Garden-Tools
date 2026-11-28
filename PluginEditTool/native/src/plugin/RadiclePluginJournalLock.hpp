#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace fo4ide::plugin::detail {

class JournalRoot;

std::filesystem::path journalIdentity(const std::filesystem::path& plugin);
bool sameJournalIdentity(const std::filesystem::path& first, const std::filesystem::path& second);

class JournalLock {
public:
    JournalLock(const JournalRoot& root, std::string_view name);
    ~JournalLock();
    JournalLock(const JournalLock&) = delete;
    JournalLock& operator=(const JournalLock&) = delete;

private:
#ifdef _WIN32
    std::shared_ptr<void> handle_;
#else
    int fd_{-1};
#endif
};

}
