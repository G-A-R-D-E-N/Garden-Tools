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
    // Move transfers exclusive ownership of the stage and OS handle; the
    // source is left empty so its destructor cannot clean up the new owner.
    StagedFile(StagedFile&& other) noexcept;
    StagedFile& operator=(StagedFile&& other);
    ~StagedFile();

    void write(std::span<const std::byte> bytes);
    // Closes the handle and syncs the staged bytes to disk (best effort).
    void finish();
    void verify(std::size_t expectedSize) const;
    const std::filesystem::path& path() const noexcept { return path_; }

    // Atomically replaces the destination with the staged file. Throws on
    // failure (original left intact); cleans up the staged file first.
    void commit() &&;

    // Closes and deletes the staged file. Used to abandon a staging attempt
    // without leaving .radicle-tmp litter behind.
    void cancel() &&;

private:
    StagedFile() = default;

    void close() noexcept;
    bool discard() noexcept;

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

// Coordinates a pair where `primary` may reference data in `dependency`.
// Both new files and a rollback copy of the existing dependency are staged
// before either destination changes. The dependency is committed first, then
// the primary. If the primary replace reports failure, restoring the dependency
// from its rollback stage is attempted before an error is returned. A rollback
// failure is reported together with the primary failure; in that case the new
// dependency may remain committed while the old primary remains untouched.
//
// This deliberately does NOT claim two-file atomicity. A process crash between
// the two replace operations can leave the new dependency with the old primary.
// Callers must therefore guarantee that `dependencyBytes` is backward-compatible
// with the currently committed primary. For localized plugins that means the
// new string table must preserve every ID/text pair the old plugin can still
// reference until the primary commit moves those references forward.
//
// The dependency must already exist as a regular, non-symlink file so a rollback
// snapshot can be taken fail-closed. The primary destination is left to the
// platform replace primitive; callers are still responsible for their normal
// destination trust/permission validation before entering this helper.
void write_file_pair_dependency_first(
    const std::filesystem::path& dependency,
    std::span<const std::byte> dependencyBytes,
    const std::filesystem::path& primary,
    std::span<const std::byte> primaryBytes);

}
