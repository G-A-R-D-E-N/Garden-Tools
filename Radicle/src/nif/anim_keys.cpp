#include "fo4/nif/anim.hpp"

#include "fo4/binary/cursor.hpp"
#include "fo4/constants.hpp"
#include "fo4/error.hpp"
#include "fo4/nif/controller_keys.hpp"
#include "nif_internal.hpp"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fo4::nif {
namespace {

// Invariant: key block layout lives in controller_keys.hpp, so this file only decodes those records.
float key_float(const KeyRecord& key, std::size_t at) {
  if (key.value.size() < at + 4) throw parse_error(0, "a key value is shorter than its interpolation type");
  std::uint32_t bits = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    bits |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(key.value[at + i])) << (8 * i);
  }
  return std::bit_cast<float>(bits);
}

std::vector<AnimKeyFloat> decode_float_group(const KeyChannel& channel) {
  std::vector<AnimKeyFloat> out;
  out.reserve(channel.keys.size());
  for (const auto& key : channel.keys) {
    AnimKeyFloat record;
    record.time = key.time;
    record.value = key_float(key, 0);
    if (channel.interpolation == k_key_quadratic) {
      record.forward = key_float(key, 4);
      record.backward = key_float(key, 8);
    } else if (channel.interpolation == k_key_tbc) {
      for (std::size_t k = 0; k < 3; ++k) record.tbc[k] = key_float(key, 4 + 4 * k);
    }
    out.push_back(record);
  }
  return out;
}

std::vector<AnimKeyVec3> decode_vec3_group(const KeyChannel& channel) {
  std::vector<AnimKeyVec3> out;
  out.reserve(channel.keys.size());
  for (const auto& key : channel.keys) {
    AnimKeyVec3 record;
    record.time = key.time;
    for (std::size_t k = 0; k < 3; ++k) record.value[k] = key_float(key, 4 * k);
    if (channel.interpolation == k_key_quadratic) {
      for (std::size_t k = 0; k < 3; ++k) {
        record.forward[k] = key_float(key, 12 + 4 * k);
        record.backward[k] = key_float(key, 24 + 4 * k);
      }
    } else if (channel.interpolation == k_key_tbc) {
      for (std::size_t k = 0; k < 3; ++k) record.tbc[k] = key_float(key, 12 + 4 * k);
    }
    out.push_back(record);
  }
  return out;
}

std::vector<AnimKeyQuat> decode_quat_group(const KeyChannel& channel) {
  std::vector<AnimKeyQuat> out;
  out.reserve(channel.keys.size());
  for (const auto& key : channel.keys) {
    AnimKeyQuat record;
    record.time = key.time;
    for (std::size_t k = 0; k < 4; ++k) record.value[k] = key_float(key, 4 * k);
    out.push_back(record);
  }
  return out;
}

// Requires the data block to be of the expected type or NiKeyframeData.
std::uint32_t require_key_data(const NifFile& nif, std::int32_t ref, const char* expected) {
  if (ref < 0 || static_cast<std::size_t>(ref) >= nif.blocks().size()) {
    throw parse_error(0, std::string(expected) + " is past the file");
  }
  const auto block = static_cast<std::uint32_t>(ref);
  const auto& type = nif.blocks()[block].type;
  if (type != expected && type != "NiKeyframeData") {
    throw parse_error(0, "block is " + type + ", not " + expected);
  }
  return block;
}

// Requires block of the expected type.
void require_anim_type(const NifFile& nif, std::uint32_t block, const char* type) {
  if (block >= nif.blocks().size()) throw parse_error(0, "animation block is out of range");
  if (nif.blocks()[block].type != type) {
    throw parse_error(0, std::string("block is ") + nif.blocks()[block].type + ", not " + type);
  }
}

