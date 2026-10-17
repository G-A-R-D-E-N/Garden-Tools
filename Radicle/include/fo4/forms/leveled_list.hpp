#pragma once

#include "fo4/formid.hpp"
#include "fo4/plugin/container.hpp"
#include "fo4/schema/localized_fields.hpp"
#include "fo4/signature.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

// Layouts and sources: Radicle/docs/LEVELED_LISTS.md (TODO-verify until LeveledListReal runs on CI).
namespace fo4::forms {

class LeveledListError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

struct LeveledListExtraData {
  FormKey owner{};
  std::uint32_t global_or_rank{0};
  float item_condition{0.0f};

  friend bool operator==(const LeveledListExtraData&, const LeveledListExtraData&) = default;
};

struct LeveledListEntry {
  std::uint16_t level{1};
  std::array<std::byte, 2> reserved_before_form_id{};
  FormKey item{};
  std::uint16_t count{1};
  std::uint8_t chance_none{0};
  std::byte reserved_tail{};
  std::optional<LeveledListExtraData> extra;

  friend bool operator==(const LeveledListEntry&, const LeveledListEntry&) = default;
};

struct LeveledList {
  FormKey key{};
  std::uint8_t chance_none{0};
  std::uint8_t max_count{0};
  std::uint8_t flags{0};
  std::optional<FormKey> use_global;
  std::optional<FormKey> epic_loot_chance;
  std::optional<schema::LocalizedString> override_name;
  std::vector<LeveledListEntry> entries;
};

LeveledList decode_leveled_list(const Plugin& plugin, const FormKey& key);

FormKey create_leveled_list(Plugin& plugin,
                            Signature signature,
                            std::optional<std::string_view> editor_id,
                            std::uint8_t chance_none,
                            std::uint8_t max_count,
                            std::uint8_t flags);

bool reset_leveled_list(Plugin& plugin, const FormKey& key);

bool replace_leveled_list_entries(Plugin& plugin,
                                  const FormKey& key,
                                  std::span<const LeveledListEntry> entries);

bool insert_leveled_list_entry(Plugin& plugin,
                               const FormKey& key,
                               std::size_t index,
                               const LeveledListEntry& entry);

bool remove_leveled_list_entry(Plugin& plugin,
                               const FormKey& key,
                               std::size_t index);

bool set_leveled_list_entry(Plugin& plugin,
                            const FormKey& key,
                            std::size_t index,
                            const LeveledListEntry& entry);

bool set_leveled_list_settings(Plugin& plugin,
                               const FormKey& key,
                               std::uint8_t chance_none,
                               std::uint8_t max_count,
                               std::uint8_t flags);

}  // namespace fo4::forms
