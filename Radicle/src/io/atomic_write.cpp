#include "fo4/io/atomic_write.hpp"
#include "fo4/io/path_text.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <windows.h>
  #include <aclapi.h>
#else
  #include <fcntl.h>
  #include <sys/stat.h>
  #include <unistd.h>
#endif

namespace fo4::io {
namespace {

std::filesystem::path temp_sibling(const std::filesystem::path& path) {
  static std::atomic<std::uint64_t> counter{0};
  const auto unique = static_cast<std::uint64_t>(
                          std::chrono::steady_clock::now().time_since_epoch().count()) ^
                      (counter.fetch_add(1, std::memory_order_relaxed) << 32u);
  for (int attempt = 0; attempt < 8; ++attempt) {
    auto candidate = path;
    candidate += ".radicle-tmp-" + std::to_string(unique) + "-" + std::to_string(attempt);
    std::error_code ec;
    if (!std::filesystem::exists(candidate, ec)) {
      return candidate;
    }
  }
  auto fallback = path;
  return fallback += ".radicle-tmp";
}

[[noreturn]] void discard_and_throw(const std::filesystem::path& temp,
                                    const std::string& message) {
  std::error_code remove_ec;
  std::filesystem::remove(temp, remove_ec);
  throw std::runtime_error(message);
}

std::vector<std::byte> read_existing_regular_file(
    const std::filesystem::path& path) {
#if defined(_WIN32)
  const auto handle = ::CreateFileW(
      path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    throw std::runtime_error(
        "unable to open dependency for rollback without following reparse points: " +
        utf8(path) + " (error " + std::to_string(::GetLastError()) + ")");
  }

  try {
    BY_HANDLE_FILE_INFORMATION info{};
    if (!::GetFileInformationByHandle(handle, &info)) {
      throw std::runtime_error(
          "unable to inspect dependency for rollback: " + utf8(path) +
          " (error " + std::to_string(::GetLastError()) + ")");
    }
    if ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
      throw std::runtime_error(
          "dependency must exist as a regular, non-reparse-point file: " +
          utf8(path));
    }

    LARGE_INTEGER size{};
    if (!::GetFileSizeEx(handle, &size) || size.QuadPart < 0 ||
        static_cast<std::uint64_t>(size.QuadPart) >
            static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
      throw std::runtime_error("unable to determine dependency size: " + utf8(path));
    }

    std::vector<std::byte> bytes(static_cast<std::size_t>(size.QuadPart));
    auto* data = reinterpret_cast<char*>(bytes.data());
    std::size_t remaining = bytes.size();
    constexpr auto k_max_chunk = static_cast<std::size_t>(0xFFFFFFFFu);
    while (remaining > 0) {
      const auto chunk = static_cast<DWORD>(remaining < k_max_chunk ? remaining : k_max_chunk);
      DWORD read = 0;
      if (!::ReadFile(handle, data, chunk, &read, nullptr) || read == 0) {
        throw std::runtime_error(
            "unable to read dependency for rollback: " + utf8(path) +
            " (error " + std::to_string(::GetLastError()) + ")");
      }
      data += read;
      remaining -= read;
    }

    ::CloseHandle(handle);
    return bytes;
  } catch (...) {
    ::CloseHandle(handle);
    throw;
  }
#else
  const auto fd = ::open(path.c_str(), O_RDONLY | O_NOFOLLOW);
  if (fd < 0) {
    throw std::runtime_error(
        "unable to open dependency for rollback without following symlinks: " +
        utf8(path) + ": " + std::strerror(errno));
  }

  try {
    struct stat status {};
    if (::fstat(fd, &status) != 0 || !S_ISREG(status.st_mode) || status.st_size < 0 ||
        static_cast<std::uintmax_t>(status.st_size) >
            static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max())) {
      throw std::runtime_error(
          "dependency must exist as a readable regular, non-symlink file: " +
          utf8(path));
    }

