#if !defined(_WIN32)
#include "RadiclePluginJournalDestination.hpp"
#include "RadiclePluginJournalManifest.hpp"

#include <fo4/io/sha256.hpp>

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <utility>

#ifdef __linux__
#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/xattr.h>
#include <unistd.h>
#endif

namespace fo4ide::plugin::detail {

#ifdef __linux__
class JournalDirectoryLease {
public:
    ~JournalDirectoryLease() { if (fd >= 0) ::close(fd); }
    int fd{-1};
};
#endif

#if defined(FO4IDE_TEST_HOOKS) && defined(__linux__)
namespace { JournalDestinationStageHook journalDestinationStageHook = nullptr; JournalDirectoryEnumerationHook journalDirectoryEnumerationHook = nullptr; }
void setJournalDestinationStageHook(JournalDestinationStageHook hook) { journalDestinationStageHook = hook; }
void setJournalDirectoryEnumerationHook(JournalDirectoryEnumerationHook hook) { journalDirectoryEnumerationHook = hook; }
#endif

struct JournalDestination::Impl {
#ifdef __linux__
    std::vector<int> directories;
    std::vector<std::string> names;
    std::string file;
    int original{-1};
    struct stat initial{};

    ~Impl() {
        if (original >= 0) ::close(original);
        for (const auto fd : directories) ::close(fd);
    }

    void verifyDirectories() const {
        bool stickyParent = false;
        for (std::size_t index = 0; index < directories.size(); ++index) {
            struct stat held{};
            if (::fstat(directories[index], &held) != 0 || !S_ISDIR(held.st_mode) ||
                (held.st_uid != 0 && held.st_uid != ::geteuid()) ||
                (stickyParent && held.st_uid != ::geteuid()) ||
                ((held.st_mode & 0022) != 0 && (held.st_mode & S_ISVTX) == 0)) {
                throw std::runtime_error("journal destination directory is unsafe");
            }
            if (index != 0) {
                struct stat linked{};
                if (::fstatat(directories[index - 1], names[index - 1].c_str(), &linked, AT_SYMLINK_NOFOLLOW) != 0 ||
                    !S_ISDIR(linked.st_mode) || linked.st_dev != held.st_dev || linked.st_ino != held.st_ino) {
                    throw std::runtime_error("journal destination directory changed");
                }
            }
            stickyParent = (held.st_mode & S_ISVTX) != 0 && (held.st_mode & 0022) != 0;
        }
    }

