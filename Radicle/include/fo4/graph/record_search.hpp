#pragma once

#include "fo4/graph/conflict_kind.hpp"
#include "fo4/plugin/container.hpp"
#include "fo4/signature.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace fo4 {

struct RecordSearchQuery {
  std::string text;
  bool text_is_regex{false};
  std::string form_id;
  std::vector<Signature> signatures;
  std::string editor_id;
  std::string editor_id_pattern;
  std::string display_name;
  std::string field_value;
  std::vector<ConflictCategory> conflict_categories;
  std::size_t limit{500};
};

struct RecordSearchHit {
  FormKey key;
  Signature signature{};
  std::string editor_id;
  std::size_t plugin_index{0};
  std::string display_name;
};

struct RecordSearchResult {
  std::vector<RecordSearchHit> hits;
  bool truncated{false};
};

// Constraint: must return UTF-8 text for a string ID of load_order[plugin_index], or nullopt.
using LocalizedNameResolver = std::function<std::optional<std::string>(std::size_t plugin_index, std::uint32_t string_id)>;

RecordSearchResult search_records(std::span<const Plugin* const> load_order,
                                  const RecordSearchQuery& query,
                                  const ConflictGraph* graph = nullptr,
                                  std::span<const ConflictPriorityRule> priorities = {},
                                  const LocalizedNameResolver& resolve_name = {});

}  // namespace fo4
