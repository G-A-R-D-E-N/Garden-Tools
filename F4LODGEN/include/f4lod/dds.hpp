#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace f4lod {

enum class DdsCompression {
    unknown,
    bc1,
    bc2,
    bc3,
    bc4,
    bc5,
    bc6h,
    bc7,
    rgba8,
};

struct DdsInspection {
    bool valid{};
    std::string error;
    bool dx10{};
    bool cube{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t mip_count{};
    std::uint32_t dxgi_format{};
    std::uint32_t array_size{1U};
    DdsCompression compression{DdsCompression::unknown};
    std::size_t header_size{};
    std::uint64_t minimum_payload_size{};
    std::uint64_t available_payload_size{};
};

// Structural DDS validation used before terrain texture decoding/blending.
// Supports DX10 BC1-BC7/RGBA8 plus common legacy DXT1/DXT3/DXT5/ATI1/ATI2
// headers. It validates complete mip payload size but does not decode pixels.
[[nodiscard]] DdsInspection inspect_dds(std::span<const unsigned char> data);
[[nodiscard]] const char* dds_compression_name(DdsCompression compression) noexcept;

} // namespace f4lod
