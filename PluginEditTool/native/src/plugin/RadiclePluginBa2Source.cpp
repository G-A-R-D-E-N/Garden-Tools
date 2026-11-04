#include "RadiclePluginBa2Source.hpp"

#include <algorithm>
#include <atomic>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#ifdef __linux__
#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace fo4ide::plugin::detail {
#ifdef FO4IDE_TEST_HOOKS
namespace { std::atomic<Ba2ReadHook> readHook{nullptr}; }
void setBa2ReadHook(Ba2ReadHook hook) { readHook.store(hook); }
#endif
namespace {
#ifdef __linux__
class ReadDirectoryAbsent : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct Handle {
    int fd;
    Handle(int parent, const char* name, int flags) : fd(::openat(parent, name, flags)) {
        if (fd < 0) throw std::system_error(errno, std::generic_category(), "cannot open BA2 read source");
    }
    ~Handle() { if (fd >= 0) ::close(fd); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};

std::vector<std::string> components(const std::filesystem::path& path) {
    std::vector<std::string> result;
    for (const auto& part : path) {
        auto name = part.string();
        if (name.empty() || name == "." || name == ".." || name.size() > 255 ||
            name.find_first_of("/\\") != name.npos || name.find('\0') != name.npos || result.size() == 128)
            throw std::runtime_error("invalid BA2 source path");
        result.push_back(std::move(name));
    }
    return result;
}

struct Directories {
    std::vector<std::shared_ptr<Handle>> held{};
    std::vector<std::string> names{};
    void verify() const {
        for (std::size_t i = 0; i < held.size(); ++i) {
            struct stat st{};
            if (::fstat(held[i]->fd, &st) || !S_ISDIR(st.st_mode) ||
                (st.st_uid != 0 && st.st_uid != ::geteuid()) ||
                ((st.st_mode & 0022) && !(st.st_mode & S_ISVTX)))
                throw std::runtime_error("unsafe BA2 read directory");
            if (i) {
                struct stat linked{};
                if (::fstatat(held[i - 1]->fd, names[i - 1].c_str(), &linked, AT_SYMLINK_NOFOLLOW) ||
                    !S_ISDIR(linked.st_mode) || linked.st_dev != st.st_dev || linked.st_ino != st.st_ino)
                    throw std::runtime_error("BA2 read directory replaced");
            }
        }
    }
    void append(const std::string& name) {
        if (held.size() >= 129) throw std::runtime_error("BA2 source path too deep");
        std::shared_ptr<Handle> next;
        try { next = std::make_shared<Handle>(held.back()->fd, name.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW); }
        catch (const std::system_error& error) {
            if (error.code().value() != ENOENT) throw;
            verify();
            struct stat entry{};
            if (::fstatat(held.back()->fd, name.c_str(), &entry, AT_SYMLINK_NOFOLLOW) == 0 || errno != ENOENT)
                throw std::runtime_error("BA2 read directory changed");
            verify();
            throw ReadDirectoryAbsent("BA2 read directory is absent");
        }
        names.push_back(name);
        held.push_back(std::move(next));
        verify();
    }
};

bool same(const struct stat& a, const struct stat& b) {
    return S_ISREG(b.st_mode) && b.st_nlink == 1 &&
        (b.st_uid == 0 || b.st_uid == ::geteuid()) && !(b.st_mode & 0022) &&
        a.st_dev == b.st_dev && a.st_ino == b.st_ino && a.st_size == b.st_size &&
        a.st_mtim.tv_sec == b.st_mtim.tv_sec && a.st_mtim.tv_nsec == b.st_mtim.tv_nsec &&
        a.st_ctim.tv_sec == b.st_ctim.tv_sec && a.st_ctim.tv_nsec == b.st_ctim.tv_nsec;
}

class Source final : public fo4::archive::Ba2ByteSource {
public:
    Source(Directories dirs, std::string name, std::uint64_t maxBytes)
        : dirs_(std::move(dirs)), name_(std::move(name)),
          file_(dirs_.held.back()->fd, name_.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK) {
        if (::fstat(file_.fd, &initial_) || initial_.st_size < 0 || !same(initial_, initial_) ||
            static_cast<std::uint64_t>(initial_.st_size) > maxBytes)
            throw std::runtime_error("unsafe or oversized BA2 source");
        verify();
    }
    std::uint64_t size() const noexcept override { return static_cast<std::uint64_t>(initial_.st_size); }
    void read(std::uint64_t offset, std::span<std::byte> out) const override {
        if (offset > size() || out.size() > size() - offset)
            throw std::runtime_error("BA2 read outside source");
#ifdef FO4IDE_TEST_HOOKS
        if (const auto hook = readHook.load()) hook(offset, out.size(), size());
#endif
        verify();
        while (!out.empty()) {
            const auto count = std::min<std::size_t>(out.size(), 1024 * 1024);
            const auto got = ::pread(file_.fd, out.data(), count, static_cast<off_t>(offset));
            if (got < 0 && errno == EINTR) continue;
            if (got <= 0) throw std::runtime_error("BA2 source read failed");
            out = out.subspan(static_cast<std::size_t>(got));
            offset += static_cast<std::uint64_t>(got);
        }
        verify();
    }
private:
    void verify() const {
        dirs_.verify();
        struct stat held{}, linked{};
        if (::fstat(file_.fd, &held) || !same(initial_, held) ||
            ::fstatat(dirs_.held.back()->fd, name_.c_str(), &linked, AT_SYMLINK_NOFOLLOW) || !same(initial_, linked))
            throw std::runtime_error("BA2 source changed");
    }
    Directories dirs_;
    std::string name_;
    Handle file_;
    struct stat initial_{};
};
#endif
}

struct Ba2ReadRoot::Impl {
    std::filesystem::path root;
#ifdef __linux__
    Directories directories;
#endif
};

Ba2ReadRoot Ba2ReadRoot::authorize(const std::filesystem::path& root) {
#ifdef __linux__
    if (!root.is_absolute() || root != root.lexically_normal()) throw std::runtime_error("BA2 read root must be normalized and absolute");
    const auto names = components(root.relative_path());
    auto impl = std::make_shared<Impl>();
    impl->root = root;
    impl->directories.held.push_back(std::make_shared<Handle>(AT_FDCWD, "/", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    impl->directories.verify();
    for (const auto& name : names) impl->directories.append(name);
    return Ba2ReadRoot(std::move(impl));
#else
    (void)root;
    throw std::runtime_error("held BA2 read sources require Linux");
#endif
}

const std::filesystem::path& Ba2ReadRoot::path() const noexcept { return impl_->root; }

bool Ba2ReadRoot::sameRoot(const Ba2ReadRoot& other) const {
#ifdef __linux__
    impl_->directories.verify();
    other.impl_->directories.verify();
    struct stat first{}, second{};
    if (::fstat(impl_->directories.held.back()->fd, &first) ||
        ::fstat(other.impl_->directories.held.back()->fd, &second))
        throw std::runtime_error("cannot inspect BA2 read roots");
    impl_->directories.verify();
    other.impl_->directories.verify();
    return first.st_dev == second.st_dev && first.st_ino == second.st_ino;
#else
    (void)other;
    throw std::runtime_error("held BA2 read sources require Linux");
#endif
}

std::shared_ptr<const fo4::archive::Ba2ByteSource> Ba2ReadRoot::open(
    const std::filesystem::path& relative, std::uint64_t maxBytes) const {
#ifdef __linux__
    if (!impl_ || relative.is_absolute()) throw std::runtime_error("invalid BA2 read authority");
    auto names = components(relative);
    if (names.empty()) throw std::runtime_error("missing BA2 source filename");
    auto dirs = impl_->directories;
    dirs.verify();
    for (std::size_t i = 0; i + 1 < names.size(); ++i) dirs.append(names[i]);
    return std::make_shared<Source>(std::move(dirs), std::move(names.back()), maxBytes);
#else
    (void)relative;
    (void)maxBytes;
    throw std::runtime_error("held BA2 read sources require Linux");
#endif
}

std::vector<std::string> Ba2ReadRoot::list(const std::filesystem::path& relativeDirectory,
                                         std::size_t maxEntries) const {
#ifdef __linux__
    if (!impl_ || relativeDirectory.is_absolute()) throw std::runtime_error("invalid BA2 read authority");
    auto dirs = impl_->directories;
    dirs.verify();
    for (const auto& name : components(relativeDirectory)) dirs.append(name);
    Handle scan(dirs.held.back()->fd, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    std::unique_ptr<DIR, decltype(&::closedir)> stream(::fdopendir(scan.fd), ::closedir);
    if (!stream) throw std::runtime_error("cannot enumerate BA2 source directory");
    scan.fd = -1;
    struct stat before{}, after{};
    if (::fstat(::dirfd(stream.get()), &before)) throw std::runtime_error("cannot inspect BA2 source directory");
    std::vector<std::string> names;
    while (true) {
        errno = 0;
        const auto entry = ::readdir(stream.get());
        if (!entry) {
            if (errno) throw std::runtime_error("cannot enumerate BA2 source directory");
            break;
        }
        const std::string_view name(entry->d_name);
        if (name == "." || name == "..") continue;
        if (names.size() == maxEntries) throw std::runtime_error("BA2 source directory entry limit exceeded");
        names.emplace_back(name);
    }
    if (::fstat(::dirfd(stream.get()), &after) ||
        before.st_dev != after.st_dev || before.st_ino != after.st_ino ||
        before.st_mtim.tv_sec != after.st_mtim.tv_sec || before.st_mtim.tv_nsec != after.st_mtim.tv_nsec ||
        before.st_ctim.tv_sec != after.st_ctim.tv_sec || before.st_ctim.tv_nsec != after.st_ctim.tv_nsec)
        throw std::runtime_error("BA2 source directory changed during enumeration");
    dirs.verify();
    std::sort(names.begin(), names.end());
    if (std::adjacent_find(names.begin(), names.end()) != names.end())
        throw std::runtime_error("BA2 source directory returned duplicate entries");
    return names;
#else
    (void)relativeDirectory;
    (void)maxEntries;
    throw std::runtime_error("held BA2 read sources require Linux");
#endif
}

std::optional<std::vector<std::string>> Ba2ReadRoot::listOptional(const std::filesystem::path& relativeDirectory,
                                                                   std::size_t maxEntries) const {
#ifdef __linux__
    try { return list(relativeDirectory, maxEntries); }
    catch (const ReadDirectoryAbsent&) { return std::nullopt; }
#else
    (void)relativeDirectory;
    (void)maxEntries;
    throw std::runtime_error("held BA2 read sources require Linux");
#endif
}
}
