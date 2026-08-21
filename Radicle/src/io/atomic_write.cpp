#include "fo4/io/atomic_write.hpp"

#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>

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
      std::error_code remove_ec;
      std::filesystem::remove(temp, remove_ec);
      throw std::runtime_error("unable to write temporary file for " + path.string());
    }
  }

  // POSIX rename replaces an existing destination; MSVC's rename fails when
  // the target exists, so fall back to a remove-then-rename. The window in
  // which the destination is absent is tiny compared to truncating the file
  // in place before the replacement bytes are durable.
  std::error_code ec;
  std::filesystem::rename(temp, path, ec);
  if (ec) {
    std::error_code remove_ec;
    std::filesystem::remove(path, remove_ec);
    ec.clear();
    std::filesystem::rename(temp, path, ec);
  }
  if (ec) {
    std::error_code remove_ec;
    std::filesystem::remove(temp, remove_ec);
    throw std::runtime_error("unable to replace " + path.string() + ": " + ec.message());
  }
}

}  // namespace fo4::io
