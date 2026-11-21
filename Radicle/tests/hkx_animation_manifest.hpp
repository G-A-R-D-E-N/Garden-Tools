#pragma once

#include "fo4/hkx/animation.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

namespace fo4::test::hkx {

inline std::int64_t manifest_ten_thousandths(float value) {
  return std::llround(static_cast<double>(value) * 10000.0);
}

// Requires BGS's HkxAnimationManifestTests shape: ten-thousandths per value, then an FNV-1a 64 hash line.
inline std::string animation_manifest_body(const fo4::hkx::HkxAnimation& animation) {
  std::string out = "hkx-animation-manifest 1\n";
  out += "class " + animation.class_name + "\n";
  out += "frames " + std::to_string(animation.frame_count) + "\n";
  out += "tracks " + std::to_string(animation.tracks.size()) + "\n";
  out += "blocks " + std::to_string(animation.blocks.count) + "\n";
  out += "max_frames_per_block " + std::to_string(animation.blocks.max_frames_per_block) + "\n";
  out += "duration_tenk " + std::to_string(manifest_ten_thousandths(animation.duration)) + "\n";
  out += "frame_duration_tenk " + std::to_string(manifest_ten_thousandths(animation.frame_duration)) + "\n";

  const std::size_t frames = animation.frame_count;
  const std::size_t pose_indices[3] = {0, frames / 2, frames == 0 ? 0 : frames - 1};
  const char* pose_names[3] = {"first", "middle", "last"};
  for (std::size_t track = 0; track < animation.tracks.size(); ++track) {
    const fo4::hkx::HkxAnimationTrack& data = animation.tracks[track];
    std::string flags = "track " + std::to_string(track) + " flags ";
    for (std::size_t axis = 0; axis < 3; ++axis) {
      flags += (data.translation_animated[axis] ? "1" : "0");
    }
    flags += " ";
    flags += (data.rotation_animated ? "1" : "0");
    flags += " ";
    for (std::size_t axis = 0; axis < 3; ++axis) {
      flags += (data.scale_animated[axis] ? "1" : "0");
    }
    out += flags + "\n";
    if (frames == 0) continue;

    for (std::size_t pose = 0; pose < 3; ++pose) {
      const std::size_t frame = pose_indices[pose];
      std::string line = "track " + std::to_string(track) + " " + pose_names[pose];
      const std::array<float, 3>& translation = data.translations[frame];
      const std::array<float, 4>& rotation = data.rotations[frame];
      const std::array<float, 3>& scale = data.scales[frame];
      for (float value : translation) line += " " + std::to_string(manifest_ten_thousandths(value));
      for (float value : rotation) line += " " + std::to_string(manifest_ten_thousandths(value));
      for (float value : scale) line += " " + std::to_string(manifest_ten_thousandths(value));
      out += line + "\n";
    }
  }
  return out;
}

inline std::string animation_manifest(const fo4::hkx::HkxAnimation& animation) {
  const std::string body = animation_manifest_body(animation);
  std::uint64_t hash = 14695981039346656037ull;
  for (char ch : body) {
    hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(ch));
    hash *= 1099511628211ull;
  }
  char hex[17] = {};
  std::snprintf(hex, sizeof hex, "%016llx", static_cast<unsigned long long>(hash));
  return body + "hash " + hex + "\n";
}

}  // namespace fo4::test::hkx
