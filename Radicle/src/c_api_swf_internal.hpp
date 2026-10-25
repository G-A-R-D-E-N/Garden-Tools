#pragma once

#include "fo4/abc/abc.hpp"
#include "fo4/abc/summary.hpp"
#include "fo4/c_api_swf.h"
#include "fo4/swf/swf.hpp"

#include <cstddef>
#include <cstdint>
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

}  // namespace radicle_swf_detail

struct radicle_swf {
  fo4::swf::SwfFile file;
  fo4::swf::SwfSummary summary;
  bool declared_length_matches{true};
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
