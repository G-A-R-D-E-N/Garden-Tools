#pragma once

#include "fo4/swf/swf.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::swf {

struct SwfTransform {
  double scale_x{1.0};
  double scale_y{1.0};
  double rotate_skew0{0.0};
  double rotate_skew1{0.0};
  double translate_x{0.0};
  double translate_y{0.0};

  bool operator==(const SwfTransform&) const = default;
};

struct SwfBounds {
  double x{0.0};
  double y{0.0};
  double width{0.0};
  double height{0.0};

  bool operator==(const SwfBounds&) const = default;
};

SwfTransform transform_from_matrix(const SwfMatrix& matrix) noexcept;

SwfTransform compose_transform(const SwfTransform& parent, const SwfTransform& child) noexcept;

SwfBounds transformed_bounds(const SwfTransform& transform, const SwfRect& bounds) noexcept;

struct SwfDisplayObject {
  std::uint16_t depth{0};
  std::uint16_t character_id{0};
  std::string name;
  SwfTransform transform{};
  std::optional<std::uint16_t> ratio;
  std::optional<std::uint16_t> clip_depth;

  bool operator==(const SwfDisplayObject&) const = default;
};

struct SwfPlacementRow {
  std::size_t tag_index{0};
  std::uint32_t frame{1};
  std::uint16_t depth{0};
  std::uint16_t character_id{0};
  std::string name;
  SwfTransform transform{};
};

struct SwfTimelineFrame {
  std::vector<SwfDisplayObject> objects;

  bool operator==(const SwfTimelineFrame&) const = default;
};

struct SwfTimeline {
  std::uint16_t character_id{0};
  std::uint32_t declared_frames{0};
  std::vector<SwfTimelineFrame> frames;
  std::vector<std::string> labels;
  std::vector<SwfPlacementRow> placements;
};

enum class SwfTimelineStatus : std::uint8_t { ok, malformed };

struct SwfMovie {
  SwfTimeline root;
  std::map<std::uint16_t, SwfTimeline> sprites;
  SwfTimelineStatus status{SwfTimelineStatus::malformed};
  std::string message;
};

struct SwfCensus {
  std::uint32_t shapes{0};
  std::uint32_t sprites{0};
  std::uint32_t morphs{0};
  std::uint32_t images{0};
  std::uint32_t text_fields{0};
  std::uint32_t buttons{0};
  std::uint32_t videos{0};
  std::uint32_t symbols{0};
  std::uint32_t imports{0};
  std::uint32_t root_frames{0};
  std::uint32_t root_labels{0};
  std::uint32_t sprite_labels{0};
  std::uint32_t labelled_sprite_definitions{0};
};

struct SwfInspectionOptions {
  std::size_t max_rows{400};
  unsigned max_depth{4};
};

SwfMovie build_movie(const SwfFile& file);

SwfCensus census(const SwfFile& file);

std::string inspect_swf(const SwfFile& file, std::string_view name,
                        const SwfInspectionOptions& options = {});

}  // namespace fo4::swf
