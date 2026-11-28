#pragma once

#include "fo4/plugin/container.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace fo4::forms {

enum class ObjectTemplateWrite {
  updated,
  record_missing,
  unsupported_record,
  combination_missing,
  malformed,
  index_out_of_range,
  invalid_value,
  ambiguous,
};

struct ObjectTemplateInclude {
  FormKey mod;
  std::uint8_t attach_point;
  std::uint8_t optional;
  std::uint8_t dont_use_all;
};

struct ObjectTemplateProperty {
  std::uint8_t value_type;
  std::uint8_t function_type;
  std::uint16_t property;
  std::uint32_t value1;
  std::uint32_t value2;
  std::uint32_t step_bits;
  std::optional<FormKey> form_value;
};

struct ObjectTemplateRows {
  std::vector<FormKey> keywords;
  std::vector<ObjectTemplateInclude> includes;
  std::vector<ObjectTemplateProperty> properties;
  std::uint8_t min_level_for_ranks{};
  std::uint8_t alt_levels_per_tier{};
};

enum class ObjectTemplateRowKind { keyword, include, property };

struct ObjectTemplateInsertRow {
  FormKey link{};
  std::uint8_t attach_point{};
  std::uint8_t optional{};
  std::uint8_t dont_use_all{};
  ObjectTemplateProperty property{};
};

ObjectTemplateWrite insert_object_template_row(Plugin& plugin, const FormKey& record,
    std::size_t combination, ObjectTemplateRowKind kind, std::size_t index,
    const ObjectTemplateInsertRow& row);

ObjectTemplateWrite clone_object_template_row(Plugin& plugin, const FormKey& record,
    std::size_t combination, ObjectTemplateRowKind kind, std::size_t source, std::size_t destination);
ObjectTemplateWrite remove_object_template_row(Plugin& plugin, const FormKey& record,
    std::size_t combination, ObjectTemplateRowKind kind, std::size_t index);
ObjectTemplateWrite move_object_template_row(Plugin& plugin, const FormKey& record,
    std::size_t combination, ObjectTemplateRowKind kind, std::size_t source, std::size_t destination);

ObjectTemplateWrite read_object_template_rows(Plugin& plugin, const FormKey& record,
                                              std::size_t combination, ObjectTemplateRows& out);
ObjectTemplateWrite set_object_template_rank_tier(Plugin& plugin, const FormKey& record,
    std::size_t combination, std::uint8_t min_level_for_ranks, std::uint8_t alt_levels_per_tier);
ObjectTemplateWrite set_object_template_include_options(Plugin& plugin, const FormKey& record,
    std::size_t combination, std::size_t index, std::uint8_t attach_point,
    std::uint8_t optional, std::uint8_t dont_use_all);
ObjectTemplateWrite set_object_template_property_value(Plugin& plugin, const FormKey& record,
    std::size_t combination, std::size_t index, std::uint8_t part, std::uint32_t value);
ObjectTemplateWrite set_object_template_property(Plugin& plugin, const FormKey& record,
    std::size_t combination, std::size_t index, const ObjectTemplateProperty& property);

ObjectTemplateWrite set_object_template_keyword(Plugin& plugin, const FormKey& record,
                                                std::size_t combination, std::size_t index,
                                                const FormKey& target);
ObjectTemplateWrite set_object_template_include(Plugin& plugin, const FormKey& record,
                                                std::size_t combination, std::size_t index,
                                                const FormKey& target);
ObjectTemplateWrite set_object_template_property_form(Plugin& plugin, const FormKey& record,
                                                      std::size_t combination, std::size_t index,
                                                      const FormKey& target);

}  // namespace fo4::forms
