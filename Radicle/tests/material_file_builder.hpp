#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::test {

struct MaterialShape {
  std::uint32_t version{2};
  bool emit{false};
  bool glass{false};
  bool terrain{false};
  bool trailing{true};
};

class MaterialSink {
public:
  void u8(std::uint8_t value) { bytes_.push_back(static_cast<std::byte>(value)); }

  void u32(std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
      u8(static_cast<std::uint8_t>(value >> shift));
    }
  }

  void f32(float value) {
    std::uint32_t bits{};
    std::memcpy(&bits, &value, sizeof(bits));
    u32(bits);
  }

  void flag(std::uint8_t raw = 1) { u8(raw); }

  void color(float r, float g, float b) {
    f32(r);
    f32(g);
    f32(b);
  }

  void text(std::string_view value) {
    u32(static_cast<std::uint32_t>(value.size()));
    for (const char ch : value) {
      u8(static_cast<std::uint8_t>(ch));
    }
  }

  void trailing() {
    u8(0xDE);
    u8(0xAD);
    u8(0xBE);
    u8(0xEF);
  }

  std::vector<std::byte> bytes_;
};

inline std::string nul(std::string_view text) {
  std::string out(text);
  out.push_back('\0');
  return out;
}

inline std::uint8_t raw_of(bool value) { return static_cast<std::uint8_t>(value ? 1 : 0); }

inline void material_header(MaterialSink& s, std::uint32_t magic, std::uint32_t version, std::uint8_t testRef) {
  s.u32(magic);
  s.u32(version);
  s.u32(0x80000003u);
  s.f32(0.5F);
  s.f32(0.25F);
  s.f32(1.5F);
  s.f32(2.0F);
  s.f32(1.0F);
  s.u8(3);
  s.u32(6);
  s.u32(7);
  s.u8(testRef);
  s.flag(2);
  for (int i = 0; i < 10; ++i) {
    s.flag(1);
  }
  s.f32(0.75F);
  if (version < 10) {
    s.flag(1);
    s.f32(0.125F);
  } else {
    s.flag(1);
  }
  s.flag(0);
  if (version >= 6) {
    s.u8(2);
  }
}

inline std::vector<std::byte> material_bgsm(const MaterialShape& shape) {
  MaterialSink s;
  const auto v = shape.version;
  material_header(s, 0x4D534742u, v, 128);
  if (v > 2) {
    s.text(nul("Textures/d.dds"));
    s.text("NoNul");
    s.text(nul("Textures/s.dds"));
    s.text(nul("Textures/g.dds"));
    s.text(nul("Textures/glow.dds"));
    s.text(nul("Textures/w.dds"));
    s.text(nul("Textures/spec.dds"));
    s.text(nul("Textures/l.dds"));
    s.text(nul("Textures/f.dds"));
    if (v >= 17) {
      s.text(nul("Textures/df.dds"));
    }
  } else {
    s.text(nul("Textures/d.dds"));
    s.text("NoNul");
    s.text(nul("Textures/s.dds"));
    s.text(nul("Textures/g.dds"));
    s.text(nul("Textures/env.dds"));
    s.text(nul("Textures/glow.dds"));
    s.text(nul("Textures/inner.dds"));
    s.text(nul("Textures/w.dds"));
    s.text(nul("Textures/disp.dds"));
  }
  s.flag(1);
  if (v >= 8) {
    s.flag(1);
    s.flag(0);
    s.flag(1);
    s.color(0.1F, 0.2F, 0.3F);
    s.f32(0.4F);
    s.f32(0.5F);
  } else {
    s.flag(1);
    s.f32(0.6F);
    s.f32(0.7F);
    s.flag(0);
    s.f32(0.8F);
  }
  s.flag(1);
  s.color(0.9F, 0.8F, 0.7F);
  s.f32(1.0F);
  s.f32(0.5F);
  s.f32(0.2F);
  s.f32(0.1F);
  s.f32(0.1F);
  s.f32(0.1F);
  if (v < 10) {
    s.f32(0.1F);
  }
  s.f32(0.1F);
  s.f32(0.1F);
  if (v > 2) {
    s.flag(1);
    if (v >= 9) {
      s.flag(1);
      s.f32(0.3F);
    }
  }
  s.text("");
  s.flag(0);
  s.flag(raw_of(shape.emit));
  if (shape.emit) {
    s.color(0.25F, 0.5F, 0.75F);
  }
  s.f32(0.9F);
  s.flag(1);
  s.flag(0);
  if (v >= 12) {
    s.f32(2.5F);
  }
  if (v >= 13) {
    s.flag(1);
    s.f32(0.1F);
    s.f32(0.2F);
    s.f32(0.3F);
  }
  if (v < 8) {
    s.flag(1);
  }
  for (int i = 0; i < 6; ++i) {
    s.flag(1);
  }
  if (v < 7) {
    s.flag(0);
    s.flag(0);
  }
  s.flag(0);
  s.color(0.1F, 0.1F, 0.1F);
  s.flag(0);
  s.flag(0);
  s.flag(0);
  s.flag(0);
  if (v < 3) {
    for (int i = 0; i < 5; ++i) {
      s.f32(0.2F);
    }
  }
  s.f32(0.4F);
  if (v >= 1) {
    s.flag(1);
  }
  if (v >= 3) {
    s.flag(raw_of(shape.terrain));
    if (shape.terrain) {
      if (v == 3) {
        s.u32(7);
      }
      s.f32(0.1F);
      s.f32(0.2F);
      s.f32(0.3F);
    }
  }
  if (shape.trailing) {
    s.trailing();
  }
  return s.bytes_;
}

inline std::vector<std::byte> material_bgem(const MaterialShape& shape) {
  MaterialSink s;
  const auto v = shape.version;
  material_header(s, 0x4D454742u, v, 7);
  s.text(nul("Textures/base.dds"));
  s.text(nul("Textures/grey.dds"));
  s.text(nul("Textures/env.dds"));
  s.text("NoNul");
  s.text("");
  if (v >= 11) {
    s.text(nul("Textures/spec.dds"));
    s.text(nul("Textures/light.dds"));
    s.text(nul("Textures/glow.dds"));
  }
  if (v >= 21) {
    s.text(nul("Textures/scratch.dds"));
    s.text(nul("Textures/dirt.dds"));
    s.flag(raw_of(shape.glass));
    if (shape.glass) {
      s.color(0.4F, 0.5F, 0.6F);
      s.f32(0.7F);
      if (v >= 22) {
        s.f32(0.8F);
      }
      s.f32(0.9F);
    }
  }
  if (v >= 10) {
    s.flag(1);
    s.f32(0.3F);
  }
  s.flag(1);
  s.flag(0);
  s.flag(1);
  s.flag(0);
  s.flag(1);
  s.flag(0);
  s.color(1.0F, 0.9F, 0.8F);
  s.f32(0.5F);
  s.f32(0.1F);
  s.f32(0.2F);
  s.f32(0.3F);
  s.f32(0.4F);
  s.f32(0.6F);
  s.u8(7);
  s.f32(0.25F);
  if (v >= 11) {
    s.color(0.25F, 0.5F, 0.75F);
  }
  if (v >= 15) {
    s.f32(0.1F);
    s.f32(0.2F);
    s.f32(0.3F);
  }
  if (v >= 16) {
    s.flag(1);
  }
  if (v >= 20) {
    s.flag(1);
  }
  if (shape.trailing) {
    s.trailing();
  }
  return s.bytes_;
}

}  // namespace fo4::test
