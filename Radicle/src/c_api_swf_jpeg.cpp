#include "c_api_env_internal.hpp"
#include "c_api_internal.hpp"
#include "c_api_swf_internal.hpp"

#include "fo4/c_api_swf.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <span>
#include <vector>

using radicle_c_detail::clear_error;
using radicle_c_detail::fail;
using radicle_c_env_detail::guarded;

namespace {

bool starts_with(std::span<const std::byte> data, std::uint8_t first, std::uint8_t second) {
  return data.size() >= 2 && data[0] == std::byte{first} && data[1] == std::byte{second};
}

bool ends_with(std::span<const std::byte> data, std::uint8_t first, std::uint8_t second) {
  return data.size() >= 2 && data[data.size() - 2] == std::byte{first} && data.back() == std::byte{second};
}

std::span<const std::byte> without_leading_seam(std::span<const std::byte> image) {
  if (starts_with(image, 0xFF, 0xD9) && starts_with(image.subspan(2), 0xFF, 0xD8)) {
    return image.subspan(4);
  }
  return image;
}

const fo4::swf::SwfTag* nearest_jpeg_tables(const radicle_swf* swf, std::size_t index) {
  for (std::size_t before = index; before-- > 0;) {
    const auto* candidate = swf->tags[before].tag;
    if (candidate->code == fo4::swf::tag::kJpegTables) return candidate;
  }
  return nullptr;
}

}  // namespace

extern "C" {

radicle_status radicle_swf_bitmap_jpeg(const radicle_swf* swf, size_t index, uint8_t** out_jpeg,
                                       size_t* out_size) {
  clear_error();
  if (out_jpeg) *out_jpeg = nullptr;
  if (out_size) *out_size = 0;
  if (swf == nullptr || index >= swf->tags.size() || out_jpeg == nullptr || out_size == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "swf, out_jpeg, out_size and a tag index are required");
  }
  const auto* tag = swf->tags[index].tag;
  if (!tag->jpeg || (tag->code != fo4::swf::tag::kDefineBits && tag->code != fo4::swf::tag::kDefineBitsJpeg2)) {
    return fail(RADICLE_STATUS_NOT_FOUND, "the tag carries no JPEG without alpha");
  }
  return guarded("unknown exception while exporting a JPEG", [&] {
    std::span<const std::byte> image = without_leading_seam(std::span<const std::byte>(tag->payload).subspan(2));
    if (image.empty()) return fail(RADICLE_STATUS_NOT_FOUND, "the JPEG carries no stream after the erroneous seam");
    const auto* tables = tag->code == fo4::swf::tag::kDefineBits ? nearest_jpeg_tables(swf, index) : nullptr;
    std::vector<std::byte> jpeg;
    if (tables != nullptr && !tables->payload.empty()) {
      auto head = std::span<const std::byte>(tables->payload);
      if (ends_with(head, 0xFF, 0xD9)) head = head.first(head.size() - 2);
      if (starts_with(image, 0xFF, 0xD8)) image = image.subspan(2);
      jpeg.assign(head.begin(), head.end());
    }
    jpeg.insert(jpeg.end(), image.begin(), image.end());
    auto* buffer = static_cast<uint8_t*>(std::malloc(jpeg.size()));
    if (buffer == nullptr) return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate the JPEG bytes");
    std::memcpy(buffer, jpeg.data(), jpeg.size());
    *out_jpeg = buffer;
    *out_size = jpeg.size();
    return RADICLE_STATUS_OK;
  });
}

}  // extern "C"
