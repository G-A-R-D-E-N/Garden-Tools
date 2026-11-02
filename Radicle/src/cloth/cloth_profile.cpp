#include "fo4/cloth/cloth_profile.hpp"

#include "fo4/error.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace fo4::cloth {
namespace {

constexpr std::size_t k_max_bones = 1024;

[[noreturn]] void fail(std::size_t offset, std::string message) {
  throw parse_error(offset, std::move(message));
}

char lower(char character) {
  return static_cast<char>(character >= 'A' && character <= 'Z' ? character - 'A' + 'a' : character);
}

double read_axis(const nlohmann::json& scale, const char* axis, std::size_t entry) {
  const auto found = scale.find(axis);
  if (found == scale.end()) {
    fail(entry, std::string{"bone entry has no "} + axis + " scale");
  }
  if (!found->is_number()) {
    fail(entry, std::string{"bone "} + axis + " scale is not a number");
  }
  const auto value = found->get<double>();
  if (!std::isfinite(value)) {
    fail(entry, std::string{"bone "} + axis + " scale is not finite");
  }
  if (!(value > 0.0)) {
    fail(entry, std::string{"bone "} + axis + " scale is not positive");
  }
  return value;
}

}  // namespace

std::string cloth_profile_path(std::string_view outfit_nif) {
  const auto slash = outfit_nif.find_last_of("/\\");
  const auto component_start = slash == std::string_view::npos ? 0 : slash + 1;
  const auto dot = outfit_nif.find_last_of('.');
  if (dot == std::string_view::npos || dot < component_start) {
    throw std::invalid_argument("cloth profile path requires a file name with a .nif extension");
  }
  const auto extension = outfit_nif.substr(dot + 1);
  if (extension.size() != 3 || lower(extension[0]) != 'n' || lower(extension[1]) != 'i' ||
      lower(extension[2]) != 'f') {
    throw std::invalid_argument("cloth profile path requires a .nif extension");
  }
  std::string path{outfit_nif.substr(0, dot)};
  path += ".sclp";
  return path;
}

ClothProfile parse_cloth_profile(std::string_view json) {
  nlohmann::json document;
  try {
    document = nlohmann::json::parse(json.begin(), json.end());
  } catch (const nlohmann::json::parse_error& error) {
    fail(static_cast<std::size_t>(error.byte), std::string{"invalid cloth profile JSON: "} + error.what());
  }
  if (!document.is_array()) {
    fail(0, "cloth profile is not a JSON array");
  }
  if (document.size() > k_max_bones) {
    fail(0, "cloth profile holds more bones than the reader accepts");
  }
  ClothProfile profile;
  profile.bones.reserve(document.size());
  std::unordered_set<std::string> names;
  for (std::size_t index = 0; index < document.size(); ++index) {
    const auto& element = document[index];
    if (!element.is_object()) {
      fail(index, "cloth profile bone entry is not a JSON object");
    }
    if (element.size() != 2) {
      fail(index, "cloth profile bone entry must hold Name and Scale only");
    }
    const auto name = element.find("Name");
    if (name == element.end() || !name->is_string()) {
      fail(index, "cloth profile bone entry has no string Name");
    }
    if (name->get_ref<const std::string&>().empty()) {
      fail(index, "cloth profile bone name is empty");
    }
    const auto scale = element.find("Scale");
    if (scale == element.end() || !scale->is_object()) {
      fail(index, "cloth profile bone entry has no Scale object");
    }
    if (scale->size() != 3) {
      fail(index, "cloth profile Scale must hold x, y and z only");
    }
    BoneEntry bone;
    bone.name = name->get<std::string>();
    if (!names.insert(bone.name).second) {
      fail(index, "cloth profile bone name repeats");
    }
    bone.scale.x = read_axis(*scale, "x", index);
    bone.scale.y = read_axis(*scale, "y", index);
    bone.scale.z = read_axis(*scale, "z", index);
    profile.bones.push_back(std::move(bone));
  }
  return profile;
}

std::string serialize_cloth_profile(const ClothProfile& profile) {
  nlohmann::json document = nlohmann::json::array();
  for (const auto& bone : profile.bones) {
    nlohmann::json scale;
    scale["x"] = bone.scale.x;
    scale["y"] = bone.scale.y;
    scale["z"] = bone.scale.z;
    nlohmann::json entry;
    entry["Name"] = bone.name;
    entry["Scale"] = std::move(scale);
    document.push_back(std::move(entry));
  }
  const auto text = document.dump(2);
  std::string out;
  out.reserve(text.size() + text.size() / 16);
  for (const char character : text) {
    if (character == '\n') {
      out += "\r\n";
    } else {
      out += character;
    }
  }
  return out;
}

ClothBonePairing compare_cloth_bones(const ClothProfile& profile, std::span<const std::string> mesh_bones) {
  std::unordered_set<std::string> profile_names;
  for (const auto& bone : profile.bones) profile_names.insert(bone.name);
  std::unordered_set<std::string> mesh_names;
  for (const auto& name : mesh_bones) {
    if (!name.empty()) mesh_names.insert(name);
  }

  ClothBonePairing pairing;
  for (const auto& bone : profile.bones) {
    if (mesh_names.find(bone.name) == mesh_names.end()) {
      pairing.profile_only.push_back(bone.name);
    }
  }
  std::unordered_set<std::string> reported;
  for (const auto& name : mesh_bones) {
    if (name.empty() || profile_names.find(name) != profile_names.end()) continue;
    if (!reported.insert(name).second) continue;
    pairing.mesh_only.push_back(name);
  }
  return pairing;
}

}  // namespace fo4::cloth
