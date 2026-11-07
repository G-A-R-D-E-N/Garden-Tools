#include "RadiclePluginWindowsJournalDirectory.hpp"
#include "RadiclePluginWindowsJournalNative.hpp"
#include "RadiclePluginJournalRoot.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <aclapi.h>
#include <winternl.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <system_error>

namespace fo4ide::plugin::detail {
namespace {
#ifdef FO4IDE_TEST_HOOKS
WindowsJournalReplacementHook replacementHook = nullptr;
#endif
struct HandleCloser {
    void operator()(void* handle) const noexcept {
        if (handle != nullptr && handle != INVALID_HANDLE_VALUE) ::CloseHandle(handle);
    }
};
using Handle = std::unique_ptr<void, HandleCloser>;
using LocalMemory = std::unique_ptr<void, decltype(&::LocalFree)>;

[[noreturn]] void fail(const char* operation) {
    throw std::system_error(static_cast<int>(::GetLastError()), std::system_category(), operation);
}

std::wstring checkedName(std::string_view name) {
    if (name.empty() || name == "." || name == ".." || name.size() > 200 || name.back() == '.' ||
        name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-") != name.npos) {
        throw std::runtime_error("invalid journal file name");
    }
    auto base = std::string(name.substr(0, name.find('.')));
    for (auto& ch : base) if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - ('a' - 'A'));
    if (base == "CON" || base == "PRN" || base == "AUX" || base == "NUL" ||
        (base.size() == 4 && (base.starts_with("COM") || base.starts_with("LPT")) && base[3] >= '1' && base[3] <= '9')) {
        throw std::runtime_error("reserved journal file name");
    }
    return {name.begin(), name.end()};
}

LocalMemory requirePrivate(HANDLE handle, bool directory) {
    BY_HANDLE_FILE_INFORMATION info{};
    if (!::GetFileInformationByHandle(handle, &info) || ::GetFileType(handle) != FILE_TYPE_DISK ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
        ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) != directory ||
        (!directory && info.nNumberOfLinks != 1)) {
        throw std::runtime_error("journal object is not a private regular file or directory");
    }
    HANDLE rawToken = nullptr;
    if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &rawToken)) fail("open journal user token");
    Handle token(rawToken);
    DWORD length = 0;
    ::GetTokenInformation(token.get(), TokenUser, nullptr, 0, &length);
    if (length == 0 || ::GetLastError() != ERROR_INSUFFICIENT_BUFFER) fail("size journal user token");
    std::vector<std::uintptr_t> userBytes((length + sizeof(std::uintptr_t) - 1) / sizeof(std::uintptr_t));
    if (!::GetTokenInformation(token.get(), TokenUser, userBytes.data(), length, &length)) fail("read journal user token");
    const auto user = reinterpret_cast<TOKEN_USER*>(userBytes.data())->User.Sid;
    PSID owner = nullptr;
    PACL dacl = nullptr;
    PSECURITY_DESCRIPTOR raw = nullptr;
    const auto error = ::GetSecurityInfo(handle, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
        &owner, nullptr, &dacl, nullptr, &raw);
    if (error != ERROR_SUCCESS) throw std::system_error(static_cast<int>(error), std::system_category(), "read journal ACL");
    LocalMemory security(raw, ::LocalFree);
    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD revision = 0;
    ACL_SIZE_INFORMATION aclInfo{};
    if (owner == nullptr || !::EqualSid(owner, user) || dacl == nullptr ||
        !::GetSecurityDescriptorControl(raw, &control, &revision) ||
        (directory && (control & SE_DACL_PROTECTED) == 0) ||
        !::GetAclInformation(dacl, &aclInfo, sizeof(aclInfo), AclSizeInformation) || aclInfo.AceCount != 1) {
        throw std::runtime_error("journal ACL is not private");
    }
    void* rawAce = nullptr;
    if (!::GetAce(dacl, 0, &rawAce)) fail("read journal ACL entry");
    const auto* ace = static_cast<const ACCESS_ALLOWED_ACE*>(rawAce);
    if (ace->Header.AceType != ACCESS_ALLOWED_ACE_TYPE ||
        (directory && (ace->Header.AceFlags & (OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE)) !=
            (OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE)) ||
        (directory && (ace->Header.AceFlags & (NO_PROPAGATE_INHERIT_ACE | INHERITED_ACE)) != 0) ||
        (ace->Header.AceFlags & INHERIT_ONLY_ACE) != 0 || (ace->Mask & FILE_ALL_ACCESS) != FILE_ALL_ACCESS ||
        !::EqualSid(const_cast<DWORD*>(&ace->SidStart), user)) {
        throw std::runtime_error("journal ACL is not private");
    }
    return security;
}