    std::vector<std::byte> bytes(static_cast<std::size_t>(status.st_size));
    auto* data = reinterpret_cast<char*>(bytes.data());
    std::size_t remaining = bytes.size();
    while (remaining > 0) {
      const auto read = ::read(fd, data, remaining);
      if (read < 0 && errno == EINTR) continue;
      if (read <= 0) {
        throw std::runtime_error(
            "unable to read dependency for rollback: " + utf8(path) +
            (read < 0 ? ": " + std::string(std::strerror(errno)) : ": unexpected EOF"));
      }
      data += read;
      remaining -= static_cast<std::size_t>(read);
    }

    ::close(fd);
    return bytes;
  } catch (...) {
    ::close(fd);
    throw;
  }
#endif
}

#if defined(_WIN32)
// Best effort by design: a failure here must never fail the publish.
void copy_dacl_to_staged(const std::filesystem::path& from, const std::filesystem::path& to) {
  PACL dacl = nullptr;
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  if (::GetNamedSecurityInfoW(from.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                              nullptr, nullptr, &dacl, nullptr, &descriptor) != ERROR_SUCCESS) {
    return;
  }
  SECURITY_DESCRIPTOR_CONTROL control = 0;
  DWORD revision = 0;
  if (::GetSecurityDescriptorControl(descriptor, &control, &revision)) {
    const SECURITY_INFORMATION info =
        DACL_SECURITY_INFORMATION |
        ((control & SE_DACL_PROTECTED) != 0 ? PROTECTED_DACL_SECURITY_INFORMATION
                                            : UNPROTECTED_DACL_SECURITY_INFORMATION);
    static_cast<void>(::SetNamedSecurityInfoW(const_cast<wchar_t*>(to.c_str()), SE_FILE_OBJECT,
                                              info, nullptr, nullptr, dacl, nullptr));
  }
  ::LocalFree(descriptor);
}
#endif

StagedFile stage_bytes(const std::filesystem::path& path,
                       const std::span<const std::byte> bytes) {
  auto staged = StagedFile::create(path);
  staged.write(bytes);
  staged.finish();
  try {
    staged.verify(bytes.size());
  } catch (...) {
    std::move(staged).cancel();
    throw;
  }
  return staged;
}

}  // namespace

#if defined(_WIN32)
StagedFile::StagedFile(StagedFile&& other) noexcept
    : path_(std::move(other.path_)),
      destination_(std::move(other.destination_)),
      handle_(std::exchange(other.handle_, nullptr)) {
  other.path_.clear();
  other.destination_.clear();
}
#else
StagedFile::StagedFile(StagedFile&& other) noexcept
    : path_(std::move(other.path_)),
      destination_(std::move(other.destination_)),
      fd_(std::exchange(other.fd_, -1)) {
  other.path_.clear();
  other.destination_.clear();
}
#endif

StagedFile& StagedFile::operator=(StagedFile&& other) {
  if (this != &other) {
    if (!discard()) {
      throw std::runtime_error("unable to discard staged file " + utf8(path_));
    }
    path_ = std::move(other.path_);
    destination_ = std::move(other.destination_);
#if defined(_WIN32)
    handle_ = std::exchange(other.handle_, nullptr);
#else
    fd_ = std::exchange(other.fd_, -1);
#endif
    other.path_.clear();
    other.destination_.clear();
  }
  return *this;
}

StagedFile::~StagedFile() { discard(); }

