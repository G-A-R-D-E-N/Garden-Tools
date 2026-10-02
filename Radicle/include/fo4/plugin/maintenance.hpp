#pragma once

#include "fo4/plugin/container.hpp"
#include "fo4/plugin_key.hpp"
#include "fo4/signature.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace fo4 {

struct IdenticalToMaster {
  FormKey key;
  Signature signature{};
  PluginKey previous;
};

struct DeletedRecordCounts {
  std::size_t records{0};
  std::size_t references{0};
  std::size_t navmeshes{0};
};

std::vector<IdenticalToMaster> find_identical_to_master(std::span<const Plugin* const> load_order,
                                                        std::size_t plugin_index,
                                                        std::size_t limit = 0);

std::size_t remove_records(Plugin& plugin, std::span<const FormKey> keys);

DeletedRecordCounts count_deleted_records(const Plugin& plugin);

struct EslEligibility {
  std::size_t native_records{0};
  std::size_t out_of_range{0};
  std::size_t capacity{0xFFE};

  [[nodiscard]] bool ids_fit_light_range() const noexcept { return out_of_range == 0; }
  [[nodiscard]] bool can_fit() const noexcept { return native_records <= capacity; }
};

EslEligibility esl_eligibility(const Plugin& plugin);

}  // namespace fo4
