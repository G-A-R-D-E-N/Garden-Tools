#pragma once

#include "fo4/forms/components.hpp"
#include "fo4/formid.hpp"
#include "fo4/plugin/container.hpp"
#include "fo4/schema/ctda.hpp"
#include "fo4/schema/localized_fields.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fo4::forms {

class UiRecordError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// COBJ/PERK/NOTE/CMPO layouts: fields.json; MISC DATA and TERM BTXT/ITXT/CNTO ordering: xEdit definitions.
// Constraint: each CTDA of a COBJ or of a TERM body text or menu item resolves its FormID slots (global, param1, param2, run-on reference) through the plugin's masters; a slot the function does not treat as a form stays empty.
struct ConditionLinks {
  std::optional<FormKey> global;
  std::optional<FormKey> param1;
  std::optional<FormKey> param2;
  std::optional<FormKey> reference;
};

struct ConstructibleObject {
  FormKey key{};
  std::optional<FormKey> workbench_keyword;
  std::optional<FormKey> created_object;
  std::vector<FormKey> category_keywords;
  std::vector<ComponentEntry> components;
  std::vector<CtdaCondition> conditions;
  std::vector<ConditionLinks> condition_links;
  std::optional<std::uint16_t> created_count;
  std::optional<std::uint16_t> priority;
  std::optional<schema::LocalizedString> description;
};

struct PerkData {
  std::uint8_t trait{0};
  std::uint8_t level{0};
  std::uint8_t num_ranks{0};
  std::uint8_t playable{0};
  std::uint8_t hidden{0};
};

struct PerkInfo {
  FormKey key{};
  std::optional<schema::LocalizedString> full_name;
  std::optional<schema::LocalizedString> description;
  std::optional<FormKey> next_perk;
  std::optional<FormKey> sound;
  std::optional<PerkData> data;
  std::optional<std::string> image;
  std::optional<std::string> swf;
};

// Constraint: value must be read only when it is set; entry_point and function are raw bytes; conditions[i] pairs with condition_links[i].
struct PerkEntryPointTab {
  std::int8_t run_on{0};
  std::vector<CtdaCondition> conditions;
  std::vector<ConditionLinks> condition_links;
};

struct PerkEntryPointEffect {
  std::uint8_t rank{0};
  std::uint8_t priority{0};
  std::uint8_t entry_point{0};
  std::uint8_t function{0};
  std::optional<std::uint8_t> parameter_type;
  std::optional<float> value;
  std::vector<PerkEntryPointTab> tabs;
  std::uint8_t function_type{0};
  std::optional<std::pair<float, float>> value_pair;
  std::optional<FormKey> form;
  std::optional<std::pair<FormKey, float>> actor_value;
  std::optional<std::string> text;
};

struct PerkEntryPointsInfo {
  FormKey key{};
  std::vector<PerkEntryPointEffect> effects;
};

struct NoteInfo {
  FormKey key{};
  std::optional<schema::LocalizedString> full_name;
  std::optional<std::uint8_t> type;
  std::optional<std::string> program_file;
  std::optional<FormKey> target;
  std::optional<std::uint32_t> value;
  std::optional<float> weight;
};

// Constraint: conditions[i] and condition_links[i] describe the same CTDA row owned by the BTXT/ITXT above it; condition_count equals conditions.size().
struct TerminalBodyText {
  std::optional<schema::LocalizedString> text;
  std::size_t condition_count{0};
  std::vector<CtdaCondition> conditions;
  std::vector<ConditionLinks> condition_links;
};

struct TerminalMenuItem {
  std::optional<schema::LocalizedString> item_text;
  std::optional<schema::LocalizedString> response_text;
  std::optional<schema::LocalizedString> display_text;
  std::optional<std::uint8_t> type;
  std::optional<std::uint16_t> item_id;
  std::optional<std::string> image;
  std::optional<FormKey> submenu;
  std::size_t condition_count{0};
  std::vector<CtdaCondition> conditions;
  std::vector<ConditionLinks> condition_links;
};

struct TerminalHolotape {
  FormKey holotape{};
  std::int32_t count{0};
};

struct TerminalInfo {
  FormKey key{};
  std::optional<schema::LocalizedString> full_name;
  std::optional<schema::LocalizedString> header_text;
  std::optional<schema::LocalizedString> welcome_text;
  std::vector<TerminalBodyText> body_texts;
  std::vector<TerminalMenuItem> menu_items;
  std::vector<TerminalHolotape> holotapes;
};

struct MiscInfo {
  FormKey key{};
  std::optional<schema::LocalizedString> full_name;
  std::optional<std::int32_t> value;
  std::optional<float> weight;
  std::vector<ComponentEntry> components;
};

struct ItemCount {
  FormKey item{};
  std::int32_t count{0};
};