StagedFile StagedFile::create(const std::filesystem::path& destination) {
  if (destination.empty()) {
    throw std::runtime_error("destination path is empty");
  }

  const auto candidate = temp_sibling(destination);
  StagedFile staged;

#if defined(_WIN32)
  const auto handle = ::CreateFileW(candidate.c_str(), GENERIC_WRITE,
                                    FILE_SHARE_READ, nullptr, CREATE_NEW,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    // The candidate is not ours unless CREATE_NEW succeeds. In particular,
    // losing a collision race must never make RAII cleanup delete the file
    // that another process created at this path.
    throw std::runtime_error("unable to create exclusive staging file " +
                             utf8(candidate));
  }
  try {
    staged.path_ = candidate;
    staged.destination_ = destination;
    staged.handle_ = handle;
  } catch (...) {
    ::CloseHandle(handle);
    std::error_code remove_ec;
    std::filesystem::remove(candidate, remove_ec);
    throw;
  }
#else
  const auto fd = ::open(candidate.c_str(),
                         O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0644);
  if (fd < 0) {
    // The candidate is not ours unless O_EXCL succeeds. In particular,
    // losing a collision race must never make RAII cleanup delete the file
    // that another process created at this path.
    throw std::runtime_error("unable to create exclusive staging file " +
                             utf8(candidate));
  }
  try {
    staged.path_ = candidate;
    staged.destination_ = destination;
    staged.fd_ = fd;
  } catch (...) {
    ::close(fd);
    std::error_code remove_ec;
    std::filesystem::remove(candidate, remove_ec);
    throw;
  }
#endif
  return staged;
}

void StagedFile::write(const std::span<const std::byte> bytes) {
#if defined(_WIN32)
  const auto* data = reinterpret_cast<const char*>(bytes.data());
  std::size_t remaining = bytes.size();
  // WriteFile accepts at most DWORD bytes per call; a size_t cast would
  // truncate at exactly 4 GiB and loop forever above it. Chunk explicitly.
  constexpr std::size_t k_max_chunk = static_cast<std::size_t>(0xFFFFFFFFu);
  while (remaining > 0) {
    const auto chunk = static_cast<DWORD>(remaining < k_max_chunk ? remaining : k_max_chunk);
    DWORD written = 0;
    if (!::WriteFile(static_cast<HANDLE>(handle_), data, chunk, &written, nullptr) ||
        written == 0) {
      const auto error = ::GetLastError();
      close();
      discard_and_throw(path_,
                        "unable to write staged file " + utf8(path_) +
                            " (error " + std::to_string(error) + ")");
    }
    data += written;
    remaining -= written;
  }
#else
  const auto* data = reinterpret_cast<const char*>(bytes.data());
  std::size_t remaining = bytes.size();
  while (remaining > 0) {
    const auto written = ::write(fd_, data, remaining);
    if (written <= 0) {
      if (written < 0) {
        const int saved = errno;
        close();
        discard_and_throw(path_,
                          "unable to write staged file " + utf8(path_) +
                              ": " + std::strerror(saved));
      }
      close();
      discard_and_throw(path_, "short write to staged file " + utf8(path_));
    }
    data += written;
    remaining -= static_cast<std::size_t>(written);
  }
#endif
}

void StagedFile::finish() {
#if defined(_WIN32)
  if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) {
    const auto handle = static_cast<HANDLE>(handle_);
    handle_ = nullptr;
    if (!::FlushFileBuffers(handle)) {
      const auto error = ::GetLastError();
      ::CloseHandle(handle);
      discard_and_throw(path_, "unable to flush staged file " + utf8(path_) +
                                  " (error " + std::to_string(error) + ")");
    }
    if (!::CloseHandle(handle)) {
      const auto error = ::GetLastError();
      discard_and_throw(path_, "unable to close staged file " + utf8(path_) +
                                  " (error " + std::to_string(error) + ")");
    }
  }
#else
  if (fd_ >= 0) {
    const auto fd = std::exchange(fd_, -1);
    if (::fsync(fd) != 0) {
      const int saved = errno;
      ::close(fd);
      discard_and_throw(path_, "unable to flush staged file " + utf8(path_) +
                                  ": " + std::strerror(saved));
    }
    if (::close(fd) != 0) {
      const int saved = errno;
      discard_and_throw(path_, "unable to close staged file " + utf8(path_) +
                                  ": " + std::strerror(saved));
    }
  }
#endif
}

void StagedFile::verify(const std::size_t expectedSize) const {
  std::error_code ec;
  const auto status = std::filesystem::symlink_status(path_, ec);
  if (ec || !std::filesystem::is_regular_file(status)) {
    throw std::runtime_error("staged output is not a regular file: " + utf8(path_));
  }
  const auto size = std::filesystem::file_size(path_, ec);
  if (ec || size != expectedSize) {
    throw std::runtime_error("staged output size mismatch: " + utf8(path_));
  }
}