Handle openFile(HANDLE directory, std::string_view name, ACCESS_MASK access, ULONG disposition, ULONG sharing = FILE_SHARE_READ) {
    auto wide = checkedName(name);
    auto security = requirePrivate(directory, true);
    UNICODE_STRING component{};
    component.Length = static_cast<USHORT>(wide.size() * sizeof(wchar_t));
    component.MaximumLength = component.Length;
    component.Buffer = wide.data();
    OBJECT_ATTRIBUTES attributes{};
    attributes.Length = sizeof(attributes);
    attributes.RootDirectory = directory;
    attributes.ObjectName = &component;
    attributes.Attributes = OBJ_CASE_INSENSITIVE;
    attributes.SecurityDescriptor = disposition == FILE_OPEN ? nullptr : security.get();
    IO_STATUS_BLOCK block{};
    HANDLE raw = nullptr;
    const auto status = ::NtCreateFile(&raw, access | READ_CONTROL | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
        &attributes, &block, nullptr, FILE_ATTRIBUTE_NORMAL, sharing, disposition,
        FILE_OPEN_REPARSE_POINT | FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT, nullptr, 0);
    if (status < 0) {
        throw std::system_error(static_cast<int>(::RtlNtStatusToDosError(status)), std::system_category(), "open held journal file");
    }
    return Handle(raw);
}

void removeHandle(HANDLE handle) {
    FILE_DISPOSITION_INFO disposition{};
    disposition.DeleteFile = TRUE;
    if (!::SetFileInformationByHandle(handle, FileDispositionInfo, &disposition, sizeof(disposition))) fail("remove held journal file");
}

WindowsJournalFileIdentity fileIdentity(HANDLE file) {
    FILE_ID_INFO info{};
    if (!::GetFileInformationByHandleEx(file, FileIdInfo, &info, sizeof(info))) fail("read journal file identity");
    WindowsJournalFileIdentity identity;
    identity.volume = info.VolumeSerialNumber;
    std::memcpy(identity.file.data(), info.FileId.Identifier, identity.file.size());
    return identity;
}

std::vector<std::byte> readHandle(HANDLE file, std::size_t maxBytes) {
    LARGE_INTEGER size{};
    if (!::GetFileSizeEx(file, &size)) fail("size journal file");
    if (size.QuadPart < 0 || static_cast<unsigned long long>(size.QuadPart) > maxBytes ||
        static_cast<unsigned long long>(size.QuadPart) > std::numeric_limits<std::size_t>::max()) {
        throw std::runtime_error("journal file exceeds size bound");
    }
    LARGE_INTEGER start{};
    if (!::SetFilePointerEx(file, start, nullptr, FILE_BEGIN)) fail("seek journal file");
    std::vector<std::byte> bytes(static_cast<std::size_t>(size.QuadPart));
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto chunk = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - offset, MAXDWORD));
        DWORD read = 0;
        if (!::ReadFile(file, bytes.data() + offset, chunk, &read, nullptr)) fail("read journal file");
        if (read == 0) throw std::runtime_error("short journal read");
        offset += read;
    }
    return bytes;
}

void requireSnapshot(HANDLE file, const WindowsJournalFileSnapshot& expected) {
    static_cast<void>(requirePrivate(file, false));
    if (fileIdentity(file) != expected.identity || readHandle(file, expected.bytes.size()) != expected.bytes) {
        throw std::runtime_error("journal replacement identity or content changed");
    }
}

void renameHandle(HANDLE directory, HANDLE file, const std::wstring& destination) {
    static_cast<void>(requirePrivate(directory, true));
    const auto length = sizeof(WindowsJournalRenameInfo) + destination.size() * sizeof(wchar_t);
    std::vector<std::uintptr_t> storage((length + sizeof(std::uintptr_t) - 1) / sizeof(std::uintptr_t));
    auto* rename = reinterpret_cast<WindowsJournalRenameInfo*>(storage.data());
    rename->ReplaceIfExists = FALSE;
    rename->RootDirectory = directory;
    rename->FileNameLength = static_cast<DWORD>(destination.size() * sizeof(wchar_t));
    std::memcpy(rename->FileName, destination.c_str(), (destination.size() + 1) * sizeof(wchar_t));
    IO_STATUS_BLOCK block{};
    const auto status = NtSetInformationFile(file, &block, rename, static_cast<ULONG>(length), WindowsJournalFileInformationClass::Rename);
    if (status < 0) {
        throw std::system_error(static_cast<int>(::RtlNtStatusToDosError(status)), std::system_category(), "publish held journal file");
    }
}
}

#ifdef FO4IDE_TEST_HOOKS
void setWindowsJournalReplacementHook(WindowsJournalReplacementHook hook) { replacementHook = hook; }
#endif

