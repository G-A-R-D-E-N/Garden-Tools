#pragma once

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winternl.h>

namespace fo4ide::plugin::detail {

enum class WindowsJournalFileInformationClass : int { ExtendedAttributes = 7, Rename = 10 };

extern "C" NTSYSAPI NTSTATUS NTAPI NtQueryInformationFile(
    HANDLE file, PIO_STATUS_BLOCK status, PVOID information, ULONG length, WindowsJournalFileInformationClass infoClass);

extern "C" NTSYSAPI NTSTATUS NTAPI NtSetInformationFile(
    HANDLE file, PIO_STATUS_BLOCK status, PVOID information, ULONG length, WindowsJournalFileInformationClass infoClass);

struct WindowsJournalRenameInfo {
    BOOLEAN ReplaceIfExists;
    HANDLE RootDirectory;
    ULONG FileNameLength;
    WCHAR FileName[1];
};

}
#endif
