#include "fo4/environment/cell_view.hpp"

#include "fo4/plugin/exterior_cell.hpp"

#include <algorithm>
#include <exception>
#include <string>

namespace fo4::env {
namespace {

std::optional<FormKey> resolve_record_id(const Environment& env,
                                         std::string_view id,
                                         Signature signature) {
  if (id.empty()) return std::nullopt;
  if (id.find(':') == std::string_view::npos) {
    const auto* ref = env.find_by_editor_id(id, signature);
    if (ref == nullptr) return std::nullopt;
    return ref->record->form_key;
  }
  try {
    const auto key = FormKey::parse(id);
    const auto* winner = env.winner(key);
    if (winner == nullptr || winner->record->header.signature != signature) return std::nullopt;
    return winner->record->form_key;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

}  // namespace

std::optional<FormKey> resolve_cell_id(const Environment& env, std::string_view id) {
  return resolve_record_id(env, id, sig_cell);
}

std::optional<FormKey> resolve_worldspace_id(const Environment& env, std::string_view id) {
  return resolve_record_id(env, id, sig_wrld);
}

std::vector<FormKey> find_cells_at_grid(const Environment& env,
                                        const FormKey& world,
                                        std::int32_t x,
                                        std::int32_t y) {
  std::vector<FormKey> keys;
  if (!exterior_cell_lookup::exterior_cell_coordinate_valid(x) ||
      !exterior_cell_lookup::exterior_cell_coordinate_valid(y)) {
    return keys;
  }
  std::vector<FormKey> seen;
  std::vector<const RecordRef*> here;
  for (std::size_t index = env.plugins().size(); index-- > 0;) {
    const auto* cell = exterior_cell_lookup::find_exterior_cell(env.plugin(index), world, x, y);
    if (cell == nullptr || std::find(seen.begin(), seen.end(), cell->form_key) != seen.end()) continue;
    seen.push_back(cell->form_key);
    const auto* winner = env.winner(cell->form_key);
    if (winner == nullptr || winner->record->header.flags.is_deleted()) continue;
    const auto data = decode_cell(env, *winner);
    if (data.grid && data.grid->x == x && data.grid->y == y && data.worldspace == world) {
      here.push_back(winner);
    }
  }
  std::stable_sort(here.begin(), here.end(),
                   [](const RecordRef* a, const RecordRef* b) { return a->plugin > b->plugin; });
  for (const auto* winner : here) keys.push_back(winner->record->form_key);
  return keys;
}

std::optional<FormKey> find_cell_at_grid(const Environment& env,
                                         const FormKey& world,
                                         std::int32_t x,
                                         std::int32_t y) {
  const auto keys = find_cells_at_grid(env, world, x, y);
  if (keys.empty()) return std::nullopt;
  return keys.front();
}

std::optional<CellSummary> describe_cell(const Environment& env, const FormKey& cell) {
  const auto* ref = env.winner(cell);
  if (ref == nullptr || ref->record->header.signature != sig_cell) return std::nullopt;
  const auto meta = env.metadata(*ref);
  const auto data = decode_cell(env, *ref);

  CellSummary summary;
  summary.key = ref->record->form_key;
  summary.editor_id = std::string{meta.editor_id.value_or(std::string_view{})};
  summary.name = meta.name;
  summary.interior = data.flags.has_value() && (*data.flags & 0x1u) != 0;
  summary.grid = data.grid;
  summary.worldspace = data.worldspace;
  summary.winning_plugin = ref->plugin;
  summary.localized = env.plugins()[ref->plugin].localized;
  return summary;
}

}  // namespace fo4::env
