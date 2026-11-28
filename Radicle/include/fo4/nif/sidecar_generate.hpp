#pragma once

#include "fo4/nif/nif.hpp"
#include "fo4/nif/sidecars.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::nif {

struct SsfDiagnostics {
  std::vector<std::uint32_t> unresolved_ids;
  std::vector<std::uint32_t> ambiguous_ids;
};

SsfShape ssf_shape_from_segments(const SegmentTable& table, std::string_view shape_name,
                                 std::span<const std::string> strings, SsfDiagnostics* diagnostics = nullptr);
std::string generate_ssf(const SegmentTable& table, std::string_view shape_name,
                         std::span<const std::string> strings, SsfDiagnostics* diagnostics = nullptr);
bool ssf_lists_segments(const SsfShape& shape, const SegmentTable& table);

}  // namespace fo4::nif
