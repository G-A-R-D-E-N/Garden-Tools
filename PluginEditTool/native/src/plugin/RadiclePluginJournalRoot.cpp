#include "RadiclePluginJournalRoot.hpp"

#include <algorithm>
#include <stdexcept>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <winternl.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace fo4ide::plugin::detail {

#if defined(FO4IDE_TEST_HOOKS) && !defined(_WIN32)
namespace { JournalRootSyncHook journalRootSyncHook = nullptr; }
void setJournalRootSyncHook(JournalRootSyncHook hook) { journalRootSyncHook = hook; }
#endif

#ifdef _WIN32
namespace {

struct HandleCloser {
    void operator()(void* value) const noexcept {
        if (value != nullptr && value != INVALID_HANDLE_VALUE) ::CloseHandle(value);
    }
};

using ScopedHandle = std::unique_ptr<void, HandleCloser>;
using LocalMemory = std::unique_ptr<void, decltype(&::LocalFree)>;

std::vector<std::uintptr_t> currentUser() {
    HANDLE rawToken = nullptr;
    if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &rawToken)) {
        throw std::system_error(static_cast<int>(::GetLastError()), std::system_category(), "open user token");
    }
    ScopedHandle token(rawToken);
    DWORD length = 0;
    ::GetTokenInformation(token.get(), TokenUser, nullptr, 0, &length);
    if (length == 0 || ::GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        throw std::system_error(static_cast<int>(::GetLastError()), std::system_category(), "size user token");
    }
    std::vector<std::uintptr_t> bytes((length + sizeof(std::uintptr_t) - 1) / sizeof(std::uintptr_t));
    if (!::GetTokenInformation(token.get(), TokenUser, bytes.data(), length, &length)) {
        throw std::system_error(static_cast<int>(::GetLastError()), std::system_category(), "read user token");
    }
    return bytes;
}

void requirePrivate(HANDLE handle, PSID user, bool directory) {
    PSID owner = nullptr;
    PACL dacl = nullptr;
    PSECURITY_DESCRIPTOR raw = nullptr;
    const auto error = ::GetSecurityInfo(handle, SE_FILE_OBJECT,
        OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &owner, nullptr, &dacl, nullptr, &raw);
    if (error != ERROR_SUCCESS) throw std::system_error(static_cast<int>(error), std::system_category(), "read journal ACL");
    LocalMemory descriptor(raw, ::LocalFree);
    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD revision = 0;
    ACL_SIZE_INFORMATION info{};
    if (owner == nullptr || !::EqualSid(owner, user) || dacl == nullptr ||
        !::GetSecurityDescriptorControl(raw, &control, &revision) ||
        (directory && (control & SE_DACL_PROTECTED) == 0) ||
        !::GetAclInformation(dacl, &info, static_cast<DWORD>(sizeof(info)), AclSizeInformation) || info.AceCount != 1) {
        throw std::runtime_error("journal directory ACL is not private");
    }
    void* rawAce = nullptr;
    if (!::GetAce(dacl, 0, &rawAce)) throw std::runtime_error("cannot read journal directory ACL");
    const auto* ace = static_cast<const ACCESS_ALLOWED_ACE*>(rawAce);
    if (ace->Header.AceType != ACCESS_ALLOWED_ACE_TYPE ||
        (directory && (ace->Header.AceFlags & (OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE)) !=
            (OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE)) ||
        (directory && (ace->Header.AceFlags & (NO_PROPAGATE_INHERIT_ACE | INHERITED_ACE)) != 0) ||
        (ace->Header.AceFlags & INHERIT_ONLY_ACE) != 0 ||
        (ace->Mask & FILE_ALL_ACCESS) != FILE_ALL_ACCESS ||
        !::EqualSid(const_cast<DWORD*>(&ace->SidStart), user)) {
        throw std::runtime_error("journal directory ACL is not private");
    }
}

bool trustedSid(PSID sid, PSID user) {
    static const LocalMemory installer([] {
        PSID value = nullptr;
        if (!::ConvertStringSidToSidW(L"S-1-5-80-956008885-3418522649-1831038044-1853292631-2271478464", &value)) {
            throw std::system_error(static_cast<int>(::GetLastError()), std::system_category(), "create TrustedInstaller SID");
        }
        return value;
    }(), ::LocalFree);
    return ::EqualSid(sid, user) || ::IsWellKnownSid(sid, WinLocalSystemSid) ||
        ::IsWellKnownSid(sid, WinBuiltinAdministratorsSid) || ::EqualSid(sid, installer.get());
}

