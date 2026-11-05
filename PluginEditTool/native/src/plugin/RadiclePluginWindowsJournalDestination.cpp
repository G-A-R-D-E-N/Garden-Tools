#include "RadiclePluginJournalDestination.hpp"
#include "RadiclePluginWindowsJournalIntent.hpp"
#include "RadiclePluginWindowsJournalNative.hpp"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <winternl.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <string>
#include <system_error>
#include <vector>

namespace fo4ide::plugin::detail {
namespace {
struct HandleCloser {
    void operator()(void* handle) const noexcept {
        if (handle && handle != INVALID_HANDLE_VALUE) ::CloseHandle(handle);
    }
};
using Handle = std::unique_ptr<void, HandleCloser>;
using LocalMemory = std::unique_ptr<void, decltype(&::LocalFree)>;
[[noreturn]] void fail(const char* operation) {
    throw std::system_error(static_cast<int>(::GetLastError()), std::system_category(), operation);
}
// Requires: a refusal names the object it is about, so the operator can find the folder to fix.
std::string describeHandle(HANDLE handle) {
    std::vector<wchar_t> wide(32768);
    const auto length = ::GetFinalPathNameByHandleW(handle, wide.data(), static_cast<DWORD>(wide.size()),
                                                    FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (length == 0 || length >= wide.size()) return {};
    const auto size = ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(length), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string text(static_cast<std::size_t>(size), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(length), text.data(), size, nullptr, nullptr);
    return text;
}
struct Identity {
    std::uint64_t volume{};
    std::array<std::byte, 16> file{};
    bool operator==(const Identity&) const = default;
};
Identity identity(HANDLE handle) {
    FILE_ID_INFO info{};
    if (!::GetFileInformationByHandleEx(handle, FileIdInfo, &info, sizeof(info))) fail("identify destination");
    Identity result;
    result.volume = info.VolumeSerialNumber;
    std::memcpy(result.file.data(), info.FileId.Identifier, result.file.size());
    return result;
}
FILE_BASIC_INFO basic(HANDLE handle) {
    FILE_BASIC_INFO result{};
    if (!::GetFileInformationByHandleEx(handle, FileBasicInfo, &result, sizeof(result))) fail("inspect destination");
    return result;
}
bool same(const FILE_BASIC_INFO& a, const FILE_BASIC_INFO& b) {
    return a.CreationTime.QuadPart == b.CreationTime.QuadPart && a.LastWriteTime.QuadPart == b.LastWriteTime.QuadPart &&
        a.ChangeTime.QuadPart == b.ChangeTime.QuadPart && a.FileAttributes == b.FileAttributes;
}
std::wstring checkedName(std::wstring name) {
    if (name.empty() || name.size() > 255 || name == L"." || name == L".." || name.back() == L'.' || name.back() == L' ' ||
        name.find_first_of(L"\\/:*?\"<>|") != name.npos ||
        std::any_of(name.begin(), name.end(), [](wchar_t c) { return c < 0x20; }))
        throw std::runtime_error("invalid destination component");
    auto base = name.substr(0, name.find(L'.'));
    for (auto& c : base) if (c >= L'a' && c <= L'z') c = static_cast<wchar_t>(c - (L'a' - L'A'));
    if (base == L"CON" || base == L"PRN" || base == L"AUX" || base == L"NUL" || base == L"CONIN$" || base == L"CONOUT$" ||
        (base.size() == 4 && (base.starts_with(L"COM") || base.starts_with(L"LPT")) &&
            ((base[3] >= L'1' && base[3] <= L'9') || base[3] == L'\u00b9' || base[3] == L'\u00b2' || base[3] == L'\u00b3')))
        throw std::runtime_error("reserved destination component");
    return name;
}
std::wstring wideName(std::string_view name) {
    if (name.empty() || name.size() > 1024) throw std::runtime_error("invalid destination component");
    const auto count = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name.data(), static_cast<int>(name.size()), nullptr, 0);
    if (!count) fail("decode destination name");
    std::wstring wide(static_cast<std::size_t>(count), L'\0');
    if (::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name.data(), static_cast<int>(name.size()), wide.data(), count) != count)
        fail("decode destination name");
    return checkedName(std::move(wide));
}
std::string narrowName(std::wstring_view name) {
    const auto count = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, name.data(), static_cast<int>(name.size()), nullptr, 0, nullptr, nullptr);
    if (!count) fail("encode destination name");
    std::string result(static_cast<std::size_t>(count), '\0');
    if (::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, name.data(), static_cast<int>(name.size()), result.data(), count, nullptr, nullptr) != count)
        fail("encode destination name");
    return result;
}
std::vector<std::uintptr_t> currentUser() {
    HANDLE raw = nullptr;
    if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &raw)) fail("open destination token");
    Handle token(raw);
    DWORD length = 0;
    ::GetTokenInformation(token.get(), TokenUser, nullptr, 0, &length);
    if (!length || ::GetLastError() != ERROR_INSUFFICIENT_BUFFER) fail("size destination token");
    std::vector<std::uintptr_t> storage((length + sizeof(std::uintptr_t) - 1) / sizeof(std::uintptr_t));
    if (!::GetTokenInformation(token.get(), TokenUser, storage.data(), length, &length)) fail("read destination token");
    return storage;
}
bool trusted(PSID sid, PSID user, bool ancestor) {
    if (::EqualSid(sid, user) || ::IsWellKnownSid(sid, WinLocalSystemSid) || ::IsWellKnownSid(sid, WinBuiltinAdministratorsSid)) return true;
    if (!ancestor) return false;
    PSID raw = nullptr;
    if (!::ConvertStringSidToSidW(L"S-1-5-80-956008885-3418522649-1831038044-1853292631-2271478464", &raw)) fail("create installer SID");
    LocalMemory installer(raw, ::LocalFree);
    return ::EqualSid(sid, installer.get()) != FALSE;
}
void requireSafe(HANDLE handle, bool directory, bool ancestor = false) {
    BY_HANDLE_FILE_INFORMATION info{};
    if (!::GetFileInformationByHandle(handle, &info) || ::GetFileType(handle) != FILE_TYPE_DISK ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) != directory || (!directory && info.nNumberOfLinks != 1))
        throw std::runtime_error("destination is not a safe regular object" + describeHandle(handle));
    auto userStorage = currentUser();
    const auto user = reinterpret_cast<TOKEN_USER*>(userStorage.data())->User.Sid;
    PSID owner = nullptr;
    PACL dacl = nullptr;
    PSECURITY_DESCRIPTOR raw = nullptr;
    const auto error = ::GetSecurityInfo(handle, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
        &owner, nullptr, &dacl, nullptr, &raw);
    if (error != ERROR_SUCCESS) throw std::system_error(static_cast<int>(error), std::system_category(), "read destination ACL");
    LocalMemory security(raw, ::LocalFree);
    ACL_SIZE_INFORMATION acl{};
    if (!owner || !trusted(owner, user, ancestor) || !dacl || !::IsValidAcl(dacl) ||
        !::GetAclInformation(dacl, &acl, sizeof(acl), AclSizeInformation))
        throw std::runtime_error("destination ACL is not trusted" + describeHandle(handle));
    const ACCESS_MASK mutation = FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES | FILE_DELETE_CHILD | DELETE | WRITE_DAC | WRITE_OWNER |
        (ancestor ? 0 : FILE_WRITE_DATA | FILE_APPEND_DATA);
    GENERIC_MAPPING mapping{FILE_GENERIC_READ, FILE_GENERIC_WRITE, FILE_GENERIC_EXECUTE, FILE_ALL_ACCESS};
    for (DWORD i = 0; i < acl.AceCount; ++i) {
        void* entry = nullptr;
        if (!::GetAce(dacl, i, &entry)) fail("read destination ACE");
        const auto header = static_cast<const ACE_HEADER*>(entry);
        if (header->AceType == ACCESS_DENIED_ACE_TYPE) continue;
        if (header->AceType != ACCESS_ALLOWED_ACE_TYPE) throw std::runtime_error("unsupported destination ACE" + describeHandle(handle));
        const auto ace = static_cast<const ACCESS_ALLOWED_ACE*>(entry);
        auto mask = ace->Mask;
        ::MapGenericMask(&mask, &mapping);
        if (!(header->AceFlags & INHERIT_ONLY_ACE) && (mask & mutation) &&
            !trusted(const_cast<DWORD*>(&ace->SidStart), user, ancestor))
            throw std::runtime_error("destination permits untrusted mutation" + describeHandle(handle));
    }
}
Handle openChild(HANDLE parent, const std::wstring& name, bool directory, ACCESS_MASK extraAccess = 0) {
    UNICODE_STRING component{};
    component.Length = static_cast<USHORT>(name.size() * sizeof(wchar_t));
    component.MaximumLength = component.Length;
    component.Buffer = const_cast<wchar_t*>(name.data());
    OBJECT_ATTRIBUTES attributes{};
    attributes.Length = sizeof(attributes);
    attributes.RootDirectory = parent;
    attributes.ObjectName = &component;
    attributes.Attributes = OBJ_CASE_INSENSITIVE;
    IO_STATUS_BLOCK block{};
    HANDLE raw = nullptr;
    const auto status = ::NtCreateFile(&raw, extraAccess | READ_CONTROL | FILE_READ_ATTRIBUTES | SYNCHRONIZE |
        (directory ? FILE_LIST_DIRECTORY | FILE_TRAVERSE : FILE_READ_DATA), &attributes, &block, nullptr,
        FILE_ATTRIBUTE_NORMAL, directory ? FILE_SHARE_READ | FILE_SHARE_WRITE : FILE_SHARE_READ, FILE_OPEN,
        FILE_OPEN_REPARSE_POINT | FILE_SYNCHRONOUS_IO_NONALERT | (directory ? FILE_DIRECTORY_FILE : FILE_NON_DIRECTORY_FILE), nullptr, 0);
    if (status < 0) {
        const auto error = ::RtlNtStatusToDosError(status);
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return {};
        throw std::system_error(static_cast<int>(error), std::system_category(), "open destination component");
    }
    return Handle(raw);
}
void createDirectoryChild(HANDLE parent, const std::wstring& name) {
    UNICODE_STRING component{};
    component.Length = static_cast<USHORT>(name.size() * sizeof(wchar_t));
    component.MaximumLength = component.Length;
    component.Buffer = const_cast<wchar_t*>(name.data());
    OBJECT_ATTRIBUTES attributes{};
    attributes.Length = sizeof(attributes);
    attributes.RootDirectory = parent;
    attributes.ObjectName = &component;
    attributes.Attributes = OBJ_CASE_INSENSITIVE;
    IO_STATUS_BLOCK block{};
    HANDLE raw = nullptr;
    const auto status = ::NtCreateFile(&raw, READ_CONTROL | FILE_READ_ATTRIBUTES | SYNCHRONIZE, &attributes, &block, nullptr,
        FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_CREATE, FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT,
        nullptr, 0);
    Handle created(raw);
    if (status >= 0) return;
    const auto error = ::RtlNtStatusToDosError(status);
    if (error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS) return;
    throw std::system_error(static_cast<int>(error), std::system_category(), "create destination directory");
}
WindowsJournalFileMetadata fileMetadata(HANDLE file, bool inheritedOnly = false) {
    requireSafe(file, false);
    const auto information = basic(file);
    const auto attributes = information.FileAttributes;
    ULONG extendedAttributes = 0;
    IO_STATUS_BLOCK query{};
    const auto status = NtQueryInformationFile(file, &query, &extendedAttributes, sizeof(extendedAttributes),
        WindowsJournalFileInformationClass::ExtendedAttributes);
    if (status < 0) throw std::system_error(static_cast<int>(::RtlNtStatusToDosError(status)), std::system_category(), "inspect replacement extended attributes");
    if (query.Information != sizeof(extendedAttributes) || extendedAttributes)
        throw std::runtime_error("replacement extended attributes are unsupported");
    constexpr DWORD supported = FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM |
        FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED;
    if (attributes & ~supported) throw std::runtime_error("unsupported replacement file attributes");
    alignas(FILE_STREAM_INFO) std::array<std::byte, 4096> streams{};
    if (!::GetFileInformationByHandleEx(file, FileStreamInfo, streams.data(), static_cast<DWORD>(streams.size())))
        fail("inspect replacement streams");
    const auto* stream = reinterpret_cast<const FILE_STREAM_INFO*>(streams.data());
    if (stream->NextEntryOffset || stream->StreamNameLength != 7 * sizeof(wchar_t) ||
        std::wstring_view(stream->StreamName, 7) != L"::$DATA") throw std::runtime_error("replacement named streams are unsupported");
    PSECURITY_DESCRIPTOR raw = nullptr;
    PACL dacl = nullptr;
    const auto error = ::GetSecurityInfo(file, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION |
        DACL_SECURITY_INFORMATION | LABEL_SECURITY_INFORMATION, nullptr, nullptr, &dacl, nullptr, &raw);
    if (error != ERROR_SUCCESS) throw std::system_error(static_cast<int>(error), std::system_category(), "inspect replacement security");
    LocalMemory security(raw, ::LocalFree);
    if (inheritedOnly) {
        SECURITY_DESCRIPTOR_CONTROL control{}; DWORD revision = 0;
        if (!::GetSecurityDescriptorControl(raw, &control, &revision) || (control & SE_DACL_PROTECTED) || !dacl)
            throw std::runtime_error("absent replacement requires inherited security");
        for (DWORD i = 0; i < dacl->AceCount; ++i) {
            void* entry = nullptr;
            if (!::GetAce(dacl, i, &entry)) fail("inspect inherited replacement ACE");
            const auto* header = static_cast<ACE_HEADER*>(entry);
            if (!(header->AceFlags & INHERITED_ACE))
                throw std::runtime_error("absent replacement requires inherited ACEs: ace " + std::to_string(i) + " of " +
                    std::to_string(dacl->AceCount) + " type " + std::to_string(header->AceType) + " flags " + std::to_string(header->AceFlags) +
                    " control " + std::to_string(control));
        }
    }
    wchar_t* encoded = nullptr;
    if (!::ConvertSecurityDescriptorToStringSecurityDescriptorW(raw, SDDL_REVISION_1,
        OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION | SACL_SECURITY_INFORMATION | LABEL_SECURITY_INFORMATION,
        &encoded, nullptr)) fail("encode replacement security");
    LocalMemory text(encoded, ::LocalFree);
    auto result = narrowName(encoded);
    if (result.size() > 4096) throw std::runtime_error("replacement security exceeds bound");
    return {std::move(result), attributes, information.CreationTime.QuadPart};
}

