#include <fo4ide/papyrus/SecureSourceSnapshot.hpp>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>

#ifdef __linux__
#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <linux/magic.h>
#include <linux/openat2.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/vfs.h>
#include <unistd.h>
#endif

namespace fo4ide::papyrus {

#ifdef __linux__
namespace {

#ifdef FO4IDE_TEST_HOOKS
SnapshotReadHook readHook = nullptr;
SnapshotReadHook beforeOpenHook = nullptr;
#endif

class Descriptor {
public:
    explicit Descriptor(int value) : value_(value) {
        if (value_ < 0) throw std::system_error(errno, std::generic_category(), "open import entry");
    }
    ~Descriptor() { if (value_ >= 0) ::close(value_); }
    Descriptor(const Descriptor&) = delete;
    Descriptor& operator=(const Descriptor&) = delete;
    int get() const { return value_; }
private:
    int value_;
};

struct DirectoryCloser {
    void operator()(DIR* stream) const noexcept { ::closedir(stream); }
};

bool isSource(std::string name) {
    for (char& ch : name) if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch + ('a' - 'A'));
    return name.ends_with(".psc");
}

int openContained(int directory, const std::string& name, int flags) {
    struct open_how options{};
    options.flags = static_cast<std::uint64_t>(flags);
    options.resolve = RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS | RESOLVE_NO_XDEV;
    return static_cast<int>(::syscall(SYS_openat2, directory, name.c_str(), &options, sizeof(options)));
}

void collect(int dir, const std::filesystem::path& relative, std::size_t maxEntries,
             std::size_t maxFileBytes, std::size_t maxTotalBytes, std::size_t& entries,
             std::size_t& total, std::vector<SnapshotSource>& result, std::size_t depth, int procFd) {
    const int duplicate = ::dup(dir);
    if (duplicate < 0) throw std::system_error(errno, std::generic_category(), "duplicate import directory");
    DIR* opened = ::fdopendir(duplicate);
    if (!opened) { const int error = errno; ::close(duplicate); throw std::system_error(error, std::generic_category(), "read import directory"); }
    std::unique_ptr<DIR, DirectoryCloser> stream(opened);
    std::vector<std::string> names;
    errno = 0;
    while (const auto* entry = ::readdir(stream.get())) {
        const std::string name(entry->d_name);
        if (name == "." || name == "..") continue;
        if (++entries > maxEntries) throw std::runtime_error("Papyrus import tree exceeds its entry limit");
        names.push_back(name);
        errno = 0;
    }
    const int readError = errno;
    stream.reset();
    if (readError) throw std::system_error(readError, std::generic_category(), "enumerate import directory");
    std::sort(names.begin(), names.end());
    for (const auto& name : names) {
        struct stat before{};
        if (::fstatat(dir, name.c_str(), &before, AT_SYMLINK_NOFOLLOW) != 0) {
            throw std::system_error(errno, std::generic_category(), "inspect import entry");
        }
        if (S_ISLNK(before.st_mode) || (!S_ISDIR(before.st_mode) && !S_ISREG(before.st_mode))) {
            throw std::runtime_error("unsupported import entry");
        }
        if (S_ISREG(before.st_mode) && !isSource(name) &&
            !(relative.empty() && name == "Institute_Papyrus_Flags.flg")) continue;
#ifdef FO4IDE_TEST_HOOKS
        if (S_ISREG(before.st_mode) && beforeOpenHook) beforeOpenHook();
#endif
        const int flags = O_CLOEXEC | O_NOFOLLOW | (S_ISDIR(before.st_mode) ? O_RDONLY | O_DIRECTORY : O_PATH);
        Descriptor child(openContained(dir, name, flags));
        struct stat opened{};
        if (::fstat(child.get(), &opened) != 0 || opened.st_dev != before.st_dev || opened.st_ino != before.st_ino ||
            (opened.st_mode & S_IFMT) != (before.st_mode & S_IFMT)) {
            throw std::runtime_error("import entry changed while opening");
        }
        if (S_ISDIR(opened.st_mode)) {
            if (depth >= 64) throw std::runtime_error("Papyrus import tree exceeds its depth limit");
            collect(child.get(), relative / name, maxEntries, maxFileBytes, maxTotalBytes, entries, total, result, depth + 1, procFd);
            continue;
        }
        if (opened.st_nlink != 1 || opened.st_size < 0 || static_cast<std::uintmax_t>(opened.st_size) > maxFileBytes) {
            throw std::runtime_error("Papyrus source is not a bounded regular file");
        }
        const auto descriptorName = std::to_string(child.get());
        Descriptor readable(::openat(procFd, descriptorName.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK));
        struct stat reopened{};
        if (::fstat(readable.get(), &reopened) != 0 || !S_ISREG(reopened.st_mode) ||
            reopened.st_dev != opened.st_dev || reopened.st_ino != opened.st_ino) {
            throw std::runtime_error("Papyrus source changed while reopening");
        }
        std::string text;
        char buffer[8192];
        for (;;) {
            const auto count = ::read(readable.get(), buffer, sizeof(buffer));
            if (count < 0) { if (errno == EINTR) continue; throw std::system_error(errno, std::generic_category(), "read Papyrus source"); }
            if (count == 0) break;
            const auto size = static_cast<std::size_t>(count);
            if (size > maxFileBytes - text.size() || size > maxTotalBytes - total) {
                throw std::runtime_error("Papyrus source exceeds its byte limit");
            }
            text.append(buffer, size);
            total += size;
        }
        struct stat after{};
#ifdef FO4IDE_TEST_HOOKS
        if (readHook) readHook();
#endif
        if (::fstat(readable.get(), &after) != 0 || after.st_size != opened.st_size ||
            after.st_mtim.tv_sec != opened.st_mtim.tv_sec || after.st_mtim.tv_nsec != opened.st_mtim.tv_nsec ||
            after.st_ctim.tv_sec != opened.st_ctim.tv_sec || after.st_ctim.tv_nsec != opened.st_ctim.tv_nsec ||
            text.size() != static_cast<std::size_t>(opened.st_size)) {
            throw std::runtime_error("Papyrus source changed while reading");
        }
        result.push_back({relative / name, std::move(text)});
    }
}

}
#endif