const float* value_of(const AnimKeyFloat& key) { return &key.value; }
const float* value_of(const AnimKeyVec3& key) { return key.value; }
const float* forward_of(const AnimKeyFloat& key) { return &key.forward; }
const float* forward_of(const AnimKeyVec3& key) { return key.forward; }
const float* backward_of(const AnimKeyFloat& key) { return &key.backward; }
const float* backward_of(const AnimKeyVec3& key) { return key.backward; }

struct Tangents {
  float in{0.0f};
  float out{0.0f};
};

// Constraint: tbc is (tension, continuity, bias) in file order, not the nif.xml t, b, c order (code note Radicle.md, anim_keys.cpp:tbc_tangents); tangents are per-segment Hermite units.
Tangents tbc_tangents(const float* tbc, bool has_prev, float prev, float value, bool has_next, float next, float dt_prev,
                      float dt_next) {
  const float tension = tbc[0];
  const float continuity = tbc[1];
  const float bias = tbc[2];
  const float in_prev = (1.0f - tension) * (1.0f - continuity) * (1.0f + bias);
  const float in_next = (1.0f - tension) * (1.0f + continuity) * (1.0f - bias);
  const float out_prev = (1.0f - tension) * (1.0f + continuity) * (1.0f + bias);
  const float out_next = (1.0f - tension) * (1.0f - continuity) * (1.0f - bias);
  if (!has_prev || !has_next) {
    const float delta = has_next ? next - value : value - prev;
    return {delta * (in_prev + in_next) * 0.5f, delta * (out_prev + out_next) * 0.5f};
  }
  const float span = dt_prev + dt_next;
  if (!(span > 0.0f)) return {};
  const float before = value - prev;
  const float after = next - value;
  return {(before * in_prev + after * in_next) * (dt_prev / span), (before * out_prev + after * out_next) * (dt_next / span)};
}

// Invariant: float and vec3 channels share this sampler; the segment is bracketed by keys[i - 1] and keys[i].
template <std::size_t N, class Key>
void sample_keys(const std::vector<Key>& keys, AnimKeyType type, float time, const float* pose, float* out) {
  if (keys.empty()) {
    for (std::size_t k = 0; k < N; ++k) out[k] = pose[k];
    return;
  }
  std::size_t i = 1;
  while (i < keys.size() && !(time < keys[i].time)) ++i;
  if (time <= keys.front().time || i == keys.size()) {
    const Key& end = time <= keys.front().time ? keys.front() : keys.back();
    for (std::size_t k = 0; k < N; ++k) out[k] = value_of(end)[k];
    return;
  }
  const Key& k0 = keys[i - 1];
  const Key& k1 = keys[i];
  const float* v0 = value_of(k0);
  const float* v1 = value_of(k1);
  if (type == AnimKeyType::Const) {
    for (std::size_t k = 0; k < N; ++k) out[k] = v0[k];
    return;
  }
  const float span = k1.time - k0.time;
  const float alpha = span > 0.0f ? (time - k0.time) / span : 0.0f;
  if (type != AnimKeyType::Quadratic && type != AnimKeyType::Tbc) {
    for (std::size_t k = 0; k < N; ++k) out[k] = v0[k] + alpha * (v1[k] - v0[k]);
    return;
  }
  const float a2 = alpha * alpha;
  const float a3 = a2 * alpha;
  const float h00 = 2.0f * a3 - 3.0f * a2 + 1.0f;
  const float h01 = -2.0f * a3 + 3.0f * a2;
  const float h10 = a3 - 2.0f * a2 + alpha;
  const float h11 = a3 - a2;
  const bool has_prev = i >= 2;
  const bool has_next = i + 1 < keys.size();
  for (std::size_t k = 0; k < N; ++k) {
    float start = 0.0f;
    float end = 0.0f;
    if (type == AnimKeyType::Quadratic) {
      start = backward_of(k0)[k];
      end = forward_of(k1)[k];
    } else {
      const float prev = has_prev ? value_of(keys[i - 2])[k] : 0.0f;
      const float next = has_next ? value_of(keys[i + 1])[k] : 0.0f;
      const float dt_prev = has_prev ? k0.time - keys[i - 2].time : 0.0f;
      const float dt_next = has_next ? keys[i + 1].time - k1.time : 0.0f;
      start = tbc_tangents(k0.tbc, has_prev, prev, v0[k], true, v1[k], dt_prev, span).out;
      end = tbc_tangents(k1.tbc, true, v0[k], v1[k], has_next, next, span, dt_next).in;
    }
    out[k] = h00 * v0[k] + h01 * v1[k] + h10 * start + h11 * end;
  }
}