std::vector<std::byte> readHeldFile(HANDLE file, const Identity& originalIdentity, const FILE_BASIC_INFO& originalBasic, std::size_t maxBytes) {
    requireSafe(file, false);
    const auto before = basic(file);
    LARGE_INTEGER size{};
    if (!same(before, originalBasic) || identity(file) != originalIdentity || !::GetFileSizeEx(file, &size) ||
        size.QuadPart < 0 || static_cast<unsigned long long>(size.QuadPart) > maxBytes)
        throw std::runtime_error("destination changed or exceeds read bound");
    std::vector<std::byte> result(static_cast<std::size_t>(size.QuadPart));
    LARGE_INTEGER start{};
    if (!::SetFilePointerEx(file, start, nullptr, FILE_BEGIN)) fail("seek destination");
    for (std::size_t offset = 0; offset < result.size();) {
        const auto wanted = static_cast<DWORD>(std::min<std::size_t>(result.size() - offset, MAXDWORD));
        DWORD count = 0;
        if (!::ReadFile(file, result.data() + offset, wanted, &count, nullptr)) fail("read destination");
        if (!count) throw std::runtime_error("short destination read");
        offset += count;
    }
    LARGE_INTEGER afterSize{};
    if (!same(before, basic(file)) || !::GetFileSizeEx(file, &afterSize) || size.QuadPart != afterSize.QuadPart)
        throw std::runtime_error("destination changed during read");
    return result;
}

}

