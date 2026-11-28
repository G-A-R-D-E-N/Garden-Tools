#include "fo4/nif/tri_file.hpp"

#include "fo4/error.hpp"
#include "fo4/nif/hknp_encode.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>

namespace fo4::nif {
namespace {

constexpr char k_magic[] = "FRTRI003";
constexpr std::size_t k_magic_size = 8;
constexpr std::size_t k_header_size = 64;
constexpr std::int32_t k_flag_texture_triangles = 1;
constexpr std::int32_t k_max_name = 256;

class Cursor {
public:
  explicit Cursor(std::span<const std::byte> bytes) : bytes_(bytes) {}

  std::size_t offset() const { return offset_; }

  void skip(std::uint64_t count) {
    need(count);
    offset_ += static_cast<std::size_t>(count);
  }

  template <typename T>
  T take() {
    need(sizeof(T));
    T value;
    std::memcpy(&value, bytes_.data() + offset_, sizeof(T));
    offset_ += sizeof(T);
    return value;
  }

  const std::byte* here(std::uint64_t count) {
    need(count);
    return bytes_.data() + offset_;
  }

  bool done() const { return offset_ == bytes_.size(); }

private:
  void need(std::uint64_t count) const {
    if (count > bytes_.size() - offset_) throw parse_error(offset_, "tri file ends inside a block");
  }

  std::span<const std::byte> bytes_;
  std::size_t offset_{0};
};

}  // namespace

const TriMorph* TriFile::find(std::string_view name) const {
  for (const auto& morph : morphs) {
    if (morph.name == name) return &morph;
  }
  return nullptr;
}

TriFile parse_tri(std::span<const std::byte> bytes) {
  if (bytes.size() < k_header_size || std::memcmp(bytes.data(), k_magic, k_magic_size) != 0) {
    throw parse_error(0, "not an FRTRI003 tri file");
  }
  Cursor in(bytes);
  in.skip(k_magic_size);
  const auto vertex_count = in.take<std::int32_t>();
  const auto triangle_count = in.take<std::int32_t>();
  const auto quad_count = in.take<std::int32_t>();
  const auto unknown_a = in.take<std::int32_t>();
  const auto unknown_b = in.take<std::int32_t>();
  const auto uv_count = in.take<std::int32_t>();
  const auto flags = in.take<std::int32_t>();
  const auto morph_count = in.take<std::int32_t>();
  const auto modifier_count = in.take<std::int32_t>();
  const auto modifier_vertex_count = in.take<std::int32_t>();
  std::int32_t tail = 0;
  for (int i = 0; i < 4; ++i) tail |= in.take<std::int32_t>();
  if (vertex_count <= 0 || triangle_count < 0 || morph_count < 0) throw parse_error(8, "tri file has a negative count");
  if (quad_count != 0 || unknown_a != 0 || unknown_b != 0 || tail != 0 || modifier_count != 0 ||
      modifier_vertex_count != 0 || flags != k_flag_texture_triangles || uv_count != vertex_count) {
    throw parse_error(8, "tri file layout is not the shipped FRTRI003 shape (quads, modifiers or a different UV or flag set)");
  }

  TriFile file;
  file.vertex_count = static_cast<std::uint32_t>(vertex_count);
  file.triangle_count = static_cast<std::uint32_t>(triangle_count);
  const std::uint64_t vertices = file.vertex_count;
  const auto* position_bytes = in.here(vertices * 12);
  file.positions.resize(vertices * 3);
  std::memcpy(file.positions.data(), position_bytes, vertices * 12);
  in.skip(vertices * 12);
  in.skip(static_cast<std::uint64_t>(triangle_count) * 12);
  in.skip(vertices * 8);
  in.skip(static_cast<std::uint64_t>(triangle_count) * 12);

  for (std::int32_t i = 0; i < morph_count; ++i) {
    const auto name_size = in.take<std::int32_t>();
    if (name_size <= 0 || name_size > k_max_name) throw parse_error(in.offset(), "tri morph name length is out of range");
    TriMorph morph;
    const auto* name_bytes = reinterpret_cast<const char*>(in.here(static_cast<std::uint64_t>(name_size)));
    std::size_t name_length = 0;
    while (name_length < static_cast<std::size_t>(name_size) && name_bytes[name_length] != '\0') ++name_length;
    if (name_length == 0) throw parse_error(in.offset(), "tri morph name is empty");
    morph.name.assign(name_bytes, name_length);
    in.skip(static_cast<std::uint64_t>(name_size));
    morph.scale = in.take<float>();
    if (!std::isfinite(morph.scale)) throw parse_error(in.offset(), "tri morph scale is not finite");
    const auto* delta_bytes = in.here(vertices * 6);
    morph.deltas.resize(vertices * 3);
    std::memcpy(morph.deltas.data(), delta_bytes, vertices * 6);
    in.skip(vertices * 6);
    file.morphs.push_back(std::move(morph));
  }
  if (!in.done()) throw parse_error(in.offset(), "tri file has bytes after its last morph");
  return file;
}

std::vector<float> tri_morph_deltas(const TriMorph& morph) {
  std::vector<float> out(morph.deltas.size());
  for (std::size_t i = 0; i < out.size(); ++i) out[i] = static_cast<float>(morph.deltas[i]) * morph.scale;
  return out;
}

std::vector<float> tri_to_engine_frame(std::span<const float> xyz, float units_per_metre) {
  const float units = units_per_metre == 0.0f ? k_hknp_game_units_per_metre : units_per_metre;
  if (!std::isfinite(units) || units <= 0.0f) throw std::invalid_argument("units_per_metre must be finite and positive");
  if (xyz.size() % 3 != 0) throw std::invalid_argument("a tri vector array holds 3 floats per vertex");
  std::vector<float> out(xyz.size());
  for (std::size_t i = 0; i + 2 < xyz.size(); i += 3) {
    out[i] = xyz[i] / units;
    out[i + 1] = xyz[i + 2] / units;
    out[i + 2] = -xyz[i + 1] / units;
  }
  return out;
}

}  // namespace fo4::nif
