#include "fo4/hkx/project_index.hpp"
#include "fo4/hash.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::hkx {
namespace {

constexpr std::string_view kBehaviorPrefix = "hkb";
constexpr std::size_t kMaxClassesPerFile = 16;

std::uint64_t hash_bytes(std::uint64_t hash, std::span<const std::byte> bytes) {
  return fnv1a64_bytes(bytes, hash);
}

std::uint64_t hash_text(std::uint64_t hash, std::string_view text) {
  return fnv1a64(text, hash);
}

bool is_animation_class(std::string_view name) { return name.ends_with("Animation"); }

bool is_skeleton_class(std::string_view name) { return name == "hkaSkeleton"; }

std::string lower_of(std::string_view text) {
  std::string lowered;
  lowered.reserve(text.size());
  for (const char ch : text) lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
  return lowered;
}

}  // namespace

HkxProjectFile index_project_file(std::string_view name, std::span<const std::byte> bytes) {
  HkxProjectFile file;
  file.name = std::string(name);
  HkxPackfile packfile;
  if (const auto status = read_packfile(bytes, packfile); status != HkxStatus::Ok) {
    file.error = "packfile read failed";
    return file;
  }
  file.read_ok = true;
  file.object_count = packfile.objects.size();
  std::uint64_t hash = k_fnv64_offset;
  bool hashed = false;
  for (const HkxObjectView& object : packfile.objects) {
    if (object.class_name.rfind(kBehaviorPrefix, 0) == 0) {
      ++file.behavior_objects;
      // Constraint: the hash covers the class name and the object bytes, so two graphs with the same bytes collide.
      hash = hash_text(hash, object.class_name);
      hash = hash_bytes(hash, object.data);
      hashed = true;
    }
    if (is_animation_class(object.class_name)) ++file.animation_objects;
    if (is_skeleton_class(object.class_name)) ++file.skeleton_objects;
    if (file.classes.size() < kMaxClassesPerFile &&
        std::find(file.classes.begin(), file.classes.end(), object.class_name) == file.classes.end()) {
      file.classes.push_back(std::string(object.class_name));
    }
  }
  file.behavior_hash = hashed ? hash : 0;
  return file;
}

std::vector<std::size_t> search_project_index(const HkxProjectIndex& index, std::string_view query) {
  std::vector<std::size_t> hits;
  const std::string needle = lower_of(query);
  for (std::size_t at = 0; at < index.files.size(); ++at) {
    const HkxProjectFile& file = index.files[at];
    if (needle.empty() || lower_of(file.name).find(needle) != std::string::npos) {
      hits.push_back(at);
      continue;
    }
    const bool match = std::any_of(file.classes.begin(), file.classes.end(), [&](const std::string& name) {
      return lower_of(name).find(needle) != std::string::npos;
    });
    if (match) hits.push_back(at);
  }
  return hits;
}

std::vector<std::vector<std::size_t>> duplicate_behavior_groups(const HkxProjectIndex& index) {
  std::map<std::uint64_t, std::vector<std::size_t>> by_hash;
  for (std::size_t at = 0; at < index.files.size(); ++at) {
    const HkxProjectFile& file = index.files[at];
    if (!file.read_ok || file.behavior_hash == 0) continue;
    by_hash[file.behavior_hash].push_back(at);
  }
  std::vector<std::vector<std::size_t>> groups;
  for (const auto& [hash, members] : by_hash) {
    (void)hash;
    if (members.size() < 2) continue;
    groups.push_back(members);
  }
  return groups;
}

}  // namespace fo4::hkx
