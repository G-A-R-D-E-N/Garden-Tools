#include "c_api_env_internal.hpp"
#include "fo4/c_api_script_set.h"
#include "fo4/pex_analysis/script_set.hpp"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <vector>

struct radicle_script_set {
  const radicle_assets* assets{nullptr};  // invariant: outlives the set
  std::vector<fo4::pex_analysis::ScriptWinner> winners;
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

radicle_status copy_bytes(const std::vector<std::byte>& bytes, uint8_t** out_bytes, size_t* out_size) {
  if (bytes.empty()) return RADICLE_STATUS_OK;
  auto* result = static_cast<uint8_t*>(std::malloc(bytes.size()));
  if (result == nullptr) return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate script bytes");
  std::memcpy(result, bytes.data(), bytes.size());
  *out_bytes = result;
  *out_size = bytes.size();
  return RADICLE_STATUS_OK;
}

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
    return copy_bytes(set->assets->value.read(set->winners[index].winner.location), out_bytes, out_size);
  });
}

void radicle_script_set_destroy(radicle_script_set* set) {
  clear_error();
  delete set;
}

}  // extern "C"
