#include "c_api_env_internal.hpp"
#include "fo4/c_api_script_set.h"
#include "fo4/io/path_text.hpp"
#include "fo4/pex_analysis/script_library.hpp"
#include "fo4/pex_analysis/script_set.hpp"

#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct radicle_script_set_failure_entry {
  size_t archive_index{0};
  std::string path;
  std::string message;
};

struct radicle_script_set {
  std::unique_ptr<radicle_assets> owned;  // invariant: set only by radicle_script_set_open
  const radicle_assets* assets{nullptr};  // invariant: outlives the set
  std::vector<fo4::pex_analysis::ScriptWinner> winners;
  std::vector<radicle_script_set_failure_entry> failures;
  mutable std::unique_ptr<fo4::pex_analysis::ScriptLibrary> library;
};

struct radicle_script_census {
  fo4::pex_analysis::ScriptCensus value;
};

struct radicle_native_surface_entry {
  std::string script;
  std::string declaring_class;
  std::string native;
  size_t call_count{0};
  bool bound{false};
};

struct radicle_native_surface {
  std::vector<radicle_native_surface_entry> rows;
  std::vector<std::string> unresolved;
};

namespace {

using namespace radicle_c_detail;
using namespace radicle_c_env_detail;

radicle_asset_location to_c_location(const fo4::archive::AssetLocation& location) {
  if (location.origin == fo4::archive::AssetOrigin::loose) return {RADICLE_ASSET_LOOSE, 0, 0};
  return {RADICLE_ASSET_ARCHIVE, location.archive, location.entry};
}

radicle_script_set_candidate to_c_candidate(const fo4::pex_analysis::ScriptCandidate& candidate) {
  radicle_script_set_candidate out{};
  out.name = candidate.name.c_str();
  out.path = candidate.path.c_str();
  out.origin = candidate.kind == fo4::pex_analysis::ScriptSourceKind::loose ? RADICLE_ASSET_LOOSE
                                                                           : RADICLE_ASSET_ARCHIVE;
  out.container = candidate.container.c_str();
  out.load_index = candidate.load_index;
  out.size = candidate.size;
  out.location = to_c_location(candidate.location);
  return out;
}

bool valid_paths(const char* const* values, size_t count) {
  if (values == nullptr) return count == 0;
  for (size_t i = 0; i < count; ++i) {
    if (values[i] == nullptr) return false;
  }
  return true;
}

// Invariant: names compare without case for ASCII letters, the same rule the census uses for object names.
std::string fold_name(std::string_view text) {
  std::string out(text);
  for (char& ch : out) {
    if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch + 32);
  }
  return out;
}

fo4::pex_analysis::ScriptLibrary& library_of(const radicle_script_set& set) {
  if (!set.library) {
    set.library = std::make_unique<fo4::pex_analysis::ScriptLibrary>(
        set.assets->value, std::span<const fo4::pex_analysis::ScriptWinner>(set.winners));
  }
  return *set.library;
}

constexpr CopyMessages k_copy_messages{"out_bytes and out_size are required", "failed to allocate script bytes"};

}  // namespace

extern "C" {

radicle_status radicle_script_set_resolve(const radicle_assets* assets,
                                          const int32_t* root_load_index,
                                          size_t root_load_index_count,
                                          radicle_script_set** out_set) {
  clear_error();
  if (out_set != nullptr) *out_set = nullptr;
  if (assets == nullptr || out_set == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "assets and out_set are required");
  }
  if (root_load_index_count != 0 && root_load_index == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "root_load_index is required when its count is not zero");
  }
  if (root_load_index_count != 0 && root_load_index_count != assets->value.roots().size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "a script set requires one load index per assets root");
  }
  return guarded("unknown exception while resolving the script set", [&] {
    const std::span<const std::int32_t> indexes =
        root_load_index == nullptr ? std::span<const std::int32_t>()
                                   : std::span<const std::int32_t>(root_load_index, root_load_index_count);
    auto set = std::make_unique<radicle_script_set>();
    set->winners = fo4::pex_analysis::resolve_script_set(assets->value, indexes);
    set->assets = assets;
    *out_set = set.release();
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_script_set_open(const char* const* root_paths,
                                       size_t root_count,
                                       const char* const* archive_paths,
                                       size_t archive_count,
                                       radicle_archive_policy policy,
                                       radicle_script_set** out_set) {
  clear_error();
  if (out_set != nullptr) *out_set = nullptr;
  if (out_set == nullptr || !valid_paths(root_paths, root_count) || !valid_paths(archive_paths, archive_count)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "non-NULL root paths, archive paths and out_set are required");
  }
  if (policy != RADICLE_ARCHIVE_POLICY_FAIL && policy != RADICLE_ARCHIVE_POLICY_SKIP) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the archive policy is not a known value");
  }
  return guarded("unknown exception while opening the script set", [&] {
    std::vector<std::filesystem::path> roots;
    for (size_t i = 0; i < root_count; ++i) roots.push_back(utf8_path(root_paths[i]));
    std::vector<std::filesystem::path> archives;
    for (size_t i = 0; i < archive_count; ++i) archives.push_back(utf8_path(archive_paths[i]));
    auto opened = fo4::archive::AssetResolver::open_roots(
        roots, archives,
        policy == RADICLE_ARCHIVE_POLICY_SKIP ? fo4::archive::ArchiveFailurePolicy::skip
                                              : fo4::archive::ArchiveFailurePolicy::fail);
    auto set = std::make_unique<radicle_script_set>();
    set->owned = std::make_unique<radicle_assets>(radicle_assets{std::move(opened.resolver)});
    set->assets = set->owned.get();
    for (const auto& failure : opened.failures) {
      set->failures.push_back({failure.index, fo4::io::utf8(failure.path), failure.message});
    }
    set->winners = fo4::pex_analysis::resolve_script_set(set->owned->value);
    *out_set = set.release();
    return RADICLE_STATUS_OK;
  });
}

