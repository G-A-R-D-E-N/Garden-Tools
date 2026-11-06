#include "RadiclePluginJournalLock.hpp"
#include "RadiclePluginJournalRoot.hpp"

#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "RadiclePluginWindowsJournalDirectory.hpp"
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace fo4ide::plugin::detail {

std::filesystem::path journalIdentity(const std::filesystem::path& plugin) {
    if (plugin.empty() || plugin.native().find(std::filesystem::path::value_type{}) != std::filesystem::path::string_type::npos) {
        throw std::runtime_error("invalid plugin path");
    }
#ifdef _WIN32
    struct CloseHandle {
        void operator()(void* handle) const noexcept {
            if (handle != nullptr && handle != INVALID_HANDLE_VALUE) ::CloseHandle(handle);
        }
    };
    std::unique_ptr<void, CloseHandle> handle(::CreateFileW(plugin.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (handle.get() == INVALID_HANDLE_VALUE) {
        throw std::system_error(static_cast<int>(::GetLastError()), std::system_category(), "open plugin identity");
    }
    BY_HANDLE_FILE_INFORMATION info{};
    if (!::GetFileInformationByHandle(handle.get(), &info) || info.nNumberOfLinks != 1 ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) != 0) {
        throw std::runtime_error("plugin identity is not a single regular file");
    }
    const DWORD size = ::GetFinalPathNameByHandleW(handle.get(), nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_GUID);
    if (size == 0 || size == MAXDWORD) throw std::runtime_error("cannot size plugin identity");
    std::vector<wchar_t> buffer(static_cast<std::size_t>(size) + 1);
    const DWORD length = ::GetFinalPathNameByHandleW(handle.get(), buffer.data(),
        static_cast<DWORD>(buffer.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_GUID);
    if (length == 0 || length >= buffer.size()) throw std::runtime_error("cannot resolve plugin identity");
    return std::filesystem::path(std::wstring(buffer.data(), length));
#else
    const auto canonical = std::filesystem::canonical(plugin);
    struct stat info{};
    if (::stat(canonical.c_str(), &info) != 0 || !S_ISREG(info.st_mode) || info.st_nlink != 1) {
        throw std::runtime_error("plugin identity is not a single regular file");
    }
    return canonical;
#endif
}

bool sameJournalIdentity(const std::filesystem::path& first, const std::filesystem::path& second) {
#ifdef _WIN32
    const auto& left = first.native();
    const auto& right = second.native();
    if (left.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        right.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::runtime_error("plugin identity is too long");
    }
    const auto result = ::CompareStringOrdinal(left.data(), static_cast<int>(left.size()),
        right.data(), static_cast<int>(right.size()), TRUE);
    if (result == 0) throw std::system_error(static_cast<int>(::GetLastError()), std::system_category(), "compare plugin identities");
    if (result != CSTR_EQUAL) return false;
    std::error_code error;
    const bool equivalent = std::filesystem::equivalent(first, second, error);
    if (error) throw std::system_error(error, "verify plugin identities");
    return equivalent;
#else
    return first == second;
#endif
}

#ifndef _WIN32
namespace {

void validateAndLock(int& fd) {
    if (fd < 0) throw std::system_error(errno, std::generic_category(), "open journal lock");
    struct stat status{};
    if (::fstat(fd, &status) != 0 || !S_ISREG(status.st_mode) || status.st_nlink != 1 ||
        status.st_uid != ::geteuid() || (status.st_mode & 0077) != 0) {
        ::close(fd);
        fd = -1;
        throw std::runtime_error("journal lock is not a private regular file");
    }
    if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
        const int error = errno;
        ::close(fd);
        fd = -1;
        throw std::system_error(error, std::generic_category(), "lock journal");
    }
}

}
#endif

JournalLock::JournalLock(const JournalRoot& root, std::string_view name) {
    if (name.empty() || name == "." || name == ".." || name.find('/') != name.npos ||
        name.find('\\') != name.npos || name.find('\0') != name.npos || name.back() == '.' ||
        name.size() > 200 || name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-") != name.npos) {
        throw std::runtime_error("invalid journal lock name");
    }
#ifdef _WIN32
    WindowsJournalDirectory directory(root.nativeHandle());
    handle_ = directory.lockFile(name);
#else
    const std::string file(name);
    fd_ = ::openat(root.descriptor(), file.c_str(),
                   O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK | O_NOCTTY, 0600);
    validateAndLock(fd_);
#endif
}

JournalLock::~JournalLock() {
#ifndef _WIN32
    if (fd_ >= 0) ::close(fd_);
#endif
}

}
