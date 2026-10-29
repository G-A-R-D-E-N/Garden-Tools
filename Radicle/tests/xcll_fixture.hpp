#pragma once

#include "plugin_bytes.hpp"

#include <cstddef>
#include <vector>

namespace fo4::test {

inline std::vector<std::byte> xcll_bytes(std::size_t size) {
  ByteWriter out;
  out.u32(0x00302010);  // ambient
  out.u32(0x00050403);  // directional
  out.u32(0x00090807);  // fog near color
  out.f32(64.0f);
  out.f32(4096.0f);
  out.i32(45);
  out.i32(-30);
  out.f32(1.0f);
  out.f32(8192.0f);
  out.f32(2.0f);
  for (int i = 0; i < 32; ++i) out.u8(0);  // directional ambient + specular + fresnel
  out.u32(0x00CCBBAA);                     // fog far color
  out.f32(0.75f);
  out.f32(100.0f);
  out.f32(200.0f);
  out.u32(0x14);  // inherits fog near + fog far
  out.f32(300.0f);
  out.f32(400.0f);
  out.u32(0x00121110);
  out.u32(0x00222120);
  out.f32(2.0f);
  out.f32(3.0f);
  out.f32(4.0f);
  out.f32(5.0f);
  out.f32(6.0f);
  out.f32(500.0f);
  out.f32(600.0f);
  out.bytes.resize(size);
  return out.bytes;
}

}  // namespace fo4::test
