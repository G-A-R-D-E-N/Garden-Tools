#pragma once

#include "fo4/formid.hpp"
#include "fo4/signature.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace fo4 {

std::vector<std::size_t> vmad_formid_offsets(std::span<const std::byte> data,
                                              Signature record_signature);

struct VmadCoverage {
  bool unsupported{false};
  bool failed{false};
  bool trailing_data{false};
  bool fully_consumed{false};

  bool complete() const {
    return !unsupported && !failed && !trailing_data && fully_consumed;
  }
};

VmadCoverage vmad_coverage(std::span<const std::byte> data,
                           Signature record_signature);

bool vmad_has_unsupported_properties(std::span<const std::byte> data,
                                     Signature record_signature);

std::optional<std::vector<std::size_t>> vmad_formid_offsets_for_remap(
    std::span<const std::byte> data,
    Signature record_signature);

struct QuestVmadBindings {
  bool binds_stages{false};
  bool binds_aliases{false};
  bool unparseable{false};
};

QuestVmadBindings quest_vmad_bindings(std::span<const std::byte> data);

struct QuestVmadAliasBinding {
  std::size_t alias_id_offset{0};
  std::int16_t alias_id{0};
};

struct QuestVmadFragmentBinding {
  std::size_t stage_offset{0};
  std::uint16_t stage{0};
  std::size_t stage_index_offset{0};
  std::int32_t stage_index{0};
};

struct QuestVmadLayout {
  bool unparseable{false};
  bool unsupported{false};
  std::int16_t object_format{0};
  std::vector<QuestVmadFragmentBinding> fragments;
  std::vector<QuestVmadAliasBinding> aliases;
};

QuestVmadLayout inspect_quest_vmad(std::span<const std::byte> data);

struct VmadProperty {
  std::string name;
  std::uint8_t type{0};
  std::uint8_t flags{0};
  std::optional<RawFormId> object;
  std::optional<std::int16_t> alias;
  std::optional<std::string> text;
  std::optional<std::int32_t> integer;
  std::optional<float> real;
  std::optional<bool> boolean;
  // Leading u32 count of struct and array properties.
  std::optional<std::uint32_t> element_count;
  std::size_t value_offset{0};
  std::size_t value_size{0};
};

struct VmadScript {
  std::string name;
  std::uint8_t flags{0};
  std::vector<VmadProperty> properties;
};

struct VmadScripts {
  std::int16_t version{0};
  std::int16_t object_format{0};
  std::vector<VmadScript> scripts;
  // Type 6 and nonempty type 16 stop decoding.
  bool unsupported{false};
  // Bytes after the script list, e.g. record-specific fragment data.
  std::size_t trailing_bytes{0};
};

// Reads the leading script list of any VMAD; throws fo4::parse_error on truncated or unknown data.
VmadScripts read_vmad_scripts(std::span<const std::byte> data);

/*
 * The quest-specific block that follows the leading script list of a QUST VMAD:
 * the script the stage fragments belong to and its typed properties, the stage
 * fragments (stage, stage index, fragment script and function names), and each
 * alias's script attachments with their typed properties.
 *
 * `object_format` is the one read_vmad_scripts reported for the same VMAD. The
 * block layout is the one walk_quest_extra consumes; this reader keeps the names
 * and property values that walker only skips. A property type this reader cannot
 * decode, or a truncated block, sets `unsupported` with a reason instead of
 * throwing, so a caller can report exactly what stopped.
 */
struct QuestVmadFragment {
  std::uint16_t stage{0};
  std::uint16_t unknown{0};
  std::int32_t stage_index{0};
  std::string script;
  std::string function;
};

struct QuestVmadAliasScripts {
  std::int16_t alias_id{0};
  std::int16_t script_format{0};
  std::vector<VmadScript> scripts;
};

struct QuestVmadExtras {
  std::string script_name;
  std::vector<VmadProperty> properties;
  std::vector<QuestVmadFragment> fragments;
  std::vector<QuestVmadAliasScripts> aliases;
  bool unsupported{false};
  std::string unsupported_reason;
  bool fully_consumed{false};
};

QuestVmadExtras read_quest_vmad_extras(std::span<const std::byte> data, std::int16_t object_format);

}  // namespace fo4
