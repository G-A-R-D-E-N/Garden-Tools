#include "fo4/nif/customization_remap.hpp"

#include "fo4/error.hpp"
#include "customization_remap_internal.hpp"
#include "nif_internal.hpp"
#include "write_model.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::nif {
namespace {

constexpr std::string_view k_binary_extra = "NiBinaryExtraData";
constexpr float k_sum_tolerance = 0.01f;
constexpr std::size_t k_problem_limit = 64;

struct Owned {
  std::int32_t remap{-1};
  std::int32_t new_bones{-1};
};

float half_to_float(std::uint16_t bits) {
  const auto value = static_cast<std::uint32_t>(bits);
  const float sign = (value & 0x8000U) != 0 ? -1.0f : 1.0f;
  const auto exponent = static_cast<int>((value >> 10U) & 0x1FU);
  const auto fraction = static_cast<float>(value & 0x3FFU);
  if (exponent == 0) return sign * std::ldexp(fraction, -24);
  if (exponent == 0x1F) {
    return fraction == 0.0f ? sign * std::numeric_limits<float>::infinity()
                            : std::numeric_limits<float>::quiet_NaN();
  }
  return sign * std::ldexp(fraction + 1024.0f, exponent - 25);
}

std::uint32_t le32(const std::byte* at) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) value |= std::to_integer<std::uint32_t>(at[i]) << (8U * i);
  return value;
}

Owned owned_blocks(const NifFile& nif, std::uint32_t shape) {
  if (shape >= nif.blocks().size() || !is_shape_block_type(nif.blocks()[shape].type)) {
    throw std::invalid_argument("the remap data is read from a tri-shape block");
  }
  auto cursor = detail::block_cursor(nif, shape);
  cursor.read_i32();
  const auto count = cursor.read_u32();
  if (static_cast<std::size_t>(count) > cursor.remaining() / 4) {
    throw parse_error(cursor.absolute_offset(), "extra-data list runs past the end of its block");
  }
  Owned owned;
  for (std::uint32_t i = 0; i < count; ++i) {
    const auto ref = cursor.read_i32();
    if (ref < 0 || static_cast<std::size_t>(ref) >= nif.blocks().size()) continue;
    if (nif.blocks()[static_cast<std::size_t>(ref)].type != k_binary_extra) continue;
    auto extra = detail::block_cursor(nif, static_cast<std::uint32_t>(ref));
    const auto name_index = extra.read_i32();
    if (name_index < 0 || static_cast<std::size_t>(name_index) >= nif.strings().size()) continue;
    const auto& name = nif.strings()[static_cast<std::size_t>(name_index)];
    if (name == k_customization_remap_name) {
      if (owned.remap >= 0) throw parse_error(cursor.absolute_offset(), "the shape lists CustomizationRemapData twice");
      owned.remap = ref;
    } else if (name == k_customization_remap_new_bones_name) {
      if (owned.new_bones >= 0) {
        throw parse_error(cursor.absolute_offset(), "the shape lists CustomizationRemapNewBonesData twice");
      }
      owned.new_bones = ref;
    }
  }
  return owned;
}

std::span<const std::byte> payload_of(const NifFile& nif, std::int32_t block) {
  auto cursor = detail::block_cursor(nif, static_cast<std::uint32_t>(block));
  cursor.read_i32();
  const auto size = cursor.read_u32();
  if (static_cast<std::size_t>(size) > cursor.remaining()) {
    throw parse_error(cursor.absolute_offset(), "binary extra data runs past the end of its block");
  }
  return cursor.read_bytes(size);
}

void replace_payload(write_detail::Model& model, std::int32_t block, const write_detail::Bytes& data) {
  auto& bytes = model.blocks[static_cast<std::size_t>(block)];
  write_detail::Bytes out(bytes.begin(), bytes.begin() + 4);
  write_detail::append_int(out, static_cast<std::uint32_t>(data.size()));
  out.insert(out.end(), data.begin(), data.end());
  bytes = std::move(out);
}

RemapRecord decode_record(const std::byte* at) {
  RemapRecord record;
  for (std::size_t k = 0; k < 4; ++k) {
    const auto bits = static_cast<std::uint16_t>(std::to_integer<std::uint32_t>(at[2 * k]) |
                                                 (std::to_integer<std::uint32_t>(at[2 * k + 1]) << 8U));
    record.weight[k] = half_to_float(bits);
    record.bone[k] = std::to_integer<std::uint8_t>(at[8 + k]);
  }
  return record;
}