void StagedFile::commit() && {
  static_cast<void>(publish(true));
}

bool StagedFile::commit_if_absent() && {
  return publish(false);
}

bool StagedFile::publish(const bool replace_existing) {
  std::error_code status_ec;
  const auto existing = std::filesystem::symlink_status(destination_, status_ec);
  if (!status_ec && std::filesystem::is_regular_file(existing)) {
    std::error_code perm_ec;
    std::filesystem::permissions(path_, existing.permissions(), perm_ec);
#if defined(_WIN32)
    if (replace_existing) copy_dacl_to_staged(destination_, path_);
#endif
  }

#if defined(_WIN32)
  const DWORD flags = replace_existing ? (MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)
                                       : MOVEFILE_WRITE_THROUGH;
  const auto replaced = ::MoveFileExW(path_.c_str(), destination_.c_str(), flags) != 0;
  const auto error = replaced ? DWORD{0} : ::GetLastError();
  const auto exists = !replace_existing && (error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS);
#else
  std::error_code ec;
  if (replace_existing) {
    std::filesystem::rename(path_, destination_, ec);
  } else if (::link(path_.c_str(), destination_.c_str()) == 0) {
    std::error_code remove_ec;
    std::filesystem::remove(path_, remove_ec);
  } else if (const auto link_error = errno; link_error == EPERM || link_error == ENOTSUP || link_error == EXDEV) {
    const int placeholder = ::open(destination_.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0666);
    if (placeholder < 0) {
      ec = std::error_code(errno, std::generic_category());
    } else {
      ::close(placeholder);
      std::filesystem::rename(path_, destination_, ec);
      if (ec) ::unlink(destination_.c_str());
    }
  } else {
    ec = std::error_code(link_error, std::generic_category());
  }
  const auto replaced = !ec;
  const auto exists = !replace_existing && ec == std::errc::file_exists;
#endif

  if (exists) {
    static_cast<void>(discard());
    return false;
  }

  if (!replaced) {
    const auto destination = utf8(destination_);
    std::error_code remove_ec;
    std::filesystem::remove(path_, remove_ec);
    if (!remove_ec) {
      path_.clear();
      destination_.clear();
    }
#if defined(_WIN32)
    throw std::runtime_error("unable to replace " + destination +
                             " atomically (error " + std::to_string(error) +
                             "); original file left untouched");
#else
    throw std::runtime_error("unable to replace " + destination +
                             " atomically: " + ec.message() +
                             "; original file left untouched");
#endif
  }

  path_.clear();
  destination_.clear();
  return true;
}

void StagedFile::cancel() && {
  static_cast<void>(discard());
}

bool StagedFile::discard() noexcept {
  close();
  if (!path_.empty()) {
    std::error_code remove_ec;
    std::filesystem::remove(path_, remove_ec);
    if (remove_ec) return false;
    path_.clear();
  }
  destination_.clear();
  return true;
}

void StagedFile::close() noexcept {
#if !defined(_WIN32)
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
#else
  if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) {
    ::CloseHandle(static_cast<HANDLE>(handle_));
    handle_ = nullptr;
  }
#endif
}

void write_file_atomic(const std::filesystem::path& path,
                       const std::span<const std::byte> bytes) {
  if (!path.parent_path().empty()) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
      throw std::runtime_error("unable to create output directory " +
                               utf8(path.parent_path()) + ": " + ec.message());
    }
  }

  auto staged = StagedFile::create(path);
  staged.write(bytes);
  staged.finish();
  try {
    if (!bytes.empty()) {
      staged.verify(bytes.size());
    }
  } catch (...) {
    // Abandoning the attempt must not leave .radicle-tmp litter.
    std::move(staged).cancel();
    throw;
  }
  std::move(staged).commit();
}

