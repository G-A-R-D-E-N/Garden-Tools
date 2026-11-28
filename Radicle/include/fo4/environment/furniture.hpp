#pragma once

#include "fo4/environment/world.hpp"

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace fo4::env {

// Constraint: one row per RACE subgraph block; SRAF closes a block and its role 2 means Furniture (xEdit wbDefinitionsFO4.pas:11313-11326).
struct RaceSubgraphRow {
  std::vector<FormKey> actor_keywords;
  std::vector<FormKey> target_keywords;
  std::optional<std::string_view> behavior_graph;
  std::vector<std::string_view> path_prefixes;
  std::uint16_t role{0};
  std::uint16_t perspective{0};
};

constexpr std::uint16_t k_subgraph_role_furniture = 2;

std::vector<RaceSubgraphRow> decode_race_subgraphs(const Environment& env, const RecordRef& ref);

// Constraint: returns the KWDA links of a FURN in stored order; a stored KSIZ that disagrees with the KWDA length fails closed.
std::vector<FormKey> decode_furniture_keywords(const Environment& env, const RecordRef& ref);

// Constraint: keeps null links (the C export drops them); QUST alias blocks repeat KSIZ/KWDA, so QUST must not be passed.
std::vector<FormKey> decode_record_keywords(const Environment& env, const RecordRef& ref);

}  // namespace fo4::env
