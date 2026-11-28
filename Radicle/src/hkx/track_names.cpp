#include "fo4/hkx/track_names.hpp"

#include "fo4/io/path_text.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <system_error>
#include <utility>

namespace fo4::hkx {
namespace {

constexpr std::uintmax_t kMaxSkeletonFileBytes = 64ull * 1024 * 1024;
constexpr std::uintmax_t kMaxSiblingFileBytes = 16ull * 1024 * 1024;
constexpr std::size_t kMaxSiblingCandidates = 32;

struct Sibling {
  std::filesystem::path path;
  HkxSkeleton skeleton;
};

bool equal_ignore_case(const std::string& a, const char* b) {
  const std::string text = b;
  if (a.size() != text.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(text[i]))) return false;
  }
  return true;
}

std::optional<HkxSkeleton> read_skeleton_file(const std::filesystem::path& path, std::uintmax_t max_bytes) {
  std::error_code ec;
  if (!std::filesystem::is_regular_file(path, ec) || ec) return std::nullopt;
  const auto size = std::filesystem::file_size(path, ec);
  if (ec || size == 0 || size > max_bytes) return std::nullopt;
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::nullopt;
  std::vector<std::byte> bytes(static_cast<std::size_t>(size));
  in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (static_cast<std::size_t>(in.gcount()) != bytes.size()) return std::nullopt;
  HkxPackfile packfile;
  if (read_packfile(bytes, packfile) != HkxStatus::Ok) return std::nullopt;
  HkxSkeleton skeleton;
  if (read_skeleton(packfile, skeleton) != HkxStatus::Ok) return std::nullopt;
  return skeleton;
}

std::vector<std::filesystem::path> hkx_files_in(const std::filesystem::path& folder) {
  std::vector<std::filesystem::path> files;
  std::error_code ec;
  for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
    std::error_code type_ec;
    if (!it->is_regular_file(type_ec) || type_ec) continue;
    const std::string extension = io::utf8(it->path().extension());
    if (equal_ignore_case(extension, ".hkx")) files.push_back(it->path());
  }
  std::sort(files.begin(), files.end(), [](const std::filesystem::path& a, const std::filesystem::path& b) {
    return io::utf8(a.filename()) < io::utf8(b.filename());
  });
  return files;
}

std::filesystem::path character_assets_of(const std::filesystem::path& animation_path) {
  std::error_code ec;
  std::filesystem::path dir = std::filesystem::absolute(animation_path, ec).parent_path();
  if (ec) return {};
  while (!dir.empty()) {
    if (equal_ignore_case(io::utf8(dir.filename()), "Animations")) {
      const std::filesystem::path assets = dir.parent_path() / "CharacterAssets";
      return std::filesystem::is_directory(assets, ec) && !ec ? assets : std::filesystem::path{};
    }
    const std::filesystem::path up = dir.parent_path();
    if (up == dir) break;
    dir = up;
  }
  return {};
}

bool stays_inside(const std::filesystem::path& file, const std::filesystem::path& base) {
  std::error_code ec;
  const std::filesystem::path real = std::filesystem::canonical(file, ec);
  if (ec) return false;
  const std::filesystem::path root = std::filesystem::canonical(base, ec);
  if (ec) return false;
  for (std::filesystem::path dir = real.parent_path();; dir = dir.parent_path()) {
    if (dir == root) return true;
    if (dir.empty() || dir == dir.parent_path()) return false;
  }
}

bool fits_binding(const HkxSkeleton& skeleton, const HkxAnimationBinding* binding) {
  if (binding == nullptr) return true;
  if (!binding->original_skeleton_name.empty() && !skeleton.name.empty() &&
      !equal_ignore_case(binding->original_skeleton_name, skeleton.name.c_str())) {
    return false;
  }
  for (const std::int16_t bone : binding->transform_track_to_bone_indices) {
    if (bone >= 0 && static_cast<std::size_t>(bone) >= skeleton.bone_names.size()) return false;
  }
  return true;
}

