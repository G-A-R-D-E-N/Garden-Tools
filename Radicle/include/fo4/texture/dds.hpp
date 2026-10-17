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

  std::uint8_t dxgi_format() const noexcept { return format.dxgi; }
  std::int64_t payload_size() const;
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

std::vector<std::uint8_t> decode_dds_rgba(std::span<const std::uint8_t> bytes,
                                          std::uint32_t mip,
                                          bool reconstruct_z,
                                          std::uint32_t& width,
                                          std::uint32_t& height);

}
