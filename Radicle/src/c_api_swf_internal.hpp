#pragma once

#include "fo4/abc/abc.hpp"
#include "fo4/abc/summary.hpp"
#include "fo4/c_api_swf.h"
#include "fo4/swf/swf.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace radicle_swf_detail {

struct tag_view {
  const fo4::swf::SwfTag* tag{nullptr};
  std::size_t parent{RADICLE_SWF_NO_INDEX};
  std::uint16_t sprite_id{0};
  bool inside_sprite{false};
};

struct abc_view {
  fo4::abc::AbcFile file;
  fo4::abc::AbcSummary summary;
  std::vector<std::string> method_names;  // invariant: parallel to file.methods
  std::vector<std::string> method_return_types;
  std::string name;
  std::uint32_t flags{0};
  std::size_t tag_index{0};
  std::size_t payload_size{0};
  bool parsed{false};
};

struct morph_frame {
  bool filled{false};
  double ratio{0.0};
  std::vector<fo4::swf::SwfShapeRecord> commands;
};

/* Requires a fresh set per build: a partial set must never reach a live handle. */
struct swf_views {
  fo4::swf::SwfSummary summary;
  std::vector<tag_view> tags;
  std::vector<fo4::swf::SwfSymbol> symbols;
  std::vector<fo4::swf::SwfSymbol> exports;
  std::vector<std::string> frame_labels;
  std::vector<std::size_t> frame_label_tags;
  std::vector<std::uint16_t> frame_label_sprites;
  std::vector<std::uint8_t> frame_label_inside_sprite;
  std::vector<abc_view> abcs;
};

/* Invariant: every frame-list handle shares one state, so the revision outlives the movie handle. */
struct revision_state {
  std::uint64_t revision{0};
};

}  // namespace radicle_swf_detail

struct radicle_swf {
  fo4::swf::SwfFile file;
  fo4::swf::SwfSummary summary;
  bool declared_length_matches{true};
  std::shared_ptr<radicle_swf_detail::revision_state> revision{
      std::make_shared<radicle_swf_detail::revision_state>()};
  std::vector<radicle_swf_detail::tag_view> tags;
  std::vector<fo4::swf::SwfSymbol> symbols;
  std::vector<fo4::swf::SwfSymbol> exports;
  std::vector<std::string> frame_labels;
  std::vector<std::size_t> frame_label_tags;
  std::vector<std::uint16_t> frame_label_sprites;
  std::vector<std::uint8_t> frame_label_inside_sprite;
  std::vector<radicle_swf_detail::abc_view> abcs;
  mutable std::vector<radicle_swf_detail::morph_frame> morph_frames;
};

namespace radicle_swf_detail {

/* Requires `out` to be a fresh set; the handle must stay untouched when this throws. */
void build_views(const fo4::swf::SwfFile& file, swf_views& out);

/* Invariant: the installed views must describe the file the handle already holds. */
void install_views(radicle_swf& swf, swf_views&& views);

/* Every cached view holds pointers into file.tags, so a mutation must rebuild them before any getter runs. */
void rebuild_views(radicle_swf& swf);

}  // namespace radicle_swf_detail
