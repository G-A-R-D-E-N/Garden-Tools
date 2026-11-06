#pragma once

#include "fo4/environment/environment.hpp"
#include "fo4/environment/world.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace fo4::env {

struct CellSummary {
  FormKey key;
  std::string editor_id;
  RecordName name;
  bool interior{false};
  std::optional<CellGrid> grid;
  std::optional<FormKey> worldspace;
  std::size_t winning_plugin{0};
  bool localized{false};
};

std::optional<FormKey> resolve_cell_id(const Environment& env, std::string_view id);
std::optional<FormKey> resolve_worldspace_id(const Environment& env, std::string_view id);
std::optional<FormKey> find_cell_at_grid(const Environment& env,
                                         const FormKey& world,
                                         std::int32_t x,
                                         std::int32_t y);
std::optional<CellSummary> describe_cell(const Environment& env, const FormKey& cell);

}  // namespace fo4::env