class JournalDirectoryLease {
public:
    std::vector<Handle> handles;
    std::vector<Identity> identities;
    std::size_t rootIndex{};
    void verify() const {
        for (std::size_t i = 0; i < handles.size(); ++i) {
            requireSafe(handles[i].get(), true, i < rootIndex);
            if (identity(handles[i].get()) != identities[i]) throw std::runtime_error("destination directory changed");
        }
    }
};
struct JournalDestination::Impl {
    std::shared_ptr<JournalDirectoryLease> chain = std::make_shared<JournalDirectoryLease>();
    std::wstring file{};
    Handle original{};
    Identity originalIdentity{};
    FILE_BASIC_INFO originalBasic{};
    void refreshEntry() {
        original.reset();
        original = openChild(chain->handles.back().get(), file, false);
        if (original) {
            requireSafe(original.get(), false);
            originalIdentity = identity(original.get());
            originalBasic = basic(original.get());
        }
    }
    void verifyEntry() const {
        auto linked = openChild(chain->handles.back().get(), file, false);
        if (static_cast<bool>(linked) != static_cast<bool>(original)) throw std::runtime_error("destination entry changed");
        if (linked) {
            requireSafe(linked.get(), false);
            if (identity(linked.get()) != originalIdentity) throw std::runtime_error("destination entry changed");
        }
    }
};

