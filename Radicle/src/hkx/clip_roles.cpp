#include "fo4/hkx/clip_roles.hpp"

#include "fo4/hkx/layout.hpp"
#include "hkx_read_internal.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>

namespace fo4::hkx {
namespace {

using detail::in_section;
using detail::local_target;
using detail::read_string;
using detail::value_at;

struct RoleName {
  std::string_view key;
  std::string_view role;
};

constexpr std::array<RoleName, 9> k_roles{{
    {"idle", "idle"},
    {"sneakidle", "sneak_idle"},
    {"walkforward", "walk"},
    {"runforward", "run"},
    {"sprintforward", "sprint"},
    {"turnleft90", "turn_left_90"},
    {"turnright90", "turn_right_90"},
    {"turnleft180", "turn_left_180"},
    {"turnright180", "turn_right_180"},
}};

constexpr std::int8_t k_mode_looping = 1;
constexpr std::size_t k_pointer_size = 8;

std::string normalize_path(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (const char raw : text) {
    const char c = raw == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(raw)));
    if (c == '/' && (out.empty() || out.back() == '/')) continue;
    out.push_back(c);
  }
  return out;
}

bool is_safe(std::string_view path) {
  if (path.empty() || path.find(':') != std::string_view::npos) return false;
  std::size_t start = 0;
  while (start <= path.size()) {
    std::size_t end = path.find('/', start);
    if (end == std::string_view::npos) end = path.size();
    const std::string_view segment = path.substr(start, end - start);
    if (segment == ".." || segment == ".") return false;
    start = end + 1;
  }
  return true;
}

std::optional<std::string> with_hkx_extension(std::string path) {
  const std::size_t slash = path.rfind('/');
  const std::size_t name_at = slash == std::string::npos ? 0 : slash + 1;
  const std::size_t dot = path.rfind('.');
  if (dot == std::string::npos || dot < name_at) return path + ".hkx";
  const std::string_view extension = std::string_view(path).substr(dot);
  if (extension != ".hkt" && extension != ".hkx") return std::nullopt;
  path.replace(dot, std::string::npos, ".hkx");
  return path;
}

std::string role_key(std::string_view animation) {
  std::string name = normalize_path(animation);
  const std::size_t slash = name.rfind('/');
  if (slash != std::string::npos) name.erase(0, slash + 1);
  const std::size_t dot = name.rfind('.');
  if (dot != std::string::npos) name.erase(dot);
  constexpr std::string_view non_looping = "_nonlooping";
  if (name.ends_with(non_looping)) name.erase(name.size() - non_looping.size());
  std::string_view rest = name;
  if (rest.starts_with("relaxedturninplace")) rest.remove_prefix(std::string_view("relaxed").size());
  constexpr std::string_view in_place = "turninplace";
  if (rest.starts_with(in_place)) return "turn" + std::string(rest.substr(in_place.size()));
  return std::string(rest);
}

std::optional<std::size_t> role_index(std::string_view animation) {
  const std::string key = role_key(animation);
  for (std::size_t i = 0; i < k_roles.size(); ++i) {
    if (k_roles[i].key == key) return i;
  }
  return std::nullopt;
}

}  // namespace

HkxStatus read_behavior_clips(const HkxPackfile& packfile, std::vector<HkxBehaviorClip>& out) {
  out.clear();
  const HkxClassLayout* node = find_layout("hkbNode");
  std::uint32_t name_offset = 0;
  bool has_name_offset = false;
  if (node != nullptr) {
    if (const auto found = find_member_offset(*node, "name")) {
      name_offset = *found;
      has_name_offset = true;
    }
  }
  std::vector<HkxBehaviorClip> clips;
  for (const HkxObjectView& object : packfile.objects) {
    if (object.class_name != "hkbClipGenerator") continue;
    if (packfile.pointer_size != k_pointer_size || !has_name_offset) return HkxStatus::UnsupportedLayout;
    const HkxClassLayout* layout = find_layout(object.class_name, object.signature);
    if (layout == nullptr) return HkxStatus::UnsupportedLayout;
    const HkxSection& section = packfile.sections[packfile.data_section];
    const auto animation_offset = find_member_offset(*layout, "animationName");
    const auto mode_offset = find_member_offset(*layout, "mode");
    const auto speed_offset = find_member_offset(*layout, "playbackSpeed");
    if (!animation_offset || !mode_offset || !speed_offset) return HkxStatus::UnsupportedLayout;
    const std::size_t reach = std::max({static_cast<std::size_t>(name_offset) + k_pointer_size,
                                        static_cast<std::size_t>(*animation_offset) + k_pointer_size,
                                        static_cast<std::size_t>(*mode_offset) + 1,
                                        static_cast<std::size_t>(*speed_offset) + sizeof(float)});
    if (object.data.size() < reach) return HkxStatus::Truncated;
    const std::size_t base = object.offset;
    if (!in_section(section, base + name_offset, k_pointer_size) ||
        !in_section(section, base + *animation_offset, k_pointer_size) || !in_section(section, base + *mode_offset, 1) ||
        !in_section(section, base + *speed_offset, 4)) {
      return HkxStatus::FixupOutOfRange;
    }
    HkxBehaviorClip clip;
    if (const auto target = local_target(section, base + name_offset)) {
      if (const auto status = read_string(section, *target, clip.generator); status != HkxStatus::Ok) return status;
    }
    if (const auto target = local_target(section, base + *animation_offset)) {
      if (const auto status = read_string(section, *target, clip.animation); status != HkxStatus::Ok) return status;
    }
    clip.mode = value_at<std::int8_t>(section.data, base + *mode_offset);
    const float speed = value_at<float>(section.data, base + *speed_offset);
    clip.playback_speed = std::isfinite(speed) ? speed : 1.0f;
    clips.push_back(std::move(clip));
  }
  if (clips.empty()) return HkxStatus::MissingObject;
  out = std::move(clips);
  return HkxStatus::Ok;
}

std::string_view clip_role_of(std::string_view animation) {
  const auto index = role_index(animation);
  return index ? k_roles[*index].role : std::string_view{};
}

std::vector<HkxClipRole> resolve_clip_roles(std::span<const HkxBehaviorClip> clips, std::string_view animation_root,
                                            std::string_view directory) {
  std::string root = normalize_path(animation_root);
  while (root.ends_with('/')) root.pop_back();
  std::string wanted = normalize_path(directory);
  if (!wanted.empty() && !wanted.ends_with('/')) wanted.push_back('/');

  std::array<std::optional<HkxClipRole>, k_roles.size()> found{};
  for (const HkxBehaviorClip& clip : clips) {
    const auto index = role_index(clip.animation);
    if (!index || found[*index]) continue;
    auto relative = with_hkx_extension(normalize_path(clip.animation));
    if (!relative || !is_safe(*relative)) continue;
    if (!wanted.empty() && !relative->starts_with(wanted)) continue;
    std::string entry = root.empty() ? *relative : root + "/" + *relative;
    if (!is_safe(entry)) continue;
    found[*index] = HkxClipRole{std::string(k_roles[*index].role), std::move(entry), clip.mode == k_mode_looping,
                                clip.playback_speed};
  }
  std::vector<HkxClipRole> out;
  for (auto& role : found) {
    if (role) out.push_back(std::move(*role));
  }
  return out;
}

}  // namespace fo4::hkx
