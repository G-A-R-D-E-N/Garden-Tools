#include "fo4/nif/nif_write.hpp"

#include "fo4/error.hpp"
#include "fo4/nif/vertex_desc.hpp"
#include "nif_internal.hpp"
#include "write_model.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::nif {
namespace {

using write_detail::Bytes;
using write_detail::Model;

constexpr std::uint32_t k_has_uv = 1U << 1;
constexpr std::uint32_t k_has_uv2 = 1U << 2;
constexpr std::uint32_t k_has_normal = 1U << 3;
constexpr std::uint32_t k_has_tangent = 1U << 4;
constexpr std::uint32_t k_has_colour = 1U << 5;
constexpr std::uint32_t k_full_precision = 1U << 10;
constexpr std::size_t k_descriptor_fixed = 100;
constexpr std::size_t k_descriptor_bytes = 18;
constexpr std::size_t k_triangle_bytes = 6;
constexpr std::size_t k_colour_bytes = 4;
constexpr std::size_t k_max_stride = 60;
constexpr std::uint64_t k_colour_descriptor_bit = static_cast<std::uint64_t>(k_has_colour) << 44U;

[[noreturn]] void fail(const NifFile& nif, std::uint32_t block, const std::string& message) {
  const auto offset = block < nif.blocks().size() ? nif.blocks()[block].offset : std::size_t{0};
  throw parse_error(offset, "vertex colour on block " + std::to_string(block) + ": " + message);
}

struct Layout {
  std::size_t descriptor_at{0};
  std::uint64_t descriptor{0};
  std::size_t triangle_count{0};
  std::size_t vertex_count{0};
  std::size_t data_size{0};
  std::size_t stride{0};
  std::uint32_t flags{0};

