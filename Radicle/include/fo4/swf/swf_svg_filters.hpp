#pragma once

#include "fo4/swf/swf.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fo4::swf {

struct SvgFilterDefs {
  std::string defs;
  std::vector<std::string> ids;
  std::vector<std::uint8_t> skipped;
};

// Requires an id unique within the document and the stage in twips; skipped lists the SWF filter ids that are not drawn.
SvgFilterDefs filter_defs(const std::vector<SwfFilter>& filters, const std::string& id,
                          const SwfRect& stage);

// Requires a SWF blend mode value; returns an empty string when no CSS mix-blend-mode declaration applies.
std::string blend_mode_style(std::uint8_t blend_mode);

// Requires the id that filter_defs was given for this content; returns content unchanged when it has no filter or blend.
std::string filtered_group(const std::vector<SwfFilter>& filters,
                           std::optional<std::uint8_t> blend_mode, const std::string& id,
                           const SwfRect& stage, const std::string& content);

}  // namespace fo4::swf
