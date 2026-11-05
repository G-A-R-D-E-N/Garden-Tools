#pragma once

#include "fo4/hkx/packfile.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace fo4::hkx {

struct HkxAppend {
  std::vector<std::byte> bytes;
  // Invariant: object_index is the appended object's position in the file's object order.
  std::size_t object_index{0};
};

// Requires a reader-accepted packfile, a class the layout table knows, and instance_bytes of exactly its size.
HkxStatus append_object(std::span<const std::byte> source, std::string_view class_name, std::uint32_t signature,
                        std::span<const std::byte> instance_bytes, HkxAppend& out);

// Invariant: verify needs one more object with unchanged old offsets and bytes; empty skips the byte check.
HkxStatus verify_append(std::span<const std::byte> source, std::span<const std::byte> written,
                        std::span<const std::byte> expected_instance = {});

}  // namespace fo4::hkx
