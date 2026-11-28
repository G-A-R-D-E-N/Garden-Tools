#pragma once

#include <zlib.h>

#include <cstddef>
#include <span>
#include <utility>
#include <vector>

namespace fo4::binary {

// Requires data.size() to fit uLong; out is replaced only on success.
inline bool zlib_deflate(std::span<const std::byte> data, int level, std::vector<std::byte>& out) {
  uLongf size = compressBound(static_cast<uLong>(data.size()));
  std::vector<std::byte> packed(static_cast<std::size_t>(size));
  if (compress2(reinterpret_cast<Bytef*>(packed.data()), &size,
                reinterpret_cast<const Bytef*>(data.data()), static_cast<uLong>(data.size()),
                level) != Z_OK) {
    return false;
  }
  packed.resize(static_cast<std::size_t>(size));
  out = std::move(packed);
  return true;
}

}  // namespace fo4::binary
