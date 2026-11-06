#pragma once

#include "fo4/plugin/container.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace fo4 {

// Constraint: every INFO response must start with a twenty-byte TRDA header.
inline constexpr std::size_t k_info_response_size = 20;

// One INFO response: the TRDA header and the members that must stay with it.
struct InfoResponse {
  // Constraint: `first` and `last` index record.subrecords, so a caller never rescans the record.
  std::size_t trda{0};
  std::size_t first{0};
  std::size_t last{0};
  std::uint32_t emotion{0};
  std::uint32_t sound{0};
  std::uint8_t number{0};
  std::uint8_t unknown{0};
  std::uint16_t interrupt_percent{0};
  std::int32_t camera_target_alias{0};
  std::int32_t camera_location_alias{0};
  // Constraint: a header of another size must be reported rather than thrown so a read tool still shows it.
  bool malformed{false};
};

// Constraint: a response body ends at the next TRDA, CTDA or RNAM, so the conditions and the prompt stay out.
std::vector<InfoResponse> read_info_responses(const Record& record);

// Returns that response's first subrecord with `signature`; the body must hold it.
const Subrecord* info_response_subrecord(const Record& record, const InfoResponse& response,
                                         Signature signature);

}  // namespace fo4
