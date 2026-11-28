#include "fo4/c_api_search.h"

#include "c_api_env_internal.hpp"
#include "fo4/graph/plugin_diff.hpp"
#include "fo4/graph/record_search.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace radicle_c_detail;
using namespace radicle_c_env_detail;

constexpr size_t k_default_search_limit = 500;
constexpr uint32_t k_all_category_bits = 127;

const fo4::ConflictGraph& env_graph(const radicle_env& env) {
  std::call_once(env.graph_once, [&] {
    std::vector<const fo4::Plugin*> plugins;
    for (size_t i = 0; i < env.value.plugins().size(); ++i) plugins.push_back(&env.value.plugin(i));
    env.graph = fo4::ConflictGraph::build(plugins);
  });
  return *env.graph;
}

std::string text_or_empty(const char* text) {
  return text == nullptr ? std::string{} : std::string(text);
}

std::vector<fo4::ConflictCategory> categories_from_mask(uint32_t mask) {
  if ((mask & ~k_all_category_bits) != 0) {
    throw std::invalid_argument("conflict_category_mask has unknown bits");
  }
  constexpr std::array<std::pair<uint32_t, fo4::ConflictCategory>, 7> table{{
      {RADICLE_CONFLICT_BIT_UNKNOWN, fo4::ConflictCategory::unknown},
      {RADICLE_CONFLICT_BIT_ONLY_ONE, fo4::ConflictCategory::only_one},
      {RADICLE_CONFLICT_BIT_NO_CONFLICT, fo4::ConflictCategory::no_conflict},
      {RADICLE_CONFLICT_BIT_BENIGN, fo4::ConflictCategory::benign},
      {RADICLE_CONFLICT_BIT_OVERRIDE, fo4::ConflictCategory::override_},
      {RADICLE_CONFLICT_BIT_CONFLICT, fo4::ConflictCategory::conflict},
      {RADICLE_CONFLICT_BIT_CRITICAL, fo4::ConflictCategory::critical},
  }};
  std::vector<fo4::ConflictCategory> out;
  for (const auto& [bit, category] : table) {
    if ((mask & bit) != 0) out.push_back(category);
  }
  return out;
}

std::vector<fo4::Signature> signatures_from(const radicle_record_search& query) {
  std::vector<fo4::Signature> out;
  if (query.signature_count == 0) return out;
  if (query.signatures == nullptr) throw std::invalid_argument("signatures is NULL");
  for (size_t i = 0; i < query.signature_count; ++i) {
    const auto signature = to_signature(query.signatures[i]);
    if (!signature) throw std::invalid_argument("signature must be four characters");
    out.push_back(*signature);
  }
  return out;
}

fo4::RecordSearchQuery search_query_from(const radicle_record_search& query) {
  fo4::RecordSearchQuery search;
  search.text = text_or_empty(query.text);
  search.form_id = text_or_empty(query.form_id);
  search.signatures = signatures_from(query);
  search.editor_id = text_or_empty(query.editor_id);
  search.editor_id_pattern = text_or_empty(query.editor_id_pattern);
  search.display_name = text_or_empty(query.display_name);
  search.field_value = text_or_empty(query.field_value);
  search.conflict_categories = categories_from_mask(query.conflict_category_mask);
  search.limit = query.limit == 0 ? k_default_search_limit : query.limit;
  return search;
}

std::string_view change_name(fo4::RecordChange change) {
  switch (change) {
    case fo4::RecordChange::added:
      return "added";
    case fo4::RecordChange::removed:
      return "removed";
    case fo4::RecordChange::changed:
      return "changed";
  }
  return "changed";
}

std::string diff_row(const fo4::RecordDiff& diff) {
  std::string row = diff.key.to_string();
  row += '\t';
  row += change_name(diff.change);
  row += '\t';
  row += diff.signature.to_string();
  row += '\t';
  row += diff.header_changed ? "1" : "0";
  row += '\t';
  row += diff.placement_changed ? "1" : "0";
  row += '\t';
  for (std::size_t index = 0; index < diff.changed_subrecords.size(); ++index) {
    if (index != 0) row += ',';
    row += diff.changed_subrecords[index].to_string();
  }
  return row;
}