write_detail::Bytes encode_records(const std::vector<RemapRecord>& records) {
  write_detail::Bytes out;
  for (std::size_t v = 0; v < records.size(); ++v) {
    for (const float weight : records[v].weight) {
      if (!std::isfinite(weight) || weight < 0.0f || weight > 1.0f) {
        throw std::invalid_argument("remap record " + std::to_string(v) + " has an invalid weight");
      }
    }
    remap_detail::append_record(out, records[v]);
  }
  return out;
}

write_detail::Bytes encode_new_bones(const std::vector<RemapNewBone>& new_bones) {
  write_detail::Bytes out;
  for (const auto& bone : new_bones) out.insert(out.end(), bone.raw.begin(), bone.raw.end());
  return out;
}

std::uint16_t type_index(write_detail::Model& model, std::string_view type) {
  auto found = std::find(model.block_types.begin(), model.block_types.end(), std::string(type));
  if (found == model.block_types.end()) {
    model.block_types.emplace_back(type);
    found = model.block_types.end() - 1;
  }
  return static_cast<std::uint16_t>(found - model.block_types.begin());
}

std::uint32_t append_block(write_detail::Model& model, std::string_view type, write_detail::Bytes bytes) {
  model.blocks.push_back(std::move(bytes));
  model.block_type_index.push_back(type_index(model, type));
  return static_cast<std::uint32_t>(model.blocks.size() - 1);
}

// Requires the shape block to start with its name index, then the extra-data count and references.
void add_extra_ref(write_detail::Model& model, std::uint32_t shape, std::uint32_t extra) {
  auto& bytes = model.blocks[shape];
  if (bytes.size() < 8) throw parse_error(0, "shape block is shorter than its extra-data list");
  const auto count = write_detail::read_int<std::uint32_t>(bytes, 4);
  const auto at = 8 + 4 * static_cast<std::size_t>(count);
  if (at > bytes.size()) throw parse_error(0, "extra-data list runs past the end of its shape block");
  write_detail::Bytes ref;
  write_detail::append_int(ref, static_cast<std::int32_t>(extra));
  bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(at), ref.begin(), ref.end());
  write_detail::write_int(bytes, 4, count + 1);
}

void add_binary_extra(write_detail::Model& model, std::uint32_t shape, std::string_view name,
                      const write_detail::Bytes& data) {
  write_detail::Bytes block;
  write_detail::append_int(block, write_detail::intern_string(model, std::string(name)));
  write_detail::append_int(block, static_cast<std::uint32_t>(data.size()));
  block.insert(block.end(), data.begin(), data.end());
  add_extra_ref(model, shape, append_block(model, k_binary_extra, std::move(block)));
}

}  // namespace

std::string remap_new_bone_name(const RemapNewBone& bone) {
  std::string name;
  for (std::size_t i = 0; i < k_remap_new_bone_name_bytes; ++i) {
    const auto ch = std::to_integer<unsigned char>(bone.raw[i]);
    if (ch == 0) break;
    name.push_back(static_cast<char>(ch));
  }
  return name;
}

std::array<float, 16> remap_new_bone_matrix(const RemapNewBone& bone) {
  std::array<float, 16> matrix{};
  for (std::size_t i = 0; i < matrix.size(); ++i) {
    matrix[i] = std::bit_cast<float>(le32(bone.raw.data() + k_remap_new_bone_matrix_at + 4 * i));
  }
  return matrix;
}

void set_remap_new_bone_matrix(RemapNewBone& bone, const std::array<float, 16>& matrix) {
  for (std::size_t i = 0; i < matrix.size(); ++i) {
    const auto bits = std::bit_cast<std::uint32_t>(matrix[i]);
    for (std::size_t k = 0; k < 4; ++k) {
      bone.raw[k_remap_new_bone_matrix_at + 4 * i + k] = static_cast<std::byte>((bits >> (8U * k)) & 0xFFU);
    }
  }
}

