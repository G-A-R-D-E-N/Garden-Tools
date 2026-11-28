#include "fo4/c_api_conflict.h"

#include "c_api_env_internal.hpp"
#include "fo4/graph/conflict_kind.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace radicle_c_detail;
using namespace radicle_c_env_detail;

std::string conflict_row(std::string_view record,
                         std::string_view level,
                         std::string_view path,
                         fo4::ConflictCategory category,
                         const std::vector<fo4::ConflictRole>& roles) {
  std::string row{record};
  row += '\t';
  row += level;
  row += '\t';
  row += path;
  row += '\t';
  row += fo4::conflict_category_name(category);
  row += '\t';
  for (std::size_t index = 0; index < roles.size(); ++index) {
    if (index != 0) row += ',';
    row += fo4::conflict_role_name(roles[index]);
  }
  return row;
}

}  // namespace

extern "C" {

radicle_status radicle_env_classify_conflicts(const radicle_env* env,
                                              char*** out_strings,
                                              size_t* out_count) {
  clear_error();
  if (env == nullptr || out_strings == nullptr || out_count == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, out_strings and out_count are required");
  }
  *out_strings = nullptr;
  *out_count = 0;
  return guarded("unknown exception while classifying conflicts", [&] {
    std::vector<const fo4::Plugin*> load_order;
    for (std::size_t i = 0; i < env->value.plugins().size(); ++i) {
      load_order.push_back(&env->value.plugin(i));
    }
    const auto graph = fo4::ConflictGraph::build(load_order);

    std::vector<std::string> rows;
    for (const auto& record : fo4::classify_conflicts(graph, load_order)) {
      const auto key = record.key.to_string();
      rows.push_back(conflict_row(key, "record", {}, record.category, record.roles));
      for (const auto& subrecord : record.subrecords) {
        rows.push_back(conflict_row(key, "subrecord", subrecord.signature.to_string(),
                                    subrecord.category, subrecord.roles));
      }
      for (const auto& field : record.fields) {
        rows.push_back(conflict_row(key, "field", field.path, field.category, field.roles));
      }
    }
    return copy_strings(rows, out_strings, out_count);
  });
}

}  // extern "C"
