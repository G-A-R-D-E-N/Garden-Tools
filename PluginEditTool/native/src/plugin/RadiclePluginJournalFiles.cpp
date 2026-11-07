#include "RadiclePluginJournalRoot.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>

#ifdef _WIN32
#include "RadiclePluginWindowsJournalDirectory.hpp"
#else
#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __linux__
#include <linux/fs.h>
#include <sys/syscall.h>
#endif
#endif

namespace fo4ide::plugin::detail {
namespace {

#ifndef _WIN32
std::string checkedName(std::string_view name) {
    if (name.empty() || name == "." || name == ".." || name.back() == '.' || name.size() > 200 ||
        name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-") != name.npos) {
        throw std::runtime_error("invalid journal file name");
    }
    return std::string(name);
}

struct File {
    int value{-1};
    ~File() { if (value >= 0) ::close(value); }
};

struct stat requireRegular(int fd) {
    struct stat info{};
    if (::fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_nlink != 1 ||
        info.st_uid != ::geteuid() || (info.st_mode & 0077) != 0 || info.st_size < 0) {
        throw std::runtime_error("journal file is not a private regular file");
    }
    return info;
}

void syncDirectory(const JournalRoot& root) {
    if (::fsync(root.descriptor()) != 0) throw std::system_error(errno, std::generic_category(), "sync journal directory");
}
#endif

}

void JournalRoot::writeFile(std::string_view name, std::span<const std::byte> bytes) const {
#ifdef _WIN32
    WindowsJournalDirectory directory(handles_.back());
    directory.writeFile(name, bytes);
#else
    const auto fileName = checkedName(name);
    File file{::openat(descriptor(), fileName.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600)};
    if (file.value < 0) throw std::system_error(errno, std::generic_category(), "create journal file");
    try {
        static_cast<void>(requireRegular(file.value));
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            const auto written = ::write(file.value, bytes.data() + offset, bytes.size() - offset);
            if (written < 0 && errno == EINTR) continue;
            if (written < 0) throw std::system_error(errno, std::generic_category(), "write journal file");
            if (written == 0) throw std::runtime_error("short write to journal file");
            offset += static_cast<std::size_t>(written);
        }
        if (::fsync(file.value) != 0) throw std::system_error(errno, std::generic_category(), "flush journal file");
        const int fd = file.value;
        file.value = -1;
        if (::close(fd) != 0) throw std::system_error(errno, std::generic_category(), "close journal file");
        syncDirectory(*this);
    } catch (...) {
        if (file.value >= 0) ::close(file.value);
        file.value = -1;
        ::unlinkat(descriptor(), fileName.c_str(), 0);
        throw;
    }
#endif
}

std::vector<std::byte> JournalRoot::readFile(std::string_view name, std::size_t maxBytes) const {
#ifdef _WIN32
    WindowsJournalDirectory directory(handles_.back());
    return directory.readFile(name, maxBytes);
#else
    const auto fileName = checkedName(name);
    File file{::openat(descriptor(), fileName.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK | O_NOCTTY)};
    if (file.value < 0) throw std::system_error(errno, std::generic_category(), "open journal file");
    const auto info = requireRegular(file.value);
    if (static_cast<std::uintmax_t>(info.st_size) > std::numeric_limits<std::size_t>::max() ||
        static_cast<std::uintmax_t>(info.st_size) > maxBytes) {
        throw std::runtime_error("invalid journal file size");
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(info.st_size));
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto read = ::read(file.value, bytes.data() + offset, bytes.size() - offset);
        if (read < 0 && errno == EINTR) continue;
        if (read <= 0) throw std::runtime_error("cannot read complete journal file");
        offset += static_cast<std::size_t>(read);
    }
    return bytes;
#endif
}

std::vector<std::string> JournalRoot::listFiles(std::size_t maxEntries) const {
    if (maxEntries > kJournalMaxCleanupEntries) throw std::runtime_error("journal enumeration limit exceeds the maximum");
#ifndef _WIN32
    std::vector<std::string> names;
#endif
#ifdef _WIN32
    WindowsJournalDirectory directory(handles_.back());
    return directory.listFiles(maxEntries);
#else
    File directory{::openat(descriptor(), ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW)};
    if (directory.value < 0) throw std::system_error(errno, std::generic_category(), "open journal directory");
    DIR* raw = ::fdopendir(directory.value);
    if (raw == nullptr) throw std::system_error(errno, std::generic_category(), "list journal files");
    directory.value = -1;
    struct DirectoryCloser { void operator()(DIR* dir) const noexcept { ::closedir(dir); } };
    std::unique_ptr<DIR, DirectoryCloser> stream(raw);
    for (;;) {
        errno = 0;
        const auto* entry = ::readdir(stream.get());
        if (entry == nullptr) {
            if (errno != 0) throw std::system_error(errno, std::generic_category(), "list journal files");
            break;
        }
        const std::string name(entry->d_name);
        if (name == "." || name == "..") continue;
        const auto checked = checkedName(name);
        struct stat info{};
        if (::fstatat(descriptor(), checked.c_str(), &info, AT_SYMLINK_NOFOLLOW) != 0 ||
            !S_ISREG(info.st_mode) || info.st_nlink != 1 || info.st_uid != ::geteuid() ||
            (info.st_mode & 0077) != 0) {
            throw std::runtime_error("unexpected journal directory entry");
        }
        if (names.size() >= maxEntries) throw std::runtime_error("too many journal directory entries");
        names.push_back(checked);
    }
    std::sort(names.begin(), names.end());
    return names;
#endif
}

void JournalRoot::publishFile(std::string_view staged, std::string_view published) const {
    if (staged == published) throw std::runtime_error("journal publication paths must differ");
#ifdef _WIN32
    WindowsJournalDirectory directory(handles_.back());
    directory.publishFile(staged, published);
#else
    const auto source = checkedName(staged);
    const auto destination = checkedName(published);
    {
        File file{::openat(descriptor(), source.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK | O_NOCTTY)};
        if (file.value < 0) throw std::system_error(errno, std::generic_category(), "open staged journal file");
        static_cast<void>(requireRegular(file.value));
    }
#if defined(__linux__) && defined(SYS_renameat2)
    if (::syscall(SYS_renameat2, descriptor(), source.c_str(), descriptor(), destination.c_str(), RENAME_NOREPLACE) != 0) {
        throw std::system_error(errno, std::generic_category(), "publish journal file");
    }
    syncDirectory(*this);
#else
    throw std::runtime_error("exclusive journal publication is unavailable on this platform");
#endif
#endif
}

void JournalRoot::removeFile(std::string_view name) const {
#ifdef _WIN32
    WindowsJournalDirectory directory(handles_.back());
    directory.removeFile(name);
#else
    const auto fileName = checkedName(name);
    if (::unlinkat(descriptor(), fileName.c_str(), 0) != 0) throw std::system_error(errno, std::generic_category(), "remove journal file");
    syncDirectory(*this);
#endif
}

}
