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
  int block_bytes{0};
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
  int row_pitch{};

  std::uint8_t dxgi_format() const noexcept { return format.dxgi; }
  int face_count() const noexcept;
  std::int64_t payload_size() const;
  std::int64_t face_offset(std::uint32_t face) const;
  int mip_row_pitch(std::uint32_t mip) const noexcept;
  std::int64_t mip_size(std::uint32_t mip) const noexcept;
  std::int64_t mip_offset(std::uint32_t mip) const noexcept;
};

std::optional<DdsFormatInfo> lookup_format(std::uint8_t dxgi) noexcept;

std::int64_t mip_size(const DdsFormatInfo& format, int width, int height) noexcept;

DdsInfo read_dds(std::span<const std::uint8_t> bytes);

std::vector<std::uint8_t> build_dds_header(const DdsInfo& info);

bool can_decode_block_format(std::uint8_t dxgi) noexcept;

std::vector<std::uint8_t> decode_block_format(std::span<const std::uint8_t> surface,
                                              std::uint8_t dxgi,
                                              int width,
                                              int height);

bool is_bc5(std::span<const std::uint8_t> bytes) noexcept;

// Requires a block-compressed format; returns the stored blocks of mips [first_mip, first_mip + mip_count) of one face as one run.
// Invariant: unsigned_rgtc rewrites signed BC4 and BC5 (dxgi 81 and 84) blocks to their unsigned form and leaves every other format as stored.
std::vector<std::uint8_t> read_dds_block_chain(std::span<const std::uint8_t> bytes,
                                               std::uint32_t face,
                                               std::uint32_t first_mip,
                                               std::uint32_t mip_count,
                                               bool unsigned_rgtc);

// Requires a block-compressed format and 1 <= mip_count <= mips_per_face <= the full chain of the larger side; face n starts at n * (the bytes of mips_per_face mips) after the header, whatever the header's face count and mip count say.
// Invariant: returns the first mip_count mips of that face and the file must hold them; unsigned_rgtc behaves as in read_dds_block_chain.
std::vector<std::uint8_t> read_dds_face_blocks(std::span<const std::uint8_t> bytes,
                                               std::uint32_t face,
                                               std::uint32_t mips_per_face,
                                               std::uint32_t mip_count,
                                               bool unsigned_rgtc);

std::vector<std::uint8_t> decode_dds_rgba(std::span<const std::uint8_t> bytes,
                                          std::uint32_t mip,
                                          bool reconstruct_z,
                                          std::uint32_t& width,
                                          std::uint32_t& height);

std::vector<std::uint8_t> decode_dds_rgba(std::span<const std::uint8_t> bytes,
                                          std::uint32_t face,
                                          std::uint32_t mip,
                                          bool reconstruct_z,
                                          std::uint32_t& width,
                                          std::uint32_t& height);

// Invariant: BC3 output is 16 bytes per 4x4 block and decodes back through decode_block_format(77).
std::vector<std::uint8_t> encode_bc3(std::span<const std::uint8_t> rgba, int width, int height);

// Invariant: BC1 output is 8 bytes per 4x4 block, opaque (colour0 > colour1), and decodes through decode_block_format(71).
std::vector<std::uint8_t> encode_bc1(std::span<const std::uint8_t> rgba, int width, int height);

// Invariant: BC4 output is 8 bytes per 4x4 block over the red channel and decodes through decode_block_format(80).
std::vector<std::uint8_t> encode_bc4(std::span<const std::uint8_t> rgba, int width, int height);

// Invariant: BC5 output is 16 bytes per 4x4 block (red then green) and decodes through decode_block_format(83).
std::vector<std::uint8_t> encode_bc5(std::span<const std::uint8_t> rgba, int width, int height);

}