JournalDestination::JournalDestination(const std::filesystem::path& root, const std::vector<std::string>& directories,
    std::string file, bool createDirectories, std::optional<JournalDirectoryIdentity> expectedRoot)
    : JournalDestination(root, directories, std::move(file), createDirectories, std::move(expectedRoot), false) {}
JournalDestination::JournalDestination(const std::filesystem::path& root, const std::vector<std::string>& directories,
    std::string file, bool createDirectories, std::optional<JournalDirectoryIdentity> expectedRoot, bool captureOnly)
    : impl_(std::make_unique<Impl>()) {
    const auto drive = root.root_name().native();
    if (!root.is_absolute() || drive.size() != 2 || drive[1] != L':' ||
        !((drive[0] >= L'A' && drive[0] <= L'Z') || (drive[0] >= L'a' && drive[0] <= L'z')))
        throw std::runtime_error("destination must be an absolute drive path");
    std::vector<std::wstring> names;
    for (const auto& name : root.relative_path()) names.push_back(checkedName(name.native()));
    if (names.empty()) throw std::runtime_error("destination cannot be a drive root");
    const auto rootCount = names.size();
    for (const auto& name : directories) names.push_back(wideName(name));
    if (!captureOnly) impl_->file = wideName(file);
    auto& chain = *impl_->chain;
    chain.rootIndex = rootCount;
    Handle driveHandle(::CreateFileW(root.root_path().c_str(), FILE_LIST_DIRECTORY | FILE_TRAVERSE | READ_CONTROL | FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (driveHandle.get() == INVALID_HANDLE_VALUE) fail("open destination drive");
    chain.identities.push_back(identity(driveHandle.get()));
    chain.handles.push_back(std::move(driveHandle));
    const auto rootText = narrowName(root.native());
    const auto requireSafeAt = [&](HANDLE handle, bool directory, bool ancestor) {
        try { requireSafe(handle, directory, ancestor); }
        catch (const std::exception& error) {
            throw std::runtime_error(std::string(error.what()) + " [destination " + rootText + "]");
        }
    };
    requireSafeAt(chain.handles.back().get(), true, true);
    for (std::size_t i = 0; i < names.size(); ++i) {
        const auto parent = chain.handles.back().get();
        auto next = openChild(parent, names[i], true);
        if (!next && createDirectories && !captureOnly && i >= rootCount) {
            auto writable = openChild(chain.handles[i - 1].get(), names[i - 1], true, GENERIC_WRITE);
            if (!writable || identity(writable.get()) != chain.identities[i]) throw std::runtime_error("destination directory changed");
            createDirectoryChild(writable.get(), names[i]);
            if (!::FlushFileBuffers(writable.get())) {
                const auto error = ::GetLastError();
                if (error != ERROR_INVALID_FUNCTION && error != ERROR_ACCESS_DENIED && error != ERROR_INVALID_HANDLE)
                    throw std::system_error(static_cast<int>(error), std::system_category(), "sync destination parent");
            }
            next = openChild(parent, names[i], true);
        }
        if (!next) {
            if (i >= rootCount && (captureOnly || !createDirectories)) throw JournalDirectoryAbsent("journal directory is absent");
            throw std::runtime_error("destination directory is absent");
        }
        requireSafeAt(next.get(), true, i + 1 < rootCount);
        chain.identities.push_back(identity(next.get()));
        chain.handles.push_back(std::move(next));
        if (i + 1 == rootCount && expectedRoot) {
            if (!expectedRoot->lease || expectedRoot->device != chain.identities.back().volume || expectedRoot->inode != 0)
                throw std::runtime_error("authorized destination root changed");
            expectedRoot->lease->verify();
            if (identity(expectedRoot->lease->handles.back().get()) != chain.identities.back())
                throw std::runtime_error("authorized destination root changed");
        }
    }
    chain.verify();
    if (captureOnly) return;
    impl_->original = openChild(chain.handles.back().get(), impl_->file, false);
    if (impl_->original) {
        requireSafe(impl_->original.get(), false);
        impl_->originalIdentity = identity(impl_->original.get());
        impl_->originalBasic = basic(impl_->original.get());
    }
    impl_->verifyEntry();
}
JournalDestination::~JournalDestination() = default;
JournalDirectoryIdentity JournalDestination::captureRoot(const std::filesystem::path& root) {
    JournalDestination captured(root, {}, {}, false, std::nullopt, true);
    return {captured.impl_->chain->identities.back().volume, 0, captured.impl_->chain};
}
std::optional<std::vector<std::byte>> JournalDestination::read(std::size_t maxBytes) const {
    impl_->chain->verify();
    impl_->verifyEntry();
    if (!impl_->original) return std::nullopt;
    auto result = readHeldFile(impl_->original.get(), impl_->originalIdentity, impl_->originalBasic, maxBytes);
    impl_->chain->verify();
    impl_->verifyEntry();
    return result;
}
WindowsJournalFileMetadata JournalDestination::metadata() const {
    impl_->chain->verify();
    impl_->verifyEntry();
    if (!impl_->original) throw std::runtime_error("destination metadata is absent");
    return fileMetadata(impl_->original.get());
}
void* JournalDestination::stagingParent() const {
    impl_->chain->verify();
    impl_->verifyEntry();
    return impl_->chain->handles.back().get();
}
WindowsJournalFileSnapshot JournalDestination::stagingSnapshot(void* file, std::size_t maxBytes) {
    const auto id = identity(file);
    return {{id.volume, id.file}, readHeldFile(file, id, basic(file), maxBytes), fileMetadata(file)};
}
WindowsJournalFileMetadata JournalDestination::stagingMetadata() const {
    const auto parent = stagingParent();
    auto unsupported = [] { throw std::runtime_error("unsupported Windows staging metadata"); };
    if (impl_->original) {
        auto result = fileMetadata(impl_->original.get());
        PSECURITY_DESCRIPTOR raw = nullptr;
        const std::wstring encoded(result.security.begin(), result.security.end());
        if (!::ConvertStringSecurityDescriptorToSecurityDescriptorW(encoded.c_str(), SDDL_REVISION_1, &raw, nullptr))
            fail("decode staging security");
        LocalMemory descriptor(raw, ::LocalFree);
        SECURITY_DESCRIPTOR_CONTROL control{}; DWORD revision = 0;
        if (!::GetSecurityDescriptorControl(raw, &control, &revision)) fail("inspect staging security control");
        if (control & SE_DACL_AUTO_INHERITED) unsupported();
        PACL sacl = nullptr; BOOL present = FALSE, defaulted = FALSE;
        if (!::GetSecurityDescriptorSacl(raw, &present, &sacl, &defaulted)) fail("inspect staging label");
        if (sacl) for (DWORD index = 0; index < sacl->AceCount; ++index) {
            void* ace = nullptr;
            if (!::GetAce(sacl, index, &ace)) fail("inspect staging label ACE");
            const auto header = static_cast<const ACE_HEADER*>(ace);
            if (header->AceType != SYSTEM_MANDATORY_LABEL_ACE_TYPE || (header->AceFlags & INHERITED_ACE)) unsupported();
        }
        return result;
    }
    PSECURITY_DESCRIPTOR raw = nullptr;
    const auto error = ::GetSecurityInfo(parent, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION |
        DACL_SECURITY_INFORMATION | LABEL_SECURITY_INFORMATION, nullptr, nullptr, nullptr, nullptr, &raw);
    if (error != ERROR_SUCCESS) throw std::system_error(static_cast<int>(error), std::system_category(), "inspect staging parent security");
    LocalMemory parentSecurity(raw, ::LocalFree);
    PACL sacl = nullptr; BOOL present = FALSE, defaulted = FALSE;
    if (!::GetSecurityDescriptorSacl(raw, &present, &sacl, &defaulted)) fail("inspect staging parent label");
    if (sacl && sacl->AceCount) unsupported();
    HANDLE rawToken = nullptr;
    if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &rawToken)) fail("open staging token");
    Handle token(rawToken);
    GENERIC_MAPPING mapping{FILE_GENERIC_READ, FILE_GENERIC_WRITE, FILE_GENERIC_EXECUTE, FILE_ALL_ACCESS};
    PSECURITY_DESCRIPTOR derived = nullptr;
    if (!::CreatePrivateObjectSecurityEx(raw, nullptr, &derived, nullptr, FALSE, SEF_DACL_AUTO_INHERIT, token.get(), &mapping))
        fail("derive staging security");
    struct DerivedCloser { void operator()(void* value) const { ::DestroyPrivateObjectSecurity(&value); } };
    std::unique_ptr<void, DerivedCloser> derivedSecurity(derived);
    if (!::SetSecurityDescriptorControl(derived, SE_DACL_AUTO_INHERITED, 0)) fail("prepare creation security control");
    wchar_t* encoded = nullptr;
    if (!::ConvertSecurityDescriptorToStringSecurityDescriptorW(derived, SDDL_REVISION_1,
        OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION | SACL_SECURITY_INFORMATION |
        LABEL_SECURITY_INFORMATION, &encoded, nullptr)) fail("encode staging security");
    LocalMemory encodedSecurity(encoded, ::LocalFree);
    FILETIME now{}; ::GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER time{}; time.LowPart = now.dwLowDateTime; time.HighPart = now.dwHighDateTime;
    return {narrowName(encoded), FILE_ATTRIBUTE_ARCHIVE, static_cast<std::int64_t>(time.QuadPart)};
}
WindowsJournalReplacementArtifacts JournalDestination::replacementArtifacts(const WindowsJournalReplacementRequest& request,
    std::uint64_t originalSize, std::uint64_t finalSize, bool inheritedStage) const {
    if (wideName(request.destination) != impl_->file || originalSize > kWindowsReplacementMaxFileBytes ||
        finalSize > kWindowsReplacementMaxFileBytes) throw std::runtime_error("replacement destination request mismatch");
    impl_->chain->verify();
    impl_->verifyEntry();
    const auto parent = impl_->chain->handles.back().get();
    const auto parentIdentity = identity(parent);
    WindowsJournalReplacementArtifacts result;
    result.directory = {parentIdentity.volume, parentIdentity.file};
    std::array<Handle, 3> files{
        openChild(parent, wideName(request.destination), false),
        openChild(parent, wideName(request.staged), false),
        openChild(parent, wideName(request.backup), false)};
    std::array<std::optional<WindowsJournalFileSnapshot>*, 3> outputs{&result.destination, &result.staged, &result.backup};
    const std::array<std::uint64_t, 3> bounds{std::max(originalSize, finalSize), finalSize, originalSize};
    for (std::size_t i = 0; i < files.size(); ++i) {
        if (!files[i]) continue;
        const auto id = identity(files[i].get());
        *outputs[i] = WindowsJournalFileSnapshot{{id.volume, id.file},
            readHeldFile(files[i].get(), id, basic(files[i].get()), static_cast<std::size_t>(bounds[i])),
            fileMetadata(files[i].get(), inheritedStage && i == 1)};
    }
    impl_->chain->verify();
    impl_->verifyEntry();
    return result;
}
void JournalDestination::renameReplacementArtifact(const WindowsJournalReplacementRequest& request,
    const WindowsJournalReplacementArtifacts& expected, bool backupOriginal) {
    impl_->chain->verify();
    impl_->verifyEntry();
    if (wideName(request.destination) != impl_->file) throw std::runtime_error("replacement destination request mismatch");
    const auto parent = impl_->chain->handles.back().get();
    const auto parentId = identity(parent);
    if (WindowsJournalFileIdentity{parentId.volume, parentId.file} != expected.directory)
        throw std::runtime_error("replacement directory changed");
    impl_->original.reset();
    try {
        {
            const auto sourceIndex = backupOriginal ? 0u : 1u;
            const std::array<std::string, 3> names{request.destination, request.staged, request.backup};
            const std::array<const std::optional<WindowsJournalFileSnapshot>*, 3> snapshots{
                &expected.destination, &expected.staged, &expected.backup};
            std::array<Handle, 3> files;
            for (std::size_t i = 0; i < files.size(); ++i) {
                files[i] = openChild(parent, wideName(names[i]), false, i == sourceIndex ? DELETE : 0);
                if (static_cast<bool>(files[i]) != snapshots[i]->has_value()) throw std::runtime_error("replacement entry changed");
                if (!files[i]) continue;
                const auto& snapshot = **snapshots[i];
                const auto id = identity(files[i].get());
                if (WindowsJournalFileIdentity{id.volume, id.file} != snapshot.identity ||
                    fileMetadata(files[i].get()) != snapshot.metadata ||
                    readHeldFile(files[i].get(), id, basic(files[i].get()), snapshot.bytes.size()) != snapshot.bytes)
                    throw std::runtime_error("replacement snapshot changed before rename");
            }
            if (!files[sourceIndex]) throw std::runtime_error("replacement source is absent");
            impl_->chain->verify();
            const auto target = wideName(backupOriginal ? request.backup : request.destination);
            const auto length = offsetof(WindowsJournalRenameInfo, FileName) + target.size() * sizeof(wchar_t);
            std::vector<std::uintptr_t> buffer((length + sizeof(std::uintptr_t) - 1) / sizeof(std::uintptr_t));
            auto* rename = reinterpret_cast<WindowsJournalRenameInfo*>(buffer.data());
            rename->ReplaceIfExists = FALSE;
            rename->RootDirectory = parent;
            rename->FileNameLength = static_cast<ULONG>(target.size() * sizeof(wchar_t));
            std::memcpy(rename->FileName, target.data(), rename->FileNameLength);
            IO_STATUS_BLOCK status{};
            const auto result = NtSetInformationFile(files[sourceIndex].get(), &status, rename, static_cast<ULONG>(length),
                WindowsJournalFileInformationClass::Rename);
            if (result < 0) throw std::system_error(static_cast<int>(::RtlNtStatusToDosError(result)), std::system_category(), "rename replacement artifact");
        }
        impl_->refreshEntry();
    } catch (...) {
        impl_->refreshEntry();
        throw;
    }
}
std::vector<std::string> JournalDestination::listDirectory(const std::filesystem::path& root,
    const std::vector<std::string>& directories, const JournalDirectoryIdentity& expected) {
    JournalDestination held(root, directories, {}, false, expected, true);
    auto directory = held.impl_->chain->handles.back().get();
    const auto before = basic(directory);
    std::vector<std::string> result;
    alignas(FILE_FULL_DIR_INFO) std::array<std::byte, 65536> buffer{};
    auto kind = FileFullDirectoryRestartInfo;
    for (;;) {
        if (!::GetFileInformationByHandleEx(directory, kind, buffer.data(), static_cast<DWORD>(buffer.size()))) {
            if (::GetLastError() == ERROR_NO_MORE_FILES) break;
            fail("enumerate destination directory");
        }
        kind = FileFullDirectoryInfo;
        std::size_t offset = 0;
        for (;;) {
            constexpr auto header = offsetof(FILE_FULL_DIR_INFO, FileName);
            if (offset > buffer.size() - header) throw std::runtime_error("invalid directory enumeration");
            const auto* entry = reinterpret_cast<const FILE_FULL_DIR_INFO*>(buffer.data() + offset);
            if (entry->FileNameLength % sizeof(wchar_t) || entry->FileNameLength > buffer.size() - offset - header)
                throw std::runtime_error("invalid directory name length");
            const std::wstring_view name(entry->FileName, entry->FileNameLength / sizeof(wchar_t));
            if (name != L"." && name != L"..") {
                checkedName(std::wstring(name));
                if (entry->FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) throw std::runtime_error("source directory contains a reparse point");
                if (result.size() == 4096) throw std::runtime_error("source directory exceeds entry bound");
                result.push_back(narrowName(name));
            }
            if (!entry->NextEntryOffset) break;
            if (entry->NextEntryOffset < header + entry->FileNameLength || entry->NextEntryOffset % alignof(FILE_FULL_DIR_INFO) ||
                entry->NextEntryOffset > buffer.size() - offset) throw std::runtime_error("invalid directory entry offset");
            offset += entry->NextEntryOffset;
        }
    }
    held.impl_->chain->verify();
    if (!same(before, basic(directory))) throw std::runtime_error("source directory changed during enumeration");
    return result;
}
void JournalDestination::replace(bool, std::string_view, std::uint64_t, std::span<const std::byte>, std::string_view) {
    throw std::runtime_error("Windows destination replacement is not implemented");
}
}
#endif
