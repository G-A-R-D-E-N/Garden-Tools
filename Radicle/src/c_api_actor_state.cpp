#include "fo4/c_api_actor_state.h"

#include "c_api_internal.hpp"
#include "fo4/actor_state/actor_state.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace as = fo4::actor_state;

static_assert(RADICLE_ACTOR_STATE_FORMAT_VERSION == as::kFormatVersion);
static_assert(RADICLE_ACTOR_STATE_MAX_STRING_BYTES == as::kMaxStringBytes);
static_assert(RADICLE_ACTOR_STATE_MAX_LIST_ENTRIES == as::kMaxListEntries);
static_assert(RADICLE_ACTOR_FLAG_ENABLED == as::kFlagEnabled);
static_assert(RADICLE_ACTOR_FLAG_DELETED == as::kFlagDeleted);
static_assert(RADICLE_ACTOR_FLAG_DEAD == as::kFlagDead);
static_assert(sizeof(radicle_actor_ref_kind) == sizeof(std::int32_t));
static_assert(static_cast<int>(RADICLE_ACTOR_REF_PLUGIN) == static_cast<int>(as::RefKind::PluginReference));
static_assert(static_cast<int>(RADICLE_ACTOR_REF_CREATED) == static_cast<int>(as::RefKind::CreatedReference));

// Invariant: the view points into the record and the vectors below; it is built once after decode and the handle is never moved.
struct radicle_actor_state_decoded {
  as::ActorStateRecord record;
  std::vector<radicle_actor_inventory_delta> inventory;
  std::vector<radicle_actor_value_modifier> actor_values;
  std::vector<radicle_actor_faction_rank> factions;
  radicle_actor_state view{};
};

namespace {

using namespace radicle_c_detail;

std::string text_from(const char* text, size_t size) {
  if (size > as::kMaxStringBytes) throw std::invalid_argument("text is above the string cap");
  if (size == 0) return {};
  if (text == nullptr) throw std::invalid_argument("text is NULL with a non-zero size");
  return std::string(text, size);
}

template <typename T>
void check_list(const T* items, size_t count) {
  if (count > as::kMaxListEntries) throw std::invalid_argument("list is above the entry cap");
  if (count != 0 && items == nullptr) throw std::invalid_argument("list is NULL with a non-zero count");
}

as::FormKey form_from(const radicle_actor_form_key& key) {
  as::FormKey out;
  out.plugin = text_from(key.plugin, key.plugin_size);
  out.local_id = key.local_id;
  return out;
}

// Invariant: ref.kind is read as its int32 bytes; an enum object holding an unlisted value is never compared or converted.
as::RefIdentity ref_from(const radicle_actor_ref& ref) {
  std::int32_t kind = 0;
  std::memcpy(&kind, &ref.kind, sizeof kind);
  if (kind != RADICLE_ACTOR_REF_PLUGIN && kind != RADICLE_ACTOR_REF_CREATED) {
    throw std::invalid_argument("reference kind is not a radicle_actor_ref_kind");
  }
  as::RefIdentity out;
  out.kind = static_cast<as::RefKind>(kind);
  out.form = form_from(ref.form);
  out.created_id = ref.created_id;
  out.base = form_from(ref.base);
  out.spawn_seed = ref.spawn_seed;
  return out;
}

as::ActorStateRecord record_from(const radicle_actor_state& state) {
  as::ActorStateRecord out;
  out.ref = ref_from(state.ref);
  out.flags = state.flags;
  out.cell = form_from(state.cell);
  std::copy(std::begin(state.position), std::end(state.position), out.transform.position.begin());
  std::copy(std::begin(state.rotation), std::end(state.rotation), out.transform.rotation.begin());
  out.transform.scale = state.scale;

  check_list(state.inventory, state.inventory_count);
  out.inventory.reserve(state.inventory_count);
  for (size_t i = 0; i < state.inventory_count; ++i) {
    out.inventory.push_back({form_from(state.inventory[i].item), state.inventory[i].count});
  }
  check_list(state.actor_values, state.actor_value_count);
  out.actor_values.reserve(state.actor_value_count);
  for (size_t i = 0; i < state.actor_value_count; ++i) {
    const radicle_actor_value_modifier& modifier = state.actor_values[i];
    out.actor_values.push_back({text_from(modifier.actor_value, modifier.actor_value_size), modifier.value});
  }
  check_list(state.factions, state.faction_count);
  out.factions.reserve(state.faction_count);
  for (size_t i = 0; i < state.faction_count; ++i) {
    out.factions.push_back({form_from(state.factions[i].faction), state.factions[i].rank});
  }

  out.forced_package = form_from(state.forced_package);
  out.current_package = form_from(state.current_package);
  if (state.has_combat_target != 0 && state.has_combat_target != 1) {
    throw std::invalid_argument("has_combat_target must be 0 or 1");
  }
  if (state.has_combat_target == 1) out.combat_target = ref_from(state.combat_target);
  out.bounty = state.bounty;
  out.spawn_level = state.spawn_level;
  if (state.unknown_tail_size != 0 && state.unknown_tail == nullptr) {
    throw std::invalid_argument("unknown_tail is NULL with a non-zero size");
  }
  if (state.unknown_tail_size != 0) {
    out.unknown_tail.assign(state.unknown_tail, state.unknown_tail + state.unknown_tail_size);
  }
  return out;
}

radicle_actor_form_key form_to(const as::FormKey& key) {
  radicle_actor_form_key out{};
  out.plugin = key.plugin.data();
  out.plugin_size = key.plugin.size();
  out.local_id = key.local_id;
  return out;
}

radicle_actor_ref ref_to(const as::RefIdentity& ref) {
  radicle_actor_ref out{};
  out.kind = static_cast<radicle_actor_ref_kind>(ref.kind);
  out.form = form_to(ref.form);
  out.created_id = ref.created_id;
  out.base = form_to(ref.base);
  out.spawn_seed = ref.spawn_seed;
  return out;
}

void build_view(radicle_actor_state_decoded& decoded) {
  const as::ActorStateRecord& record = decoded.record;
  for (const as::InventoryDelta& delta : record.inventory) {
    decoded.inventory.push_back({form_to(delta.item), delta.count});
  }
  for (const as::ActorValueModifier& modifier : record.actor_values) {
    decoded.actor_values.push_back({modifier.actor_value.data(), modifier.actor_value.size(), modifier.value});
  }
  for (const as::FactionRank& faction : record.factions) {
    decoded.factions.push_back({form_to(faction.faction), faction.rank});
  }

  radicle_actor_state view{};
  view.ref = ref_to(record.ref);
  view.flags = record.flags;
  view.cell = form_to(record.cell);
  std::copy(record.transform.position.begin(), record.transform.position.end(), std::begin(view.position));
  std::copy(record.transform.rotation.begin(), record.transform.rotation.end(), std::begin(view.rotation));
  view.scale = record.transform.scale;
  view.inventory = decoded.inventory.empty() ? nullptr : decoded.inventory.data();
  view.inventory_count = decoded.inventory.size();
  view.actor_values = decoded.actor_values.empty() ? nullptr : decoded.actor_values.data();
  view.actor_value_count = decoded.actor_values.size();
  view.factions = decoded.factions.empty() ? nullptr : decoded.factions.data();
  view.faction_count = decoded.factions.size();
  view.forced_package = form_to(record.forced_package);
  view.current_package = form_to(record.current_package);
  if (record.combat_target.has_value()) {
    view.has_combat_target = 1;
    view.combat_target = ref_to(*record.combat_target);
  }
  view.bounty = record.bounty;
  view.spawn_level = record.spawn_level;
  view.unknown_tail = record.unknown_tail.empty() ? nullptr : record.unknown_tail.data();
  view.unknown_tail_size = record.unknown_tail.size();
  decoded.view = view;
}

}  // namespace

