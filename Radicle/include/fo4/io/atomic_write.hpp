#pragma once

#include <filesystem>
#include <span>

namespace fo4::io {

// Serializes `bytes` to `path` without ever truncating or removing the
// existing file in place: the payload lands in a sibling temporary file, is
// flushed, and is then moved over the destination with a single replace
// primitive (rename() on POSIX, MoveFileExW(REPLACE_EXISTING) on Windows).
// If any step fails — including the final replacement — the previous file is
// left byte-for-byte intact and only the temp file is cleaned up.
//
// Durability of the staged bytes before the rename is best-effort (fsync /
// FlushFileBuffers). Throws std::runtime_error when the write cannot be
// completed. Parent directories are created as needed.
void write_file_atomic(const std::filesystem::path& path,
                       std::span<const std::byte> bytes);

}
