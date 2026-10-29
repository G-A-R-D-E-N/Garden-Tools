#pragma once

#include <fo4/avm2/display.hpp>
#include <fo4/avm2/sound.hpp>
#include <fo4/swf/swf.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fo4::avm2 {

struct SnapshotMatrix {
  double a{1.0};
  double b{0.0};
  double c{0.0};
  double d{1.0};
  double tx{0.0};
  double ty{0.0};

  bool operator==(const SnapshotMatrix&) const = default;
};

struct SnapshotColorTransform {
  double red_multiplier{1.0};
  double green_multiplier{1.0};
  double blue_multiplier{1.0};
  double alpha_multiplier{1.0};
  std::int32_t red_offset{0};
  std::int32_t green_offset{0};
  std::int32_t blue_offset{0};
  std::int32_t alpha_offset{0};

  bool operator==(const SnapshotColorTransform&) const = default;
};

struct SnapshotGlyph {
  std::uint16_t font_id{0};
  std::uint16_t glyph_id{0};
  double x{0.0};
  double advance{0.0};

  bool operator==(const SnapshotGlyph&) const = default;
};

struct SnapshotTextRun {
  std::uint16_t font_id{0};
  double font_size{0.0};
  double x{0.0};
  double y{0.0};
  std::uint32_t color{0};
  std::vector<SnapshotGlyph> glyphs;

  bool operator==(const SnapshotTextRun&) const = default;
};

enum class SnapshotEntryKind : std::uint8_t {
  DisplayObject = 0,
  Shape,
  Bitmap,
  Text,
};

struct SnapshotEntry {
  SnapshotEntryKind kind{SnapshotEntryKind::DisplayObject};
  std::uint32_t character_id{0};
  std::int32_t clip_depth{0};
  std::uint8_t blend_mode{0};
  bool visible{true};
  double alpha{1.0};
  std::string path;
  std::string name;
  SnapshotMatrix matrix;
  SnapshotColorTransform color;
  std::vector<SnapshotTextRun> runs;
  std::size_t filter_count{0};
  std::optional<std::uint16_t> ratio;
  bool cache_as_bitmap{false};
  std::vector<swf::SwfFilter> placed_filters;

  bool operator==(const SnapshotEntry&) const = default;
};


struct FrameSnapshot {
  std::vector<SnapshotEntry> entries;
  std::vector<SnapshotSoundEvent> sounds;

  bool operator==(const FrameSnapshot&) const = default;
};

/* Requires only the object itself; the matrix is its own translation, rotation and scale. */
SnapshotMatrix local_matrix(const DisplayObject& object);

/* Requires child to be local to parent; the result maps child space into the parent's space. */
SnapshotMatrix compose_matrix(const SnapshotMatrix& parent, const SnapshotMatrix& child);

/* A display tree must snapshot depth-first in child order with composed transforms. */
FrameSnapshot build_frame_snapshot(const DisplayObject& root);

/* A snapshot's own fields must hash only from its contents, so equal snapshots hash equal. */
std::uint64_t snapshot_hash(const FrameSnapshot& snapshot);

}  // namespace fo4::avm2
