#include "fo4/nif/controller_keys.hpp"

#include "fo4/binary/cursor.hpp"
#include "fo4/error.hpp"
#include "nif_internal.hpp"
#include "write_model.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fo4::nif {
namespace {

using write_detail::Bytes;

// Invariant: a TBC tail is three floats, 12 bytes, kept verbatim; they are tension, continuity, bias in file order.
constexpr std::size_t k_tbc_bytes = 12;
constexpr std::size_t k_string_ref_bytes = 4;
constexpr std::string_view k_transform_data = "NiTransformData";
constexpr std::string_view k_keyframe_data = "NiKeyframeData";
constexpr std::string_view k_pos_data = "NiPosData";
constexpr std::string_view k_float_data = "NiFloatData";
constexpr std::string_view k_bool_data = "NiBoolData";
constexpr std::string_view k_text_data = "NiTextKeyExtraData";

// Invariant: the value size follows the Key/QuatKey layout exactly.
// Invariant: quadratic keys carry two tangent values per component.
// Invariant: TBC keys carry the three TBC floats after the value.
std::size_t key_value_size(std::uint32_t interpolation, std::size_t components, std::size_t element) {
  const std::size_t plain = components * element;
  switch (interpolation) {
    case k_key_linear:
    case k_key_const:
      return plain;
    case k_key_quadratic:
      return plain * 3;
    case k_key_tbc:
      return plain + k_tbc_bytes;
    default:
      throw parse_error(0, "key interpolation " + std::to_string(interpolation) + " is not a KeyType");
  }
}

// Invariant: a key group's count and interpolation come first, then its keys.
KeyChannel read_group(binary::Cursor& cursor, std::size_t components, std::size_t element, std::uint32_t shape) {
  KeyChannel channel;
  const auto count = cursor.read_u32();
  if (count == 0) return channel;
  if (count > cursor.remaining() / 4) {
    throw parse_error(cursor.absolute_offset(), "block " + std::to_string(shape) + " key count runs past its block");
  }
  channel.interpolation = cursor.read_u32();
  const auto size = key_value_size(channel.interpolation, components, element);
  if (size > cursor.remaining() || (cursor.remaining() < count * (4 + size))) {
    throw parse_error(cursor.absolute_offset(), "block " + std::to_string(shape) + " keys run past their block");
  }
  for (std::uint32_t i = 0; i < count; ++i) {
    KeyRecord key;
    key.time = cursor.read_f32();
    if (!std::isfinite(key.time)) {
      throw parse_error(cursor.absolute_offset(), "block " + std::to_string(shape) + " has a non-finite key time");
    }
    const auto bytes = cursor.read_bytes(size);
    key.value.assign(bytes.begin(), bytes.end());
    channel.keys.push_back(std::move(key));
  }
  return channel;
}

// Invariant: nif.xml QuatKey never has tangents, so a quadratic rotation key is the bare quaternion.
// Invariant: only a TBC rotation key carries a tail, the same 12 bytes a plain key does.
std::size_t quat_value_size(std::uint32_t interpolation) {
  constexpr std::size_t k_quat_bytes = 16;
  return interpolation == k_key_tbc ? k_quat_bytes + k_tbc_bytes : k_quat_bytes;
}

// Invariant: rotation keys share the already-read count and type, so they have no group header of their own.
KeyChannel read_quat_keys(binary::Cursor& cursor, std::uint32_t count, std::uint32_t interpolation,
                          std::uint32_t shape) {
  KeyChannel channel;
  if (interpolation < k_key_linear || interpolation > k_key_const || interpolation == k_key_xyz) {
    throw parse_error(cursor.absolute_offset(),
                      "block " + std::to_string(shape) + " has a rotation type that is not a quaternion KeyType");
  }
  channel.interpolation = interpolation;
  const auto size = quat_value_size(interpolation);
  if (count > cursor.remaining() / (4 + size)) {
    throw parse_error(cursor.absolute_offset(), "block " + std::to_string(shape) + " rotation keys run past its block");
  }
  for (std::uint32_t i = 0; i < count; ++i) {
    KeyRecord key;
    key.time = cursor.read_f32();
    if (!std::isfinite(key.time)) {
      throw parse_error(cursor.absolute_offset(), "block " + std::to_string(shape) + " has a non-finite key time");
    }
    const auto bytes = cursor.read_bytes(size);
    key.value.assign(bytes.begin(), bytes.end());
    channel.keys.push_back(std::move(key));
  }
  return channel;
}

// Invariant: NiTextKeyExtraData is the NiExtraData name, a count, then one (time, string-table index) pair per key.
// Invariant: FO4 strings are header string-table indices, so a text value is the 4-byte index, never inline text.
// Constraint: every index must be -1 or name a header string.
void require_string_ref(const NifFile& nif, std::int32_t ref, std::size_t offset, std::uint32_t shape) {
  if (ref < -1 || (ref >= 0 && static_cast<std::size_t>(ref) >= nif.strings().size())) {
    throw parse_error(offset, "block " + std::to_string(shape) + " names a string past the header table");
  }
}

KeyChannel read_text_group(const NifFile& nif, binary::Cursor& cursor, std::uint32_t shape) {
  KeyChannel channel;
  if (cursor.remaining() < 8) {
    throw parse_error(cursor.absolute_offset(), "block " + std::to_string(shape) + " is shorter than its name and count");
  }
  require_string_ref(nif, cursor.read_i32(), cursor.absolute_offset(), shape);
  const auto count = cursor.read_u32();
  if (count == 0) return channel;
  if (count > cursor.remaining() / (4 + k_string_ref_bytes)) {
    throw parse_error(cursor.absolute_offset(), "block " + std::to_string(shape) + " text keys run past their block");
  }
  channel.interpolation = k_key_linear;
  for (std::uint32_t i = 0; i < count; ++i) {
    KeyRecord key;
    key.time = cursor.read_f32();
    const auto bytes = cursor.read_bytes(k_string_ref_bytes);
    key.value.assign(bytes.begin(), bytes.end());
    std::uint32_t ref = 0;
    for (std::size_t k = 0; k < 4; ++k) ref |= std::to_integer<std::uint32_t>(key.value[k]) << (8 * k);
    require_string_ref(nif, static_cast<std::int32_t>(ref), cursor.absolute_offset(), shape);
    channel.keys.push_back(std::move(key));
  }
  return channel;
}

void append_u32(Bytes& out, std::uint32_t value) {
  for (std::size_t i = 0; i < 4; ++i) out.push_back(std::byte{static_cast<std::uint8_t>(value >> (8 * i))});
}

void append_key(Bytes& out, const KeyRecord& key) {
  append_u32(out, std::bit_cast<std::uint32_t>(key.time));
  out.insert(out.end(), key.value.begin(), key.value.end());
}

// Constraint: times must be finite and non-decreasing, and every value must match the layout size.
void append_value(Bytes& out, const KeyRecord& key, std::size_t size, float previous, std::uint32_t shape) {
  if (!std::isfinite(key.time) || key.time < previous) {
    throw std::invalid_argument("block " + std::to_string(shape) + " keys must be finite and non-decreasing");
  }
  if (key.value.size() != size) {
    throw std::invalid_argument("block " + std::to_string(shape) + " has a key of the wrong size");
  }
  append_key(out, key);
}

void write_group(Bytes& out, const KeyChannel& channel, std::size_t components, std::size_t element,
                 std::uint32_t shape) {
  append_u32(out, static_cast<std::uint32_t>(channel.keys.size()));
  if (channel.keys.empty()) {
    if (channel.interpolation != 0) {
      throw std::invalid_argument("block " + std::to_string(shape) + " has an interpolation on an empty group");
    }
    return;
  }
  if (channel.interpolation < k_key_linear || channel.interpolation > k_key_const) {
    throw std::invalid_argument("block " + std::to_string(shape) + " has an interpolation that is not a KeyType");
  }
  const auto size = key_value_size(channel.interpolation, components, element);
  append_u32(out, channel.interpolation);
  float previous = -std::numeric_limits<float>::infinity();
  for (const auto& key : channel.keys) {
    append_value(out, key, size, previous, shape);
    previous = key.time;
  }
}

// Invariant: the block keeps its own name reference; only the keys are replaced.
void write_text_group(Bytes& out, const NifFile& nif, const KeyChannel& channel, std::uint32_t shape) {
  auto existing = detail::block_cursor(nif, shape);
  if (existing.remaining() < 4) throw parse_error(0, "block " + std::to_string(shape) + " has no name reference");
  append_u32(out, static_cast<std::uint32_t>(existing.read_i32()));
  append_u32(out, static_cast<std::uint32_t>(channel.keys.size()));
  float previous = -std::numeric_limits<float>::infinity();
  for (const auto& key : channel.keys) {
    if (!std::isfinite(key.time) || key.time < previous) {
      throw std::invalid_argument("block " + std::to_string(shape) + " keys must be finite and non-decreasing");
    }
    previous = key.time;
    if (key.value.size() != k_string_ref_bytes) {
      throw std::invalid_argument("block " + std::to_string(shape) + " has a text key that is not a string index");
    }
    std::uint32_t ref = 0;
    for (std::size_t i = 0; i < 4; ++i) ref |= std::to_integer<std::uint32_t>(key.value[i]) << (8 * i);
    const auto index = static_cast<std::int32_t>(ref);
    if (index < -1 || (index >= 0 && static_cast<std::size_t>(index) >= nif.strings().size())) {
      throw std::invalid_argument("block " + std::to_string(shape) + " has a text key naming a string past the table");
    }
    append_key(out, key);
  }
}

}  // namespace

