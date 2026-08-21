#pragma once

#include <filesystem>
#include <span>

namespace fo4::io {

// Serializes `bytes` to `path` without ever truncating the existing file in
// place: the payload lands in a sibling temporary file first and is renamed
// over the destination only after the stream has been flushed and closed.
// A failed or interrupted write therefore leaves the previous file intact.
//
// Throws std::runtime_error when the temporary file cannot be created,
// written, or renamed into place. Parent directories are created as needed.
void write_file_atomic(const std::filesystem::path& path,
                       std::span<const std::byte> bytes);

}