bool is_keyless_tes4(const fo4::RecordDiff& diff) {
  return diff.signature == fo4::Signature::from_string("TES4") && diff.key.to_string().empty();
}

radicle_status run_search(const radicle_env* env,
                              const radicle_record_search* query,
                              const radicle_strings* strings,
                              char*** out_strings,
                              size_t* out_count,
                              int32_t* out_truncated) {
  clear_error();
  if (env == nullptr || query == nullptr || out_strings == nullptr || out_count == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, query, out_strings and out_count are required");
  }
  *out_strings = nullptr;
  *out_count = 0;
  if (out_truncated != nullptr) *out_truncated = 0;
  if (strings != nullptr && !strings_belong_to(strings, env)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "strings do not belong to this environment");
  }
  return guarded("unknown exception while searching records", [&] {
    try {
      const auto search = search_query_from(*query);
      std::vector<const fo4::Plugin*> load_order;
      for (size_t i = 0; i < env->value.plugins().size(); ++i) load_order.push_back(&env->value.plugin(i));
      const fo4::ConflictGraph* graph = search.conflict_categories.empty() ? nullptr : &env_graph(*env);
      fo4::LocalizedNameResolver resolve_name;
      if (strings != nullptr) {
        resolve_name = [strings](std::size_t plugin_index, std::uint32_t string_id) {
          return strings_display_name(strings, plugin_index, string_id);
        };
      }
      const auto result = fo4::search_records(load_order, search, graph, {}, resolve_name);

      std::vector<std::string> rows;
      rows.reserve(result.hits.size());
      for (const auto& hit : result.hits) {
        std::string row = hit.key.to_string();
        row += '\t';
        row += hit.signature.to_string();
        row += '\t';
        row += std::to_string(hit.plugin_index);
        row += '\t';
        row += hit.editor_id;
        rows.push_back(std::move(row));
      }
      if (out_truncated != nullptr) *out_truncated = result.truncated ? 1 : 0;
      return copy_strings(rows, out_strings, out_count);
    } catch (const std::invalid_argument& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
  });
}

}  // namespace

extern "C" {

radicle_status radicle_env_search_records(const radicle_env* env,
                                          const radicle_record_search* query,
                                          char*** out_strings,
                                          size_t* out_count,
                                          int32_t* out_truncated) {
  return run_search(env, query, nullptr, out_strings, out_count, out_truncated);
}

radicle_status radicle_env_search_records_with_strings(const radicle_env* env,
                                                       const radicle_record_search* query,
                                                       const radicle_strings* strings,
                                                       char*** out_strings,
                                                       size_t* out_count,
                                                       int32_t* out_truncated) {
  return run_search(env, query, strings, out_strings, out_count, out_truncated);
}

radicle_status radicle_env_diff_plugins(const radicle_env* env,
                                        const char* before_plugin,
                                        const char* after_plugin,
                                        char*** out_strings,
                                        size_t* out_count) {
  clear_error();
  if (env == nullptr || before_plugin == nullptr || after_plugin == nullptr ||
      out_strings == nullptr || out_count == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                "env, before_plugin, after_plugin, out_strings and out_count are required");
  }
  *out_strings = nullptr;
  *out_count = 0;
  return guarded("unknown exception while diffing plugins", [&] {
    const auto before = env->value.find_plugin(before_plugin);
    const auto after = env->value.find_plugin(after_plugin);
    if (!before || !after) {
      return fail(RADICLE_STATUS_NOT_FOUND, "plugin is not loaded in this environment");
    }
    std::vector<std::string> rows;
    for (const auto& diff : fo4::diff_plugins(env->value.plugin(*before), env->value.plugin(*after))) {
      if (is_keyless_tes4(diff)) continue;
      rows.push_back(diff_row(diff));
    }
    return copy_strings(rows, out_strings, out_count);
  });
}

}  // extern "C"
