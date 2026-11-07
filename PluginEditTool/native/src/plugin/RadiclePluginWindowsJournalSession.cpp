#include "RadiclePluginWindowsJournalSession.hpp"
#include "RadiclePluginWindowsJournalNative.hpp"

#if defined(_WIN32)
#include <fo4/io/path_text.hpp>
#include <fo4/io/sha256.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <windows.h>
#include <winternl.h>
#include <sddl.h>
#include <aclapi.h>
#include <cstring>
#include <system_error>
#include <set>

namespace fo4ide::plugin::detail {
namespace {
using Json = nlohmann::json;
constexpr std::size_t completionLimit = 16 * kJournalMaxManifestBytes;
struct HandleCloser { void operator()(void* handle) const { if (handle && handle != INVALID_HANDLE_VALUE) ::CloseHandle(handle); } };
using Handle = std::unique_ptr<void, HandleCloser>;
using LocalMemory = std::unique_ptr<void, decltype(&::LocalFree)>;
[[noreturn]] void nativeFailure(NTSTATUS status, const char* operation) {
    throw std::system_error(static_cast<int>(::RtlNtStatusToDosError(status)), std::system_category(), operation);
}
WindowsJournalFileIdentity nativeIdentity(HANDLE handle) {
    FILE_ID_INFO information{};
    if (!::GetFileInformationByHandleEx(handle, FileIdInfo, &information, sizeof(information)))
        throw std::system_error(static_cast<int>(::GetLastError()), std::system_category(), "identify staging object");
    WindowsJournalFileIdentity result; result.volume = information.VolumeSerialNumber;
    std::memcpy(result.file.data(), information.FileId.Identifier, result.file.size());
    return result;
}
Handle relative(HANDLE parent, std::string_view name, bool directory, bool create = false,
    ACCESS_MASK extra = 0, PSECURITY_DESCRIPTOR security = nullptr, bool absentIfDeletePending = false) {
    validateWindowsJournalFileName(name);
    const std::wstring wide(name.begin(), name.end());
    UNICODE_STRING component{static_cast<USHORT>(wide.size() * sizeof(wchar_t)), static_cast<USHORT>(wide.size() * sizeof(wchar_t)), const_cast<wchar_t*>(wide.data())};
    OBJECT_ATTRIBUTES attributes{}; attributes.Length = sizeof(attributes); attributes.RootDirectory = parent;
    attributes.ObjectName = &component; attributes.Attributes = OBJ_CASE_INSENSITIVE; attributes.SecurityDescriptor = security;
    IO_STATUS_BLOCK status{}; HANDLE raw = nullptr;
    const auto result = ::NtCreateFile(&raw, READ_CONTROL | FILE_READ_ATTRIBUTES | SYNCHRONIZE | extra |
        (directory ? FILE_LIST_DIRECTORY | FILE_TRAVERSE : FILE_READ_DATA), &attributes, &status, nullptr,
        FILE_ATTRIBUTE_NORMAL, directory ? FILE_SHARE_READ | FILE_SHARE_WRITE : FILE_SHARE_READ,
        create ? FILE_CREATE : FILE_OPEN, FILE_OPEN_REPARSE_POINT | FILE_SYNCHRONOUS_IO_NONALERT |
        (directory ? FILE_DIRECTORY_FILE : FILE_NON_DIRECTORY_FILE), nullptr, 0);
    if (result < 0) {
        const auto error = ::RtlNtStatusToDosError(result);
        if (!create && (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)) return {};
        if (create && (error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS)) return {};
        if (absentIfDeletePending && result == kWindowsDeletePendingStatus) return {};
        nativeFailure(result, "open Windows staging object");
    }
    return Handle(raw);
}
LocalMemory privateSecurity() {
    HANDLE raw = nullptr;
    if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &raw)) throw std::runtime_error("cannot open staging owner token");
    Handle token(raw); DWORD size = 0;
    ::GetTokenInformation(token.get(), TokenUser, nullptr, 0, &size);
    std::vector<std::uintptr_t> storage((size + sizeof(std::uintptr_t) - 1) / sizeof(std::uintptr_t));
    if (!size || !::GetTokenInformation(token.get(), TokenUser, storage.data(), size, &size)) throw std::runtime_error("cannot read staging owner token");
    wchar_t* sid = nullptr;
    if (!::ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(storage.data())->User.Sid, &sid)) throw std::runtime_error("cannot encode staging owner");
    LocalMemory sidMemory(sid, ::LocalFree);
    const auto sddl = std::wstring(L"O:") + sid + L"D:P(A;;FA;;;" + sid + L")";
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!::ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr))
        throw std::runtime_error("cannot create staging directory security");
    return LocalMemory(descriptor, ::LocalFree);
}
void requirePrivateStageDirectory(HANDLE directory) {
    BY_HANDLE_FILE_INFORMATION information{};
    if (!::GetFileInformationByHandle(directory, &information) || ::GetFileType(directory) != FILE_TYPE_DISK ||
        !(information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
        throw std::runtime_error("Windows staging directory is not regular");
    PSECURITY_DESCRIPTOR raw = nullptr; PSID owner = nullptr; PACL dacl = nullptr;
    const auto error = ::GetSecurityInfo(directory, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
        &owner, nullptr, &dacl, nullptr, &raw);
    if (error != ERROR_SUCCESS) throw std::runtime_error("cannot inspect private Windows staging directory");
    LocalMemory security(raw, ::LocalFree);
    const auto expected = privateSecurity(); PSID expectedOwner = nullptr; BOOL defaulted = FALSE;
    SECURITY_DESCRIPTOR_CONTROL control{}; DWORD revision = 0; ACL_SIZE_INFORMATION acl{};
    if (!::GetSecurityDescriptorOwner(expected.get(), &expectedOwner, &defaulted) || !owner || !::EqualSid(owner, expectedOwner) ||
        !::GetSecurityDescriptorControl(raw, &control, &revision) || !(control & SE_DACL_PROTECTED) || !dacl || !::IsValidAcl(dacl) ||
        !::GetAclInformation(dacl, &acl, sizeof(acl), AclSizeInformation) || acl.AceCount != 1)
        throw std::runtime_error("Windows staging directory ACL is not private");
    void* entry = nullptr;
    if (!::GetAce(dacl, 0, &entry)) throw std::runtime_error("cannot inspect private Windows staging ACE");
    const auto ace = static_cast<const ACCESS_ALLOWED_ACE*>(entry);
    if (ace->Header.AceType != ACCESS_ALLOWED_ACE_TYPE || ace->Header.AceFlags || ace->Mask != FILE_ALL_ACCESS ||
        !::EqualSid(const_cast<DWORD*>(&ace->SidStart), expectedOwner))
        throw std::runtime_error("Windows staging directory permits unexpected access");
}
std::filesystem::path heldPath(HANDLE handle) {
    const auto size = ::GetFinalPathNameByHandleW(handle, nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!size || size > 32768) throw std::runtime_error("cannot size staging path");
    std::wstring value(size + 1, L'\0');
    const auto length = ::GetFinalPathNameByHandleW(handle, value.data(), static_cast<DWORD>(value.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!length || length >= value.size()) throw std::runtime_error("cannot resolve staging path");
    value.resize(length);
    if (value.starts_with(L"\\\\?\\") && value.size() > 6 && value[5] == L':') value.erase(0, 4);
    return value;
}
Json identityRecord(const WindowsJournalFileIdentity& identity) {
    std::string file;
    for (const auto byte : identity.file) {
        const auto value = std::to_integer<unsigned>(byte);
        file += "0123456789abcdef"[value >> 4]; file += "0123456789abcdef"[value & 15];
    }
    return {{"volume", identity.volume}, {"file", file}};
}
void fields(const Json& value, std::initializer_list<const char*> names) {
    if (!value.is_object() || value.size() != names.size()) throw std::runtime_error("invalid Windows journal authority fields");
    for (const auto name : names) if (!value.contains(name)) throw std::runtime_error("missing Windows journal authority field");
}
WindowsJournalFileIdentity decodeIdentity(const Json& record) {
    fields(record, {"volume", "file"});
    if (!record.at("volume").is_number_unsigned() || !record.at("file").is_string())
        throw std::runtime_error("invalid Windows journal authority identity");
    const auto encoded = record.at("file").get<std::string>();
    if (encoded.size() != 32 || encoded.find_first_not_of("0123456789abcdef") != encoded.npos)
        throw std::runtime_error("invalid Windows journal authority file ID");
    WindowsJournalFileIdentity result; result.volume = record.at("volume").get<std::uint64_t>();
    const auto digit = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    for (std::size_t i = 0; i < result.file.size(); ++i)
        result.file[i] = static_cast<std::byte>((digit(encoded[i * 2]) << 4) | digit(encoded[i * 2 + 1]));
    return result;
}
std::string hash(std::string_view document) {
    const auto begin = reinterpret_cast<const std::byte*>(document.data());
    return fo4::io::sha256_hex(std::vector<std::byte>(begin, begin + document.size()));
}
WindowsJournalReplacementRequest request(const JournalManifest& manifest, std::size_t index,
    const std::filesystem::path& path) {
    const auto prefix = "ws-" + manifest.transaction + "-" + std::to_string(index);
    return {manifest.transaction, static_cast<std::uint32_t>(index), fo4::io::utf8(path.filename()), prefix + ".stage", prefix + ".backup"};
}
Json metadataRecord(const WindowsJournalFileMetadata& value) {
    return {{"security", value.security}, {"attributes", value.attributes}, {"creationTime", value.creationTime}};
}
WindowsJournalFileMetadata decodeMetadata(const Json& record) {
    fields(record, {"security", "attributes", "creationTime"});
    if (!record.at("security").is_string() || !record.at("attributes").is_number_unsigned() ||
        !record.at("creationTime").is_number_integer() || record.at("creationTime").get<std::int64_t>() <= 0 ||
        record.at("attributes").get<std::uint64_t>() > UINT32_MAX)
        throw std::runtime_error("invalid Windows journal authority metadata");
    auto security = record.at("security").get<std::string>();
    if (security.empty() || security.size() > 4096 || security.find('\0') != security.npos)
        throw std::runtime_error("invalid Windows journal authority security");
    return {std::move(security), record.at("attributes").get<std::uint32_t>(), record.at("creationTime").get<std::int64_t>()};
}
struct Record {
    Json value{};
    WindowsJournalFileSnapshot snapshot{};
    std::string name{};
    bool pending{};
};
std::optional<Record> readRecord(const WindowsJournalDirectory& journal, const std::string& name, std::size_t limit = kJournalMaxManifestBytes) {
    auto complete = journal.trySnapshotFile(name, limit);
    auto pending = journal.trySnapshotFile(name + ".tmp", limit);
    if (complete && pending) throw std::runtime_error("ambiguous Windows staging receipt");
    if (!complete && !pending) return {};
    auto snapshot = complete ? std::move(*complete) : std::move(*pending);
    return Record{Json::parse(snapshot.bytes), std::move(snapshot), name, !complete.has_value()};
}
void publishRecord(const WindowsJournalDirectory& journal, const std::string& name, const Json& value, std::size_t limit = kJournalMaxManifestBytes) {
    const auto encoded = value.dump();
    if (encoded.size() > limit) throw std::runtime_error("Windows journal record exceeds bound");
    journal.writeFile(name + ".tmp", std::as_bytes(std::span(encoded.data(), encoded.size())));
    journal.publishSnapshot(name + ".tmp", journal.snapshotFile(name + ".tmp", limit), name);
}
std::string embeddedRecord(const Json& completion, const std::string& name) {
    const auto& records = completion.at("records");
    if (!records.is_array() || records.size() > 4096) throw std::runtime_error("invalid Windows completion evidence");
    const Json* selected = nullptr;
    for (const auto& row : records) if (row.at("name") == name) {
        if (selected) throw std::runtime_error("duplicate Windows completion evidence");
        selected = &row;
    }
    if (!selected || !selected->at("content").is_string()) throw std::runtime_error("missing Windows completion document");
    auto content = selected->at("content").get<std::string>();
    if (content.size() > kJournalMaxManifestBytes || !selected->at("size").is_number_unsigned() || selected->at("size") != content.size() ||
        selected->at("sha256") != hash(content)) throw std::runtime_error("Windows completion document digest mismatch");
    return content;
}
void finishRecord(const WindowsJournalDirectory& journal, const Record& record) {
    if (record.pending) journal.publishSnapshot(record.name + ".tmp", record.snapshot, record.name);
}
std::string stageDirectoryName(const JournalManifest& manifest, std::size_t index, unsigned attempt) {
    return "ws-" + manifest.transaction + "-" + std::to_string(index) + "-" + std::to_string(attempt) + ".private";
}
std::string receiptName(const JournalManifest& manifest, std::size_t index, unsigned attempt, bool directory) {
    return std::string(directory ? "wd-" : "wf-") + manifest.transaction + "-" + std::to_string(index) + "-" + std::to_string(attempt) + ".receipt";
}
void receiptBinding(const Json& value, const JournalManifest& manifest, std::size_t index, unsigned attempt) {
    if (!value.at("version").is_number_unsigned() || value.at("version") != 1 || value.at("transaction") != manifest.transaction ||
        !value.at("index").is_number_unsigned() || value.at("index") != index ||
        !value.at("attempt").is_number_unsigned() || value.at("attempt") != attempt)
        throw std::runtime_error("Windows staging receipt transaction mismatch");
}
Handle receiptDirectory(HANDLE parent, const Json& value, const JournalManifest& manifest, std::size_t index, unsigned attempt) {
    fields(value, {"version", "transaction", "index", "attempt", "parent", "identity", "name"});
    receiptBinding(value, manifest, index, attempt);
    const auto name = stageDirectoryName(manifest, index, attempt);
    if (value.at("name") != name || decodeIdentity(value.at("parent")) != nativeIdentity(parent))
        throw std::runtime_error("Windows staging directory authority mismatch");
    auto directory = relative(parent, name, true);
    if (!directory || nativeIdentity(directory.get()) != decodeIdentity(value.at("identity")))
        throw std::runtime_error("Windows staging directory changed");
    requirePrivateStageDirectory(directory.get());
    return directory;
}
using Snapshot = WindowsJournalFileSnapshot (*)(void*, std::size_t);
struct OwnedStage {
    unsigned attempt{};
    Handle directory{};
    Record directoryReceipt{};
    Record fileReceipt{};
    WindowsJournalFileIdentity identity{};
    bool exposed{};
};
std::optional<OwnedStage> ownedStage(const WindowsJournalDirectory& journal, HANDLE parent,
    const JournalManifest& manifest, std::size_t index, const Json& row,
    const WindowsJournalReplacementArtifacts& artifacts, Snapshot snapshot) {
    std::optional<OwnedStage> result;
    const auto& entry = manifest.files[index];
    for (unsigned attempt = 0; attempt < 8; ++attempt) {
        auto directoryReceipt = readRecord(journal, receiptName(manifest, index, attempt, true));
        auto fileReceipt = readRecord(journal, receiptName(manifest, index, attempt, false));
        if (!directoryReceipt) {
            if (fileReceipt) throw std::runtime_error("Windows stage lacks directory receipt");
            continue;
        }
        auto directory = receiptDirectory(parent, directoryReceipt->value, manifest, index, attempt);
        if (!fileReceipt) continue;
        if (result) throw std::runtime_error("multiple receipted Windows stages");
        const auto& value = fileReceipt->value;
        fields(value, {"version", "transaction", "index", "attempt", "sourceDirectory", "sourceName", "exposedName", "stage"});
        receiptBinding(value, manifest, index, attempt);
        if (value.at("sourceDirectory") != directoryReceipt->value.at("identity") || value.at("sourceName") != "payload" ||
            value.at("exposedName") != row.at("stage")) throw std::runtime_error("Windows stage receipt names mismatch");
        const auto& recorded = value.at("stage");
        fields(recorded, {"identity", "size", "sha256", "metadata"});
        if (!recorded.at("size").is_number_unsigned() || recorded.at("size") != entry.finalSize ||
            recorded.at("sha256") != entry.finalHash || recorded.at("metadata") != row.at("metadata"))
            throw std::runtime_error("Windows stage receipt payload mismatch");
        const auto id = decodeIdentity(recorded.at("identity"));
        if (id.volume != artifacts.directory.volume) throw std::runtime_error("Windows stage volume mismatch");
        auto file = relative(directory.get(), "payload", false);
        std::optional<WindowsJournalFileSnapshot> actual;
        if (file) {
            if (artifacts.staged) throw std::runtime_error("Windows stage exposure is ambiguous");
            actual = snapshot(file.get(), static_cast<std::size_t>(entry.finalSize));
        } else if (artifacts.staged) actual = artifacts.staged;
        else if (artifacts.destination && artifacts.destination->identity == id) actual = artifacts.destination;
        if (!actual || actual->identity != id || actual->metadata != decodeMetadata(row.at("metadata")) ||
            actual->bytes.size() != entry.finalSize || fo4::io::sha256_hex(actual->bytes) != entry.finalHash)
            throw std::runtime_error("Windows receipted stage changed");
        result.emplace(OwnedStage{attempt, std::move(directory), std::move(*directoryReceipt), std::move(*fileReceipt), id, !file});
    }
    return result;
}
}

std::string WindowsJournalSessionState::authorize(const WindowsJournalDirectory& journal, std::string_view document,
    const JournalManifest& manifest, const std::vector<std::filesystem::path>& paths, const Targets& targets) {
    if (paths.size() != manifest.files.size() || targets.size() != paths.size()) throw std::runtime_error("Windows journal participant mismatch");
    Json entries = Json::array();
    for (std::size_t index = 0; index < paths.size(); ++index) {
        const auto& entry = manifest.files[index];
        const auto names = request(manifest, index, paths[index]);
        const auto artifacts = targets[index]->replacementArtifacts(names, entry.originalSize, entry.finalSize);
        if (artifacts.staged || artifacts.backup || artifacts.destination.has_value() != entry.originalExists)
            throw std::runtime_error("Windows journal participant artifacts already exist");
        const auto metadata = targets[index]->stagingMetadata();
        if (artifacts.destination && (artifacts.destination->bytes.size() != entry.originalSize ||
            fo4::io::sha256_hex(artifacts.destination->bytes) != entry.originalHash || artifacts.destination->metadata != metadata))
            throw std::runtime_error("Windows journal original changed before authorization");
        entries.push_back({{"index", index}, {"path", fo4::io::utf8(paths[index])}, {"directory", identityRecord(artifacts.directory)},
            {"original", artifacts.destination ? identityRecord(artifacts.destination->identity) : Json(nullptr)},
            {"metadata", metadataRecord(metadata)}, {"stage", names.staged}, {"backup", names.backup}});
    }
    auto encoded = Json{{"version", 1}, {"transaction", manifest.transaction}, {"manifestHash", hash(document)},
        {"recordDirectory", identityRecord(journal.directoryIdentity())}, {"entries", std::move(entries)}}.dump();
    if (encoded.size() > kJournalMaxManifestBytes) throw std::runtime_error("Windows journal authority exceeds bound");
    return encoded;
}

std::string WindowsJournalSessionState::completionDocument(const WindowsJournalDirectory& journal, std::string_view name) {
    const auto record = readRecord(journal, std::string(name), completionLimit);
    if (!record) throw std::runtime_error("Windows completion marker disappeared");
    const auto transaction = record->value.at("transaction").get<std::string>();
    if (name != transaction + ".complete" || decodeIdentity(record->value.at("recordDirectory")) != journal.directoryIdentity())
        throw std::runtime_error("Windows completion marker is not authorized");
    auto document = embeddedRecord(record->value, transaction + ".manifest");
    if (record->value.at("manifestHash") != hash(document) || decodeJournalManifest(document).transaction != transaction)
        throw std::runtime_error("Windows completion manifest mismatch");
    return document;
}

std::filesystem::path WindowsJournalSessionState::pluginIdentity(const JournalRoots& roots) {
    if (!roots.pluginDirectoryIdentity) throw std::runtime_error("Windows plugin parent is not authorized");
    JournalDestination target(roots.plugin.parent_path(), {}, fo4::io::utf8(roots.plugin.filename()), false, roots.pluginDirectoryIdentity);
    const auto parent = target.stagingParent();
    const auto size = ::GetFinalPathNameByHandleW(parent, nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_GUID);
    if (!size || size > 32768) throw std::runtime_error("cannot size Windows plugin parent identity");
    std::wstring value(size + 1, L'\0');
    const auto length = ::GetFinalPathNameByHandleW(parent, value.data(), static_cast<DWORD>(value.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_GUID);
    if (!length || length >= value.size()) throw std::runtime_error("cannot resolve Windows plugin parent identity");
    value.resize(length);
    return (std::filesystem::path(value) / roots.plugin.filename()).lexically_normal();
}
bool WindowsJournalSessionState::samePath(const std::filesystem::path& first, const std::filesystem::path& second) {
    const auto a = first.lexically_normal().native(), b = second.lexically_normal().native();
    if (a.size() > 32768 || b.size() > 32768) throw std::runtime_error("Windows journal path exceeds bound");
    const auto result = ::CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(), static_cast<int>(b.size()), TRUE);
    if (!result) throw std::runtime_error("cannot compare Windows journal namespace identity");
    return result == CSTR_EQUAL;
}

void WindowsJournalSessionState::inspect(const WindowsJournalDirectory& journal, std::string_view document,
    const JournalManifest& manifest, const std::vector<std::filesystem::path>& paths, const Targets& targets) {
    const auto bytes = journal.readFile(manifest.transaction + ".authority", kJournalMaxManifestBytes);
    const auto authority = Json::parse(bytes);
    fields(authority, {"version", "transaction", "manifestHash", "recordDirectory", "entries"});
    if (!authority.at("version").is_number_unsigned() || authority.at("version") != 1 || authority.at("transaction") != manifest.transaction ||
        authority.at("manifestHash") != hash(document) || decodeIdentity(authority.at("recordDirectory")) != journal.directoryIdentity() ||
        !authority.at("entries").is_array() || authority.at("entries").size() != manifest.files.size() ||
        paths.size() != manifest.files.size() || targets.size() != paths.size())
        throw std::runtime_error("Windows journal authority does not match manifest");
    if (cleanup(journal, document, manifest, paths, targets, nullptr, {}, true)) return;
    for (std::size_t index = 0; index < paths.size(); ++index) {
        const auto& row = authority.at("entries").at(index);
        fields(row, {"index", "path", "directory", "original", "metadata", "stage", "backup"});
        const auto names = request(manifest, index, paths[index]);
        const auto& entry = manifest.files[index];
        if (!row.at("index").is_number_unsigned() || row.at("index") != index || row.at("path") != fo4::io::utf8(paths[index]) ||
            row.at("stage") != names.staged || row.at("backup") != names.backup || row.at("original").is_null() == entry.originalExists)
            throw std::runtime_error("Windows journal participant authorization mismatch");
        const auto artifacts = targets[index]->replacementArtifacts(names, entry.originalSize, entry.finalSize);
        const auto metadata = decodeMetadata(row.at("metadata"));
        if (artifacts.directory != decodeIdentity(row.at("directory"))) throw std::runtime_error("Windows journal parent changed");
        const WindowsJournalReplacementExpectation expected{artifacts.directory, entry.originalExists, entry.originalSize,
            entry.originalHash, entry.finalSize, entry.finalHash, metadata};
        auto stage = ownedStage(journal, targets[index]->stagingParent(), manifest, index, row, artifacts,
            &JournalDestination::stagingSnapshot);
        const auto intent = journal.trySnapshotFile("wr-" + manifest.transaction + "-" + std::to_string(index) + ".intent", kJournalMaxManifestBytes);
        if (intent) {
            if (!stage || !stage->exposed) throw std::runtime_error("Windows intent lacks exposed stage receipt");
            const auto state = journal.inspectReplacement(names, expected, *targets[index]);
            if (entry.originalExists) {
                const auto& original = state == WindowsJournalReplacementState::prepared ? artifacts.destination : artifacts.backup;
                if (!original || original->identity != decodeIdentity(row.at("original")))
                    throw std::runtime_error("Windows journal intent original authority changed");
            }
        } else {
            if ((artifacts.staged && (!stage || !stage->exposed)) || artifacts.backup || artifacts.destination.has_value() != entry.originalExists)
                throw std::runtime_error("Windows journal unreceipted destination artifacts");
            if (artifacts.destination && (artifacts.destination->identity != decodeIdentity(row.at("original")) ||
                artifacts.destination->bytes.size() != entry.originalSize || fo4::io::sha256_hex(artifacts.destination->bytes) != entry.originalHash ||
                artifacts.destination->metadata != metadata)) throw std::runtime_error("Windows journal original authority changed");
        }
    }
}

void WindowsJournalSessionState::prepare(const WindowsJournalDirectory& journal, std::string_view document,
    const JournalManifest& manifest, const std::vector<std::filesystem::path>& paths, const Targets& targets,
    WindowsJournalSessionHook hook, const std::function<void()>& revalidate) {
    inspect(journal, document, manifest, paths, targets);
    const auto authority = Json::parse(journal.readFile(manifest.transaction + ".authority", kJournalMaxManifestBytes));
    for (std::size_t index = 0; index < paths.size(); ++index) {
        const auto& entry = manifest.files[index];
        if (entry.verifyOnly) continue;
        const auto& row = authority.at("entries").at(index);
        const auto names = request(manifest, index, paths[index]);
        const auto metadata = decodeMetadata(row.at("metadata"));
        const auto parent = targets[index]->stagingParent();
        auto artifacts = targets[index]->replacementArtifacts(names, entry.originalSize, entry.finalSize);
        auto stage = ownedStage(journal, parent, manifest, index, row, artifacts, &JournalDestination::stagingSnapshot);
        if (journal.trySnapshotFile("wr-" + manifest.transaction + "-" + std::to_string(index) + ".intent", kJournalMaxManifestBytes)) continue;
        if (!stage) {
            for (unsigned attempt = 0; attempt < 8 && !stage; ++attempt) {
                revalidate();
                const auto directoryName = stageDirectoryName(manifest, index, attempt);
                const auto directoryReceiptName = receiptName(manifest, index, attempt, true);
                auto directoryReceipt = readRecord(journal, directoryReceiptName);
                Handle directory;
                if (directoryReceipt) {
                    directory = receiptDirectory(parent, directoryReceipt->value, manifest, index, attempt);
                    finishRecord(journal, *directoryReceipt);
                } else {
                    if (nativeIdentity(parent) != decodeIdentity(row.at("directory")))
                        throw std::runtime_error("Windows staging parent changed");
                    const auto security = privateSecurity();
                    directory = relative(parent, directoryName, true, true, FILE_ADD_FILE, security.get());
                    if (!directory) continue;
                    requirePrivateStageDirectory(directory.get());
                    const auto id = nativeIdentity(directory.get());
                    if (id.volume != artifacts.directory.volume) throw std::runtime_error("Windows staging directory volume changed");
                    publishRecord(journal, directoryReceiptName, {{"version", 1}, {"transaction", manifest.transaction},
                        {"index", index}, {"attempt", attempt}, {"parent", row.at("directory")}, {"identity", identityRecord(id)}, {"name", directoryName}});
                }
                revalidate();
                const auto securityText = metadata.security;
                const std::wstring wideSecurity(securityText.begin(), securityText.end());
                PSECURITY_DESCRIPTOR rawSecurity = nullptr;
                if (!::ConvertStringSecurityDescriptorToSecurityDescriptorW(wideSecurity.c_str(), SDDL_REVISION_1, &rawSecurity, nullptr))
                    throw std::runtime_error("invalid Windows stage security");
                LocalMemory security(rawSecurity, ::LocalFree);
                auto file = relative(directory.get(), "payload", false, true, FILE_WRITE_DATA | FILE_WRITE_ATTRIBUTES, security.get());
                if (!file) continue;
                const auto payload = journal.readFile(entry.payload, static_cast<std::size_t>(entry.finalSize));
                if (payload.size() != entry.finalSize || fo4::io::sha256_hex(payload) != entry.finalHash)
                    throw std::runtime_error("Windows staging payload changed");
                std::size_t offset = 0;
                while (offset < payload.size()) {
                    const auto count = static_cast<DWORD>(std::min<std::size_t>(payload.size() - offset, 1024 * 1024));
                    DWORD written = 0;
                    if (!::WriteFile(file.get(), payload.data() + offset, count, &written, nullptr) || !written)
                        throw std::runtime_error("cannot write Windows stage");
                    offset += written;
                }
                FILE_BASIC_INFO basic{}; basic.CreationTime.QuadPart = metadata.creationTime; basic.FileAttributes = metadata.attributes;
                if (!::SetFileInformationByHandle(file.get(), FileBasicInfo, &basic, sizeof(basic)) || !::FlushFileBuffers(file.get()))
                    throw std::runtime_error("cannot flush Windows stage metadata");
                const auto snapshot = JournalDestination::stagingSnapshot(file.get(), static_cast<std::size_t>(entry.finalSize));
                if (snapshot.bytes != payload || snapshot.metadata != metadata) throw std::runtime_error("Windows stage does not preserve approved metadata");
                file.reset();
                const auto fileReceiptName = receiptName(manifest, index, attempt, false);
                if (hook) hook(WindowsJournalSessionStep::stageCreated, index, heldPath(directory.get()) / "payload", heldPath(journal.directory_) / fileReceiptName);
                revalidate();
                publishRecord(journal, fileReceiptName, {{"version", 1}, {"transaction", manifest.transaction}, {"index", index}, {"attempt", attempt},
                    {"sourceDirectory", identityRecord(nativeIdentity(directory.get()))}, {"sourceName", "payload"}, {"exposedName", names.staged},
                    {"stage", {{"identity", identityRecord(snapshot.identity)}, {"size", entry.finalSize}, {"sha256", entry.finalHash}, {"metadata", row.at("metadata")}}}});
                if (hook) hook(WindowsJournalSessionStep::stageReceipted, index, heldPath(directory.get()) / "payload", heldPath(journal.directory_) / fileReceiptName);
                stage = ownedStage(journal, parent, manifest, index, row, artifacts, &JournalDestination::stagingSnapshot);
            }
            if (!stage) throw JournalRecoveryRequired("Windows staging retry limit reached; retained unreceipted directories require manual attention");
        }
        revalidate();
        finishRecord(journal, stage->directoryReceipt); finishRecord(journal, stage->fileReceipt);
        if (!stage->exposed) {
            auto file = relative(stage->directory.get(), "payload", false, false, DELETE);
            if (!file) throw std::runtime_error("Windows owned stage disappeared");
            const auto snapshot = JournalDestination::stagingSnapshot(file.get(), static_cast<std::size_t>(entry.finalSize));
            if (snapshot.identity != stage->identity || snapshot.metadata != metadata || fo4::io::sha256_hex(snapshot.bytes) != entry.finalHash)
                throw std::runtime_error("Windows owned stage changed before exposure");
            const std::wstring exposed(names.staged.begin(), names.staged.end());
            const auto length = offsetof(WindowsJournalRenameInfo, FileName) + exposed.size() * sizeof(wchar_t);
            std::vector<std::uintptr_t> storage((length + sizeof(std::uintptr_t) - 1) / sizeof(std::uintptr_t));
            auto* rename = reinterpret_cast<WindowsJournalRenameInfo*>(storage.data());
            rename->ReplaceIfExists = FALSE; rename->RootDirectory = parent; rename->FileNameLength = static_cast<ULONG>(exposed.size() * sizeof(wchar_t));
            std::memcpy(rename->FileName, exposed.data(), rename->FileNameLength);
            IO_STATUS_BLOCK status{};
            const auto result = NtSetInformationFile(file.get(), &status, rename, static_cast<ULONG>(length), WindowsJournalFileInformationClass::Rename);
            if (result < 0) nativeFailure(result, "expose Windows receipted stage");
            file.reset();
            if (hook) hook(WindowsJournalSessionStep::stageExposed, index, heldPath(parent) / names.staged, heldPath(journal.directory_) / stage->fileReceipt.name);
        }
        revalidate();
        const WindowsJournalReplacementExpectation expected{decodeIdentity(row.at("directory")), entry.originalExists, entry.originalSize,
            entry.originalHash, entry.finalSize, entry.finalHash, metadata};
        journal.persistReplacementIntent(names, expected, *targets[index]);
    }
    revalidate();
}

void WindowsJournalSessionState::apply(const WindowsJournalDirectory& journal, const JournalManifest& manifest,
    const std::vector<std::filesystem::path>& paths, const Targets& targets,
    const std::function<void()>& revalidate, const std::function<void(std::size_t)>& committed) {
    revalidate();
    const auto authority = Json::parse(journal.readFile(manifest.transaction + ".authority", kJournalMaxManifestBytes));
    std::size_t count = 0;
    for (const bool plugin : {false, true}) for (std::size_t index = 0; index < paths.size(); ++index) {
        const auto& entry = manifest.files[index];
        if (entry.verifyOnly || (entry.kind == "plugin") != plugin) continue;
        revalidate();
        const auto& row = authority.at("entries").at(index);
        const auto names = request(manifest, index, paths[index]);
        const WindowsJournalReplacementExpectation expected{decodeIdentity(row.at("directory")), entry.originalExists, entry.originalSize,
            entry.originalHash, entry.finalSize, entry.finalHash, decodeMetadata(row.at("metadata"))};
        journal.resumeReplacement(names, expected, *targets[index]);
        revalidate();
        if (committed) committed(++count);
    }
}

bool WindowsJournalSessionState::cleanup(const WindowsJournalDirectory& journal, std::string_view document,
    const JournalManifest& manifest, const std::vector<std::filesystem::path>& paths, const Targets& targets,
    WindowsJournalSessionHook hook, const std::function<void()>& revalidate, bool inspectOnly) {
    const auto completionName = manifest.transaction + ".complete";
    auto completion = readRecord(journal, completionName, completionLimit);
    if (inspectOnly && !completion) return false;
    if (!inspectOnly) revalidate();
    const bool manifestPresent = journal.trySnapshotFile(manifest.transaction + ".manifest", kJournalMaxManifestBytes).has_value();
    const auto authorityDocument = completion ? embeddedRecord(completion->value, manifest.transaction + ".authority") : std::string{};
    const auto authorityBytes = manifestPresent ? journal.readFile(manifest.transaction + ".authority", kJournalMaxManifestBytes) :
        std::vector<std::byte>(reinterpret_cast<const std::byte*>(authorityDocument.data()), reinterpret_cast<const std::byte*>(authorityDocument.data() + authorityDocument.size()));
    const auto authority = Json::parse(authorityBytes);
    fields(authority, {"version", "transaction", "manifestHash", "recordDirectory", "entries"});
    if (!authority.at("version").is_number_unsigned() || authority.at("version") != 1 || authority.at("transaction") != manifest.transaction ||
        authority.at("manifestHash") != hash(document) || decodeIdentity(authority.at("recordDirectory")) != journal.directoryIdentity() ||
        !authority.at("entries").is_array() || authority.at("entries").size() != paths.size() || targets.size() != paths.size())
        throw std::runtime_error("Windows completion authority mismatch");
    std::set<std::string> allowed{manifest.transaction + ".manifest", manifest.transaction + ".authority"};
    std::set<std::string> required = allowed;
    for (std::size_t index = 0; index < paths.size(); ++index) {
        allowed.insert(manifest.files[index].payload); required.insert(manifest.files[index].payload);
        if (!manifest.files[index].verifyOnly) for (const auto suffix : {".intent", ".done"}) {
            const auto name = "wr-" + manifest.transaction + "-" + std::to_string(index) + suffix;
            allowed.insert(name); required.insert(name);
        }
        for (unsigned attempt = 0; attempt < 8; ++attempt) for (const bool directory : {false, true})
            allowed.insert(receiptName(manifest, index, attempt, directory));
    }
    if (!completion) {
        Json entries = Json::array(), directories = Json::array(), records = Json::array();
        bool retained = false;
        for (std::size_t index = 0; index < paths.size(); ++index) {
            const auto& entry = manifest.files[index]; const auto& row = authority.at("entries").at(index);
            const auto artifacts = targets[index]->replacementArtifacts(request(manifest, index, paths[index]), entry.originalSize, entry.finalSize);
            entries.push_back({{"index", index}, {"path", row.at("path")}, {"directory", row.at("directory")},
                {"final", artifacts.destination ? identityRecord(artifacts.destination->identity) : Json(nullptr)}});
            auto owned = ownedStage(journal, targets[index]->stagingParent(), manifest, index, row, artifacts, &JournalDestination::stagingSnapshot);
            if (!entry.verifyOnly && (!owned || !owned->exposed || !artifacts.destination || artifacts.destination->identity != owned->identity))
                throw std::runtime_error("Windows completion target is not receipted");
            retained = retained || (owned && owned->attempt != 0);
            for (unsigned attempt = 0; attempt < 8; ++attempt) {
                const auto receipt = readRecord(journal, receiptName(manifest, index, attempt, true));
                if (!receipt) continue;
                const bool remove = readRecord(journal, receiptName(manifest, index, attempt, false)).has_value();
                directories.push_back({{"index", index}, {"attempt", attempt}, {"name", receipt->value.at("name")},
                    {"identity", receipt->value.at("identity")}, {"remove", remove}});
                retained = retained || !remove;
            }
        }
        for (const auto& name : allowed) {
            auto snapshot = journal.trySnapshotFile(name, kJournalMaxFileBytes);
            if (!snapshot) { if (required.contains(name)) throw std::runtime_error("Windows completion record is missing"); continue; }
            const bool payload = std::any_of(manifest.files.begin(), manifest.files.end(), [&](const auto& entry) { return entry.payload == name; });
            records.push_back({{"name", name}, {"identity", identityRecord(snapshot->identity)}, {"size", snapshot->bytes.size()},
                {"sha256", fo4::io::sha256_hex(snapshot->bytes)}, {"content", payload ? Json(nullptr) : Json(std::string(reinterpret_cast<const char*>(snapshot->bytes.data()), snapshot->bytes.size()))}});
        }
        publishRecord(journal, completionName, {{"version", 1}, {"transaction", manifest.transaction}, {"manifestHash", hash(document)},
            {"authorityHash", fo4::io::sha256_hex(authorityBytes)}, {"recordDirectory", identityRecord(journal.directoryIdentity())},
            {"entries", std::move(entries)}, {"directories", std::move(directories)}, {"records", std::move(records)}, {"retained", retained}}, completionLimit);
        completion = readRecord(journal, completionName, completionLimit);
        if (hook) hook(WindowsJournalSessionStep::completionPublished, 0, heldPath(journal.directory_) / completionName, heldPath(journal.directory_) / completionName);
    }
    const auto& value = completion->value;
    fields(value, {"version", "transaction", "manifestHash", "authorityHash", "recordDirectory", "entries", "directories", "records", "retained"});
    if (!value.at("version").is_number_unsigned() || value.at("version") != 1 || value.at("transaction") != manifest.transaction ||
        value.at("manifestHash") != hash(document) || value.at("authorityHash") != fo4::io::sha256_hex(authorityBytes) ||
        decodeIdentity(value.at("recordDirectory")) != journal.directoryIdentity() || !value.at("entries").is_array() ||
        value.at("entries").size() != paths.size() || !value.at("directories").is_array() || value.at("directories").size() > paths.size() * 8 ||
        !value.at("records").is_array() || value.at("records").size() > allowed.size() || !value.at("retained").is_boolean())
        throw std::runtime_error("Windows completion authorization mismatch");
    std::vector<std::pair<std::string, WindowsJournalFileSnapshot>> privateFiles;
    std::set<std::string> recordedNames;
    for (const auto& record : value.at("records")) {
        fields(record, {"name", "identity", "size", "sha256", "content"});
        if (!record.at("name").is_string() || !record.at("size").is_number_unsigned() || record.at("size").get<std::uint64_t>() > kJournalMaxFileBytes)
            throw std::runtime_error("invalid Windows completion file record");
        const auto name = record.at("name").get<std::string>();
        if (!allowed.contains(name) || !recordedNames.insert(name).second) throw std::runtime_error("Windows cleanup file is not authorized");
        const auto payload = std::find_if(manifest.files.begin(), manifest.files.end(), [&](const auto& entry) { return entry.payload == name; });
        if (payload == manifest.files.end()) static_cast<void>(embeddedRecord(value, name));
        else if (!record.at("content").is_null() || record.at("size") != payload->finalSize || record.at("sha256") != payload->finalHash)
            throw std::runtime_error("Windows completion payload evidence mismatch");
        auto snapshot = journal.trySnapshotFile(name, record.at("size").get<std::size_t>());
        if (!snapshot) { if (manifestPresent) throw std::runtime_error("Windows active completion file disappeared"); continue; }
        if (snapshot->identity != decodeIdentity(record.at("identity")) || snapshot->bytes.size() != record.at("size") ||
            record.at("sha256") != fo4::io::sha256_hex(snapshot->bytes)) throw std::runtime_error("Windows completion file changed");
        privateFiles.emplace_back(name, std::move(*snapshot));
    }
    for (const auto& name : required) if (!recordedNames.contains(name)) throw std::runtime_error("Windows completion omitted required evidence");
    for (std::size_t index = 0; index < paths.size(); ++index) {
        const auto& row = authority.at("entries").at(index); const auto& final = value.at("entries").at(index); const auto& entry = manifest.files[index];
        fields(row, {"index", "path", "directory", "original", "metadata", "stage", "backup"});
        fields(final, {"index", "path", "directory", "final"});
        const auto names = request(manifest, index, paths[index]);
        if (!row.at("index").is_number_unsigned() || row.at("index") != index || row.at("path") != fo4::io::utf8(paths[index]) || row.at("stage") != names.staged || row.at("backup") != names.backup ||
            !final.at("index").is_number_unsigned() || final.at("index") != index || final.at("path") != row.at("path") || final.at("directory") != row.at("directory"))
            throw std::runtime_error("Windows completed participant mismatch");
        const auto artifacts = targets[index]->replacementArtifacts(names, entry.originalSize, entry.finalSize);
        const bool exists = !entry.verifyOnly || entry.originalExists;
        if (artifacts.directory != decodeIdentity(row.at("directory")) || artifacts.staged || artifacts.destination.has_value() != exists || final.at("final").is_null() == exists)
            throw std::runtime_error("Windows completed destination changed");
        if (exists && (artifacts.destination->identity != decodeIdentity(final.at("final")) || artifacts.destination->metadata != decodeMetadata(row.at("metadata")) ||
            artifacts.destination->bytes.size() != (entry.verifyOnly ? entry.originalSize : entry.finalSize) ||
            fo4::io::sha256_hex(artifacts.destination->bytes) != (entry.verifyOnly ? entry.originalHash : entry.finalHash)))
            throw std::runtime_error("Windows completed file identity changed");
        if (artifacts.backup && (!entry.originalExists || entry.verifyOnly || artifacts.backup->identity != decodeIdentity(row.at("original")) ||
            artifacts.backup->metadata != decodeMetadata(row.at("metadata")) || artifacts.backup->bytes.size() != entry.originalSize ||
            fo4::io::sha256_hex(artifacts.backup->bytes) != entry.originalHash)) throw std::runtime_error("Windows cleanup backup changed");
        if (!manifestPresent && artifacts.backup) throw std::runtime_error("Windows completed cleanup has an unexpected backup");
    }
    std::set<std::pair<std::size_t, unsigned>> directoryKeys;
    std::vector<unsigned> finalReceipts(paths.size());
    for (const auto& row : value.at("directories")) {
        fields(row, {"index", "attempt", "name", "identity", "remove"});
        if (!row.at("index").is_number_unsigned() || !row.at("attempt").is_number_unsigned() || row.at("attempt").get<std::uint64_t>() >= 8 || !row.at("remove").is_boolean())
            throw std::runtime_error("invalid Windows cleanup directory");
        const auto index = row.at("index").get<std::size_t>(); const auto attempt = row.at("attempt").get<unsigned>();
        if (index >= targets.size() || attempt >= 8 || !directoryKeys.emplace(index, attempt).second || row.at("name") != stageDirectoryName(manifest, index, attempt) ||
            !recordedNames.contains(receiptName(manifest, index, attempt, true))) throw std::runtime_error("Windows cleanup directory is not authorized");
        const auto binding = Json::parse(embeddedRecord(value, receiptName(manifest, index, attempt, true)));
        fields(binding, {"version", "transaction", "index", "attempt", "parent", "identity", "name"});
        receiptBinding(binding, manifest, index, attempt);
        if (binding.at("parent") != authority.at("entries").at(index).at("directory") || binding.at("identity") != row.at("identity") || binding.at("name") != row.at("name"))
            throw std::runtime_error("Windows cleanup directory receipt changed");
        if (row.at("remove").get<bool>()) {
            if (!recordedNames.contains(receiptName(manifest, index, attempt, false)) || ++finalReceipts[index] != 1)
                throw std::runtime_error("Windows completed file receipt is missing or ambiguous");
            const auto receipt = Json::parse(embeddedRecord(value, receiptName(manifest, index, attempt, false)));
            fields(receipt, {"version", "transaction", "index", "attempt", "sourceDirectory", "sourceName", "exposedName", "stage"});
            receiptBinding(receipt, manifest, index, attempt);
            const auto& rowAuthority = authority.at("entries").at(index); const auto& stage = receipt.at("stage");
            fields(stage, {"identity", "size", "sha256", "metadata"});
            if (receipt.at("sourceDirectory") != row.at("identity") || receipt.at("sourceName") != "payload" || receipt.at("exposedName") != rowAuthority.at("stage") ||
                stage.at("identity") != value.at("entries").at(index).at("final") || !stage.at("size").is_number_unsigned() || stage.at("size") != manifest.files[index].finalSize ||
                stage.at("sha256") != manifest.files[index].finalHash || stage.at("metadata") != rowAuthority.at("metadata"))
                throw std::runtime_error("Windows completed destination is not receipt-bound");
        }
        auto directory = relative(targets[index]->stagingParent(), row.at("name").get<std::string>(), true);
        if (directory) { requirePrivateStageDirectory(directory.get()); if (nativeIdentity(directory.get()) != decodeIdentity(row.at("identity"))) throw std::runtime_error("Windows cleanup directory changed"); }
        else if (!row.at("remove").get<bool>()) throw std::runtime_error("Windows retained directory disappeared");
        if (!manifestPresent && directory && row.at("remove").get<bool>()) throw std::runtime_error("Windows completed cleanup has an unexpected staging directory");
    }
    for (std::size_t index = 0; index < paths.size(); ++index)
        if (finalReceipts[index] != (manifest.files[index].verifyOnly ? 0u : 1u)) throw std::runtime_error("Windows completion lacks final file receipt");
    if (inspectOnly) return true;
    finishRecord(journal, *completion);
    for (std::size_t index = 0; index < paths.size(); ++index) {
        const auto& entry = manifest.files[index]; if (entry.verifyOnly || !entry.originalExists) continue;
        revalidate(); const auto& row = authority.at("entries").at(index); const auto name = row.at("backup").get<std::string>();
        auto file = relative(targets[index]->stagingParent(), name, false, false, DELETE, nullptr, true);
        if (!file) continue;
        const auto snapshot = JournalDestination::stagingSnapshot(file.get(), static_cast<std::size_t>(entry.originalSize));
        if (snapshot.identity != decodeIdentity(row.at("original")) || snapshot.metadata != decodeMetadata(row.at("metadata")) ||
            snapshot.bytes.size() != entry.originalSize || fo4::io::sha256_hex(snapshot.bytes) != entry.originalHash) throw std::runtime_error("Windows backup changed before cleanup");
        FILE_DISPOSITION_INFO disposition{TRUE};
        if (!::SetFileInformationByHandle(file.get(), FileDispositionInfo, &disposition, sizeof(disposition))) throw std::runtime_error("cannot remove verified Windows backup");
        file.reset();
        if (hook) hook(WindowsJournalSessionStep::backupRemoved, index, heldPath(targets[index]->stagingParent()) / name, heldPath(journal.directory_) / completionName);
    }
    for (const auto& row : value.at("directories")) {
        if (!row.at("remove").get<bool>()) continue;
        revalidate(); const auto index = row.at("index").get<std::size_t>(); const auto name = row.at("name").get<std::string>();
        auto directory = relative(targets[index]->stagingParent(), name, true, false, DELETE, nullptr, true);
        if (!directory) continue;
        requirePrivateStageDirectory(directory.get());
        if (nativeIdentity(directory.get()) != decodeIdentity(row.at("identity"))) throw std::runtime_error("Windows staging directory changed before cleanup");
        FILE_DISPOSITION_INFO disposition{TRUE};
        if (!::SetFileInformationByHandle(directory.get(), FileDispositionInfo, &disposition, sizeof(disposition))) throw std::runtime_error("cannot remove verified empty Windows staging directory");
        directory.reset();
        if (hook) hook(WindowsJournalSessionStep::stagingDirectoryRemoved, index, heldPath(targets[index]->stagingParent()) / name, heldPath(journal.directory_) / completionName);
    }
    revalidate();
    const auto manifestName = manifest.transaction + ".manifest";
    const auto manifestFile = std::find_if(privateFiles.begin(), privateFiles.end(), [&](const auto& item) { return item.first == manifestName; });
    if (manifestPresent) {
        if (manifestFile == privateFiles.end()) throw std::runtime_error("Windows cleanup manifest evidence is absent");
        journal.removeSnapshot(manifestName, manifestFile->second);
        if (hook) hook(WindowsJournalSessionStep::manifestRemoved, 0, heldPath(journal.directory_) / manifestName, heldPath(journal.directory_) / completionName);
    }
    for (const auto& [name, snapshot] : privateFiles) if (name != manifestName) {
        journal.removeSnapshot(name, snapshot);
        if (hook) hook(WindowsJournalSessionStep::privateRecordRemoved, 0, heldPath(journal.directory_) / name, heldPath(journal.directory_) / completionName);
    }
    if (value.at("retained").get<bool>()) journal.publishSnapshot(completionName, completion->snapshot, manifest.transaction + ".retained");
    else journal.removeSnapshot(completionName, completion->snapshot);
    return true;
}
}
#endif