void requireSafeAncestor(HANDLE handle, PSID user) {
    BY_HANDLE_FILE_INFORMATION info{};
    if (!::GetFileInformationByHandle(handle, &info) || ::GetFileType(handle) != FILE_TYPE_DISK ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        throw std::runtime_error("journal path ancestor is not a regular directory");
    }
    PSID owner = nullptr;
    PACL dacl = nullptr;
    PSECURITY_DESCRIPTOR raw = nullptr;
    const auto error = ::GetSecurityInfo(handle, SE_FILE_OBJECT,
        OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &owner, nullptr, &dacl, nullptr, &raw);
    if (error != ERROR_SUCCESS) throw std::system_error(static_cast<int>(error), std::system_category(), "read journal ancestor ACL");
    LocalMemory descriptor(raw, ::LocalFree);
    BOOL present = FALSE, defaulted = FALSE;
    ACL_SIZE_INFORMATION aclInfo{};
    if (owner == nullptr || !trustedSid(owner, user) ||
        !::GetSecurityDescriptorDacl(raw, &present, &dacl, &defaulted) || !present || dacl == nullptr ||
        !::GetAclInformation(dacl, &aclInfo, sizeof(aclInfo), AclSizeInformation)) {
        throw std::runtime_error("journal path ancestor ACL is not trusted");
    }
    constexpr ACCESS_MASK writeAccess = FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES | FILE_DELETE_CHILD |
        DELETE | WRITE_DAC | WRITE_OWNER;
    GENERIC_MAPPING mapping{FILE_GENERIC_READ, FILE_GENERIC_WRITE, FILE_GENERIC_EXECUTE, FILE_ALL_ACCESS};
    for (DWORD index = 0; index < aclInfo.AceCount; ++index) {
        void* rawAce = nullptr;
        if (!::GetAce(dacl, index, &rawAce)) throw std::runtime_error("cannot read journal ancestor ACL");
        const auto* header = static_cast<const ACE_HEADER*>(rawAce);
        if (header->AceType == ACCESS_DENIED_ACE_TYPE) continue;
        if (header->AceType != ACCESS_ALLOWED_ACE_TYPE) {
            throw std::runtime_error("journal path ancestor ACL has unsupported entries");
        }
        const auto* ace = static_cast<const ACCESS_ALLOWED_ACE*>(rawAce);
        ACCESS_MASK mask = ace->Mask;
        ::MapGenericMask(&mask, &mapping);
        if ((header->AceFlags & INHERIT_ONLY_ACE) == 0 && !trustedSid(const_cast<DWORD*>(&ace->SidStart), user) &&
            (mask & writeAccess) != 0) {
            throw std::runtime_error("journal path ancestor allows untrusted writes");
        }
    }
}

HANDLE openDirectory(HANDLE parent, std::wstring_view name, ACCESS_MASK access, ULONG disposition,
                     PSECURITY_DESCRIPTOR security = nullptr) {
    if (name.empty() || name.size() > USHRT_MAX / sizeof(wchar_t)) throw std::runtime_error("invalid journal directory component");
    UNICODE_STRING component{};
    component.Length = static_cast<USHORT>(name.size() * sizeof(wchar_t));
    component.MaximumLength = component.Length;
    component.Buffer = const_cast<wchar_t*>(name.data());
    OBJECT_ATTRIBUTES attributes{};
    attributes.Length = sizeof(attributes);
    attributes.RootDirectory = parent;
    attributes.ObjectName = &component;
    attributes.Attributes = OBJ_CASE_INSENSITIVE;
    attributes.SecurityDescriptor = security;
    IO_STATUS_BLOCK block{};
    HANDLE raw = nullptr;
    const auto status = ::NtCreateFile(&raw, access | SYNCHRONIZE, &attributes, &block, nullptr,
        FILE_ATTRIBUTE_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE, disposition,
        FILE_OPEN_REPARSE_POINT | FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT, nullptr, 0);
    if (status < 0) {
        throw std::system_error(static_cast<int>(::RtlNtStatusToDosError(status)), std::system_category(), "open journal path component");
    }
    return raw;
}

bool missingComponent(int error) {
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
}

HANDLE openDriveRoot(const std::filesystem::path& root, ACCESS_MASK access) {
    return ::CreateFileW(root.c_str(), access | SYNCHRONIZE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
}

void requireDirectoryRoot(HANDLE handle, PSID user) {
    requirePrivate(handle, user, true);
    BY_HANDLE_FILE_INFORMATION info{};
    if (!::GetFileInformationByHandle(handle, &info) || ::GetFileType(handle) != FILE_TYPE_DISK ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != FILE_ATTRIBUTE_DIRECTORY) {
        throw std::runtime_error("journal root is not a regular directory");
    }
}

std::wstring checkedComponent(const std::filesystem::path& component) {
    const auto name = component.native();
    if (name.empty() || name == L"." || name == L".." || name.back() == L'.' || name.back() == L' ' ||
        name.find_first_of(L"\\/:*?\"<>|") != name.npos || name.find(wchar_t{}) != name.npos ||
        std::any_of(name.begin(), name.end(), [](wchar_t ch) { return ch < 0x20; })) {
        throw std::runtime_error("invalid journal path component");
    }
    const auto dot = name.find(L'.');
    std::wstring base = name.substr(0, dot);
    for (auto& ch : base) if (ch >= L'a' && ch <= L'z') ch = static_cast<wchar_t>(ch - (L'a' - L'A'));
    if (base == L"CON" || base == L"PRN" || base == L"AUX" || base == L"NUL" ||
        (base.size() == 4 && (base.starts_with(L"COM") || base.starts_with(L"LPT")) && base[3] >= L'1' && base[3] <= L'9')) {
        throw std::runtime_error("reserved journal path component");
    }
    return name;
}

}
#endif

