#pragma once

#include "fo4/nif/nif.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace fo4::nif {

// Invariant: a shader-flag edit carries flag names, so no caller passes a bit index.
using EditValue = std::variant<std::string, float, std::vector<float>, std::vector<std::string>, SegmentTable>;

struct Edit {
  std::uint32_t id{0};
  std::string key;
  EditValue value;
};

enum class ColorChannel : std::uint8_t { red, green, blue, alpha };
enum class GradientAxis : std::uint8_t { x, y, z };
enum class VertexColorKind : std::uint8_t { fill, channel, gradient, enable };

struct VertexColorOp {
  VertexColorKind kind{VertexColorKind::fill};
  std::uint32_t block{0};
  std::vector<std::uint32_t> vertices;
  std::array<float, 4> color{};
  std::array<float, 4> end_color{};
  ColorChannel channel{ColorChannel::red};
  GradientAxis axis{GradientAxis::x};
  float value{0.0f};
};

std::vector<std::byte> write_nif(const NifFile& nif);
std::vector<std::byte> apply_edits(const NifFile& nif, std::span<const Edit> edits);
std::vector<std::byte> apply_vertex_color(const NifFile& nif, const VertexColorOp& op);

// Requires a JSON array of {id, key, value} objects; value is text, one number, or a list of numbers or flag names.
std::vector<Edit> read_edits_json(std::string_view text);
std::vector<std::byte> apply_edits_json(const NifFile& nif, std::string_view text);

}  // namespace fo4::nif
