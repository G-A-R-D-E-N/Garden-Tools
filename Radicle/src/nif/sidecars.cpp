#include "fo4/nif/sidecars.hpp"

#include "fo4/cloth/cloth_profile.hpp"
#include "fo4/error.hpp"
#include "fo4/hash.hpp"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::nif {
namespace {

using json = nlohmann::json;

json parse_document(std::string_view text) {
  const std::string owned{text};
  try {
    return json::parse(owned);
  } catch (const json::exception& error) {
    throw parse_error(0, error.what());
  }
}

const json& field(const json& object, const std::string& key) {
  if (!object.is_object() || !object.contains(key)) {
    throw parse_error(0, "missing " + key);
  }
  return object.at(key);
}

std::string string_field(const json& object, const std::string& key) {
  const json& value = field(object, key);
  if (!value.is_string()) {
    throw parse_error(0, key + " must be a string");
  }
  return value.get<std::string>();
}

std::uint32_t segment_id(const json& value) {
  if (!value.is_number_unsigned()) {
    throw parse_error(0, "segment id must be an unsigned integer");
  }
  const auto id = value.get<std::uint64_t>();
  if (id > std::numeric_limits<std::uint32_t>::max()) {
    throw parse_error(0, "segment id exceeds 32 bits");
  }
  return static_cast<std::uint32_t>(id);
}

float checked_scale(double value) {
  if (!(value > 0.0 && value <= static_cast<double>(std::numeric_limits<float>::max()))) {
    throw parse_error(0, "sclp scale must be a positive finite float");
  }
  const auto narrowed = static_cast<float>(value);
  if (narrowed == 0.0f) throw parse_error(0, "sclp scale must be a positive finite float");
  return narrowed;
}

}  // namespace

std::uint32_t bone_id_hash(std::string_view name) {
  std::uint32_t crc = 0;
  for (unsigned char byte : name) {
    if (byte >= 'A' && byte <= 'Z') {
      byte = static_cast<unsigned char>(byte - 'A' + 'a');
    }
    crc = crc32_raw_step(crc, byte);
  }
  return crc;
}

std::vector<SsfShape> read_ssf(std::string_view text) {
  const json root = parse_document(text);
  std::vector<SsfShape> shapes;
  if (root.is_null()) {
    return shapes;
  }
  if (!root.is_object()) {
    throw parse_error(0, "ssf root must be an object");
  }
  for (const auto& [name, shape] : root.items()) {
    if (!shape.is_object()) {
      throw parse_error(0, "ssf shape must be an object");
    }
    SsfShape out;
    out.name = name;
    json rest = shape;
    if (shape.contains("BaseBoneName")) {
      out.base_bone = string_field(shape, "BaseBoneName");
      rest.erase("BaseBoneName");
    }
    if (shape.contains("DeltaBones") && !shape.at("DeltaBones").is_null()) {
      const json& bones = shape.at("DeltaBones");
      if (!bones.is_array()) {
        throw parse_error(0, "DeltaBones must be an array");
      }
      for (const json& bone : bones) {
        SsfBoneDelta delta;
        delta.bone_name = string_field(bone, "BoneName");
        const json ids = field(bone, "BoneDeltaList");
        if (!ids.is_array()) {
          throw parse_error(0, "BoneDeltaList must be an array");
        }
        for (const json& id : ids) {
          delta.segment_ids.push_back(segment_id(id));
        }
        json bone_rest = bone;
        bone_rest.erase("BoneName");
        bone_rest.erase("BoneDeltaList");
        if (!bone_rest.empty()) delta.other_fields = bone_rest.dump();
        out.deltas.push_back(std::move(delta));
      }
      if (!out.deltas.empty()) {
        rest.erase("DeltaBones");
        rest.erase("uiNumDeltas");
      }
    }
    if (!rest.empty()) out.other_fields = rest.dump();
    shapes.push_back(std::move(out));
  }
  return shapes;
}

std::string write_ssf(const std::vector<SsfShape>& shapes) {
  if (shapes.empty()) return "null";
  json root = json::object();
  for (const SsfShape& shape : shapes) {
    if (root.contains(shape.name)) throw parse_error(0, "duplicate SSF shape name: " + shape.name);
    json entry = shape.other_fields.empty() ? json::object() : parse_document(shape.other_fields);
    if (shape.base_bone) entry["BaseBoneName"] = *shape.base_bone;
    if (!shape.deltas.empty()) {
      json bones = json::array();
      std::size_t total = 0;
      for (const SsfBoneDelta& delta : shape.deltas) {
        json bone = delta.other_fields.empty() ? json::object() : parse_document(delta.other_fields);
        bone["BoneDeltaList"] = delta.segment_ids;
        bone["BoneName"] = delta.bone_name;
        bones.push_back(std::move(bone));
        total += delta.segment_ids.size();
      }
      entry["DeltaBones"] = std::move(bones);
      entry["uiNumDeltas"] = total;
    }
    root[shape.name] = std::move(entry);
  }
  return root.dump(3);
}

std::vector<SclpBone> read_sclp(std::string_view text) {
  std::vector<SclpBone> bones;
  if (parse_document(text).is_null()) {
    return bones;
  }
  const auto profile = cloth::parse_cloth_profile(text);
  bones.reserve(profile.bones.size());
  for (const cloth::BoneEntry& entry : profile.bones) {
    SclpBone bone;
    bone.name = entry.name;
    bone.scale[0] = checked_scale(entry.scale.x);
    bone.scale[1] = checked_scale(entry.scale.y);
    bone.scale[2] = checked_scale(entry.scale.z);
    bones.push_back(std::move(bone));
  }
  return bones;
}

std::string write_sclp(const std::vector<SclpBone>& bones) {
  json root = json::array();
  for (const SclpBone& bone : bones) {
    json scale = json::object();
    scale["x"] = checked_scale(bone.scale[0]);
    scale["y"] = checked_scale(bone.scale[1]);
    scale["z"] = checked_scale(bone.scale[2]);
    json entry = json::object();
    entry["Name"] = bone.name;
    entry["Scale"] = std::move(scale);
    root.push_back(std::move(entry));
  }
  const std::string text = root.dump(2);
  cloth::parse_cloth_profile(text);
  return text;
}

}  // namespace fo4::nif