  std::size_t data_at() const { return descriptor_at + k_descriptor_bytes; }
  bool coloured() const { return (flags & k_has_colour) != 0; }
};

std::size_t colour_offset(std::uint32_t flags) {
  std::size_t at = (flags & k_full_precision) != 0 ? 16 : 8;
  for (const std::uint32_t bit : {k_has_uv, k_has_uv2, k_has_normal, k_has_tangent}) {
    if ((flags & bit) != 0) at += 4;
  }
  return at;
}

std::uint64_t nibble_at(std::uint64_t descriptor, std::size_t index) { return (descriptor >> (4 * index)) & 0xFU; }

std::uint64_t with_nibble(std::uint64_t descriptor, std::size_t index, std::uint64_t value) {
  const auto shift = 4 * index;
  return (descriptor & ~(std::uint64_t{0xF} << shift)) | ((value & 0xFU) << shift);
}

Layout read_layout(const NifFile& nif, const Model& model, std::uint32_t block) {
  const auto type = write_detail::block_type(model, block);
  if (type != "BSTriShape" && type != "BSSubIndexTriShape") {
    fail(nif, block, "vertex colour edits need a BSTriShape or BSSubIndexTriShape block");
  }
  const auto& bytes = model.blocks[block];
  if (bytes.size() < 8) fail(nif, block, "block is too short for this edit");
  Layout layout;
  layout.descriptor_at =
      k_descriptor_fixed + 4 * static_cast<std::size_t>(write_detail::read_int<std::uint32_t>(bytes, 4));
  if (layout.descriptor_at > bytes.size() || bytes.size() - layout.descriptor_at < k_descriptor_bytes) {
    fail(nif, block, "block is too short for this edit");
  }
  const auto at = layout.descriptor_at;
  layout.descriptor = write_detail::read_int<std::uint64_t>(bytes, at);
  layout.triangle_count = write_detail::read_int<std::uint32_t>(bytes, at + 8);
  layout.vertex_count = write_detail::read_int<std::uint16_t>(bytes, at + 12);
  layout.data_size = write_detail::read_int<std::uint32_t>(bytes, at + 14);
  layout.stride = static_cast<std::size_t>(layout.descriptor & 0xFU) * 4;
  layout.flags = static_cast<std::uint32_t>(layout.descriptor >> 44U);
  if (layout.stride == 0 || layout.vertex_count == 0 || layout.data_size == 0) {
    fail(nif, block, "shape has no vertex data");
  }
  if (layout.stride != vertex_bytes_for(layout.flags)) {
    fail(nif, block, "vertex attributes do not fill the vertex stride exactly");
  }
  const auto vertex_bytes = layout.vertex_count * layout.stride;
  if (layout.data_size < vertex_bytes) fail(nif, block, "vertex data size is smaller than its vertices");
  if (layout.data_size - vertex_bytes != layout.triangle_count * k_triangle_bytes) {
    fail(nif, block, "shape triangle count disagrees with its data size");
  }
  if (layout.data_size > bytes.size() - layout.data_at()) fail(nif, block, "block is too short for this edit");
  return layout;
}

std::vector<std::size_t> target_vertices(const NifFile& nif, const VertexColorOp& op, std::size_t vertex_count) {
  std::vector<std::size_t> targets;
  if (op.vertices.empty()) {
    targets.reserve(vertex_count);
    for (std::size_t v = 0; v < vertex_count; ++v) targets.push_back(v);
    return targets;
  }
  for (const auto v : op.vertices) {
    if (v >= vertex_count) {
      fail(nif, op.block,
           "vertex " + std::to_string(v) + " is past the shape's " + std::to_string(vertex_count) + " vertices");
    }
    targets.push_back(v);
  }
  return targets;
}

void check_unit(const NifFile& nif, std::uint32_t block, float value) {
  if (!std::isfinite(value) || value < 0.0f || value > 1.0f) {
    fail(nif, block, "vertex colour values must be finite numbers in [0, 1]");
  }
}

void check_colour(const NifFile& nif, std::uint32_t block, const std::array<float, 4>& rgba) {
  for (const float value : rgba) check_unit(nif, block, value);
}

std::uint8_t byte_of(float value) {
  return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}

void write_rgba(Bytes& bytes, std::size_t at, const std::array<float, 4>& rgba) {
  for (std::size_t c = 0; c < 4; ++c) write_detail::write_int<std::uint8_t>(bytes, at + c, byte_of(rgba[c]));
}

std::vector<float> shape_positions(const NifFile& nif, std::uint32_t block, std::size_t vertex_count) {
  const auto shape = read_shape(nif, block);
  if (shape.verts.size() != 3 * vertex_count) fail(nif, block, "the shape's positions could not be read");
  for (const float value : shape.verts) {
    if (!std::isfinite(value)) fail(nif, block, "the shape's positions must be finite numbers");
  }
  return shape.verts;
}

void paint_colours(const NifFile& nif, Model& model, const VertexColorOp& op, const Layout& layout) {
  if (!layout.coloured()) fail(nif, op.block, "shape has no vertex colours; enable them first");
  const auto targets = target_vertices(nif, op, layout.vertex_count);
  const auto colour_at = layout.data_at() + colour_offset(layout.flags);
  auto& bytes = model.blocks[op.block];
  if (op.kind == VertexColorKind::channel) {
    check_unit(nif, op.block, op.value);
    const auto channel = static_cast<std::size_t>(op.channel);
    for (const auto v : targets) {
      write_detail::write_int<std::uint8_t>(bytes, colour_at + v * layout.stride + channel, byte_of(op.value));
    }
    return;
  }
  check_colour(nif, op.block, op.color);
  if (op.kind == VertexColorKind::fill) {
    for (const auto v : targets) write_rgba(bytes, colour_at + v * layout.stride, op.color);
    return;
  }
  check_colour(nif, op.block, op.end_color);
  const auto positions = shape_positions(nif, op.block, layout.vertex_count);
  const auto axis = static_cast<std::size_t>(op.axis);
  float low = positions[axis];
  float high = positions[axis];
  for (std::size_t v = 0; v < layout.vertex_count; ++v) {
    low = std::min(low, positions[3 * v + axis]);
    high = std::max(high, positions[3 * v + axis]);
  }
  const float span = high - low;
  for (const auto v : targets) {
    const float along = span > 0.0f ? (positions[3 * v + axis] - low) / span : 0.0f;
    std::array<float, 4> rgba{};
    for (std::size_t c = 0; c < 4; ++c) rgba[c] = op.color[c] + (op.end_color[c] - op.color[c]) * along;
    write_rgba(bytes, colour_at + v * layout.stride, rgba);
  }
}

void enable_colours(const NifFile& nif, Model& model, const VertexColorOp& op, const Layout& layout) {
  if (layout.coloured()) fail(nif, op.block, "shape already has vertex colours");
  if (!op.vertices.empty()) fail(nif, op.block, "enable adds colours to every vertex, so it takes no vertex list");
  const auto stride = layout.stride + k_colour_bytes;
  if (stride > k_max_stride) fail(nif, op.block, "enabling vertex colours would make the vertex stride exceed 60 bytes");
  const auto& bytes = model.blocks[op.block];
  const auto data_at = layout.data_at();
  const auto colour_at = colour_offset(layout.flags);
  auto descriptor = layout.descriptor;
  for (std::size_t index = 7; index <= 9; ++index) {
    const auto offset = nibble_at(descriptor, index);
    if (offset >= colour_at / 4) descriptor = with_nibble(descriptor, index, offset + 1);
  }
  descriptor = with_nibble(descriptor, 6, colour_at / 4);
  descriptor = with_nibble(descriptor, 0, stride / 4);
  descriptor |= k_colour_descriptor_bit;
  Bytes out(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(data_at));
  write_detail::write_int<std::uint64_t>(out, layout.descriptor_at, descriptor);
  write_detail::write_int<std::uint32_t>(
      out, layout.descriptor_at + 14,
      static_cast<std::uint32_t>(layout.data_size + layout.vertex_count * k_colour_bytes));
  const auto* vertex_at = bytes.data() + data_at;
  for (std::size_t v = 0; v < layout.vertex_count; ++v) {
    const auto* vertex = vertex_at + v * layout.stride;
    out.insert(out.end(), vertex, vertex + colour_at);
    out.insert(out.end(), k_colour_bytes, std::byte{0xFF});
    out.insert(out.end(), vertex + colour_at, vertex + layout.stride);
  }
  out.insert(out.end(), vertex_at + layout.vertex_count * layout.stride, bytes.data() + bytes.size());
  model.blocks[op.block] = std::move(out);
}

void apply_op(const NifFile& nif, Model& model, const VertexColorOp& op) {
  const auto layout = read_layout(nif, model, op.block);
  if (op.kind == VertexColorKind::enable) return enable_colours(nif, model, op, layout);
  paint_colours(nif, model, op, layout);
}

}  // namespace

std::vector<std::byte> apply_vertex_color(const NifFile& nif, const VertexColorOp& op) {
  if (op.block >= nif.blocks().size()) {
    throw parse_error(0, "vertex colour edit targets block " + std::to_string(op.block) + ", which is not in the file");
  }
  auto model = write_detail::load_model(nif);
  apply_op(nif, model, op);
  auto bytes = write_detail::serialize(model);
  read_nif(bytes);
  return bytes;
}

}  // namespace fo4::nif