NifKeys read_controller_keys(const NifFile& nif, std::uint32_t block) {
  if (block >= nif.blocks().size()) throw std::invalid_argument("key block is past the file");
  const auto& info = nif.blocks()[block];
  NifKeys keys;
  keys.block_type = info.type;
  auto cursor = detail::block_cursor(nif, block);
  if (info.type == k_transform_data || info.type == k_keyframe_data) {
    const auto rotations = cursor.read_u32();
    if (rotations == 0) {
      keys.translations = read_group(cursor, 3, 4, block);
      keys.scales = read_group(cursor, 1, 4, block);
      if (!cursor.at_end()) throw parse_error(cursor.absolute_offset(), "key block has trailing bytes");
      return keys;
    }
    keys.rotation_type = cursor.read_u32();
    if (keys.rotation_type == k_key_xyz) {
      if (rotations != 1) {
        throw parse_error(cursor.absolute_offset(), "XYZ rotations need a rotation count of one");
      }
      for (auto& channel : keys.rotation_xyz) channel = read_group(cursor, 1, 4, block);
    } else {
      keys.rotation_quat = read_quat_keys(cursor, rotations, keys.rotation_type, block);
    }
    keys.translations = read_group(cursor, 3, 4, block);
    keys.scales = read_group(cursor, 1, 4, block);
    if (!cursor.at_end()) throw parse_error(cursor.absolute_offset(), "key block has trailing bytes");
    return keys;
  }
  if (info.type == k_pos_data) {
    keys.data = read_group(cursor, 3, 4, block);
  } else if (info.type == k_float_data) {
    keys.data = read_group(cursor, 1, 4, block);
  } else if (info.type == k_bool_data) {
    keys.data = read_group(cursor, 1, 1, block);
  } else if (info.type == k_text_data) {
    keys.data = read_text_group(nif, cursor, block);
  } else {
    throw std::invalid_argument("block " + std::to_string(block) + " is " + info.type + ", not a key block");
  }
  if (!cursor.at_end()) throw parse_error(cursor.absolute_offset(), "key block has trailing bytes");
  return keys;
}