#if defined(FO4IDE_TEST_HOOKS) && defined(__linux__)
void setSnapshotReadHook(SnapshotReadHook hook) { readHook = hook; }
void setSnapshotBeforeOpenHook(SnapshotReadHook hook) { beforeOpenHook = hook; }
#endif

#ifndef _WIN32
std::vector<SnapshotSource> snapshotPapyrusSources(std::intptr_t authorizedRoot,
                                                   std::size_t maxEntries,
                                                   std::size_t maxFileBytes,
                                                   std::size_t maxTotalBytes) {
#ifndef __linux__
    (void)authorizedRoot; (void)maxEntries; (void)maxFileBytes; (void)maxTotalBytes;
    throw std::runtime_error("secure Papyrus snapshots are unavailable on this platform");
#else
    if (authorizedRoot < 0 || authorizedRoot > std::numeric_limits<int>::max() ||
        !maxEntries || !maxFileBytes || !maxTotalBytes) throw std::runtime_error("invalid Papyrus snapshot limits or root");
    Descriptor root(::openat(static_cast<int>(authorizedRoot), ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    struct stat info{};
    if (::fstat(root.get(), &info) != 0 || !S_ISDIR(info.st_mode)) throw std::runtime_error("authorized import root is not a directory");
    std::size_t entries = 0, total = 0;
    std::vector<SnapshotSource> result;
    Descriptor procFd(::open("/proc/self/fd", O_PATH | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    struct statfs procInfo{};
    if (::fstatfs(procFd.get(), &procInfo) != 0 || procInfo.f_type != PROC_SUPER_MAGIC) {
        throw std::runtime_error("verified procfs file descriptors are unavailable");
    }
    collect(root.get(), {}, maxEntries, maxFileBytes, maxTotalBytes, entries, total, result, 0, procFd.get());
    return result;
#endif
}
#endif

}