void write_rotating_backup(const std::filesystem::path& target,
                           const std::span<const std::byte> original,
                           const int keep) {
  if (keep < 1) throw std::runtime_error("backup count must be at least one");
  const auto slot = [&target](int index) {
    auto name = target;
    name += index == 0 ? std::string(".bak") : ".bak." + std::to_string(index);
    return name;
  };
  const auto fail = [](const char* what, const std::filesystem::path& at, const std::error_code& ec) {
    throw std::runtime_error(std::string(what) + " " + utf8(at) + ": " + ec.message());
  };
  const auto holds_original = [&original](const std::filesystem::path& file) {
    std::error_code check_ec;
    if (!std::filesystem::is_regular_file(file, check_ec) || check_ec) return false;
    if (std::filesystem::file_size(file, check_ec) != original.size() || check_ec) return false;
    std::ifstream in(file, std::ios::binary);
    if (!in) return false;
    std::vector<char> held(original.size());
    in.read(held.data(), static_cast<std::streamsize>(held.size()));
    if (static_cast<std::size_t>(in.gcount()) != held.size()) return false;
    return std::equal(held.begin(), held.end(), reinterpret_cast<const char*>(original.data()));
  };
  if (holds_original(slot(0))) return;
  auto staged = stage_bytes(slot(0), original);
  try {
    std::error_code ec;
    for (int index = keep - 1; index >= 1; --index) {
      const auto from = slot(index - 1);
      const auto to = slot(index);
      const bool present = std::filesystem::exists(from, ec);
      if (ec) fail("unable to inspect the backup", from, ec);
      if (!present) continue;
      if (index == keep - 1) {
        std::filesystem::remove(to, ec);
        if (ec) fail("unable to drop the oldest backup", to, ec);
      }
      std::filesystem::rename(from, to, ec);
      if (ec) fail("unable to rotate the backup", from, ec);
    }
    std::move(staged).commit();
  } catch (...) {
    std::move(staged).cancel();
    throw;
  }
}

void write_file_pair_dependency_first(
    const std::filesystem::path& dependency,
    const std::span<const std::byte> dependencyBytes,
    const std::filesystem::path& primary,
    const std::span<const std::byte> primaryBytes) {
  if (dependency.empty() || primary.empty()) {
    throw std::runtime_error("coordinated file destinations must not be empty");
  }

  std::error_code equivalent_ec;
  if (std::filesystem::equivalent(dependency, primary, equivalent_ec) &&
      !equivalent_ec) {
    throw std::runtime_error("dependency and primary destinations must be distinct");
  }

  // Take the rollback snapshot before creating any staged output. Requiring a
  // regular non-symlink dependency is intentional: the localization contract
  // only authorizes an already-resolved editable loose sidecar.
  const auto original_dependency = read_existing_regular_file(dependency);

  // Nothing has changed on disk until all three stages exist, are closed, and
  // match their expected sizes. Any staging/verification error therefore
  // leaves both destinations untouched and RAII removes every completed stage.
  auto dependency_stage = stage_bytes(dependency, dependencyBytes);
  auto primary_stage = stage_bytes(primary, primaryBytes);
  auto rollback_stage = stage_bytes(dependency, original_dependency);

  // Commit the dependency first. For the localized-CNAM use case the new
  // sidecar is a backward-compatible superset of what the old plugin can
  // reference, so a process crash here leaves a usable old-plugin/new-sidecar
  // pair rather than a plugin that points at a missing string ID.
  std::move(dependency_stage).commit();

  try {
    std::move(primary_stage).commit();
  } catch (const std::exception& primary_error) {
    try {
      std::move(rollback_stage).commit();
    } catch (const std::exception& rollback_error) {
      throw std::runtime_error(
          "primary commit failed after dependency commit, and dependency rollback also failed: " +
          std::string(primary_error.what()) + "; rollback: " + rollback_error.what());
    }
    throw std::runtime_error(
        "primary commit failed after dependency commit; dependency restored: " +
        std::string(primary_error.what()));
  }

  // Both destinations are now committed. The rollback copy is no longer
  // needed; cancel explicitly so successful transactions leave no temp file.
  std::move(rollback_stage).cancel();
}

}  // namespace fo4::io
