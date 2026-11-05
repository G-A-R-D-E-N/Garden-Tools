#pragma once

#include "fo4/hkx/animation.hpp"
#include "fo4/hkx/skeleton.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fo4::hkx {

// Invariant: a clip summary holds names and counts only, never the sampled poses, so the tree can render it.
struct HkxSummaryClip {
  std::string class_name;
  float duration{0.0f};
  std::size_t frame_count{0};
  std::size_t track_count{0};
  std::size_t key_count{0};
  std::size_t max_keys_per_track{0};
  std::size_t annotation_count{0};
};

struct HkxSummary {
  bool has_skeleton{false};
  std::string skeleton_name;
  std::vector<std::string> bone_names;
  std::vector<std::int16_t> parent_indices;
  std::vector<HkxSummaryClip> clips;
  std::vector<std::string> binding_skeletons;
  std::size_t object_count{0};
};

// Requires a decodable packfile; a container without a skeleton or an animation yields the empty section.
HkxSummary read_summary(const HkxPackfile& packfile);

// Invariant: one line per entry, so the editor renders the summary without parsing anything.
std::string render_summary(const HkxSummary& summary);

}  // namespace fo4::hkx