std::vector<std::byte> write_controller_keys(const NifFile& nif, std::uint32_t block, const NifKeys& keys) {
  if (block >= nif.blocks().size()) throw std::invalid_argument("key block is past the file");
  if (nif.blocks()[block].type != keys.block_type) {
    throw std::invalid_argument("key replacement names a different block type");
  }
  const auto& type = keys.block_type;
  Bytes out;
  if (type == k_transform_data || type == k_keyframe_data) {
    const bool xyz_typed = keys.rotation_xyz[0].interpolation != 0 || keys.rotation_xyz[1].interpolation != 0 ||
                           keys.rotation_xyz[2].interpolation != 0;
    const bool has_xyz = keys.rotation_xyz[0].keys.size() + keys.rotation_xyz[1].keys.size() +
                             keys.rotation_xyz[2].keys.size() >
                         0;
    const bool has_quat = keys.rotation_type != k_key_xyz &&
                          (!keys.rotation_quat.keys.empty() || keys.rotation_quat.interpolation != 0 ||
                           (keys.rotation_type >= k_key_linear && keys.rotation_type <= k_key_const));
    if (has_quat && (has_xyz || xyz_typed)) {
      throw std::invalid_argument("key replacement mixes quaternion and XYZ rotations");
    }
    if (!has_quat && !has_xyz && !xyz_typed) {
      append_u32(out, 0);
      write_group(out, keys.translations, 3, 4, block);
      write_group(out, keys.scales, 1, 4, block);
    } else if (has_xyz || xyz_typed || keys.rotation_type == k_key_xyz) {
      append_u32(out, 1);
      append_u32(out, k_key_xyz);
      for (const auto& channel : keys.rotation_xyz) write_group(out, channel, 1, 4, block);
      write_group(out, keys.translations, 3, 4, block);
      write_group(out, keys.scales, 1, 4, block);
    } else {
      if (keys.rotation_type < k_key_linear || keys.rotation_type > k_key_const || keys.rotation_type == k_key_xyz) {
        throw std::invalid_argument("key replacement has a rotation type that is not a quaternion KeyType");
      }
      if (keys.rotation_quat.keys.size() > 0xFFFFFFFFU) {
        throw std::invalid_argument("key replacement holds too many rotation keys");
      }
      append_u32(out, static_cast<std::uint32_t>(keys.rotation_quat.keys.size()));
      append_u32(out, keys.rotation_type);
      const auto size = quat_value_size(keys.rotation_type);
      float previous = -std::numeric_limits<float>::infinity();
      for (const auto& key : keys.rotation_quat.keys) {
        append_value(out, key, size, previous, block);
        previous = key.time;
      }
      write_group(out, keys.translations, 3, 4, block);
      write_group(out, keys.scales, 1, 4, block);
    }
  } else if (type == k_pos_data) {
    write_group(out, keys.data, 3, 4, block);
  } else if (type == k_float_data) {
    write_group(out, keys.data, 1, 4, block);
  } else if (type == k_bool_data) {
    write_group(out, keys.data, 1, 1, block);
  } else if (type == k_text_data) {
    write_text_group(out, nif, keys.data, block);
  } else {
    throw std::invalid_argument("block " + std::to_string(block) + " is " + type + ", not a key block");
  }
  auto model = write_detail::load_model(nif);
  model.blocks[block] = std::move(out);
  auto bytes = write_detail::serialize(model);
  const auto check = read_controller_keys(read_nif(bytes), block);
  if (check.block_type != type || check.rotation_quat.keys.size() != keys.rotation_quat.keys.size() ||
      check.translations.keys.size() != keys.translations.keys.size() ||
      check.scales.keys.size() != keys.scales.keys.size() || check.data.keys.size() != keys.data.keys.size()) {
    throw std::runtime_error("internal error: the keys do not read back");
  }
  return bytes;
}

}  // namespace fo4::nif