CustomizationRemap read_customization_remap(const NifFile& nif, std::uint32_t shape) {
  const auto owned = owned_blocks(nif, shape);
  CustomizationRemap remap;
  remap.remap_block = owned.remap;
  remap.new_bones_block = owned.new_bones;
  if (owned.remap >= 0) {
    const auto data = payload_of(nif, owned.remap);
    const auto offset = nif.blocks()[static_cast<std::size_t>(owned.remap)].offset;
    if (data.size() % k_remap_record_bytes != 0) {
      throw parse_error(offset, "CustomizationRemapData is not a whole number of 12-byte records");
    }
    remap.records.resize(data.size() / k_remap_record_bytes);
    for (std::size_t v = 0; v < remap.records.size(); ++v) {
      remap.records[v] = decode_record(data.data() + v * k_remap_record_bytes);
    }
  }
  if (owned.new_bones >= 0) {
    const auto data = payload_of(nif, owned.new_bones);
    if (data.size() % k_remap_new_bone_bytes != 0) {
      throw parse_error(nif.blocks()[static_cast<std::size_t>(owned.new_bones)].offset,
                        "CustomizationRemapNewBonesData is not a whole number of 208-byte entries");
    }
    remap.new_bones.resize(data.size() / k_remap_new_bone_bytes);
    for (std::size_t i = 0; i < remap.new_bones.size(); ++i) {
      std::copy_n(data.begin() + static_cast<std::ptrdiff_t>(i * k_remap_new_bone_bytes), k_remap_new_bone_bytes,
                  remap.new_bones[i].raw.begin());
    }
  }
  return remap;
}

std::vector<std::byte> write_customization_remap(const NifFile& nif, std::uint32_t shape,
                                                 const CustomizationRemap& remap) {
  const auto owned = owned_blocks(nif, shape);
  if (owned.remap < 0) throw std::invalid_argument("the shape owns no CustomizationRemapData block to replace");
  if (!remap.new_bones.empty() && owned.new_bones < 0) {
    throw std::invalid_argument("the shape owns no CustomizationRemapNewBonesData block to replace");
  }
  const auto vertices = read_shape(nif, shape).verts.size() / 3;
  if (remap.records.size() != vertices) {
    throw std::invalid_argument("the remap needs one record per vertex");
  }
  const auto records = encode_records(remap.records);
  auto model = write_detail::load_model(nif);
  replace_payload(model, owned.remap, records);
  if (owned.new_bones >= 0 && (!remap.new_bones.empty() || remap.new_bones_block >= 0)) {
    replace_payload(model, owned.new_bones, encode_new_bones(remap.new_bones));
  }
  return write_detail::serialize(model);
}

std::vector<std::byte> create_customization_remap(const NifFile& nif, std::uint32_t shape,
                                                  const CustomizationRemap& remap) {
  const auto owned = owned_blocks(nif, shape);
  const bool records = !remap.records.empty();
  const bool bones = !remap.new_bones.empty();
  if (!records && !bones) throw std::invalid_argument("there is no remap data to create");
  if (records && owned.remap >= 0) {
    throw std::invalid_argument("the shape already owns a CustomizationRemapData block");
  }
  if (bones && owned.new_bones >= 0) {
    throw std::invalid_argument("the shape already owns a CustomizationRemapNewBonesData block");
  }
  if (records && remap.records.size() != read_shape(nif, shape).verts.size() / 3) {
    throw std::invalid_argument("the remap needs one record per vertex");
  }
  auto model = write_detail::load_model(nif);
  if (records) add_binary_extra(model, shape, k_customization_remap_name, encode_records(remap.records));
  if (bones) add_binary_extra(model, shape, k_customization_remap_new_bones_name, encode_new_bones(remap.new_bones));
  auto out = write_detail::serialize(model);
  read_nif(out);
  return out;
}

std::vector<std::string> validate_customization_remap(const CustomizationRemap& remap, std::size_t skin_bone_count,
                                                      std::size_t vertex_count) {
  std::vector<std::string> problems;
  std::size_t more = 0;
  const auto report = [&problems, &more](std::string text) {
    if (problems.size() < k_problem_limit) {
      problems.push_back(std::move(text));
    } else {
      ++more;
    }
  };
  const auto bones = skin_bone_count + remap.new_bones.size();
  if (remap.remap_block >= 0 && remap.records.size() != vertex_count) {
    report("CustomizationRemapData holds " + std::to_string(remap.records.size()) + " records for " +
           std::to_string(vertex_count) + " vertices");
  }
  for (std::size_t v = 0; v < remap.records.size(); ++v) {
    const auto& record = remap.records[v];
    float sum = 0.0f;
    bool valid = true;
    for (std::size_t k = 0; k < 4; ++k) {
      if (!std::isfinite(record.weight[k]) || record.weight[k] < 0.0f) {
        valid = false;
        continue;
      }
      sum += record.weight[k];
      if (record.weight[k] > 0.0f && record.bone[k] >= bones) {
        report("record " + std::to_string(v) + " names index " + std::to_string(record.bone[k]) + ", past " +
               std::to_string(bones) + " bones");
      }
    }
    if (!valid) {
      report("record " + std::to_string(v) + " has a negative or non-finite weight");
    } else if (!(std::fabs(sum - 1.0f) <= k_sum_tolerance)) {
      report("record " + std::to_string(v) + " weights sum to " + std::to_string(sum));
    }
  }
  if (more != 0) problems.push_back("and " + std::to_string(more) + " more");
  return problems;
}