std::optional<Sibling> first_skeleton_in(const std::filesystem::path& folder, const std::filesystem::path& base,
                                         const std::filesystem::path& animation_path, const HkxAnimationBinding* binding,
                                         const HkxReadFilter& allowed) {
  std::size_t tried = 0;
  for (const auto& file : hkx_files_in(folder)) {
    std::error_code ec;
    if (std::filesystem::is_symlink(std::filesystem::symlink_status(file, ec)) || ec) continue;
    if (!stays_inside(file, base)) continue;
    std::error_code same_ec;
    if (std::filesystem::equivalent(file, animation_path, same_ec) && !same_ec) continue;
    if (++tried > kMaxSiblingCandidates) break;
    if (allowed && !allowed(file)) continue;
    auto skeleton = read_skeleton_file(file, kMaxSiblingFileBytes);
    if (skeleton && fits_binding(*skeleton, binding)) return Sibling{file, std::move(*skeleton)};
  }
  return std::nullopt;
}

std::optional<Sibling> find_sibling(const std::filesystem::path& animation_path, const HkxAnimationBinding* binding,
                                    const HkxReadFilter& allowed) {
  const std::filesystem::path assets = character_assets_of(animation_path);
  if (!assets.empty()) {
    if (auto found = first_skeleton_in(assets, assets.parent_path(), animation_path, binding, allowed)) return found;
  }
  std::error_code ec;
  const std::filesystem::path folder = std::filesystem::absolute(animation_path, ec).parent_path();
  if (ec || !std::filesystem::is_directory(folder, ec) || ec) return std::nullopt;
  return first_skeleton_in(folder, folder, animation_path, binding, allowed);
}

}  // namespace

const char* hkx_track_name_source_name(HkxTrackNameSource source) {
  switch (source) {
    case HkxTrackNameSource::None: return "none";
    case HkxTrackNameSource::Explicit: return "explicit";
    case HkxTrackNameSource::Sibling: return "sibling";
    case HkxTrackNameSource::Embedded: return "embedded";
  }
  return "none";
}

std::string track_name(const HkxAnimationBinding& binding, const HkxSkeleton* skeleton, std::size_t track) {
  if (skeleton != nullptr && track < binding.transform_track_to_bone_indices.size()) {
    const std::int16_t bone = binding.transform_track_to_bone_indices[track];
    if (bone >= 0 && static_cast<std::size_t>(bone) < skeleton->bone_names.size()) {
      return skeleton->bone_names[static_cast<std::size_t>(bone)];
    }
  }
  return "track " + std::to_string(track);
}

std::optional<std::filesystem::path> find_sibling_skeleton(const std::filesystem::path& animation_path, const HkxAnimationBinding* binding,
                                                           const HkxReadFilter& allowed) {
  if (auto found = find_sibling(animation_path, binding, allowed)) return std::move(found->path);
  return std::nullopt;
}

HkxTrackNames name_tracks(const std::filesystem::path& animation_path, const HkxPackfile& animation, const HkxAnimationBinding& binding,
                          std::size_t track_count, const std::filesystem::path& explicit_skeleton,
                          const HkxReadFilter& allowed) {
  HkxTrackNames out;
  std::optional<HkxSkeleton> skeleton;
  if (!explicit_skeleton.empty()) {
    skeleton = read_skeleton_file(explicit_skeleton, kMaxSkeletonFileBytes);
    if (skeleton) {
      out.source = HkxTrackNameSource::Explicit;
      out.skeleton = explicit_skeleton;
    }
  }
  if (!skeleton) {
    if (auto sibling = find_sibling(animation_path, &binding, allowed)) {
      skeleton = std::move(sibling->skeleton);
      out.source = HkxTrackNameSource::Sibling;
      out.skeleton = std::move(sibling->path);
    }
  }
  if (!skeleton) {
    HkxSkeleton embedded;
    if (read_skeleton(animation, embedded) == HkxStatus::Ok) {
      skeleton = std::move(embedded);
      out.source = HkxTrackNameSource::Embedded;
    }
  }
  out.names.reserve(track_count);
  for (std::size_t track = 0; track < track_count; ++track) {
    out.names.push_back(track_name(binding, skeleton ? &*skeleton : nullptr, track));
  }
  return out;
}

}  // namespace fo4::hkx
