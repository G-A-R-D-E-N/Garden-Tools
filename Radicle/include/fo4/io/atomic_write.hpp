#pragma once

#include <filesystem>
#include <span>
#include <string>

namespace fo4::io {

// A staged file next to its eventual destination. Creation is exclusive
// (O_CREAT|O_EXCL, plus O_NOFOLLOW on POSIX) so a raced symlink or
// pre-existing file at the staging path can never be written through.
// After write() + finish(), commit() replaces the destination with a single
// platform replace primitive — rename(2) on POSIX, MoveFileExW with
// MOVEFILE_REPLACE_EXISTING on Windows — so the destination is never absent
// and never observed truncated. The original is only ever touched by that
// one primitive: if it fails, the original is untouched.
class StagedFile {
public:
    // Chooses a collision-resistant sibling name of `destination` and
    // creates it exclusively. Throws std::runtime_error on failure.
    static StagedFile create(const std::filesystem::path& destination);

    StagedFile(const StagedFile&) = delete;
    StagedFile& operator=(const StagedFile&) = delete;
    StagedFile(StagedFile&&) noexcept;
    StagedFile& operator=(StagedFile&&) noexcept;
    ~StagedFile();

    void write(std::span<const std::byte> bytes);
    // Closes the handle and syncs the staged bytes to disk (best effort).
    void finish();
    void verify(std::size_t expectedSize) const;
    const std::filesystem::path& path() const noexcept { return path_; }

    // Atomically replaces the destination with the staged file. Throws on
    // failure (original left intact); cleans up the staged file first.
    void commit() &&;

private:
    StagedFile() = default;

    void close() noexcept;

    std::filesystem::path path_;
    std::filesystem::path destination_;
#if !defined(_WIN32)
    int fd_{-1};
#else
    void* handle_{nullptr};  // HANDLE, type-erased to keep windows.h out of headers
#endif
};

// Convenience wrapper: stage, write, finish, and commit in one call.
void write_file_atomic(const std::filesystem::path& path,
                       std::span<const std::byte> bytes);

}