// Constraint: ACTI/CONT/DOOR/FURN layouts follow xEdit wbDefinitionsFO4.pas (wbRecord ACTI, CONT, DOOR, FURN; wbCNTO, wbSNAMMarkerParams).
struct ActivatorInfo {
  FormKey key{};
  std::optional<schema::LocalizedString> full_name;
  std::optional<schema::LocalizedString> activate_text;
  std::optional<std::uint16_t> flags;
  std::optional<FormKey> interaction_keyword;
  std::optional<FormKey> looping_sound;
  std::optional<FormKey> activation_sound;
  std::optional<FormKey> water_type;
};

struct ContainerInfo {
  FormKey key{};
  std::optional<schema::LocalizedString> full_name;
  std::optional<std::uint8_t> flags;
  std::optional<float> weight;
  std::vector<ItemCount> items;
  std::optional<FormKey> open_sound;
  std::optional<FormKey> close_sound;
  std::optional<FormKey> take_all_sound;
  std::optional<FormKey> filter_list;
};

struct DoorInfo {
  FormKey key{};
  std::optional<schema::LocalizedString> full_name;
  std::optional<std::uint8_t> flags;
  std::optional<FormKey> open_sound;
  std::optional<FormKey> close_sound;
  std::optional<FormKey> loop_sound;
  std::optional<schema::LocalizedString> open_text;
  std::optional<schema::LocalizedString> close_text;
  std::vector<FormKey> random_destinations;
};

struct FurnitureMarker {
  std::int32_t index{0};
  std::optional<std::uint16_t> disabled_entry_points;
};

struct FurnitureEntryPoint {
  std::uint16_t anim_type{0};
  std::uint16_t entry_points{0};
};

struct FurnitureMarkerParams {
  std::array<float, 3> offset{};
  float rotation_z{0.0f};
  std::optional<FormKey> keyword;
  std::uint8_t entry_types{0};
};

struct FurnitureInfo {
  FormKey key{};
  std::optional<schema::LocalizedString> full_name;
  std::optional<schema::LocalizedString> activate_text;
  std::optional<std::uint16_t> flags;
  std::optional<std::uint32_t> marker_flags;
  std::optional<std::uint8_t> bench_type;
  std::optional<FormKey> drinking_water_type;
  std::optional<FormKey> associated_form;
  std::vector<ItemCount> items;
  std::vector<FurnitureMarker> markers;
  std::vector<FurnitureEntryPoint> entry_points;
  std::vector<FurnitureMarkerParams> marker_params;
  std::string marker_model;
};

struct ComponentInfo {
  FormKey key{};
  std::optional<schema::LocalizedString> full_name;
  std::optional<std::uint32_t> auto_calc_value;
  std::optional<FormKey> scrap_item;
  std::optional<FormKey> mod_scrap_scalar;
  std::optional<FormKey> crafting_sound;
};

struct BodyPartNodeData {
  float damage_mult{0.0f};
  std::uint8_t flags{0};
  std::uint8_t part_type{0};
  std::uint8_t health_percent{0};
  std::optional<FormKey> actor_value;
  std::uint8_t to_hit_chance{0};
  std::uint8_t explodable_explosion_chance{0};
  std::uint8_t non_lethal_dismemberment_chance{0};
  std::uint8_t geometry_segment_index{0};
};

// Constraint: a part must start at BPTN, or at BPNN when the open part already has a BPNN or none is open; BPNT and BPND belong to the open part.
struct BodyPart {
  std::optional<schema::LocalizedString> name;
  std::optional<std::string> part_node;
  std::optional<std::string> vats_target;
  std::optional<BodyPartNodeData> data;
};

struct BodyPartDataInfo {
  FormKey key{};
  std::vector<BodyPart> parts;
};

enum class LockKind : std::uint8_t {
  none = 0,
  novice = 1,
  advanced = 2,
  expert = 3,
  master = 4,
  barred = 5,
  chained = 6,
  requires_terminal = 7,
  inaccessible = 8,
  requires_key = 9
};

LockKind classify_lock_level(std::uint8_t level);
MiscInfo decode_misc_info(const Plugin& plugin, const Record& record);
ActivatorInfo decode_activator_info(const Plugin& plugin, const Record& record);
ContainerInfo decode_container_info(const Plugin& plugin, const Record& record);
DoorInfo decode_door_info(const Plugin& plugin, const Record& record);
FurnitureInfo decode_furniture_info(const Plugin& plugin, const Record& record);
ComponentInfo decode_component_info(const Plugin& plugin, const Record& record);
ConstructibleObject decode_constructible_object(const Plugin& plugin, const Record& record);
PerkInfo decode_perk_info(const Plugin& plugin, const Record& record);
PerkEntryPointsInfo decode_perk_entry_points(const Plugin& plugin, const Record& record);
NoteInfo decode_note_info(const Plugin& plugin, const Record& record);
TerminalInfo decode_terminal_info(const Plugin& plugin, const Record& record);
BodyPartDataInfo decode_body_part_data(const Plugin& plugin, const Record& record);

}  // namespace fo4::forms