size_t radicle_script_set_failure_count(const radicle_script_set* set) {
  clear_error();
  return set == nullptr ? 0 : set->failures.size();
}

radicle_status radicle_script_set_failure(const radicle_script_set* set,
                                          size_t index,
                                          radicle_script_set_failure_info* out_failure) {
  clear_error();
  if (out_failure != nullptr) *out_failure = {};
  if (set == nullptr || out_failure == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "set and out_failure are required");
  }
  if (index >= set->failures.size()) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "failure index is out of range");
  const auto& failure = set->failures[index];
  *out_failure = {failure.archive_index, failure.path.c_str(), failure.message.c_str()};
  return RADICLE_STATUS_OK;
}

radicle_status radicle_script_set_parent_chain_valid(const radicle_script_set* set,
                                                     const char* object_name,
                                                     int32_t* out_valid) {
  clear_error();
  if (out_valid != nullptr) *out_valid = 0;
  if (set == nullptr || object_name == nullptr || out_valid == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "set, object_name and out_valid are required");
  }
  return guarded("unknown exception while checking a parent chain", [&] {
    *out_valid = fo4::pex_analysis::parent_chain_valid(library_of(*set), object_name) ? 1 : 0;
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_script_set_census(const radicle_script_set* set, radicle_script_census** out_census) {
  clear_error();
  if (out_census != nullptr) *out_census = nullptr;
  if (set == nullptr || out_census == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "set and out_census are required");
  }
  return guarded("unknown exception while collecting the census", [&] {
    auto census = std::make_unique<radicle_script_census>();
    census->value = fo4::pex_analysis::collect_script_census(library_of(*set));
    *out_census = census.release();
    return RADICLE_STATUS_OK;
  });
}

void radicle_script_census_destroy(radicle_script_census* census) {
  clear_error();
  delete census;
}

radicle_status radicle_script_census_totals_get(const radicle_script_census* census,
                                                radicle_script_census_totals* out_totals) {
  clear_error();
  if (out_totals != nullptr) *out_totals = {};
  if (census == nullptr || out_totals == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "census and out_totals are required");
  }
  const auto& value = census->value;
  *out_totals = {value.objects, value.unreadable.size(), value.sites.size(), value.script_calls,
                 value.unresolved.size()};
  return RADICLE_STATUS_OK;
}

radicle_status radicle_script_census_site_get(const radicle_script_census* census,
                                              size_t index,
                                              radicle_script_census_site* out_site) {
  clear_error();
  if (out_site != nullptr) *out_site = {};
  if (census == nullptr || out_site == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "census and out_site are required");
  }
  if (index >= census->value.sites.size()) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "site index is out of range");
  const auto& site = census->value.sites[index];
  *out_site = {site.script.c_str(), site.function.c_str(), site.instruction, site.is_static ? 1 : 0,
               site.target.c_str(), site.called_function.c_str()};
  return RADICLE_STATUS_OK;
}

radicle_status radicle_script_census_unresolved(const radicle_script_census* census,
                                                size_t index,
                                                const char** out_text) {
  clear_error();
  if (out_text != nullptr) *out_text = nullptr;
  if (census == nullptr || out_text == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "census and out_text are required");
  }
  if (index >= census->value.unresolved.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "unresolved index is out of range");
  }
  *out_text = census->value.unresolved[index].c_str();
  return RADICLE_STATUS_OK;
}

radicle_status radicle_script_census_unreadable(const radicle_script_census* census,
                                                size_t index,
                                                const char** out_name) {
  clear_error();
  if (out_name != nullptr) *out_name = nullptr;
  if (census == nullptr || out_name == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "census and out_name are required");
  }
  if (index >= census->value.unreadable.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "unreadable index is out of range");
  }
  *out_name = census->value.unreadable[index].c_str();
  return RADICLE_STATUS_OK;
}

