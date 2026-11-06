#include <fo4ide/papyrus/SecureSourceSnapshot.hpp>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winternl.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace fo4ide::papyrus {
namespace {

#ifdef FO4IDE_TEST_HOOKS
SnapshotReadHook beforeOpenHook = nullptr;
SnapshotReadHook readHook = nullptr;
SnapshotReadHook afterDirectoryOpenHook = nullptr;
#endif

class Handle {
public:
    explicit Handle(HANDLE value) : value_(value) {
        if (!value_ || value_ == INVALID_HANDLE_VALUE) throw std::runtime_error("cannot open Papyrus import entry");
    }
    ~Handle() { ::CloseHandle(value_); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE get() const { return value_; }
private:
    HANDLE value_;
};

bool isSource(std::wstring name) {
    for (auto& ch : name) if (ch >= L'A' && ch <= L'Z') ch = static_cast<wchar_t>(ch + (L'a' - L'A'));
    return name.ends_with(L".psc");
}

bool isFlags(std::wstring name) {
    for (auto& ch : name) if (ch >= L'A' && ch <= L'Z') ch = static_cast<wchar_t>(ch + (L'a' - L'A'));
    return name == L"institute_papyrus_flags.flg";
}

Handle openChild(HANDLE parent, const std::wstring& name, bool directory) {
    if (name.empty() || name == L"." || name == L".." ||
        name.find_first_of(L"\\/:") != std::wstring::npos ||
        name.size() > std::numeric_limits<USHORT>::max() / sizeof(wchar_t)) {
        throw std::runtime_error("invalid Papyrus import entry name");
    }
    UNICODE_STRING text{};
    text.Length = static_cast<USHORT>(name.size() * sizeof(wchar_t));
    text.MaximumLength = text.Length;
    text.Buffer = const_cast<wchar_t*>(name.data());
    OBJECT_ATTRIBUTES attributes{};
    attributes.Length = static_cast<ULONG>(sizeof(attributes));
    attributes.RootDirectory = parent;
    attributes.ObjectName = &text;
    IO_STATUS_BLOCK statusBlock{};
    HANDLE result = nullptr;
    const ACCESS_MASK access = SYNCHRONIZE | FILE_READ_ATTRIBUTES |
                               (directory ? FILE_LIST_DIRECTORY : GENERIC_READ);
    const ULONG sharing = directory ? FILE_SHARE_READ | FILE_SHARE_WRITE : FILE_SHARE_READ;
    const NTSTATUS status = ::NtCreateFile(&result, access, &attributes, &statusBlock, nullptr,
        0, sharing, FILE_OPEN, FILE_OPEN_REPARSE_POINT | FILE_SYNCHRONOUS_IO_NONALERT, nullptr, 0);
    if (status < 0) throw std::runtime_error("cannot open Papyrus import entry relative to its directory");
    return Handle(result);
}

void collect(HANDLE directory, const std::filesystem::path& relative, DWORD volume,
             std::size_t maxEntries, std::size_t maxFileBytes, std::size_t maxTotalBytes,
             std::size_t& entries, std::size_t& total, std::vector<SnapshotSource>& result,
             std::size_t depth) {
    std::vector<std::uint64_t> buffer(8192);
    const auto bufferBytes = buffer.size() * sizeof(buffer.front());
    bool first = true;
    for (;;) {
        const auto informationClass = first ? FileIdBothDirectoryRestartInfo : FileIdBothDirectoryInfo;
        first = false;
        if (!::GetFileInformationByHandleEx(directory, informationClass, buffer.data(), static_cast<DWORD>(bufferBytes))) {
            const auto error = ::GetLastError();
            if (error == ERROR_NO_MORE_FILES) break;
            throw std::system_error(static_cast<int>(error), std::system_category(), "enumerate Papyrus import directory");
        }
        std::size_t offset = 0;
        for (;;) {
            if (offset % alignof(FILE_ID_BOTH_DIR_INFO) != 0 ||
                offset > bufferBytes - offsetof(FILE_ID_BOTH_DIR_INFO, FileName)) {
                throw std::runtime_error("invalid import directory entry");
            }
            const auto* entry = reinterpret_cast<const FILE_ID_BOTH_DIR_INFO*>(
                reinterpret_cast<const std::byte*>(buffer.data()) + offset);
            const auto remaining = bufferBytes - offset - offsetof(FILE_ID_BOTH_DIR_INFO, FileName);
            if (entry->FileNameLength % sizeof(wchar_t) != 0 || entry->FileNameLength > remaining) {
                throw std::runtime_error("invalid import directory name");
            }
            const std::wstring name(entry->FileName, entry->FileNameLength / sizeof(wchar_t));
            if (name != L"." && name != L"..") {
                if (++entries > maxEntries) throw std::runtime_error("Papyrus import tree exceeds its entry limit");
                const bool directoryEntry = (entry->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                if ((entry->FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) throw std::runtime_error("reparse point in import tree");
                if (directoryEntry || isSource(name) || (relative.empty() && isFlags(name))) {
#ifdef FO4IDE_TEST_HOOKS
                    if (beforeOpenHook) beforeOpenHook();
#endif
                    auto child = openChild(directory, name, directoryEntry);
                    BY_HANDLE_FILE_INFORMATION info{};
                    if (!::GetFileInformationByHandle(child.get(), &info) ||
                        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
                        ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) != directoryEntry ||
                        info.dwVolumeSerialNumber != volume || ::GetFileType(child.get()) != FILE_TYPE_DISK ||
                        ((static_cast<std::uint64_t>(info.nFileIndexHigh) << 32) | info.nFileIndexLow) !=
                            static_cast<std::uint64_t>(entry->FileId.QuadPart)) {
                        throw std::runtime_error("Papyrus import entry changed while opening");
                    }
                    if (directoryEntry) {
                        if (depth >= 64) throw std::runtime_error("Papyrus import tree exceeds its depth limit");
#ifdef FO4IDE_TEST_HOOKS
                        if (afterDirectoryOpenHook) afterDirectoryOpenHook();
#endif
                        collect(child.get(), relative / name, volume, maxEntries, maxFileBytes, maxTotalBytes,
                                entries, total, result, depth + 1);
                    } else {
                        LARGE_INTEGER size{};
                        FILE_BASIC_INFO before{};
                        if (info.nNumberOfLinks != 1 || !::GetFileSizeEx(child.get(), &size) || size.QuadPart < 0 ||
                            static_cast<std::uint64_t>(size.QuadPart) > maxFileBytes ||
                            !::GetFileInformationByHandleEx(child.get(), FileBasicInfo, &before, static_cast<DWORD>(sizeof(before)))) {
                            throw std::runtime_error("Papyrus source is not a bounded regular file");
                        }
                        std::string text;
                        std::array<char, 8192> chunk{};
                        for (;;) {
                            DWORD count = 0;
                            if (!::ReadFile(child.get(), chunk.data(), static_cast<DWORD>(chunk.size()), &count, nullptr)) {
                                throw std::runtime_error("cannot read Papyrus source");
                            }
                            if (!count) break;
                            if (count > maxFileBytes - text.size() || count > maxTotalBytes - total) {
                                throw std::runtime_error("Papyrus source exceeds its byte limit");
                            }
                            text.append(chunk.data(), count);
                            total += count;
                        }
#ifdef FO4IDE_TEST_HOOKS
                        if (readHook) readHook();
#endif
                        FILE_BASIC_INFO after{};
                        LARGE_INTEGER sizeAfter{};
                        if (!::GetFileSizeEx(child.get(), &sizeAfter) ||
                            !::GetFileInformationByHandleEx(child.get(), FileBasicInfo, &after, static_cast<DWORD>(sizeof(after))) ||
                            sizeAfter.QuadPart != size.QuadPart || text.size() != static_cast<std::uint64_t>(size.QuadPart) ||
                            before.LastWriteTime.QuadPart != after.LastWriteTime.QuadPart ||
                            before.ChangeTime.QuadPart != after.ChangeTime.QuadPart) {
                            throw std::runtime_error("Papyrus source changed while reading");
                        }
                        result.push_back({relative / name, std::move(text)});
                    }
                }
            }
            if (!entry->NextEntryOffset) break;
            if (entry->NextEntryOffset > bufferBytes - offset) throw std::runtime_error("invalid import directory offsets");
            offset += entry->NextEntryOffset;
        }
    }
}

}

#ifdef FO4IDE_TEST_HOOKS
void setSnapshotReadHook(SnapshotReadHook hook) { readHook = hook; }
void setSnapshotBeforeOpenHook(SnapshotReadHook hook) { beforeOpenHook = hook; }
void setSnapshotAfterDirectoryOpenHook(SnapshotReadHook hook) { afterDirectoryOpenHook = hook; }
#endif

std::vector<SnapshotSource> snapshotPapyrusSources(std::intptr_t authorizedRoot,
                                                   std::size_t maxEntries,
                                                   std::size_t maxFileBytes,
                                                   std::size_t maxTotalBytes) {
    const auto root = reinterpret_cast<HANDLE>(authorizedRoot);
    if (!root || root == INVALID_HANDLE_VALUE || !maxEntries || !maxFileBytes || !maxTotalBytes) {
        throw std::runtime_error("invalid Papyrus snapshot limits or root");
    }
    HANDLE duplicate = nullptr;
    if (!::DuplicateHandle(::GetCurrentProcess(), root, ::GetCurrentProcess(), &duplicate, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
        throw std::runtime_error("cannot duplicate authorized import root");
    }
    Handle held(duplicate);
    BY_HANDLE_FILE_INFORMATION info{};
    if (!::GetFileInformationByHandle(held.get(), &info) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 || ::GetFileType(held.get()) != FILE_TYPE_DISK) {
        throw std::runtime_error("authorized import root is not a plain directory");
    }
    std::size_t entries = 0, total = 0;
    std::vector<SnapshotSource> result;
    collect(held.get(), {}, info.dwVolumeSerialNumber, maxEntries, maxFileBytes, maxTotalBytes, entries, total, result, 0);
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.relativePath < b.relativePath; });
    return result;
}

}
#endif
