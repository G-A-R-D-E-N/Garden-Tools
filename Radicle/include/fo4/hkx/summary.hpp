#pragma once

#include "fo4/hkx/animation.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fo4::hkx {

// Invariant: a clip summary holds names, counts and the decoder's kind, never the sampled poses.
struct HkxSummaryClip {
  HkxAnimationKind kind{HkxAnimationKind::View};
  std::string class_name;
  float duration{0.0f};
  std::size_t frame_count{0};
  std::size_t track_count{0};
  std::size_t annotation_count{0};
};

// Invariant: the summary reports the first skeleton, animation and binding a packfile holds, because the decoders stop there.
struct HkxSummary {
  bool has_skeleton{false};
  std::string skeleton_name;
  std::vector<std::string> bone_names;
  std::vector<std::int16_t> parent_indices;
  std::vector<HkxSummaryClip> clips;
  std::vector<std::string> binding_skeletons;
  std::size_t object_count{0};
};

// Requires a decodable packfile; a container without a skeleton or a decoded animation yields the empty section.
HkxSummary read_summary(const HkxPackfile& packfile);

// Invariant: one line per entry with control bytes replaced, so a stored name cannot forge a summary line.
std::string render_summary(const HkxSummary& summary);

}  // namespace fo4::hkx
