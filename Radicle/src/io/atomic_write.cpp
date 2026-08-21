#include "fo4/io/atomic_write.hpp"

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
  for (int attempt = 0; attempt < 64; ++attempt) {
    const auto candidate = parent / (name + ".radicle-tmp-" + std::to_string(attempt));
    std::error_code ec;
    if (!std::filesystem::exists(candidate, ec)) {
      return candidate;
    }
  }
  return parent / (name + ".radicle-tmp");
}

// Best-effort durability of the staged bytes before the rename. Failure to
// flush is not fatal: the write is still atomic from the reader's point of
// view, just not guaranteed durable across a power cut.
void sync_file(const std::filesystem::path& path) noexcept {
#if defined(_WIN32)
  const auto handle = ::CreateFileW(path.c_str(), GENERIC_READ,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    nullptr, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle != INVALID_HANDLE_VALUE) {
    ::FlushFileBuffers(handle);
    ::CloseHandle(handle);
  }
#else
  const int fd = ::open(path.c_str(), O_RDONLY);
  if (fd >= 0) {
    ::fsync(fd);
    ::close(fd);
  }
#endif
}

[[noreturn]] void fail(const std::filesystem::path& temp,
                       const std::filesystem::path& path,
                       const std::string& reason) {
  std::error_code remove_ec;
  std::filesystem::remove(temp, remove_ec);
  throw std::runtime_error(reason + " " + path.string());
}

}  // namespace

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

  const auto temp = temp_sibling(path);
  {
    std::ofstream stream(temp, std::ios::binary | std::ios::trunc);
    if (!stream) {
      throw std::runtime_error("unable to create temporary file for " + path.string());
    }
    if (!bytes.empty()) {
      stream.write(reinterpret_cast<const char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
    }
    if (!stream) {
      stream.close();
      fail(temp, path, "unable to write temporary file for");
    }
  }
  sync_file(temp);

  // The original file is never removed first. POSIX rename() replaces an
  // existing destination atomically; Windows uses MoveFileExW with
  // MOVEFILE_REPLACE_EXISTING, which is the platform's replace primitive.
  // If the replacement itself fails the staged temp file is deleted and the
  // pre-existing destination is left byte-for-byte intact.
  std::error_code ec;
#if defined(_WIN32)
  const auto replaced = ::MoveFileExW(temp.c_str(), path.c_str(),
                                      MOVEFILE_REPLACE_EXISTING |
                                          MOVEFILE_WRITE_THROUGH) != 0;
  if (!replaced) {
    ec = std::error_code(static_cast<int>(::GetLastError()),
                         std::system_category());
  }
#else
  std::filesystem::rename(temp, path, ec);
#endif
  if (ec) {
    std::error_code remove_ec;
    std::filesystem::remove(temp, remove_ec);
    throw std::runtime_error("unable to replace " + path.string() +
                             " atomically: " + ec.message() +
                             "; original file left untouched");
  }
}

}  // namespace fo4::io
