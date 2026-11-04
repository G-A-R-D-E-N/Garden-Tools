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

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winternl.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#endif

namespace fo4ide::plugin::detail {
#ifdef FO4IDE_TEST_HOOKS
namespace { std::atomic<Ba2ReadHook> readHook{nullptr}; }
void setBa2ReadHook(Ba2ReadHook hook) { readHook.store(hook); }
#endif
namespace {
#if defined(__linux__) || defined(_WIN32)
class ReadDirectoryAbsent : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
#endif

#ifdef __linux__
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
#elif defined(_WIN32)
struct Handle {
    HANDLE handle;
    explicit Handle(HANDLE value) : handle(value) {}
    ~Handle() { ::CloseHandle(handle); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};

HANDLE openChild(HANDLE parent, std::wstring name, ACCESS_MASK access, ULONG options) {
    UNICODE_STRING component{};
    component.Length = static_cast<USHORT>(name.size() * sizeof(wchar_t));
    component.MaximumLength = component.Length;
    component.Buffer = name.data();
    OBJECT_ATTRIBUTES attributes{};
    attributes.Length = sizeof(attributes);
    attributes.RootDirectory = parent;
    attributes.ObjectName = &component;
    attributes.Attributes = OBJ_CASE_INSENSITIVE;
    HANDLE handle = nullptr;
    IO_STATUS_BLOCK ioStatus{};
    const NTSTATUS status = NtCreateFile(&handle, access | FILE_READ_ATTRIBUTES | SYNCHRONIZE, &attributes, &ioStatus,
                                         nullptr, FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ, FILE_OPEN,
                                         options | FILE_SYNCHRONOUS_IO_NONALERT, nullptr, 0);
    if (status < 0) throw std::system_error(static_cast<int>(RtlNtStatusToDosError(status)), std::system_category(), "cannot open BA2 read source");
    return handle;
}

HANDLE openBase(const std::filesystem::path& base) {
    const HANDLE handle = ::CreateFileW(base.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) throw std::system_error(static_cast<int>(::GetLastError()), std::system_category(), "cannot open BA2 read root");
    return handle;
}

struct Identity {
    std::uint64_t volume{};
    std::array<std::byte, 16> file{};
    bool operator==(const Identity&) const = default;
};

struct Snapshot {
    Identity identity{};
    std::int64_t size{};
    std::int64_t written{};
    std::int64_t changed{};
    DWORD attributes{};
    DWORD links{};
};

Snapshot inspect(HANDLE handle) {
    FILE_ID_INFO id{};
    FILE_BASIC_INFO basic{};
    FILE_STANDARD_INFO standard{};
    if (!::GetFileInformationByHandleEx(handle, FileIdInfo, &id, sizeof(id)) ||
        !::GetFileInformationByHandleEx(handle, FileBasicInfo, &basic, sizeof(basic)) ||
        !::GetFileInformationByHandleEx(handle, FileStandardInfo, &standard, sizeof(standard)))
        throw std::system_error(static_cast<int>(::GetLastError()), std::system_category(), "cannot inspect BA2 read source");
    Snapshot result{};
    result.identity.volume = id.VolumeSerialNumber;
    std::memcpy(result.identity.file.data(), id.FileId.Identifier, sizeof(id.FileId.Identifier));
    result.size = standard.EndOfFile.QuadPart;
    result.written = basic.LastWriteTime.QuadPart;
    result.changed = basic.ChangeTime.QuadPart;
    result.attributes = basic.FileAttributes;
    result.links = standard.NumberOfLinks;
    return result;
}

bool identical(const Snapshot& a, const Snapshot& b) {
    return a.identity == b.identity && a.size == b.size && a.written == b.written && a.changed == b.changed;
}

bool regularFile(const Snapshot& s) {
    return (s.attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0 && s.links == 1;
}

bool plainDirectory(const Snapshot& s) {
    return (s.attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == FILE_ATTRIBUTE_DIRECTORY;
}

std::vector<std::wstring> components(const std::filesystem::path& path) {
    std::vector<std::wstring> result;
    for (const auto& part : path) {
        auto name = part.wstring();
        if (name.empty() || name == L"." || name == L".." || name.size() > 255 ||
            name.find_first_of(L"/\\:") != name.npos || name.find(L'\0') != name.npos || result.size() == 128)
            throw std::runtime_error("invalid BA2 source path");
        result.push_back(std::move(name));
    }
    return result;
}

struct Directories {
    std::filesystem::path base{};
    std::vector<std::shared_ptr<Handle>> held{};
    std::vector<std::wstring> names{};
    void verify() const {
        for (const auto& directory : held) {
            if (!plainDirectory(inspect(directory->handle))) throw std::runtime_error("unsafe BA2 read directory");
        }
    }
    void append(const std::wstring& name) {
        if (held.size() >= 129) throw std::runtime_error("BA2 source path too deep");
        std::shared_ptr<Handle> next;
        try { next = std::make_shared<Handle>(openChild(held.back()->handle, name, FILE_LIST_DIRECTORY, FILE_DIRECTORY_FILE | FILE_OPEN_REPARSE_POINT)); }
        catch (const std::system_error& error) {
            if (error.code().value() != ERROR_FILE_NOT_FOUND && error.code().value() != ERROR_PATH_NOT_FOUND) throw;
            verify();
            throw ReadDirectoryAbsent("BA2 read directory is absent");
        }
        names.push_back(name);
        held.push_back(std::move(next));
        verify();
    }
};

std::string utf8Name(std::wstring_view wide) {
    const auto size = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0 || wide.find(L'\0') != wide.npos) throw std::runtime_error("BA2 source directory entry name is invalid");
    std::string result(static_cast<std::size_t>(size), '\0');
    if (::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), static_cast<int>(wide.size()), result.data(), size, nullptr, nullptr) != size)
        throw std::runtime_error("BA2 source directory entry name is invalid");
    return result;
}

std::wstring finalPath(HANDLE handle) {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const auto length = ::GetFinalPathNameByHandleW(handle, path.data(), static_cast<DWORD>(path.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        if (length == 0) throw std::system_error(static_cast<int>(::GetLastError()), std::system_category(), "cannot resolve BA2 read directory");
        if (length < path.size()) return std::wstring(path.data(), length);
        path.resize(length);
    }
}

std::vector<std::string> listDirectory(const Directories& dirs, std::size_t maxEntries) {
    const HANDLE directory = dirs.held.back()->handle;
    const auto before = inspect(directory);
    if (!plainDirectory(before)) throw std::runtime_error("unsafe BA2 read directory");
    const auto path = finalPath(directory);
    std::vector<std::string> names;
    for (const auto& entry : std::filesystem::directory_iterator(path)) {
        const auto name = entry.path().filename().wstring();
        if (name == L"." || name == L"..") continue;
        if (names.size() == maxEntries) throw std::runtime_error("BA2 source directory entry limit exceeded");
        names.push_back(utf8Name(name));
    }
    const auto after = inspect(directory);
    const Handle resolved(openBase(path));
    const auto reopened = inspect(resolved.handle);
    if (!identical(before, after) || !plainDirectory(after) || reopened.identity != before.identity || !plainDirectory(reopened))
        throw std::runtime_error("BA2 source directory changed during enumeration");
    dirs.verify();
    std::sort(names.begin(), names.end());
    if (std::adjacent_find(names.begin(), names.end()) != names.end()) throw std::runtime_error("BA2 source directory returned duplicate entries");
    return names;
}

class Source final : public fo4::archive::Ba2ByteSource {
public:
    Source(Directories dirs, std::wstring name, std::uint64_t maxBytes)
        : dirs_(std::move(dirs)), name_(std::move(name)),
          file_(openChild(dirs_.held.back()->handle, name_, FILE_READ_DATA, FILE_NON_DIRECTORY_FILE | FILE_OPEN_REPARSE_POINT)) {
        initial_ = inspect(file_.handle);
        if (!regularFile(initial_) || initial_.size < 0 || static_cast<std::uint64_t>(initial_.size) > maxBytes)
            throw std::runtime_error("unsafe or oversized BA2 source");
        verify();
    }
    std::uint64_t size() const noexcept override { return static_cast<std::uint64_t>(initial_.size); }
    void read(std::uint64_t offset, std::span<std::byte> out) const override {
        if (offset > size() || out.size() > size() - offset)
            throw std::runtime_error("BA2 read outside source");
#ifdef FO4IDE_TEST_HOOKS
        if (const auto hook = readHook.load()) hook(offset, out.size(), size());
#endif
        verify();
        while (!out.empty()) {
            const auto count = static_cast<DWORD>(std::min<std::size_t>(out.size(), 1024 * 1024));
            OVERLAPPED position{};
            position.Offset = static_cast<DWORD>(offset);
            position.OffsetHigh = static_cast<DWORD>(offset >> 32);
            DWORD got = 0;
            if (!::ReadFile(file_.handle, out.data(), count, &got, &position) || got == 0)
                throw std::runtime_error("BA2 source read failed");
            out = out.subspan(static_cast<std::size_t>(got));
            offset += got;
        }
        verify();
    }
private:
    void verify() const {
        dirs_.verify();
        const auto current = inspect(file_.handle);
        if (!identical(initial_, current) || !regularFile(current)) throw std::runtime_error("BA2 source changed");
    }
    Directories dirs_;
    std::wstring name_;
    Handle file_;
    Snapshot initial_{};
};
#endif
}

struct Ba2ReadRoot::Impl {
    std::filesystem::path root;
#if defined(__linux__) || defined(_WIN32)
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
#elif defined(_WIN32)
    if (!root.is_absolute() || root != root.lexically_normal()) throw std::runtime_error("BA2 read root must be normalized and absolute");
    const auto names = components(root.relative_path());
    auto impl = std::make_shared<Impl>();
    impl->root = root;
    impl->directories.base = root.root_path();
    impl->directories.held.push_back(std::make_shared<Handle>(openBase(impl->directories.base)));
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
#elif defined(_WIN32)
    impl_->directories.verify();
    other.impl_->directories.verify();
    const auto first = inspect(impl_->directories.held.back()->handle);
    const auto second = inspect(other.impl_->directories.held.back()->handle);
    impl_->directories.verify();
    other.impl_->directories.verify();
    return first.identity == second.identity;
#else
    (void)other;
    throw std::runtime_error("held BA2 read sources require Linux");
#endif
}

std::shared_ptr<const fo4::archive::Ba2ByteSource> Ba2ReadRoot::open(
    const std::filesystem::path& relative, std::uint64_t maxBytes) const {
#if defined(__linux__) || defined(_WIN32)
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
    std::unique_ptr<DIR, int (*)(DIR*)> stream(::fdopendir(scan.fd), ::closedir);
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
#elif defined(_WIN32)
    if (!impl_ || relativeDirectory.is_absolute()) throw std::runtime_error("invalid BA2 read authority");
    auto dirs = impl_->directories;
    dirs.verify();
    for (const auto& name : components(relativeDirectory)) dirs.append(name);
    return listDirectory(dirs, maxEntries);
#else
    (void)relativeDirectory;
    (void)maxEntries;
    throw std::runtime_error("held BA2 read sources require Linux");
#endif
}

std::optional<std::vector<std::string>> Ba2ReadRoot::listOptional(const std::filesystem::path& relativeDirectory,
                                                                   std::size_t maxEntries) const {
#if defined(__linux__) || defined(_WIN32)
    try { return list(relativeDirectory, maxEntries); }
    catch (const ReadDirectoryAbsent&) { return std::nullopt; }
#else
    (void)relativeDirectory;
    (void)maxEntries;
    throw std::runtime_error("held BA2 read sources require Linux");
#endif
}
}
