#include "fo4/c_api_collision_names.h"

#include "c_api_env_internal.hpp"
#include "fo4/nif/havok_names.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/scalar.hpp"
#include "fo4/text/encoding.hpp"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct radicle_collision_names {
  std::vector<std::string> layers;
  std::vector<std::uint8_t> collides_with_nothing;
  std::vector<std::vector<std::uint32_t>> collides_with;
  std::unordered_map<std::uint32_t, std::string> materials;
};

using namespace radicle_c_env_detail;

namespace {

constexpr std::uint32_t k_layer_limit = 256;

std::optional<std::string> scalar_text(const radicle_env& env, const fo4::env::RecordRef& ref, std::string_view path) {
  const auto field = fo4::schema::scalar_field_descriptor(ref.record->header.signature, path);
  if (!field) return std::nullopt;
  try {
    return fo4::record_scalar_text(env.value.plugin(ref.plugin), *ref.record, *field, 0);
  } catch (const fo4::schema::ScalarError&) {
    return std::nullopt;
  }
}

struct CollectedCollides {
  std::vector<std::uint8_t> nothing;
  std::vector<std::vector<std::uint32_t>> lists;
};

struct CollectedLayers {
  std::vector<std::string> names;
  std::vector<std::uint8_t> collides_with_nothing;
  std::vector<std::vector<std::uint32_t>> collides_with;
};

// Invariant: a layer collides with nothing when its winning COLL list is empty and no other named winning COLL lists that layer; an unreadable list counts as neither. lists[layer] holds the layer indexes of the named winning COLL records that list names, ascending and unique; an unnamed, absent or unreadable list stays empty.
CollectedCollides collect_collides_with(const radicle_env& env,
                                        const std::map<std::uint32_t, const fo4::env::RecordRef*>& refs,
                                        std::size_t count) {
  CollectedCollides out{std::vector<std::uint8_t>(count, 0), std::vector<std::vector<std::uint32_t>>(count)};
  const auto descriptor = fo4::schema::formlink_list_descriptor(fo4::Signature::from_string("COLL"), "COLL\\CollidesWith");
  if (!descriptor) return out;
  std::unordered_map<const fo4::Record*, std::uint32_t> index_of;
  for (const auto& [index, ref] : refs) index_of[ref->record] = index;
  std::set<std::uint32_t> empty;
  std::set<std::uint32_t> listed;
  for (const auto& [index, ref] : refs) {
    std::vector<fo4::FormKey> values;
    try {
      auto read = fo4::schema::record_formlink_list_values(env.value.plugin(ref->plugin), ref->record->form_key, *descriptor);
      if (!read) continue;
      values = std::move(*read);
    } catch (const fo4::schema::FormLinkListError&) {
      continue;
    }
    if (values.empty()) empty.insert(index);
    std::set<std::uint32_t> targets;
    for (const auto& key : values) {
      const auto* target = env.value.winner(key);
      if (!target) continue;
      const auto found = index_of.find(target->record);
      if (found == index_of.end()) continue;
      listed.insert(found->second);
      targets.insert(found->second);
    }
    if (index < count) out.lists[index].assign(targets.begin(), targets.end());
  }
  for (const auto index : empty) {
    if (index < count && !listed.contains(index)) out.nothing[index] = 1;
  }
  return out;
}

CollectedLayers collect_layers(const radicle_env& env) {
  std::map<std::uint32_t, std::string> named;
  std::map<std::uint32_t, const fo4::env::RecordRef*> refs;
  for (const auto* ref : env.value.winners_of(fo4::Signature::from_string("COLL"))) {
    const auto index_text = scalar_text(env, *ref, "COLL\\Index");
    const auto name = scalar_text(env, *ref, "COLL\\Name");
    if (!index_text || !name) continue;
    std::uint32_t index = 0;
    const auto parsed = std::from_chars(index_text->data(), index_text->data() + index_text->size(), index);
    if (parsed.ec != std::errc{} || index >= k_layer_limit) continue;
    named[index] = *name;
    refs[index] = ref;
  }
  if (named.empty()) return {};
  std::vector<fo4::nif::HavokLayerRow> rows;
  const auto count = named.rbegin()->first + 1;
  rows.reserve(count);
  for (std::uint32_t index = 0; index < count; ++index) {
    const auto found = named.find(index);
    rows.push_back({index, found == named.end() ? std::string{} : found->second});
  }
  auto collides = collect_collides_with(env, refs, count);
  return {fo4::nif::build_havok_layer_names(rows), std::move(collides.nothing), std::move(collides.lists)};
}

std::unordered_map<std::uint32_t, std::string> collect_materials(const radicle_env& env) {
  std::unordered_map<std::uint32_t, std::string> materials;
  for (const auto* ref : env.value.winners_of(fo4::Signature::from_string("MATT"))) {
    const auto meta = env.value.metadata(*ref);
    if (!meta.editor_id || meta.editor_id->empty()) continue;
    materials[fo4::nif::havok_material_crc(*meta.editor_id)] = fo4::text::windows1252_to_utf8(*meta.editor_id);
  }
  return materials;
}

}  // namespace

