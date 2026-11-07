#include "fo4/environment/cell_view.hpp"

#include "fo4/forms/cell.hpp"

#include <exception>
#include <string>

namespace fo4::env {
namespace {

constexpr auto k_cell = Signature::from_string("CELL");
constexpr auto k_wrld = Signature::from_string("WRLD");

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
  return resolve_record_id(env, id, k_cell);
}

std::optional<FormKey> resolve_worldspace_id(const Environment& env, std::string_view id) {
  return resolve_record_id(env, id, k_wrld);
}

std::optional<FormKey> find_cell_at_grid(const Environment& env,
                                         const FormKey& world,
                                         std::int32_t x,
                                         std::int32_t y) {
  if (!forms::exterior_cell_coordinate_valid(x) || !forms::exterior_cell_coordinate_valid(y)) {
    return std::nullopt;
  }
  for (std::size_t index = env.plugins().size(); index-- > 0;) {
    if (const auto* cell = forms::find_exterior_cell(env.plugin(index), world, x, y)) {
      return cell->form_key;
    }
  }
  return std::nullopt;
}

std::optional<CellSummary> describe_cell(const Environment& env, const FormKey& cell) {
  const auto* ref = env.winner(cell);
  if (ref == nullptr || ref->record->header.signature != k_cell) return std::nullopt;
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
