#pragma once

#include "fo4/plugin/container.hpp"
#include "fo4/plugin_key.hpp"
#include "fo4/signature.hpp"

#include <cstddef>
#include <cstdint>
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
                                                        std::size_t limit = 0,
                                                        bool exact_only = false);

std::size_t remove_records(Plugin& plugin, std::span<const FormKey> keys);

enum class InjectedCleanupStatus { updated, no_injected_references, unsupported };
InjectedCleanupStatus cleanup_injected_form_list(std::span<Plugin*> load_order,
                                                  std::size_t plugin_index,
                                                  const FormKey& key);

DeletedRecordCounts count_deleted_records(const Plugin& plugin);

struct EslEligibility {
  std::size_t native_records{0};
  std::size_t out_of_range{0};
  std::size_t capacity{0xFFE};

  [[nodiscard]] bool ids_fit_light_range() const noexcept { return out_of_range == 0; }
  [[nodiscard]] bool can_fit() const noexcept { return native_records <= capacity; }
};

EslEligibility esl_eligibility(const Plugin& plugin);

struct EslSlotRange {
  std::uint32_t first{0};
  std::uint32_t last{0};
  std::size_t capacity{0};
};

EslSlotRange esl_slot_range(const Plugin& plugin);

struct EslCompactionBlocker {
  FormKey key;
  Signature signature{};
  bool nested{false};
};

enum class EslCompactionStatus {
  compacted,
  already_compatible,
  capacity_exceeded,
  blocked_nested_records,
  rekey_failed,
};

struct EslCompactionResult {
  EslCompactionStatus status{EslCompactionStatus::already_compatible};
  std::size_t remapped{0};
  std::uint32_t next_form_id{0};
  std::vector<EslCompactionBlocker> blockers;
};

EslCompactionResult compact_to_esl(Plugin& plugin);

struct CleanDeletedResult {
  std::size_t undeleted{0};
  std::size_t skipped{0};
};

// Constraint: clears Deleted and sets Initially Disabled on every deleted record; an undecodable record is left alone and counted as skipped.
CleanDeletedResult clean_deleted_records(Plugin& plugin);

enum class RenumberAllStatus {
  renumbered,
  unchanged,
  no_records,
  blocked_nested_records,
  out_of_range,
  rekey_failed,
};

struct RenumberAllResult {
  RenumberAllStatus status{RenumberAllStatus::no_records};
  std::size_t renumbered{0};
  std::vector<FormKey> blockers;
};

// Constraint: assigns start_local, start_local + 1, ... to the plugin's own records in their current local-ID order and rewrites every in-plugin link; nothing changes unless every record fits.
RenumberAllResult renumber_all_local_records(Plugin& plugin, std::uint32_t start_local);

}  // namespace fo4