extern "C" {

radicle_status radicle_collision_names_build(const radicle_env* env, radicle_collision_names** out_names) {
  clear_error();
  if (out_names) *out_names = nullptr;
  if (!env || !out_names) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env and out_names are required");
  return guard_exceptions("unknown exception while building collision names", [&] {
    auto handle = std::make_unique<radicle_collision_names>();
    auto layers = collect_layers(*env);
    handle->layers = std::move(layers.names);
    handle->collides_with_nothing = std::move(layers.collides_with_nothing);
    handle->collides_with = std::move(layers.collides_with);
    handle->materials = collect_materials(*env);
    *out_names = handle.release();
    return RADICLE_STATUS_OK;
  });
}

void radicle_collision_names_destroy(radicle_collision_names* names) {
  clear_error();
  delete names;
}

radicle_status radicle_collision_names_layer(const radicle_collision_names* names, std::uint32_t layer,
                                             const char** out_name) {
  clear_error();
  if (out_name) *out_name = nullptr;
  if (!names || !out_name) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "names and out_name are required");
  if (layer < names->layers.size() && !names->layers[layer].empty()) *out_name = names->layers[layer].c_str();
  return RADICLE_STATUS_OK;
}

radicle_status radicle_collision_names_layer_collides_with_nothing(const radicle_collision_names* names,
                                                                   std::uint32_t layer,
                                                                   int32_t* out_flag) {
  clear_error();
  if (out_flag) *out_flag = 0;
  if (!names || !out_flag) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "names and out_flag are required");
  if (layer < names->collides_with_nothing.size()) *out_flag = names->collides_with_nothing[layer] ? 1 : 0;
  return RADICLE_STATUS_OK;
}

radicle_status radicle_collision_names_layer_collides_with(const radicle_collision_names* names,
                                                           std::uint32_t layer,
                                                           const std::uint32_t** out_layers,
                                                           size_t* out_count) {
  clear_error();
  if (out_layers) *out_layers = nullptr;
  if (out_count) *out_count = 0;
  if (!names || !out_layers || !out_count) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "names, out_layers and out_count are required");
  if (layer < names->collides_with.size() && !names->collides_with[layer].empty()) {
    *out_layers = names->collides_with[layer].data();
    *out_count = names->collides_with[layer].size();
  }
  return RADICLE_STATUS_OK;
}

radicle_status radicle_collision_names_material(const radicle_collision_names* names, std::uint32_t material,
                                                const char** out_name) {
  clear_error();
  if (out_name) *out_name = nullptr;
  if (!names || !out_name) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "names and out_name are required");
  const auto found = names->materials.find(material);
  if (found != names->materials.end()) *out_name = found->second.c_str();
  return RADICLE_STATUS_OK;
}

radicle_status radicle_collision_names_counts(const radicle_collision_names* names, size_t* out_layers,
                                              size_t* out_materials) {
  clear_error();
  if (!names || !out_layers || !out_materials)
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "names, out_layers and out_materials are required");
  *out_layers = names->layers.size();
  *out_materials = names->materials.size();
  return RADICLE_STATUS_OK;
}

}