    void verifyEntry() const {
        struct stat linked{};
        const int result = ::fstatat(directories.back(), file.c_str(), &linked, AT_SYMLINK_NOFOLLOW);
        if (original < 0) {
            if (result == 0 || errno != ENOENT) throw std::runtime_error("journal destination appeared");
            return;
        }
        if (result != 0 || !S_ISREG(linked.st_mode) || linked.st_nlink != 1 ||
            linked.st_dev != initial.st_dev || linked.st_ino != initial.st_ino) {
            throw std::runtime_error("journal destination changed");
        }
    }
#endif
};

namespace {

#ifdef __linux__
bool component(std::string_view value) {
    return !value.empty() && value != "." && value != ".." &&
        value.find('/') == value.npos && value.find('\\') == value.npos &&
        value.find('\0') == value.npos;
}

void requireNoAttributes(int fd) {
    const auto size = ::flistxattr(fd, nullptr, 0);
    if (size < 0 && errno != ENOTSUP) throw std::system_error(errno, std::generic_category(), "inspect destination attributes");
    if (size > 0) throw std::runtime_error("journal destination has unsupported extended attributes");
}

bool sameFile(const struct stat& first, const struct stat& second) {
    return S_ISREG(second.st_mode) && second.st_nlink == 1 &&
        second.st_uid == ::geteuid() && (second.st_mode & 07022) == 0 &&
        first.st_dev == second.st_dev && first.st_ino == second.st_ino && first.st_size == second.st_size &&
        first.st_mtim.tv_sec == second.st_mtim.tv_sec && first.st_mtim.tv_nsec == second.st_mtim.tv_nsec &&
        first.st_ctim.tv_sec == second.st_ctim.tv_sec && first.st_ctim.tv_nsec == second.st_ctim.tv_nsec;
}
#endif

}

JournalDestination::JournalDestination(const std::filesystem::path& root,
                                       const std::vector<std::string>& directories,
                                       std::string file, bool createDirectories,
                                       std::optional<JournalDirectoryIdentity> expectedRoot)
    : JournalDestination(root, directories, std::move(file), createDirectories, expectedRoot, false) {}

JournalDestination::JournalDestination(const std::filesystem::path& root,
                                       const std::vector<std::string>& directories,
                                       std::string file, bool createDirectories,
                                       std::optional<JournalDirectoryIdentity> expectedRoot,
                                       bool captureOnly)
    : impl_(std::make_unique<Impl>()) {
#ifdef __linux__
    if (!root.is_absolute() || (!captureOnly && !component(file))) throw std::runtime_error("invalid journal destination");
    std::vector<std::string> names;
    for (const auto& item : root.relative_path()) {
        const auto name = item.string();
        if (!component(name)) throw std::runtime_error("invalid journal destination directory");
        names.push_back(name);
    }
    const auto rootComponents = names.size();
    for (const auto& name : directories) {
        if (!component(name)) throw std::runtime_error("invalid journal destination directory");
        names.push_back(name);
    }
    impl_->directories.reserve(names.size() + 1);
    impl_->names.reserve(names.size());
    const int base = ::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (base < 0) throw std::system_error(errno, std::generic_category(), "open destination root");
    impl_->directories.push_back(base);
    impl_->verifyDirectories();
    const auto checkRoot = [&] {
        if (!expectedRoot) return;
        struct stat info{};
        struct stat held{};
        if (::fstat(impl_->directories.back(), &info) != 0 ||
            !expectedRoot->lease || ::fstat(expectedRoot->lease->fd, &held) != 0 ||
            static_cast<std::uint64_t>(info.st_dev) != expectedRoot->device ||
            static_cast<std::uint64_t>(info.st_ino) != expectedRoot->inode ||
            held.st_dev != info.st_dev || held.st_ino != info.st_ino) {
            throw std::runtime_error("authorized journal root changed");
        }
    };
    if (rootComponents == 0) checkRoot();
    for (std::size_t index = 0; index < names.size(); ++index) {
        const auto& name = names[index];
        const int parent = impl_->directories.back();
        if (index >= rootComponents && createDirectories &&
            ::mkdirat(parent, name.c_str(), 0755) != 0 && errno != EEXIST) {
            throw std::system_error(errno, std::generic_category(), "create destination directory");
        }
        const int fd = ::openat(parent, name.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if (fd < 0 && errno == ENOENT && captureOnly && index >= rootComponents)
            throw JournalDirectoryAbsent("journal source directory is absent");
        if (fd < 0) throw std::system_error(errno, std::generic_category(), "open destination directory");
        impl_->directories.push_back(fd);
        impl_->names.push_back(name);
        impl_->verifyDirectories();
        if (index + 1 == rootComponents) checkRoot();
        if (index >= rootComponents && createDirectories && ::fsync(parent) != 0) {
            throw std::system_error(errno, std::generic_category(), "sync destination parent");
        }
    }
    if (captureOnly) return;
    impl_->file = std::move(file);
    impl_->verifyDirectories();
    impl_->original = ::openat(impl_->directories.back(), impl_->file.c_str(),
        O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK | O_NOCTTY);
    if (impl_->original < 0 && errno != ENOENT) throw std::system_error(errno, std::generic_category(), "open journal destination");
    if (impl_->original >= 0) {
        if (::fstat(impl_->original, &impl_->initial) != 0 || !S_ISREG(impl_->initial.st_mode) ||
            impl_->initial.st_nlink != 1 || impl_->initial.st_uid != ::geteuid() ||
            (impl_->initial.st_mode & 07022) != 0 || impl_->initial.st_size < 0) {
            throw std::runtime_error("journal destination is not a safe regular file");
        }
        requireNoAttributes(impl_->original);
    }
    impl_->verifyEntry();
#else
    static_cast<void>(root); static_cast<void>(directories); static_cast<void>(file); static_cast<void>(createDirectories);
    static_cast<void>(expectedRoot); static_cast<void>(captureOnly);
    throw std::runtime_error("held journal destinations are unavailable on this platform");
#endif
}

JournalDirectoryIdentity JournalDestination::captureRoot(const std::filesystem::path& root) {
#ifdef __linux__
    JournalDestination captured(root, {}, {}, false, std::nullopt, true);
    struct stat info{};
    if (::fstat(captured.impl_->directories.back(), &info) != 0) {
        throw std::system_error(errno, std::generic_category(), "inspect authorized journal root");
    }
    captured.impl_->verifyDirectories();
    auto lease = std::make_shared<JournalDirectoryLease>();
    lease->fd = ::fcntl(captured.impl_->directories.back(), F_DUPFD_CLOEXEC, 0);
    if (lease->fd < 0) throw std::system_error(errno, std::generic_category(), "hold authorized journal root");
    return {static_cast<std::uint64_t>(info.st_dev), static_cast<std::uint64_t>(info.st_ino), std::move(lease)};
#else
    static_cast<void>(root);
    throw std::runtime_error("held journal destinations are unavailable on this platform");
#endif
}

JournalDestination::~JournalDestination() = default;

std::vector<std::string> JournalDestination::listDirectory(const std::filesystem::path& root,
                                                           const std::vector<std::string>& directories,
                                                           const JournalDirectoryIdentity& identity) {
#ifdef __linux__
    JournalDestination held(root, directories, {}, false, identity, true);
    struct stat before{};
    if (::fstat(held.impl_->directories.back(), &before) != 0)
        throw std::system_error(errno, std::generic_category(), "inspect journal source directory");
    const int copy = ::fcntl(held.impl_->directories.back(), F_DUPFD_CLOEXEC, 0);
    if (copy < 0) throw std::system_error(errno, std::generic_category(), "duplicate journal directory");
    DIR* stream = ::fdopendir(copy);
    if (!stream) { const auto error = errno; ::close(copy); throw std::system_error(error, std::generic_category(), "open journal directory stream"); }
    struct StreamGuard { DIR* value; ~StreamGuard() { ::closedir(value); } } guard{stream};
    std::vector<std::string> result;
#ifdef FO4IDE_TEST_HOOKS
    bool hookCalled = false;
#endif
    for (;;) {
        errno = 0;
        const auto* entry = ::readdir(stream);
        if (!entry) { if (errno) throw std::system_error(errno, std::generic_category(), "enumerate journal directory"); break; }
        const std::string name = entry->d_name;
        if (name == "." || name == "..") continue;
        if (result.size() == 4096) throw std::runtime_error("journal source directory is too large");
        result.push_back(name);
#ifdef FO4IDE_TEST_HOOKS
        if (!hookCalled && journalDirectoryEnumerationHook) { hookCalled = true; journalDirectoryEnumerationHook(); }
#endif
    }
    held.impl_->verifyDirectories();
    struct stat after{};
    if (::fstat(held.impl_->directories.back(), &after) != 0 || before.st_dev != after.st_dev ||
        before.st_ino != after.st_ino || before.st_mtim.tv_sec != after.st_mtim.tv_sec ||
        before.st_mtim.tv_nsec != after.st_mtim.tv_nsec || before.st_ctim.tv_sec != after.st_ctim.tv_sec ||
        before.st_ctim.tv_nsec != after.st_ctim.tv_nsec)
        throw std::runtime_error("journal source directory changed during enumeration");
    return result;
#else
    static_cast<void>(root); static_cast<void>(directories); static_cast<void>(identity);
    throw std::runtime_error("held journal source directories are unavailable on this platform");
#endif
}

std::optional<std::vector<std::byte>> JournalDestination::read(std::size_t maxBytes) const {
#ifdef __linux__
    impl_->verifyDirectories();
    impl_->verifyEntry();
    if (impl_->original < 0) return std::nullopt;
    requireNoAttributes(impl_->original);
    struct stat before{};
    if (::fstat(impl_->original, &before) != 0 || !sameFile(impl_->initial, before) || before.st_size < 0 ||
        static_cast<std::uintmax_t>(before.st_size) > maxBytes) {
        throw std::runtime_error("journal destination changed or is too large");
    }
    std::vector<std::byte> result(static_cast<std::size_t>(before.st_size));
    std::size_t offset = 0;
    while (offset < result.size()) {
        const auto count = ::pread(impl_->original, result.data() + offset, result.size() - offset, static_cast<off_t>(offset));
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) throw std::runtime_error("cannot read complete journal destination");
        offset += static_cast<std::size_t>(count);
    }
    struct stat after{};
    if (::fstat(impl_->original, &after) != 0 || !sameFile(before, after)) {
        throw std::runtime_error("journal destination changed while reading");
    }
    impl_->verifyDirectories();
    impl_->verifyEntry();
    return result;
#else
    static_cast<void>(maxBytes);
    throw std::runtime_error("held journal destinations are unavailable on this platform");
#endif
}

void JournalDestination::replace(bool originalExists, std::string_view originalHash, std::uint64_t originalSize,
                                 std::span<const std::byte> bytes, std::string_view stageName) {
#ifdef __linux__
    if (!component(stageName) || stageName == impl_->file || stageName.size() > 200 ||
        stageName.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-") != stageName.npos) {
        throw std::runtime_error("invalid destination stage name");
    }
    if (bytes.size() > kJournalMaxFileBytes || originalSize > kJournalMaxFileBytes ||
        (!originalExists && (!originalHash.empty() || originalSize != 0))) {
        throw std::runtime_error("invalid or oversized journal destination");
    }
    const auto verifyOriginal = [&] {
        if (originalSize > std::numeric_limits<std::size_t>::max()) throw std::runtime_error("journal original is too large");
        const auto current = read(static_cast<std::size_t>(originalSize));
        if (current.has_value() != originalExists ||
            (current && (current->size() != originalSize || fo4::io::sha256_hex(*current) != originalHash))) {
            throw std::runtime_error("journal destination does not match the expected original");
        }
    };
    verifyOriginal();
    const std::string stage(stageName);
    const int directory = impl_->directories.back();
    int fd = ::openat(directory, stage.c_str(), O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) throw std::system_error(errno, std::generic_category(), "create destination stage");
    bool published = false;
    try {
        if (impl_->original >= 0) {
            struct stat stageInfo{};
            if (::fstat(fd, &stageInfo) != 0) throw std::system_error(errno, std::generic_category(), "inspect destination stage");
            if (stageInfo.st_gid != impl_->initial.st_gid && ::fchown(fd, static_cast<uid_t>(-1), impl_->initial.st_gid) != 0) {
                throw std::system_error(errno, std::generic_category(), "set destination group");
            }
            if (::fchmod(fd, impl_->initial.st_mode & 0777) != 0) {
                throw std::system_error(errno, std::generic_category(), "set destination permissions");
            }
        }
        requireNoAttributes(fd);
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            const auto count = ::write(fd, bytes.data() + offset, bytes.size() - offset);
            if (count < 0 && errno == EINTR) continue;
            if (count < 0) throw std::system_error(errno, std::generic_category(), "write destination stage");
            if (count == 0) throw std::runtime_error("short write to destination stage");
            offset += static_cast<std::size_t>(count);
        }
        if (::fsync(fd) != 0) throw std::system_error(errno, std::generic_category(), "flush destination stage");
        struct stat staged{};
        if (::fstat(fd, &staged) != 0 || !S_ISREG(staged.st_mode) || staged.st_nlink != 1 ||
            staged.st_uid != ::geteuid() ||
            staged.st_size < 0 || static_cast<std::uintmax_t>(staged.st_size) != bytes.size()) {
            throw std::runtime_error("journal destination stage changed");
        }
#ifdef FO4IDE_TEST_HOOKS
        if (journalDestinationStageHook != nullptr) journalDestinationStageHook();
#endif
        std::array<std::byte, 65536> buffer{};
        std::size_t checked = 0;
        while (checked < bytes.size()) {
            const auto wanted = std::min(buffer.size(), bytes.size() - checked);
            const auto count = ::pread(fd, buffer.data(), wanted, static_cast<off_t>(checked));
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0 || !std::equal(buffer.data(), buffer.data() + static_cast<std::size_t>(count), bytes.data() + checked)) {
                throw std::runtime_error("journal destination stage changed");
            }
            checked += static_cast<std::size_t>(count);
        }
        struct stat linked{};
        if (::fstat(fd, &linked) != 0 || !sameFile(staged, linked)) {
            throw std::runtime_error("journal destination stage changed");
        }
        requireNoAttributes(fd);
        verifyOriginal();
        if (::fstatat(directory, stage.c_str(), &linked, AT_SYMLINK_NOFOLLOW) != 0 || !sameFile(staged, linked)) {
            throw std::runtime_error("journal destination stage changed");
        }
        if (originalExists) {
            if (::renameat(directory, stage.c_str(), directory, impl_->file.c_str()) != 0) {
                throw std::system_error(errno, std::generic_category(), "replace journal destination");
            }
        } else {
#ifdef SYS_renameat2
            if (::syscall(SYS_renameat2, directory, stage.c_str(), directory, impl_->file.c_str(), RENAME_NOREPLACE) != 0) {
                throw std::system_error(errno, std::generic_category(), "create journal destination");
            }
#else
            throw std::runtime_error("exclusive destination publication is unavailable");
#endif
        }
        published = true;
        const int closing = fd;
        fd = -1;
        if (::close(closing) != 0) throw std::system_error(errno, std::generic_category(), "close destination stage");
        if (::fsync(directory) != 0) throw std::system_error(errno, std::generic_category(), "sync destination directory");
    } catch (...) {
        if (!published && fd >= 0) {
            struct stat held{};
            struct stat linked{};
            if (::fstat(fd, &held) == 0 &&
                ::fstatat(directory, stage.c_str(), &linked, AT_SYMLINK_NOFOLLOW) == 0 &&
                S_ISREG(linked.st_mode) && held.st_dev == linked.st_dev && held.st_ino == linked.st_ino) {
                ::unlinkat(directory, stage.c_str(), 0);
            }
        }
        if (fd >= 0) ::close(fd);
        throw;
    }
#else
    static_cast<void>(originalExists); static_cast<void>(originalHash); static_cast<void>(originalSize);
    static_cast<void>(bytes); static_cast<void>(stageName);
    throw std::runtime_error("held journal destinations are unavailable on this platform");
#endif
}

}

#endif
