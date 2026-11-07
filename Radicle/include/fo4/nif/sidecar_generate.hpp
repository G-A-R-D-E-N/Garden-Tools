#pragma once

#include "fo4/nif/nif.hpp"
#include "fo4/nif/sidecars.hpp"

#include <span>
#include <string>
#include <string_view>

namespace fo4::nif {

SsfShape ssf_shape_from_segments(const SegmentTable& table, std::string_view shape_name,
                                 std::span<const std::string> strings);
std::string generate_ssf(const SegmentTable& table, std::string_view shape_name,
                         std::span<const std::string> strings);
bool ssf_lists_segments(const SsfShape& shape, const SegmentTable& table);

}  // namespace fo4::nif