float sample_float_keys(const std::vector<AnimKeyFloat>& keys, AnimKeyType type, float time, float pose) {
  float out = pose;
  sample_keys<1>(keys, type, time, &pose, &out);
  return out;
}

void sample_vec3_keys(const std::vector<AnimKeyVec3>& keys, AnimKeyType type, float time, const float* pose,
                      float* out) {
  sample_keys<3>(keys, type, time, pose, out);
}

void normalize_quat(float* q) {
  const float len = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
  if (len > 1e-8f) {
    q[0] /= len;
    q[1] /= len;
    q[2] /= len;
    q[3] /= len;
  }
}

// Constraint: quadratic and TBC quaternion keys sample as shortest-arc slerp (code note Radicle.md, anim_keys.cpp:sample_keys).
void sample_quat_keys(const std::vector<AnimKeyQuat>& keys, AnimKeyType type, float time, const float* pose,
                      float* out) {
  if (keys.empty()) {
    out[0] = pose[0];
    out[1] = pose[1];
    out[2] = pose[2];
    out[3] = pose[3];
    return;
  }
  if (time <= keys.front().time) {
    for (int k = 0; k < 4; ++k) out[k] = keys.front().value[k];
    return;
  }
  if (time >= keys.back().time) {
    for (int k = 0; k < 4; ++k) out[k] = keys.back().value[k];
    return;
  }
  for (std::size_t i = 1; i < keys.size(); ++i) {
    if (time < keys[i].time) {
      if (type == AnimKeyType::Const) {
        for (int k = 0; k < 4; ++k) out[k] = keys[i - 1].value[k];
        return;
      }
      const float span = keys[i].time - keys[i - 1].time;
      float alpha = span > 0.0f ? (time - keys[i - 1].time) / span : 0.0f;
      float a[4] = {keys[i - 1].value[0], keys[i - 1].value[1], keys[i - 1].value[2], keys[i - 1].value[3]};
      float b[4] = {keys[i].value[0], keys[i].value[1], keys[i].value[2], keys[i].value[3]};
      float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
      if (dot < 0.0f) {
        dot = -dot;
        for (float& v : b) v = -v;
      }
      if (dot > 0.9995f) {
        for (int k = 0; k < 4; ++k) out[k] = a[k] + alpha * (b[k] - a[k]);
      } else {
        const float theta = std::acos(dot);
        const float s = std::sin(theta);
        const float wa = std::sin((1.0f - alpha) * theta) / s;
        const float wb = std::sin(alpha * theta) / s;
        for (int k = 0; k < 4; ++k) out[k] = wa * a[k] + wb * b[k];
      }
      normalize_quat(out);
      return;
    }
  }
  for (int k = 0; k < 4; ++k) out[k] = keys.back().value[k];
}

// Must map a block index to its node name; empty when the block is not a node.
std::string node_name_for_block(const NifFile& nif, std::int32_t block) {
  if (block < 0 || static_cast<std::size_t>(block) >= nif.blocks().size()) return {};
  const auto node = nif.node_for_block(static_cast<std::uint32_t>(block));
  if (node < 0 || static_cast<std::size_t>(node) >= nif.nodes().size()) return {};
  return nif.nodes()[static_cast<std::size_t>(node)].name;
}

}  // namespace