radicle_status radicle_script_native_surface_build(const radicle_script_set* set,
                                                   const char* const* scripts,
                                                   size_t script_count,
                                                   const char* const* bound_natives,
                                                   size_t bound_count,
                                                   radicle_native_surface** out_surface) {
  clear_error();
  if (out_surface != nullptr) *out_surface = nullptr;
  if (set == nullptr || out_surface == nullptr || !valid_paths(scripts, script_count) ||
      !valid_paths(bound_natives, bound_count)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                "set and out_surface are required, and script and bound names must not be NULL");
  }
  return guarded("unknown exception while building the native surface", [&] {
    std::set<std::string> wanted;
    for (size_t i = 0; i < script_count; ++i) wanted.insert(fold_name(scripts[i]));
    std::set<std::string> bound;
    for (size_t i = 0; i < bound_count; ++i) bound.insert(fold_name(bound_natives[i]));

    const auto census = fo4::pex_analysis::collect_script_census(library_of(*set));
    auto surface = std::make_unique<radicle_native_surface>();
    std::map<std::string, radicle_native_surface_entry> grouped;
    for (const auto& site : census.sites) {
      if (!wanted.contains(fold_name(site.script))) continue;
      const std::string key = fold_name(site.script) + '\n' + fold_name(site.declaring_class) + '\n' +
                              fold_name(site.called_function);
      auto [it, inserted] = grouped.try_emplace(key);
      auto& entry = it->second;
      if (inserted) {
        entry.script = site.script;
        entry.declaring_class = site.declaring_class;
        entry.native = site.called_function;
        entry.bound = bound.contains(fold_name(site.declaring_class + "." + site.called_function));
      }
      ++entry.call_count;
    }
    surface->rows.reserve(grouped.size());
    for (auto& group : grouped) surface->rows.push_back(std::move(group.second));

    for (const auto& text : census.unresolved) {
      const auto bracket = text.find('[');
      if (bracket == std::string::npos) continue;
      if (wanted.contains(fold_name(std::string_view(text).substr(0, bracket)))) {
        surface->unresolved.push_back(text);
      }
    }
    *out_surface = surface.release();
    return RADICLE_STATUS_OK;
  });
}

void radicle_native_surface_destroy(radicle_native_surface* surface) {
  clear_error();
  delete surface;
}

size_t radicle_native_surface_count(const radicle_native_surface* surface) {
  clear_error();
  return surface == nullptr ? 0 : surface->rows.size();
}

radicle_status radicle_native_surface_row_get(const radicle_native_surface* surface,
                                              size_t index,
                                              radicle_native_surface_row* out_row) {
  clear_error();
  if (out_row != nullptr) *out_row = {};
  if (surface == nullptr || out_row == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "surface and out_row are required");
  }
  if (index >= surface->rows.size()) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "row index is out of range");
  const auto& row = surface->rows[index];
  *out_row = {row.script.c_str(), row.declaring_class.c_str(), row.native.c_str(), row.call_count,
              row.bound ? 1 : 0};
  return RADICLE_STATUS_OK;
}

size_t radicle_native_surface_unresolved_count(const radicle_native_surface* surface) {
  clear_error();
  return surface == nullptr ? 0 : surface->unresolved.size();
}

radicle_status radicle_native_surface_unresolved(const radicle_native_surface* surface,
                                                 size_t index,
                                                 const char** out_text) {
  clear_error();
  if (out_text != nullptr) *out_text = nullptr;
  if (surface == nullptr || out_text == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "surface and out_text are required");
  }
  if (index >= surface->unresolved.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "unresolved index is out of range");
  }
  *out_text = surface->unresolved[index].c_str();
  return RADICLE_STATUS_OK;
}

size_t radicle_script_set_count(const radicle_script_set* set) {
  clear_error();
  return set == nullptr ? 0 : set->winners.size();
}

radicle_status radicle_script_set_get(const radicle_script_set* set,
                                      size_t index,
                                      radicle_script_set_winner* out_winner) {
  clear_error();
  if (out_winner != nullptr) *out_winner = {};
  if (set == nullptr || out_winner == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "set and out_winner are required");
  }
  if (index >= set->winners.size()) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "winner index is out of range");
  const auto& winner = set->winners[index];
  out_winner->name = winner.winner.name.c_str();
  out_winner->winner = to_c_candidate(winner.winner);
  out_winner->replaced = static_cast<uint32_t>(winner.replaced);
  return RADICLE_STATUS_OK;
}

radicle_status radicle_script_set_read(const radicle_script_set* set,
                                        size_t index,
                                        uint8_t** out_bytes,
                                        size_t* out_size) {
  clear_error();
  if (out_bytes != nullptr) *out_bytes = nullptr;
  if (out_size != nullptr) *out_size = 0;
  if (set == nullptr || out_bytes == nullptr || out_size == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "set, out_bytes and out_size are required");
  }
  if (index >= set->winners.size()) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "winner index is out of range");
  return guarded("unknown exception while reading a script payload", [&] {
    return copy_out(set->assets->value.read(set->winners[index].winner.location), out_bytes, out_size, k_copy_messages);
  });
}

void radicle_script_set_destroy(radicle_script_set* set) {
  clear_error();
  delete set;
}

}  // extern "C"
