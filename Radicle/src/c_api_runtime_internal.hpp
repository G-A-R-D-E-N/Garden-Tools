#pragma once

#include "fo4/c_api_runtime.h"
#include "fo4/c_api_runtime_art.h"
#include "fo4/avm2/snapshot.hpp"
#include "fo4/avm2/swf_runtime.hpp"
#include "fo4/swf/swf.hpp"
#include "fo4/swf/swf_art.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct radicle_runtime;

struct radicle_movie {
  radicle_runtime* owner{nullptr};
  std::shared_ptr<fo4::avm2::SwfRuntime> movie;
  std::shared_ptr<fo4::swf::SwfFile> file;
  std::string root_label;
};

namespace radicle_art_detail {

struct ShapeStore {
  std::vector<radicle_art_point> points;
  std::vector<radicle_art_contour> contours;
  std::vector<radicle_art_gradient_stop> stops;
  std::vector<radicle_art_fill> fills;
  std::vector<radicle_art_line> lines;
  radicle_art_shape shape{};
};

std::unique_ptr<ShapeStore> store_shape(const fo4::swf::ArtShape& source,
                                        std::uint16_t character_id);
void fill_placed_filters(const fo4::avm2::SnapshotEntry& entry,
                         std::vector<radicle_snapshot_filter>& out);

}  // namespace radicle_art_detail

struct radicle_runtime_snapshot {
  fo4::avm2::FrameSnapshot frame;
  std::vector<std::vector<radicle_runtime_run>> runs;
  std::vector<std::vector<std::vector<radicle_runtime_glyph>>> glyphs;
  std::vector<std::vector<radicle_snapshot_filter>> filters;
  mutable std::vector<std::unique_ptr<radicle_art_detail::ShapeStore>> drawn;
};