TransformKeys read_transform_keys(const NifFile& nif, std::uint32_t interpolator) {
  require_anim_type(nif, interpolator, "NiTransformInterpolator");
  if (nif.version() != k_nif_version || nif.user_version() != k_nif_user_version || nif.bs_version() != k_nif_bs_version) {
    throw parse_error(0, "unsupported animation reference layout version");
  }
  auto cursor = detail::block_cursor(nif, interpolator);
  TransformKeys keys;
  for (float& v : keys.pose.translation) v = cursor.read_f32();
  for (float& v : keys.pose.rotation) v = cursor.read_f32();
  keys.pose.scale = cursor.read_f32();
  const auto data_ref = cursor.read_i32();
  if (!cursor.at_end()) throw parse_error(0, "NiTransformInterpolator does not match its reference layout");
  if (data_ref < 0) return keys;
  const auto raw = read_controller_keys(nif, require_key_data(nif, data_ref, "NiTransformData"));
  keys.has_data = true;
  if (raw.rotation_type == static_cast<std::uint32_t>(AnimKeyType::XyzRot)) {
    keys.rotation_type = AnimKeyType::XyzRot;
    keys.rotation_x = decode_float_group(raw.rotation_xyz[0]);
    keys.rotation_y = decode_float_group(raw.rotation_xyz[1]);
    keys.rotation_z = decode_float_group(raw.rotation_xyz[2]);
  } else if (!raw.rotation_quat.keys.empty()) {
    keys.rotation_type = static_cast<AnimKeyType>(raw.rotation_quat.interpolation);
    keys.rotation_quat = decode_quat_group(raw.rotation_quat);
  }
  if (!raw.translations.keys.empty()) {
    keys.translation_type = static_cast<AnimKeyType>(raw.translations.interpolation);
  }
  keys.translation = decode_vec3_group(raw.translations);
  if (!raw.scales.keys.empty()) keys.scale_type = static_cast<AnimKeyType>(raw.scales.interpolation);
  keys.scale = decode_float_group(raw.scales);
  return keys;
}

ValueKeys read_value_keys(const NifFile& nif, std::uint32_t interpolator) {
  if (interpolator >= nif.blocks().size()) throw parse_error(0, "animation block is out of range");
  const auto& type = nif.blocks()[interpolator].type;
  const bool is_float = type == "NiFloatInterpolator";
  if (!is_float && type != "NiPoint3Interpolator") {
    throw parse_error(0, "block is " + type + ", not NiFloatInterpolator or NiPoint3Interpolator");
  }
  if (nif.version() != k_nif_version || nif.user_version() != k_nif_user_version || nif.bs_version() != k_nif_bs_version) {
    throw parse_error(0, "unsupported animation reference layout version");
  }
  auto cursor = detail::block_cursor(nif, interpolator);
  ValueKeys out;
  out.components = is_float ? 1U : 3U;
  for (std::uint32_t i = 0; i < out.components; ++i) out.pose[i] = cursor.read_f32();
  out.data_ref = cursor.read_i32();
  if (!cursor.at_end()) throw parse_error(0, type + " does not match its reference layout");
  if (out.data_ref < 0) return out;
  if (static_cast<std::size_t>(out.data_ref) >= nif.blocks().size()) throw parse_error(0, "key data is past the file");
  const auto data = static_cast<std::uint32_t>(out.data_ref);
  const char* expected = is_float ? "NiFloatData" : "NiPosData";
  if (nif.blocks()[data].type != expected) throw parse_error(0, "block is " + nif.blocks()[data].type + ", not " + expected);
  const auto raw = read_controller_keys(nif, data);
  out.interpolation = raw.data.interpolation;
  out.keys.reserve(raw.data.keys.size());
  for (const auto& key : raw.data.keys) {
    AnimValueKey entry;
    entry.time = key.time;
    float* const groups[3] = {entry.value, entry.forward, entry.backward};
    const std::size_t used = out.interpolation == k_key_quadratic ? 3 : 1;
    for (std::size_t g = 0; g < used; ++g) {
      for (std::uint32_t k = 0; k < out.components; ++k) groups[g][k] = key_float(key, 4 * (g * out.components + k));
    }
    if (out.interpolation == k_key_tbc) {
      for (std::size_t k = 0; k < 3; ++k) entry.tbc[k] = key_float(key, 4 * (out.components + k));
    }
    out.keys.push_back(entry);
  }
  return out;
}

