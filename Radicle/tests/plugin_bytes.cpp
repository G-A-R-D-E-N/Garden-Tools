#include "plugin_bytes.hpp"

#include <zlib.h>

#include <stdexcept>

namespace fo4::test {

void write_subrecord(ByteWriter& out, std::string_view signature, std::span<const std::byte> data) {
  if (data.size() > 0xFFFF) {
    write_xxxx_subrecord(out, signature, data);
    return;
  }
  out.sig(signature);
  out.u16(static_cast<std::uint16_t>(data.size()));
  out.raw(data);
}

void write_subrecord(ByteWriter& out, std::string_view signature, std::string_view data) {
  std::vector<std::byte> bytes;
  bytes.reserve(data.size());
  for (char ch : data) {
    bytes.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  }
  write_subrecord(out, signature, bytes);
}

void write_xxxx_subrecord(ByteWriter& out, std::string_view signature,
                          std::span<const std::byte> data) {
  out.sig("XXXX");
  out.u16(4);
  out.u32(static_cast<std::uint32_t>(data.size()));
  out.sig(signature);
  out.u16(0);
  out.raw(data);
}

std::size_t begin_record(ByteWriter& out, std::string_view signature, std::uint32_t flags,
                         std::uint32_t form_id, std::uint16_t form_version) {
  const auto offset = out.size();
  out.sig(signature);
  out.u32(0);
  out.u32(flags);
  out.u32(form_id);
  out.u16(0);
  out.u16(0);
  out.u16(form_version);
  out.u16(0);
  return offset;
}

void end_record(ByteWriter& out, std::size_t header_offset) {
  const auto payload = static_cast<std::uint32_t>(out.size() - (header_offset + 24));
  out.patch_u32(header_offset + 4, payload);
}

std::size_t begin_group(ByteWriter& out, std::string_view label, std::int32_t group_type) {
  const auto offset = out.size();
  out.sig("GRUP");
  out.u32(0);
  out.sig(label);
  out.i32(group_type);
  out.u16(0);
  out.u16(0);
  out.u16(0);
  out.u16(0);
  return offset;
}

void end_group(ByteWriter& out, std::size_t header_offset) {
  const auto size = static_cast<std::uint32_t>(out.size() - header_offset);
  out.patch_u32(header_offset + 4, size);
}

std::vector<std::byte> zlib_compress(std::span<const std::byte> data) {
  if (data.empty()) {
    return {};
  }
  uLongf bound = compressBound(static_cast<uLong>(data.size()));
  std::vector<std::byte> out(bound);
  const int rc = compress(reinterpret_cast<Bytef*>(out.data()), &bound,
                          reinterpret_cast<const Bytef*>(data.data()),
                          static_cast<uLong>(data.size()));
  if (rc != Z_OK) {
    throw std::runtime_error("zlib compress failed");
  }
  out.resize(bound);
  return out;
}

std::vector<std::byte> make_tes4_plugin(std::uint32_t flags, float version,
                                        const std::vector<std::string>& masters) {
  ByteWriter out;
  const auto tes4 = begin_record(out, "TES4", flags, 0);
  ByteWriter hedr;
  hedr.f32(version);
  hedr.u32(0);
  hedr.u32(0x800);
  write_subrecord(out, "HEDR", hedr.bytes);
  for (const auto& master : masters) {
    write_subrecord(out, "MAST", master + '\0');
    ByteWriter data;
    data.u32(0);
    data.u32(0);
    write_subrecord(out, "DATA", data.bytes);
  }
  end_record(out, tes4);
  return out.bytes;
}

std::vector<std::byte> make_minimal_plugin() {
  ByteWriter out;
  const auto tes4 = begin_record(out, "TES4", 0, 0);
  ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x800);
  write_subrecord(out, "HEDR", hedr.bytes);
  write_subrecord(out, "CNAM", std::string_view{"fo4sdk\0", 7});
  write_subrecord(out, "MAST", std::string_view{"Fallout4.esm\0", 13});
  ByteWriter data;
  data.u32(0);
  data.u32(0);
  write_subrecord(out, "DATA", data.bytes);
  end_record(out, tes4);

  const auto grup = begin_group(out, "GMST", 0);
  const auto rec = begin_record(out, "GMST", 0, 0x01000800);
  write_subrecord(out, "EDID", std::string_view{"sTest\0", 6});
  end_record(out, rec);
  end_group(out, grup);
  return out.bytes;
}

}  // namespace fo4::test
