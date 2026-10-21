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
  std::uint32_t fragment_count{0};
  std::uint32_t fragment_script_count{0};

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

struct VmadObjectElement {
  RawFormId object;
  std::int16_t alias{0};
};

struct VmadStructMember {
  std::string name;
  std::uint8_t type{0};
  std::uint8_t flags{0};
  std::optional<VmadObjectElement> object;
  std::optional<std::string> text;
  std::optional<std::int32_t> integer;
  std::optional<float> real;
  std::optional<bool> boolean;
};

struct VmadStruct {
  std::vector<VmadStructMember> members;
};

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
  std::optional<std::vector<VmadObjectElement>> object_elements;
  std::optional<std::vector<std::string>> text_elements;
  std::optional<std::vector<std::int32_t>> integer_elements;
  std::optional<std::vector<float>> real_elements;
  std::optional<std::vector<bool>> boolean_elements;
  std::optional<std::vector<VmadStruct>> struct_elements;
  std::size_t offset{0};
  std::size_t value_offset{0};
  std::size_t value_size{0};
};

struct VmadScript {
  std::string name;
  std::uint8_t flags{0};
  std::vector<VmadProperty> properties;
  std::size_t offset{0};
  std::size_t property_count_offset{0};
};

struct VmadScripts {
  std::int16_t version{0};
  std::int16_t object_format{0};
  std::vector<VmadScript> scripts;
  bool unsupported{false};
  // Bytes after the script list, e.g. record-specific fragment data.
  std::size_t trailing_bytes{0};
};

// Reads the leading script list of any VMAD; throws fo4::parse_error on truncated or unknown data.
VmadScripts read_vmad_scripts(std::span<const std::byte> data);

struct QuestVmadFragment {
  std::uint16_t stage{0};
  std::uint16_t unknown{0};
  std::int32_t stage_index{0};
  std::uint8_t unknown_byte{0};
  std::string script;
  std::string function;
  std::size_t offset{0};
  std::size_t end{0};
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
  std::size_t count_offset{0};
  std::size_t entries_begin{0};
};

QuestVmadExtras read_quest_vmad_extras(std::span<const std::byte> data, std::int16_t object_format);

struct SceneVmadPhaseFragment {
  std::uint8_t flag{0};
  std::uint32_t index{0};
  std::uint8_t unknown{0};
  std::string script;
  std::string function;
  std::size_t offset{0};
  std::size_t end{0};
};

struct SceneVmadExtras {
  std::uint8_t flags{0};
  std::vector<SceneVmadPhaseFragment> phases;
  bool unsupported{false};
  std::string unsupported_reason;
  bool fully_consumed{false};
};

// The extras span must begin after the leading script list.
SceneVmadExtras read_scene_vmad_extras(std::span<const std::byte> data,
                                       std::int16_t object_format);

}  // namespace fo4