JournalRoot::JournalRoot(const std::filesystem::path& path) {
#ifdef _WIN32
    const auto native = path.native();
    const auto drive = path.root_name().native();
    if (!path.is_absolute() || native.empty() || native.size() > 32767 ||
        native.find(wchar_t{}) != native.npos || drive.size() != 2 || drive[1] != L':' ||
        !((drive[0] >= L'A' && drive[0] <= L'Z') || (drive[0] >= L'a' && drive[0] <= L'z')) ||
        !path.has_root_directory()) {
        throw std::runtime_error("journal root must be an absolute drive path");
    }
    std::vector<std::wstring> components;
    for (const auto& component : path.relative_path()) components.push_back(checkedComponent(component));
    if (components.empty()) throw std::runtime_error("journal root must not be a filesystem root");

    struct HandleRollback {
        std::vector<void*>& handles;
        bool active{true};
        ~HandleRollback() {
            if (active) {
                for (auto* handle : handles) ::CloseHandle(handle);
                handles.clear();
            }
        }
    } rollback{handles_};

    auto userBytes = currentUser();
    auto* user = reinterpret_cast<TOKEN_USER*>(userBytes.data())->User.Sid;
    HANDLE rawRoot = openDriveRoot(path.root_path(), FILE_LIST_DIRECTORY | FILE_TRAVERSE | READ_CONTROL | FILE_READ_ATTRIBUTES);
    if (rawRoot == INVALID_HANDLE_VALUE) throw std::system_error(static_cast<int>(::GetLastError()), std::system_category(), "open journal drive root");
    ScopedHandle driveRoot(rawRoot);
    handles_.push_back(driveRoot.get());
    driveRoot.release();
    requireSafeAncestor(rawRoot, user);

    for (std::size_t index = 0; index + 1 < components.size(); ++index) {
        HANDLE next = openDirectory(static_cast<HANDLE>(handles_.back()), components[index],
            FILE_LIST_DIRECTORY | FILE_TRAVERSE | READ_CONTROL | FILE_READ_ATTRIBUTES, FILE_OPEN);
        try {
            requireSafeAncestor(next, user);
            handles_.push_back(next);
        } catch (...) {
            ::CloseHandle(next);
            throw;
        }
    }

    HANDLE root = nullptr;
    try {
        root = openDirectory(static_cast<HANDLE>(handles_.back()), components.back(),
            FILE_LIST_DIRECTORY | FILE_ADD_FILE | FILE_TRAVERSE | READ_CONTROL | FILE_READ_ATTRIBUTES,
            FILE_OPEN);
    } catch (const std::system_error& error) {
        if (!missingComponent(error.code().value())) throw;

        HANDLE parent = static_cast<HANDLE>(handles_.back());
        ScopedHandle writableParent;
        if (handles_.size() == 1) {
            writableParent.reset(openDriveRoot(path.root_path(), FILE_LIST_DIRECTORY | FILE_ADD_SUBDIRECTORY |
                FILE_TRAVERSE | READ_CONTROL | FILE_READ_ATTRIBUTES));
            if (writableParent.get() == INVALID_HANDLE_VALUE) throw std::system_error(static_cast<int>(::GetLastError()), std::system_category(), "authorize journal root creation");
            parent = static_cast<HANDLE>(writableParent.get());
        } else {
            const auto grandparent = static_cast<HANDLE>(handles_[handles_.size() - 2]);
            writableParent.reset(openDirectory(grandparent, components[components.size() - 2],
                FILE_LIST_DIRECTORY | FILE_ADD_SUBDIRECTORY | FILE_TRAVERSE | READ_CONTROL | FILE_READ_ATTRIBUTES,
                FILE_OPEN));
            requireSafeAncestor(static_cast<HANDLE>(writableParent.get()), user);
            parent = static_cast<HANDLE>(writableParent.get());
        }

        SECURITY_DESCRIPTOR security{};
        if (!::InitializeSecurityDescriptor(&security, SECURITY_DESCRIPTOR_REVISION) ||
            !::SetSecurityDescriptorOwner(&security, user, FALSE)) {
            throw std::system_error(static_cast<int>(::GetLastError()), std::system_category(), "prepare journal root security");
        }
        std::vector<std::uintptr_t> aclStorage((sizeof(ACL) + sizeof(ACCESS_ALLOWED_ACE) + ::GetLengthSid(user) + sizeof(std::uintptr_t) - 1) / sizeof(std::uintptr_t));
        auto* acl = reinterpret_cast<PACL>(aclStorage.data());
        if (!::InitializeAcl(acl, static_cast<DWORD>(aclStorage.size() * sizeof(std::uintptr_t)), ACL_REVISION) ||
            !::AddAccessAllowedAceEx(acl, ACL_REVISION, OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE, FILE_ALL_ACCESS, user) ||
            !::SetSecurityDescriptorDacl(&security, TRUE, acl, FALSE) ||
            !::SetSecurityDescriptorControl(&security, SE_DACL_PROTECTED, SE_DACL_PROTECTED)) {
            throw std::system_error(static_cast<int>(::GetLastError()), std::system_category(), "prepare journal root ACL");
        }
        root = openDirectory(parent, components.back(),
            FILE_LIST_DIRECTORY | FILE_ADD_FILE | FILE_TRAVERSE | READ_CONTROL | FILE_READ_ATTRIBUTES,
            FILE_CREATE, &security);
    }
    try {
        requireDirectoryRoot(root, user);
        handles_.push_back(root);
    } catch (...) {
        ::CloseHandle(root);
        throw;
    }
    rollback.active = false;
#else
    if (!path.is_absolute()) throw std::runtime_error("journal root must be absolute");
    std::vector<std::string> components;
    for (const auto& component : path.relative_path()) {
        const auto name = component.string();
        if (name == "." || name.empty()) continue;
        if (name == ".." || name.find('\0') != std::string::npos) {
            throw std::runtime_error("invalid journal root component");
        }
        components.push_back(name);
    }
    if (components.empty()) throw std::runtime_error("journal root must not be a filesystem root");
    int current = ::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (current < 0) throw std::system_error(errno, std::generic_category(), "open filesystem root");
    try {
        struct stat rootInfo{};
        if (::fstat(current, &rootInfo) != 0 || !S_ISDIR(rootInfo.st_mode) ||
            (rootInfo.st_uid != 0 && rootInfo.st_uid != ::geteuid()) ||
            ((rootInfo.st_mode & 0022) != 0 && (rootInfo.st_mode & S_ISVTX) == 0)) {
            throw std::runtime_error("filesystem root is not safely owned");
        }
        bool stickyParent = (rootInfo.st_mode & S_ISVTX) != 0 && (rootInfo.st_mode & 0022) != 0;
        for (std::size_t index = 0; index < components.size(); ++index) {
            const bool final = index + 1 == components.size();
            const auto* name = components[index].c_str();
            if (final && ::mkdirat(current, name, 0700) != 0 && errno != EEXIST) {
                throw std::system_error(errno, std::generic_category(), "create journal root");
            }
            const int next = ::openat(current, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
            if (next < 0) throw std::system_error(errno, std::generic_category(), "open journal directory");
            struct stat info{};
            const bool invalid = ::fstat(next, &info) != 0 || !S_ISDIR(info.st_mode) ||
                (!final && info.st_uid != 0 && info.st_uid != ::geteuid()) ||
                (stickyParent && info.st_uid != ::geteuid()) ||
                (final && (info.st_uid != ::geteuid() || (info.st_mode & 0077) != 0)) ||
                (!final && (info.st_mode & 0022) != 0 && (info.st_mode & S_ISVTX) == 0);
            if (invalid) {
                ::close(next);
                throw std::runtime_error("journal directory is not private or safely owned");
            }
            if (final) {
#ifdef FO4IDE_TEST_HOOKS
                if (journalRootSyncHook != nullptr) {
                    const int error = journalRootSyncHook();
                    if (error != 0) {
                        ::close(next);
                        throw std::system_error(error, std::generic_category(), "sync journal parent");
                    }
                }
#endif
                if (::fsync(current) != 0) {
                    const int error = errno;
                    ::close(next);
                    throw std::system_error(error, std::generic_category(), "sync journal parent");
                }
            }
            stickyParent = (info.st_mode & S_ISVTX) != 0 && (info.st_mode & 0022) != 0;
            ::close(current);
            current = next;
        }
        fd_ = current;
    } catch (...) {
        ::close(current);
        throw;
    }
#endif
}

JournalRoot::~JournalRoot() {
#ifdef _WIN32
    for (auto handle = handles_.rbegin(); handle != handles_.rend(); ++handle) ::CloseHandle(*handle);
#else
    if (fd_ >= 0) ::close(fd_);
#endif
}

}