void validateWindowsJournalFileName(std::string_view name) { static_cast<void>(checkedName(name)); }

WindowsJournalDirectory::WindowsJournalDirectory(void* directory) {
    HANDLE raw = nullptr;
    if (!::DuplicateHandle(::GetCurrentProcess(), directory, ::GetCurrentProcess(), &raw, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
        fail("duplicate journal directory");
    }
    Handle held(raw);
    static_cast<void>(requirePrivate(held.get(), true));
    directory_ = held.release();
}

WindowsJournalDirectory::~WindowsJournalDirectory() { ::CloseHandle(directory_); }

void WindowsJournalDirectory::writeFile(std::string_view name, std::span<const std::byte> bytes) const {
    auto file = openFile(directory_, name, GENERIC_WRITE | DELETE, FILE_CREATE);
    try {
        static_cast<void>(requirePrivate(file.get(), false));
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            const auto chunk = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - offset, MAXDWORD));
            DWORD written = 0;
            if (!::WriteFile(file.get(), bytes.data() + offset, chunk, &written, nullptr)) fail("write journal file");
            if (written == 0) throw std::runtime_error("short journal write");
            offset += written;
        }
        if (!::FlushFileBuffers(file.get())) fail("flush journal file");
    } catch (...) {
        FILE_DISPOSITION_INFO disposition{};
        disposition.DeleteFile = TRUE;
        ::SetFileInformationByHandle(file.get(), FileDispositionInfo, &disposition, sizeof(disposition));
        throw;
    }
}

std::vector<std::byte> WindowsJournalDirectory::readFile(std::string_view name, std::size_t maxBytes) const {
    auto file = openFile(directory_, name, GENERIC_READ, FILE_OPEN);
    static_cast<void>(requirePrivate(file.get(), false));
    return readHandle(file.get(), maxBytes);
}

WindowsJournalFileSnapshot WindowsJournalDirectory::snapshotFile(std::string_view name, std::size_t maxBytes) const {
    auto file = openFile(directory_, name, GENERIC_READ, FILE_OPEN);
    static_cast<void>(requirePrivate(file.get(), false));
    return {fileIdentity(file.get()), readHandle(file.get(), maxBytes)};
}

std::optional<WindowsJournalFileSnapshot> WindowsJournalDirectory::trySnapshotFile(std::string_view name, std::size_t maxBytes) const {
    Handle file;
    try { file = openFile(directory_, name, GENERIC_READ, FILE_OPEN); }
    catch (const std::system_error& error) {
        if (error.code().category() == std::system_category() && error.code().value() == ERROR_FILE_NOT_FOUND) return std::nullopt;
        throw;
    }
    static_cast<void>(requirePrivate(file.get(), false));
    return WindowsJournalFileSnapshot{fileIdentity(file.get()), readHandle(file.get(), maxBytes)};
}

WindowsJournalFileIdentity WindowsJournalDirectory::directoryIdentity() const {
    static_cast<void>(requirePrivate(directory_, true));
    return fileIdentity(directory_);
}

std::shared_ptr<void> WindowsJournalDirectory::lockReplacement() const {
    return lockFile(".windows-replacement.lock");
}

std::shared_ptr<void> WindowsJournalDirectory::lockFile(std::string_view name) const {
    auto file = openFile(directory_, name, GENERIC_READ | GENERIC_WRITE, FILE_OPEN_IF,
        FILE_SHARE_READ | FILE_SHARE_WRITE);
    static_cast<void>(requirePrivate(file.get(), false));
    OVERLAPPED overlap{};
    if (!::LockFileEx(file.get(), LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, MAXDWORD, MAXDWORD, &overlap)) {
        fail("lock journal file");
    }
    return std::shared_ptr<void>(file.release(), HandleCloser{});
}