extern "C" {

radicle_status radicle_actor_state_encode(const radicle_actor_state* state, uint8_t** out_bytes, size_t* out_size) {
  clear_error();
  if (out_bytes != nullptr) *out_bytes = nullptr;
  if (out_size != nullptr) *out_size = 0;
  if (state == nullptr || out_bytes == nullptr || out_size == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "state, out_bytes and out_size are required");
  }
  return guard_exceptions("unknown exception while encoding actor state", [&] {
    const std::vector<std::uint8_t> bytes = as::encode(record_from(*state));
    auto* buffer = static_cast<uint8_t*>(std::malloc(bytes.size()));
    if (buffer == nullptr) return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate the actor state blob");
    std::memcpy(buffer, bytes.data(), bytes.size());
    *out_bytes = buffer;
    *out_size = bytes.size();
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_actor_state_decode(const uint8_t* data, size_t size, radicle_actor_state_decoded** out) {
  clear_error();
  if (out != nullptr) *out = nullptr;
  if ((data == nullptr && size != 0) || out == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "data (unless size is 0) and out are required");
  }
  return guard_exceptions("unknown exception while decoding actor state", [&] {
    auto handle = std::make_unique<radicle_actor_state_decoded>();
    handle->record = as::decode(std::span<const std::uint8_t>(data, size));
    build_view(*handle);
    *out = handle.release();
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_actor_state_view(const radicle_actor_state_decoded* decoded, radicle_actor_state* out_state) {
  clear_error();
  if (out_state != nullptr) *out_state = {};
  if (decoded == nullptr || out_state == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "decoded and out_state are required");
  }
  *out_state = decoded->view;
  return RADICLE_STATUS_OK;
}

void radicle_actor_state_free(radicle_actor_state_decoded* decoded) {
  clear_error();
  delete decoded;
}

}  // extern "C"
