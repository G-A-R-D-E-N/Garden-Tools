#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::texture {

class dds_error : public std::runtime_error {
public:
  dds_error(std::string type, std::string message)
      : std::runtime_error(std::move(message)), type_(std::move(type)) {}
  const std::string& type() const noexcept { return type_; }

private:
  std::string type_;
};

struct DdsFormatInfo {
  std::uint8_t dxgi{};
  std::string_view name;
  /* Bytes per 4x4 block when the layout is block compressed, 0 otherwise. */
  int block_bytes{0};
  /* Bytes per pixel when the layout is uncompressed, 0 otherwise. */
  int pixel_bytes{0};

  bool is_block_compressed() const noexcept { return block_bytes > 0; }
};

struct DdsInfo {
  int width{};
  int height{};
  int mip_count{};
  bool is_cube_map{};
  int array_size{};
  int depth{};
  int data_offset{};
  DdsFormatInfo format{};

  std::uint8_t dxgi_format() const noexcept { return format.dxgi; }
  /* Sum of every mip's size times the array and depth slices. Throws
     dds_error("InvalidDds") when that product does not fit a signed 64-bit
     byte count. */
  std::int64_t payload_size() const;
  std::int64_t mip_size(std::uint32_t mip) const noexcept;
  /* Byte offset of one mip from the first surface, slice 0 only. */
  std::int64_t mip_offset(std::uint32_t mip) const noexcept;
};

/* Format table lookup by DXGI format code (nullopt when not recognized). */
std::optional<DdsFormatInfo> lookup_format(std::uint8_t dxgi) noexcept;

/* Size in bytes of one mip level (width and height floored to 1). */
std::int64_t mip_size(const DdsFormatInfo& format, int width, int height) noexcept;

/* Parses a DDS container (legacy or DX10). Errors: "InvalidDds",
   "UnsupportedFormat". The array size and, for a volume texture, the depth are
   refused above 65535, the 16-bit counts a BA2 entry stores. */
DdsInfo read_dds(std::span<const std::uint8_t> bytes);

/* Builds a canonical DX10 DDS_HEADER for a given layout (used by tests). */
std::vector<std::uint8_t> build_dds_header(const DdsInfo& info);

/* True when the DXGI format is a BC1-5 or BC7 block layout this decoder
   handles; BC6H is not decodable here. */
bool can_decode_block_format(std::uint8_t dxgi) noexcept;

/* Decodes one BCn surface (whole 4x4 blocks, row-major) into RGBA8. Errors:
   "InvalidData". */
std::vector<std::uint8_t> decode_block_format(std::span<const std::uint8_t> surface,
                                              std::uint8_t dxgi,
                                              int width,
                                              int height);

/* True when the container is a BC5 normal map (DXGI 82, 83 or 84). */
bool is_bc5(std::span<const std::uint8_t> bytes) noexcept;

/* Decodes one mip level of a DDS container into RGBA8. `reconstruct_z`
   rebuilds the blue channel from red and green, which an unsigned BC5 normal
   map needs. It is refused with std::invalid_argument (the C ABI's
   RADICLE_STATUS_INVALID_ARGUMENT) for every other format, BC5 SNORM included,
   because a square root cannot use signed channels.
   Errors: "InvalidDds", "UnsupportedFormat", "TooLarge". */
std::vector<std::uint8_t> decode_dds_rgba(std::span<const std::uint8_t> bytes,
                                          std::uint32_t mip,
                                          bool reconstruct_z,
                                          std::uint32_t& width,
                                          std::uint32_t& height);

}  // namespace fo4::texture
