#include "f4lod/dds.hpp"

#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

namespace {

void set_u32(std::vector<unsigned char>& out, std::size_t offset, std::uint32_t value) {
    out[offset] = static_cast<unsigned char>(value & 0xFFU);
    out[offset + 1U] = static_cast<unsigned char>((value >> 8U) & 0xFFU);
    out[offset + 2U] = static_cast<unsigned char>((value >> 16U) & 0xFFU);
    out[offset + 3U] = static_cast<unsigned char>((value >> 24U) & 0xFFU);
}

std::vector<unsigned char> dx10_bc1() {
    std::vector<unsigned char> bytes(148U + 8U, 0U);
    set_u32(bytes, 0U, 0x20534444U);
    set_u32(bytes, 4U, 124U);
    set_u32(bytes, 8U, 0x00081007U);
    set_u32(bytes, 12U, 4U);
    set_u32(bytes, 16U, 4U);
    set_u32(bytes, 20U, 8U);
    set_u32(bytes, 28U, 1U);
    set_u32(bytes, 76U, 32U);
    set_u32(bytes, 80U, 0x00000004U);
    set_u32(bytes, 84U, 0x30315844U);
    set_u32(bytes, 108U, 0x00001000U);
    set_u32(bytes, 128U, 71U);
    set_u32(bytes, 132U, 3U);
    set_u32(bytes, 140U, 1U);
    return bytes;
}

std::vector<unsigned char> legacy_dxt5() {
    std::vector<unsigned char> bytes(128U + 16U, 0U);
    set_u32(bytes, 0U, 0x20534444U);
    set_u32(bytes, 4U, 124U);
    set_u32(bytes, 8U, 0x00081007U);
    set_u32(bytes, 12U, 4U);
    set_u32(bytes, 16U, 4U);
    set_u32(bytes, 20U, 16U);
    set_u32(bytes, 28U, 1U);
    set_u32(bytes, 76U, 32U);
    set_u32(bytes, 80U, 0x00000004U);
    set_u32(bytes, 84U, 0x35545844U); // DXT5
    set_u32(bytes, 108U, 0x00001000U);
    return bytes;
}

} // namespace

int main() {
    const auto dx10 = dx10_bc1();
    const auto dx10_info = f4lod::inspect_dds(dx10);
    assert(dx10_info.valid);
    assert(dx10_info.dx10);
    assert(!dx10_info.cube);
    assert(dx10_info.width == 4U);
    assert(dx10_info.height == 4U);
    assert(dx10_info.mip_count == 1U);
    assert(dx10_info.dxgi_format == 71U);
    assert(dx10_info.compression == f4lod::DdsCompression::bc1);
    assert(dx10_info.header_size == 148U);
    assert(dx10_info.minimum_payload_size == 8U);
    assert(dx10_info.available_payload_size == 8U);
    assert(std::string(f4lod::dds_compression_name(dx10_info.compression)) == "BC1");

    const auto legacy = legacy_dxt5();
    const auto legacy_info = f4lod::inspect_dds(legacy);
    assert(legacy_info.valid);
    assert(!legacy_info.dx10);
    assert(legacy_info.compression == f4lod::DdsCompression::bc3);
    assert(legacy_info.header_size == 128U);
    assert(legacy_info.minimum_payload_size == 16U);

    auto truncated = dx10;
    truncated.pop_back();
    assert(!f4lod::inspect_dds(truncated).valid);

    auto bad_magic = dx10;
    bad_magic[0] = 0U;
    assert(!f4lod::inspect_dds(bad_magic).valid);

    auto unsupported = dx10;
    set_u32(unsupported, 128U, 45U);
    assert(!f4lod::inspect_dds(unsupported).valid);

    return 0;
}
