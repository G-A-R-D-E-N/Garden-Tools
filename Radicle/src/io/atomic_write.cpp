#include "fo4/io/atomic_write.hpp"

#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>

#if defined(_WIN32)
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <windows.h>
#else
  #include <fcntl.h>
  #include <unistd.h>
#endif

namespace fo4::io {
namespace {

std::filesystem::path temp_sibling(const std::filesystem::path& path) {
  const auto parent = path.parent_path();
  const auto name = path.filename().string();
  static std::atomic<std::uint64_t> counter{0};
  const auto unique = static_cast<std::uint64_t>(
                          std::chrono::steady_clock::now().time_since_epoch().count()) ^
                      (counter.fetch_add(1, std::memory_order_relaxed) << 32u);
  for (int attempt = 0; attempt < 8; ++attempt) {
    const auto candidate =
        parent / (name + ".radicle-tmp-" + std::to_string(unique) + "-" +
                  std::to_string(attempt));
    std::error_code ec;
    if (!std::filesystem::exists(candidate, ec)) {
      return candidate;
    }
  }
  return parent / (name + ".radicle-tmp");
}

[[noreturn]] void discard_and_throw(const std::filesystem::path& temp,
                                    const std::string& message) {
  std::error_code remove_ec;
  std::filesystem::remove(temp, remove_ec);
  throw std::runtime_error(message);
}

}  // namespace

StagedFile::StagedFile(StagedFile&&) noexcept = default;
StagedFile& StagedFile::operator=(StagedFile&&) noexcept = default;

StagedFile::~StagedFile() { close(); }

StagedFile StagedFile::create(const std::filesystem::path& destination) {
  if (destination.empty()) {
    throw std::runtime_error("destination path is empty");
  }
  StagedFile staged;
  staged.destination_ = destination;
  staged.path_ = temp_sibling(destination);

#if defined(_WIN32)
  staged.handle_ = ::CreateFileW(staged.path_.c_str(), GENERIC_WRITE,
                                 FILE_SHARE_READ, nullptr, CREATE_NEW,
                                 FILE_ATTRIBUTE_NORMAL, nullptr);
  if (staged.handle_ == INVALID_HANDLE_VALUE) {
    // Deliberately do not retry: a collision or hostile pre-created file at
    // the staging path must fail closed.
    throw std::runtime_error("unable to create exclusive staging file " +
                             staged.path_.string());
  }
#else
  staged.fd_ = ::open(staged.path_.c_str(),
                      O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0644);
  if (staged.fd_ < 0) {
    throw std::runtime_error("unable to create exclusive staging file " +
                             staged.path_.string());
  }
#endif
  return staged;
}

void StagedFile::write(const std::span<const std::byte> bytes) {
#if defined(_WIN32)
  const auto* data = reinterpret_cast<const char*>(bytes.data());
  std::size_t remaining = bytes.size();
  while (remaining > 0) {
    DWORD written = 0;
    if (!::WriteFile(static_cast<HANDLE>(handle_), data,
                     static_cast<DWORD>(remaining), &written, nullptr) ||
        written == 0) {
      const auto error = ::GetLastError();
      close();
      discard_and_throw(path_,
                        "unable to write staged file " + path_.string() +
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
                          "unable to write staged file " + path_.string() +
                              ": " + std::strerror(saved));
      }
      close();
      discard_and_throw(path_, "short write to staged file " + path_.string());
    }
    data += written;
    remaining -= static_cast<std::size_t>(written);
  }
#endif
}

void StagedFile::finish() {
#if defined(_WIN32)
  if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) {
    ::FlushFileBuffers(static_cast<HANDLE>(handle_));  // best effort
  }
#else
  if (fd_ >= 0) {
    ::fsync(fd_);  // best effort
  }
#endif
  close();
}

void StagedFile::verify(const std::size_t expectedSize) const {
  std::error_code ec;
  const auto status = std::filesystem::symlink_status(path_, ec);
  if (ec || !std::filesystem::is_regular_file(status)) {
    throw std::runtime_error("staged output is not a regular file: " + path_.string());
  }
  const auto size = std::filesystem::file_size(path_, ec);
  if (ec || size != expectedSize) {
    throw std::runtime_error("staged output size mismatch: " + path_.string());
  }
}

void StagedFile::commit() && {
  // The single replace primitive. rename(2) atomically substitutes the
  // destination on POSIX; MoveFileExW with MOVEFILE_REPLACE_EXISTING is the
  // equivalent on Windows. The destination entry is swapped, never truncated
  // and never followed through a symlink planted at its path.
#if defined(_WIN32)
  const auto replaced = ::MoveFileExW(path_.c_str(), destination_.c_str(),
                                      MOVEFILE_REPLACE_EXISTING |
                                          MOVEFILE_WRITE_THROUGH) != 0;
#else
  std::error_code ec;
  std::filesystem::rename(path_, destination_, ec);
  const auto replaced = !ec;
#endif

  if (!replaced) {
#if defined(_WIN32)
    const auto error = ::GetLastError();
#endif
    std::error_code remove_ec;
    std::filesystem::remove(path_, remove_ec);
#if defined(_WIN32)
    throw std::runtime_error("unable to replace " + destination_.string() +
                             " atomically (error " + std::to_string(error) +
                             "); original file left untouched");
#else
    throw std::runtime_error("unable to replace " + destination_.string() +
                             " atomically: " + ec.message() +
                             "; original file left untouched");
#endif
  }
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
                               path.parent_path().string() + ": " + ec.message());
    }
  }

  auto staged = StagedFile::create(path);
  staged.write(bytes);
  staged.finish();
  if (!bytes.empty()) {
    staged.verify(bytes.size());
  }
  std::move(staged).commit();
}

}  // namespace fo4::io