std::vector<std::string> WindowsJournalDirectory::listFiles(std::size_t maxEntries) const {
    if (maxEntries > kJournalMaxCleanupEntries) throw std::runtime_error("journal enumeration limit exceeds the maximum");
    static_cast<void>(requirePrivate(directory_, true));
    alignas(FILE_ID_BOTH_DIR_INFO) std::array<std::byte, 16384> buffer{};
    std::vector<std::string> names;
    auto infoClass = FileIdBothDirectoryRestartInfo;
    for (;;) {
        if (!::GetFileInformationByHandleEx(directory_, infoClass, buffer.data(), static_cast<DWORD>(buffer.size()))) {
            if (::GetLastError() == ERROR_NO_MORE_FILES) break;
            fail("enumerate held journal directory");
        }
        infoClass = FileIdBothDirectoryInfo;
        std::size_t offset = 0;
        for (;;) {
            constexpr auto header = offsetof(FILE_ID_BOTH_DIR_INFO, FileName);
            if (offset > buffer.size() - header || offset % alignof(FILE_ID_BOTH_DIR_INFO) != 0) {
                throw std::runtime_error("invalid journal directory entry");
            }
            const auto* entry = reinterpret_cast<const FILE_ID_BOTH_DIR_INFO*>(buffer.data() + offset);
            if (entry->FileNameLength % sizeof(wchar_t) != 0 || entry->FileNameLength > buffer.size() - offset - header) {
                throw std::runtime_error("invalid journal directory name length");
            }
            const std::wstring_view wide(entry->FileName, entry->FileNameLength / sizeof(wchar_t));
            if (wide != L"." && wide != L"..") {
                if (names.size() >= maxEntries) throw std::runtime_error("too many journal entries");
                if ((entry->FileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0) {
                    throw std::runtime_error("unexpected journal entry type");
                }
                std::string name;
                for (const auto ch : wide) {
                    if (ch > 127) throw std::runtime_error("invalid journal file name");
                    name.push_back(static_cast<char>(ch));
                }
                auto file = openFile(directory_, name, FILE_READ_ATTRIBUTES, FILE_OPEN);
                static_cast<void>(requirePrivate(file.get(), false));
                names.push_back(std::move(name));
            }
            if (entry->NextEntryOffset == 0) break;
            if (entry->NextEntryOffset < header + entry->FileNameLength || entry->NextEntryOffset > buffer.size() - offset) {
                throw std::runtime_error("invalid journal directory entry offset");
            }
            offset += entry->NextEntryOffset;
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

void WindowsJournalDirectory::publishFile(std::string_view staged, std::string_view published) const {
    const auto source = checkedName(staged);
    const auto destination = checkedName(published);
    if (::_wcsicmp(source.c_str(), destination.c_str()) == 0) throw std::runtime_error("journal publication names must differ");
    auto file = openFile(directory_, staged, DELETE, FILE_OPEN);
    static_cast<void>(requirePrivate(file.get(), false));
    renameHandle(directory_, file.get(), destination);
}

void WindowsJournalDirectory::publishSnapshot(std::string_view staged, const WindowsJournalFileSnapshot& expected,
                                             std::string_view published) const {
    const auto source = checkedName(staged);
    const auto destination = checkedName(published);
    if (::_wcsicmp(source.c_str(), destination.c_str()) == 0) throw std::runtime_error("journal publication names must differ");
    auto file = openFile(directory_, staged, GENERIC_READ | DELETE, FILE_OPEN);
    requireSnapshot(file.get(), expected);
    renameHandle(directory_, file.get(), destination);
}

void WindowsJournalDirectory::removeFile(std::string_view name) const {
    auto file = openFile(directory_, name, DELETE, FILE_OPEN);
    static_cast<void>(requirePrivate(file.get(), false));
    removeHandle(file.get());
}
void WindowsJournalDirectory::removeSnapshot(std::string_view name, const WindowsJournalFileSnapshot& expected) const {
    auto file = openFile(directory_, name, GENERIC_READ | DELETE, FILE_OPEN);
    requireSnapshot(file.get(), expected);
    removeHandle(file.get());
}

void WindowsJournalDirectory::replaceFileRecoverably(std::string_view destination, const WindowsJournalFileSnapshot& original,
    std::string_view staged, const WindowsJournalFileSnapshot& replacement, std::string_view backup) const {
    const auto targetName = checkedName(destination);
    const auto stagedName = checkedName(staged);
    const auto backupName = checkedName(backup);
    if (::_wcsicmp(targetName.c_str(), stagedName.c_str()) == 0 ||
        ::_wcsicmp(targetName.c_str(), backupName.c_str()) == 0 || ::_wcsicmp(stagedName.c_str(), backupName.c_str()) == 0) {
        throw std::runtime_error("journal replacement names must differ");
    }
    auto target = openFile(directory_, destination, GENERIC_READ | DELETE, FILE_OPEN);
    auto stage = openFile(directory_, staged, GENERIC_READ | DELETE, FILE_OPEN);
    requireSnapshot(target.get(), original);
    requireSnapshot(stage.get(), replacement);
#ifdef FO4IDE_TEST_HOOKS
    if (replacementHook) replacementHook(WindowsJournalReplacementStep::validated);
#endif
    renameHandle(directory_, target.get(), backupName);
#ifdef FO4IDE_TEST_HOOKS
    if (replacementHook) replacementHook(WindowsJournalReplacementStep::originalBackedUp);
#endif
    requireSnapshot(target.get(), original);
    requireSnapshot(stage.get(), replacement);
    renameHandle(directory_, stage.get(), targetName);
#ifdef FO4IDE_TEST_HOOKS
    if (replacementHook) replacementHook(WindowsJournalReplacementStep::stagedPublished);
#endif
}

}
#endif