std::vector<RemapRecord> reindex_remap_records(std::span<const RemapRecord> records,
                                               std::span<const std::int32_t> source_of_vertex) {
  std::vector<RemapRecord> out;
  out.reserve(source_of_vertex.size());
  for (std::size_t v = 0; v < source_of_vertex.size(); ++v) {
    const auto source = source_of_vertex[v];
    if (source < 0 || static_cast<std::size_t>(source) >= records.size()) {
      throw std::invalid_argument("vertex " + std::to_string(v) + " names no source record of the remap");
    }
    out.push_back(records[static_cast<std::size_t>(source)]);
  }
  return out;
}

namespace remap_detail {

void append_record(write_detail::Bytes& out, const RemapRecord& record) {
  for (const float weight : record.weight) write_detail::append_int(out, write_detail::half_bits(weight));
  for (const auto bone : record.bone) out.push_back(std::byte{bone});
}

RemapRecord record_at(const write_detail::Bytes& bytes, std::size_t at) {
  if (at > bytes.size() || bytes.size() - at < k_remap_record_bytes) {
    throw std::invalid_argument("a remap record runs past its vertex");
  }
  return decode_record(bytes.data() + at);
}

void store_records(write_detail::Model& model, std::int32_t block, std::span<const RemapRecord> records) {
  write_detail::Bytes data;
  for (const auto& record : records) append_record(data, record);
  replace_payload(model, block, data);
}

std::int32_t clone_extra(write_detail::Model& model, std::uint32_t shape, std::int32_t block) {
  const auto source = static_cast<std::size_t>(block);
  auto copy = model.blocks[source];
  const auto type = model.block_type_index[source];
  model.blocks.push_back(std::move(copy));
  model.block_type_index.push_back(type);
  const auto added = static_cast<std::int32_t>(model.blocks.size() - 1);
  auto& bytes = model.blocks[shape];
  const auto count = write_detail::read_int<std::uint32_t>(bytes, 4);
  for (std::size_t i = 0; i < count; ++i) {
    const auto at = 8 + 4 * i;
    if (write_detail::read_int<std::int32_t>(bytes, at) != block) continue;
    write_detail::write_int(bytes, at, added);
    return added;
  }
  throw std::invalid_argument("the shape does not list the extra-data block it should copy");
}

std::optional<std::string> load_for_op(const NifFile& nif, std::uint32_t shape, CustomizationRemap& remap) {
  try {
    remap = read_customization_remap(nif, shape);
  } catch (const parse_error& error) {
    return std::string("its remap data cannot be read: ") + error.what();
  }
  if (remap.remap_block < 0) {
    remap.records.clear();
    return std::nullopt;
  }
  for (std::uint32_t other = 0; other < nif.blocks().size(); ++other) {
    if (other == shape || !is_shape_block_type(nif.blocks()[other].type)) continue;
    try {
      if (owned_blocks(nif, other).remap == remap.remap_block) {
        return "its CustomizationRemapData block is shared with shape block " + std::to_string(other);
      }
    } catch (const parse_error&) {
      continue;
    }
  }
  const auto vertices = read_shape(nif, shape).verts.size() / 3;
  if (remap.records.size() != vertices) {
    return "its CustomizationRemapData holds " + std::to_string(remap.records.size()) + " records for " +
           std::to_string(vertices) + " vertices, so a vertex-count change cannot follow it";
  }
  for (const auto& record : remap.records) {
    for (const float weight : record.weight) {
      if (!std::isfinite(weight)) return std::string("its CustomizationRemapData holds a non-finite weight");
    }
  }
  return std::nullopt;
}

}  // namespace remap_detail

}  // namespace fo4::nif