std::vector<AnimNodePose> evaluate_sequence(const NifFile& nif, std::uint32_t sequence_block, float time) {
  require_anim_type(nif, sequence_block, "NiControllerSequence");
  auto cursor = detail::block_cursor(nif, sequence_block);
  // Must reuse the sequence layout: name, controlled count/array-grow, entries, weight, text keys, cycle/freq/start/stop.
  detail::read_string_ref(cursor, nif);
  const auto controlled = cursor.read_u32();
  cursor.read_u32();
  struct Entry {
    std::string node;
    std::int32_t interp{-1};
    std::int32_t controller{-1};
  };
  std::vector<Entry> entries;
  entries.reserve(controlled);
  for (std::uint32_t i = 0; i < controlled; ++i) {
    Entry entry;
    entry.interp = cursor.read_i32();
    entry.controller = cursor.read_i32();
    cursor.read_u8();
    entry.node = detail::read_string_ref(cursor, nif);
    detail::read_string_ref(cursor, nif);
    detail::read_string_ref(cursor, nif);
    detail::read_string_ref(cursor, nif);
    detail::read_string_ref(cursor, nif);
    entries.push_back(std::move(entry));
  }
  std::vector<AnimNodePose> out;
  for (const auto& entry : entries) {
    if (entry.node.empty()) continue;
    if (entry.interp < 0 || static_cast<std::size_t>(entry.interp) >= nif.blocks().size()) continue;
    if (nif.blocks()[static_cast<std::size_t>(entry.interp)].type != "NiTransformInterpolator") continue;
    const auto keys = read_transform_keys(nif, static_cast<std::uint32_t>(entry.interp));
    AnimNodePose pose;
    pose.node_name = entry.node;
    sample_vec3_keys(keys.translation, keys.translation_type, time, keys.pose.translation, pose.pose.translation);
    if (!keys.rotation_quat.empty() || keys.rotation_type != AnimKeyType::XyzRot) {
      sample_quat_keys(keys.rotation_quat, keys.rotation_type, time, keys.pose.rotation, pose.pose.rotation);
    } else {
      // Must hold XYZ-rot keys as Euler XYZ until a rotation convention is proven; keep rest pose.
      pose.pose.rotation[0] = keys.pose.rotation[0];
      pose.pose.rotation[1] = keys.pose.rotation[1];
      pose.pose.rotation[2] = keys.pose.rotation[2];
      pose.pose.rotation[3] = keys.pose.rotation[3];
    }
    pose.pose.scale = sample_float_keys(keys.scale, keys.scale_type, time, keys.pose.scale);
    out.push_back(std::move(pose));
    // Must also emit extra targets when the controller is multi-target with the same pose.
    if (entry.controller >= 0 &&
        static_cast<std::size_t>(entry.controller) < nif.blocks().size() &&
        nif.blocks()[static_cast<std::size_t>(entry.controller)].type == "NiMultiTargetTransformController") {
      auto extra = detail::block_cursor(nif, static_cast<std::uint32_t>(entry.controller));
      extra.read_i32();
      extra.read_u16();
      extra.read_f32();
      extra.read_f32();
      extra.read_f32();
      extra.read_f32();
      extra.read_i32();
      const auto targets = extra.read_u16();
      for (std::uint16_t t = 0; t < targets; ++t) {
        const auto target = extra.read_i32();
        const auto name = node_name_for_block(nif, target);
        if (name.empty() || name == out.back().node_name) continue;
        AnimNodePose copy = out.back();
        copy.node_name = name;
        out.push_back(std::move(copy));
      }
    }
  }
  return out;
}

}  // namespace fo4::nif
