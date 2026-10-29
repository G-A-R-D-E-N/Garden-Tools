#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/subrecord_authoring.hpp"
#include "fo4/schema/preflight.hpp"
#include "fo4/schema/record_constraints.hpp"
#include "fo4/schema/scalar.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

fo4::Subrecord u32_subrecord(fo4::Signature signature, std::uint32_t value) {
  fo4::Subrecord subrecord;
  subrecord.signature = signature;
  subrecord.data.resize(4);
  std::memcpy(subrecord.data.data(), &value, sizeof(value));
  return subrecord;
}

fo4::Subrecord f32_subrecord(fo4::Signature signature, float value) {
  std::uint32_t bits = 0;
  static_assert(sizeof(value) == sizeof(bits));
  std::memcpy(&bits, &value, sizeof(bits));
  return u32_subrecord(signature, bits);
}

fo4::Subrecord empty_subrecord(std::string_view signature) {
  fo4::Subrecord subrecord;
  subrecord.signature = fo4::Signature::from_string(signature);
  return subrecord;
}

fo4::Record refr_record() {
  fo4::Record record;
  record.header.signature = fo4::Signature::from_string("REFR");
  record.header.form_id = fo4::RawFormId{0x00000800u};
  record.form_key = fo4::FormKey::parse("000800:Patch.esp");
  return record;
}

fo4::Record authored_record(std::string_view signature,
                            std::uint32_t local_id,
                            std::vector<std::string_view> subrecords) {
  fo4::Record record;
  record.header.signature = fo4::Signature::from_string(signature);
  record.header.form_id = fo4::RawFormId{local_id};
  record.form_key = fo4::FormKey{
      fo4::PluginKey::from_filename("Patch.esp"),
      fo4::LocalFormId{local_id}};
  for (const auto subrecord : subrecords) {
    record.subrecords.push_back(empty_subrecord(subrecord));
  }
  return record;
}

fo4::Group top_group(std::string_view signature) {
  fo4::Group group;
  group.header.group_type = static_cast<std::int32_t>(fo4::GroupType::top);
  const auto bytes = fo4::Signature::from_string(signature).bytes();
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    group.header.label[index] = std::byte{static_cast<std::uint8_t>(bytes[index])};
  }
  return group;
}

fo4::Plugin plugin_with_top_record(fo4::Record record) {
  fo4::Plugin plugin;
  plugin.key = fo4::PluginKey::from_filename("Patch.esp");
  auto group = top_group(record.header.signature.to_string());
  group.children.emplace_back(std::move(record));
  plugin.groups.push_back(std::move(group));
  return plugin;
}

fo4::Plugin cell_under_parent_chain(std::string_view top_label,
                                    std::vector<fo4::GroupType> chain) {
  fo4::Group current;
  current.header.group_type = static_cast<std::int32_t>(chain.back());
  current.children.emplace_back(authored_record("CELL", 0x800, {"EDID", "DATA"}));
  for (auto type = chain.rbegin() + 1; type != chain.rend(); ++type) {
    fo4::Group wrapper;
    wrapper.header.group_type = static_cast<std::int32_t>(*type);
    wrapper.children.emplace_back(std::move(current));
    current = std::move(wrapper);
  }
  fo4::Plugin plugin;
  plugin.key = fo4::PluginKey::from_filename("Patch.esp");
  auto outer = top_group(top_label);
  outer.children.emplace_back(std::move(current));
  plugin.groups.push_back(std::move(outer));
  return plugin;
}

bool has_issue(const std::vector<fo4::schema::PreflightIssue>& issues,
               std::string_view code) {
  for (const auto& issue : issues) {
    if (issue.code == code) return true;
  }
  return false;
}

fo4::Subrecord sized_subrecord(std::string_view signature, std::size_t size, std::uint8_t fill) {
  fo4::Subrecord subrecord;
  subrecord.signature = fo4::Signature::from_string(signature);
  subrecord.data.assign(size, std::byte{fill});
  return subrecord;
}

fo4::Subrecord leveled_entry(std::uint32_t target) {
  fo4::Subrecord subrecord;
  subrecord.signature = fo4::Signature::from_string("LVLO");
  subrecord.data.assign(12, std::byte{0});
  subrecord.data[0] = std::byte{1};
  std::memcpy(subrecord.data.data() + 4, &target, sizeof(target));
  subrecord.data[8] = std::byte{1};
  return subrecord;
}

std::vector<std::string> record_order(const fo4::Plugin& plugin, const fo4::FormKey& key) {
  const auto* record = fo4::find_record(plugin, key);
  if (record == nullptr) return {};
  std::vector<std::string> out;
  for (const auto& subrecord : record->subrecords) out.push_back(subrecord.signature.to_string());
  return out;
}

}

TEST(ScalarPreflight, ValidGeneratedScalarsPass) {
  auto record = refr_record();
  record.subrecords.push_back(
      u32_subrecord(fo4::Signature::from_string("XHLT"), 50));
  record.subrecords.push_back(
      f32_subrecord(fo4::Signature::from_string("XSCL"), 1.25f));

  EXPECT_TRUE(fo4::schema::preflight_record(record).empty());
}

TEST(ScalarPreflight, MissingExistingOnlyScalarsAreLegal) {
  const auto record = refr_record();
  EXPECT_TRUE(fo4::schema::preflight_record(record).empty());
}

TEST(ScalarPreflight, DuplicateScalarStorageIsAnError) {
  auto record = refr_record();
  record.subrecords.push_back(
      u32_subrecord(fo4::Signature::from_string("XHLT"), 50));
  record.subrecords.push_back(
      u32_subrecord(fo4::Signature::from_string("XHLT"), 75));

  const auto issues = fo4::schema::preflight_record(record);
  ASSERT_EQ(issues.size(), 1u);
  EXPECT_EQ(issues[0].record, record.form_key);
  EXPECT_EQ(issues[0].path, "REFR\\HealthPercent");
  EXPECT_EQ(issues[0].code, "duplicate_scalar_subrecord");
  EXPECT_EQ(issues[0].severity, fo4::schema::PreflightSeverity::error);
}

TEST(ScalarPreflight, MalformedScalarWidthIsAnError) {
  auto record = refr_record();
  fo4::Subrecord scale;
  scale.signature = fo4::Signature::from_string("XSCL");
  scale.data = {std::byte{0}, std::byte{0}};
  record.subrecords.push_back(std::move(scale));

  const auto issues = fo4::schema::preflight_record(record);
  ASSERT_EQ(issues.size(), 1u);
  EXPECT_EQ(issues[0].path, "REFR\\Scale");
  EXPECT_EQ(issues[0].code, "invalid_scalar_storage");
  EXPECT_EQ(issues[0].severity, fo4::schema::PreflightSeverity::error);
}

TEST(ScalarPreflight, LegacyOutOfRangeValuesAreWarningsNotReadFailures) {
  auto record = refr_record();
  record.subrecords.push_back(
      u32_subrecord(fo4::Signature::from_string("XHLT"), 150));
  record.subrecords.push_back(
      f32_subrecord(fo4::Signature::from_string("XSCL"), 12.0f));

  const auto issues = fo4::schema::preflight_record(record);
  ASSERT_EQ(issues.size(), 2u);
  EXPECT_EQ(issues[0].code, "scalar_value_out_of_range");
  EXPECT_EQ(issues[0].severity, fo4::schema::PreflightSeverity::warning);
  EXPECT_EQ(issues[1].code, "scalar_value_out_of_range");
  EXPECT_EQ(issues[1].severity, fo4::schema::PreflightSeverity::warning);
}

TEST(ScalarPreflight, EnumerationValueWithNoGeneratedNameIsAWarning) {
  fo4::Record record;
  record.header.signature = fo4::Signature::from_string("NOTE");
  record.form_key = fo4::FormKey::parse("000800:Patch.esp");
  fo4::Subrecord type;
  type.signature = fo4::Signature::from_string("DNAM");
  type.data = {std::byte{4}};
  record.subrecords.push_back(std::move(type));

  const auto issues = fo4::schema::preflight_record(record);
  ASSERT_EQ(issues.size(), 1u);
  EXPECT_EQ(issues[0].path, "NOTE\\Type");
  EXPECT_EQ(issues[0].code, "scalar_enum_value_unnamed");
  EXPECT_EQ(issues[0].severity, fo4::schema::PreflightSeverity::warning);
  EXPECT_NE(issues[0].message.find("<Unknown: 4>"), std::string::npos);

  record.subrecords[0].data = {std::byte{3}};
  EXPECT_TRUE(fo4::schema::preflight_record(record).empty());
}

TEST(ScalarPreflight, FlagValueWithUnnamedBitsIsAWarning) {
  fo4::Record record;
  record.header.signature = fo4::Signature::from_string("CLFM");
  record.form_key = fo4::FormKey::parse("000800:Patch.esp");
  record.subrecords.push_back(
      u32_subrecord(fo4::Signature::from_string("FNAM"), 8u));

  const auto issues = fo4::schema::preflight_record(record);
  ASSERT_EQ(issues.size(), 1u);
  EXPECT_EQ(issues[0].path, "CLFM\\Flags");
  EXPECT_EQ(issues[0].code, "scalar_flag_bits_unnamed");
  EXPECT_EQ(issues[0].severity, fo4::schema::PreflightSeverity::warning);
  EXPECT_NE(issues[0].message.find("<Unknown: 3>"), std::string::npos);

  record.subrecords[0].data = {std::byte{4}, std::byte{0}, std::byte{0}, std::byte{0}};
  EXPECT_TRUE(fo4::schema::preflight_record(record).empty());
  record.subrecords[0].data = {std::byte{0x81}, std::byte{0}, std::byte{0}, std::byte{0}};
  const auto two_named = fo4::schema::preflight_record(record);
  ASSERT_EQ(two_named.size(), 1u);
  EXPECT_NE(two_named[0].message.find("<Unknown: 7>"), std::string::npos);
}

TEST(AuthoringPreflight, GeneratedEyesShapePasses) {
  auto plugin = plugin_with_top_record(authored_record(
      "EYES", 0x800, {"EDID", "FULL", "ICON", "DATA"}));
  const auto key = fo4::FormKey::parse("000800:Patch.esp");
  EXPECT_TRUE(fo4::schema::preflight_authoring_record(plugin, key).empty());

  const auto constraint =
      fo4::schema::record_authoring_constraint(fo4::Signature::from_string("EYES"));
  ASSERT_TRUE(constraint.has_value());
  EXPECT_EQ(fo4::schema::authoring_subrecords(*constraint).size(), 4u);
}

TEST(AuthoringPreflight, MissingRequiredEyesSubrecordFails) {
  auto plugin = plugin_with_top_record(authored_record(
      "EYES", 0x800, {"FULL", "ICON"}));
  const auto issues = fo4::schema::preflight_authoring_record(
      plugin, fo4::FormKey::parse("000800:Patch.esp"));
  EXPECT_TRUE(has_issue(issues, "required_subrecord_missing"));
}

TEST(AuthoringPreflight, EyesSubrecordOrderIsValidated) {
  auto plugin = plugin_with_top_record(authored_record(
      "EYES", 0x800, {"ICON", "FULL", "DATA"}));
  const auto issues = fo4::schema::preflight_authoring_record(
      plugin, fo4::FormKey::parse("000800:Patch.esp"));
  EXPECT_TRUE(has_issue(issues, "subrecord_order_invalid"));
}

TEST(AuthoringPreflight, NestedEyesPlacementFails) {
  fo4::Plugin plugin;
  plugin.key = fo4::PluginKey::from_filename("Patch.esp");
  auto outer = top_group("EYES");
  fo4::Group nested;
  nested.header.group_type = static_cast<std::int32_t>(fo4::GroupType::cell_children);
  nested.children.emplace_back(authored_record(
      "EYES", 0x800, {"FULL", "ICON", "DATA"}));
  outer.children.emplace_back(std::move(nested));
  plugin.groups.push_back(std::move(outer));

  const auto issues = fo4::schema::preflight_authoring_record(
      plugin, fo4::FormKey::parse("000800:Patch.esp"));
  EXPECT_TRUE(has_issue(issues, "record_placement_invalid"));
}

TEST(AuthoringPreflight, CellReferenceVariantAcceptsPersistentAndTemporaryChildren) {
  for (const auto type : {fo4::GroupType::cell_persistent_children, fo4::GroupType::cell_temporary_children}) {
    fo4::Plugin plugin;
    plugin.key = fo4::PluginKey::from_filename("Patch.esp");
    auto outer = top_group("CELL");
    fo4::Group children;
    children.header.group_type = static_cast<std::int32_t>(fo4::GroupType::cell_children);
    fo4::Group nested;
    nested.header.group_type = static_cast<std::int32_t>(type);
    nested.children.emplace_back(authored_record("PHZD", 0x800, {}));
    children.children.emplace_back(std::move(nested));
    outer.children.emplace_back(std::move(children));
    plugin.groups.push_back(std::move(outer));

    const auto issues = fo4::schema::preflight_authoring_record(
        plugin, fo4::FormKey::parse("000800:Patch.esp"));
    EXPECT_FALSE(has_issue(issues, "record_placement_invalid")) << static_cast<int>(type);
  }
}

TEST(AuthoringPreflight, CellReferenceVariantAtTopLevelFails) {
  auto plugin = plugin_with_top_record(authored_record("PHZD", 0x800, {}));
  const auto issues = fo4::schema::preflight_authoring_record(
      plugin, fo4::FormKey::parse("000800:Patch.esp"));
  EXPECT_TRUE(has_issue(issues, "record_placement_invalid"));
}

// Invariant: a created interior CELL (top CELL GRUP -> block 2 -> subblock 3) must not be reported as mis-placed.
TEST(AuthoringPreflight, CellPlacementAcceptsInteriorAndExteriorCellBlocks) {
  const auto key = fo4::FormKey::parse("000800:Patch.esp");
  const auto constraint =
      fo4::schema::record_authoring_constraint(fo4::Signature::from_string("CELL"));
  ASSERT_TRUE(constraint.has_value());
  EXPECT_EQ(constraint->placement, fo4::schema::RecordPlacement::cell_block);

  const auto interior = cell_under_parent_chain(
      "CELL", {fo4::GroupType::interior_cell_block, fo4::GroupType::interior_cell_subblock});
  EXPECT_FALSE(has_issue(fo4::schema::preflight_authoring_record(interior, key),
                         "record_placement_invalid"));

  const auto block_only =
      cell_under_parent_chain("CELL", {fo4::GroupType::interior_cell_block});
  EXPECT_FALSE(has_issue(fo4::schema::preflight_authoring_record(block_only, key),
                         "record_placement_invalid"));

  const auto exterior = cell_under_parent_chain(
      "WRLD", {fo4::GroupType::world_children, fo4::GroupType::exterior_cell_block,
               fo4::GroupType::exterior_cell_subblock});
  EXPECT_FALSE(has_issue(fo4::schema::preflight_authoring_record(exterior, key),
                         "record_placement_invalid"));

  auto top_level = plugin_with_top_record(authored_record("CELL", 0x800, {"EDID", "DATA"}));
  EXPECT_FALSE(has_issue(fo4::schema::preflight_authoring_record(top_level, key),
                         "record_placement_invalid"));

  const auto world_top_cell =
      cell_under_parent_chain("WRLD", {fo4::GroupType::world_children});
  EXPECT_FALSE(has_issue(fo4::schema::preflight_authoring_record(world_top_cell, key),
                         "record_placement_invalid"));

  const auto cell_children =
      cell_under_parent_chain("CELL", {fo4::GroupType::cell_children});
  EXPECT_TRUE(has_issue(fo4::schema::preflight_authoring_record(cell_children, key),
                        "record_placement_invalid"));

  const auto persistent_children =
      cell_under_parent_chain("CELL", {fo4::GroupType::cell_persistent_children});
  EXPECT_TRUE(has_issue(fo4::schema::preflight_authoring_record(persistent_children, key),
                        "record_placement_invalid"));

  const auto temporary_children =
      cell_under_parent_chain("CELL", {fo4::GroupType::cell_temporary_children});
  EXPECT_TRUE(has_issue(fo4::schema::preflight_authoring_record(temporary_children, key),
                        "record_placement_invalid"));

  fo4::Plugin mislabeled_top;
  mislabeled_top.key = fo4::PluginKey::from_filename("Patch.esp");
  auto wrong_top = top_group("WRLD");
  wrong_top.children.emplace_back(authored_record("CELL", 0x800, {"EDID", "DATA"}));
  mislabeled_top.groups.push_back(std::move(wrong_top));
  EXPECT_TRUE(has_issue(fo4::schema::preflight_authoring_record(mislabeled_top, key),
                        "record_placement_invalid"));
}

fo4::Plugin refr_in_cell(fo4::Record record,
                         fo4::GroupType bucket = fo4::GroupType::cell_persistent_children) {
  fo4::Plugin plugin;
  plugin.key = fo4::PluginKey::from_filename("Patch.esp");
  auto outer = top_group("CELL");
  fo4::Group children;
  children.header.group_type = static_cast<std::int32_t>(fo4::GroupType::cell_children);
  fo4::Group nested;
  nested.header.group_type = static_cast<std::int32_t>(bucket);
  nested.children.emplace_back(std::move(record));
  children.children.emplace_back(std::move(nested));
  outer.children.emplace_back(std::move(children));
  plugin.groups.push_back(std::move(outer));
  return plugin;
}

fo4::Record refr_with_data(std::vector<std::string_view> subrecords) {
  auto record = authored_record("REFR", 0x800, subrecords);
  for (auto& subrecord : record.subrecords) {
    if (subrecord.signature == fo4::Signature::from_string("NAME")) {
      subrecord.data.resize(4);
    }
    if (subrecord.signature == fo4::Signature::from_string("DATA")) {
      subrecord.data.resize(24);
    }
    if (subrecord.signature == fo4::Signature::from_string("XSCL")) {
      subrecord.data.resize(4);
    }
  }
  return record;
}

TEST(AuthoringPreflight, RefrAuthoringBlockListsTheSingletonsInXEditOrder) {
  const auto constraint =
      fo4::schema::record_authoring_constraint(fo4::Signature::from_string("REFR"));
  ASSERT_TRUE(constraint.has_value());
  EXPECT_EQ(constraint->placement, fo4::schema::RecordPlacement::cell_reference_children);
  const auto rules = fo4::schema::authoring_subrecords(*constraint);
  ASSERT_EQ(rules.size(), 57u);
  EXPECT_EQ(rules.front().signature, fo4::Signature::from_string("EDID"));
  EXPECT_EQ(rules.back().signature, fo4::Signature::from_string("MNAM"));
  for (const auto& rule : rules) {
    if (rule.signature != fo4::Signature::from_string("DATA")) {
      EXPECT_EQ(rule.maximum, 1u) << rule.signature.to_string();
      EXPECT_EQ(rule.minimum, 0u) << rule.signature.to_string();
      continue;
    }
    EXPECT_EQ(rule.minimum, 1u);
    ASSERT_TRUE(rule.has_default());
    EXPECT_EQ(rule.default_bytes().size(), 24u);
  }
}

TEST(AuthoringPreflight, RefrOrderedReferencePassesAndMisorderedReferenceFails) {
  const auto key = fo4::FormKey::parse("000800:Patch.esp");
  const auto ordered = refr_in_cell(refr_with_data({"NAME", "XSCL", "DATA"}));
  EXPECT_TRUE(fo4::schema::preflight_authoring_record(ordered, key).empty());

  const auto misordered = refr_in_cell(refr_with_data({"DATA", "NAME"}));
  EXPECT_TRUE(has_issue(fo4::schema::preflight_authoring_record(misordered, key),
                        "subrecord_order_invalid"));

  const auto top_level = plugin_with_top_record(
      refr_with_data({"NAME", "DATA"}));
  EXPECT_TRUE(has_issue(fo4::schema::preflight_authoring_record(top_level, key),
                        "record_placement_invalid"));
}

TEST(AuthoringPreflight, VisibleDistantChildrenAcceptReferencesOnly) {
  const auto key = fo4::FormKey::parse("000800:Patch.esp");
  const auto refr = refr_in_cell(refr_with_data({"NAME", "XSCL", "DATA"}),
                                 fo4::GroupType::cell_visible_distant_children);
  EXPECT_TRUE(fo4::schema::preflight_authoring_record(refr, key).empty());

  const auto phzd = refr_in_cell(authored_record("PHZD", 0x800, {}),
                                 fo4::GroupType::cell_visible_distant_children);
  EXPECT_TRUE(has_issue(fo4::schema::preflight_authoring_record(phzd, key),
                        "record_placement_invalid"));
}

TEST(AuthoringPreflight, GeneratedLvspCreationShapePassesWithoutOptionalFields) {
  auto record = authored_record("LVSP", 0x800, {"OBND", "LVLD", "LVLM", "LVLF", "LLCT"});
  record.subrecords.front().data.resize(12);
  for (std::size_t i = 1; i <= 3; ++i) record.subrecords[i].data.resize(1);
  auto plugin = plugin_with_top_record(record);
  const auto key = fo4::FormKey::parse("000800:Patch.esp");
  EXPECT_TRUE(fo4::schema::preflight_authoring_record(plugin, key).empty());

  const auto constraint =
      fo4::schema::record_authoring_constraint(fo4::Signature::from_string("LVSP"));
  ASSERT_TRUE(constraint.has_value());
  EXPECT_EQ(fo4::schema::authoring_subrecords(*constraint).size(), 7u);
}

TEST(AuthoringPreflight, PreExistingCountMismatchToleratesUnrelatedAdds) {
  auto clean = authored_record("LVLI", 0x800, {});
  clean.subrecords.push_back(sized_subrecord("OBND", 12, 0));
  clean.subrecords.push_back(sized_subrecord("LVLD", 1, 0));
  clean.subrecords.push_back(sized_subrecord("LVLF", 1, 0));
  clean.subrecords.push_back(sized_subrecord("LLCT", 1, 1));
  clean.subrecords.push_back(leveled_entry(0x800));
  auto clean_plugin = plugin_with_top_record(std::move(clean));
  const auto clean_key = fo4::FormKey::parse("000800:Patch.esp");
  EXPECT_FALSE(has_issue(fo4::schema::preflight_record(*fo4::find_record(clean_plugin, clean_key)),
                         "leveled_list_count_mismatch"));
  EXPECT_EQ(fo4::add_record_subrecord(clean_plugin, clean_key, fo4::Signature::from_string("LVLM")),
            fo4::SubrecordAddStatus::added);
  // Requires: coupled counters stay non-editable, so introducing a duplicate LLCT is refused.
  EXPECT_EQ(fo4::add_record_subrecord(clean_plugin, clean_key, fo4::Signature::from_string("LLCT")),
            fo4::SubrecordAddStatus::not_authorable);
  auto record = authored_record("LVLI", 0x801, {});
  record.subrecords.push_back(sized_subrecord("OBND", 12, 0));
  record.subrecords.push_back(sized_subrecord("LVLD", 1, 0));
  record.subrecords.push_back(sized_subrecord("LVLF", 1, 0));
  record.subrecords.push_back(sized_subrecord("LLCT", 1, 2));
  record.subrecords.push_back(leveled_entry(0x800));
  auto plugin = plugin_with_top_record(std::move(record));
  const auto key = fo4::FormKey::parse("000801:Patch.esp");
  EXPECT_TRUE(has_issue(fo4::schema::preflight_record(*fo4::find_record(plugin, key)),
                        "leveled_list_count_mismatch"));
  EXPECT_EQ(fo4::add_record_subrecord(plugin, key, fo4::Signature::from_string("LVLM")),
            fo4::SubrecordAddStatus::added);
  EXPECT_EQ(record_order(plugin, key),
            (std::vector<std::string>{"OBND", "LVLD", "LVLM", "LVLF", "LLCT", "LVLO"}));
  EXPECT_TRUE(has_issue(fo4::schema::preflight_record(*fo4::find_record(plugin, key)),
                        "leveled_list_count_mismatch"));
  EXPECT_EQ(fo4::add_record_subrecord(plugin, key, fo4::Signature::from_string("LLCT")),
            fo4::SubrecordAddStatus::not_authorable);
  auto lvsp = authored_record("LVSP", 0x802, {});
  lvsp.subrecords.push_back(sized_subrecord("OBND", 12, 0));
  lvsp.subrecords.push_back(sized_subrecord("LVLD", 1, 0));
  lvsp.subrecords.push_back(sized_subrecord("LVLM", 1, 0));
  lvsp.subrecords.push_back(sized_subrecord("LVLF", 1, 0));
  lvsp.subrecords.push_back(sized_subrecord("LLCT", 1, 2));
  lvsp.subrecords.push_back(leveled_entry(0x800));
  auto lvsp_plugin = plugin_with_top_record(std::move(lvsp));
  const auto lvsp_key = fo4::FormKey::parse("000802:Patch.esp");
  EXPECT_TRUE(has_issue(fo4::schema::preflight_record(*fo4::find_record(lvsp_plugin, lvsp_key)),
                        "leveled_list_count_mismatch"));
  // Requires: LVSP LLCT keeps its minimum-1 creation default, so the add stays already_present.
  EXPECT_EQ(fo4::add_record_subrecord(lvsp_plugin, lvsp_key, fo4::Signature::from_string("LLCT")),
            fo4::SubrecordAddStatus::already_present);
  EXPECT_TRUE(has_issue(fo4::schema::preflight_record(*fo4::find_record(lvsp_plugin, lvsp_key)),
                        "leveled_list_count_mismatch"));
}

TEST(AuthoringPreflight, PairCountsWorsenOnlyWhenAnEditMovesThemApart) {
  const auto signature = fo4::Signature::from_string("HDPT");
  const auto constraint = fo4::schema::record_authoring_constraint(signature);
  ASSERT_TRUE(constraint.has_value());
  const auto rules = fo4::schema::authoring_subrecords(*constraint);
  std::size_t nam0 = rules.size();
  std::size_t nam1 = rules.size();
  for (std::size_t index = 0; index < rules.size(); ++index) {
    if (rules[index].signature == fo4::Signature::from_string("NAM0")) nam0 = index;
    if (rules[index].signature == fo4::Signature::from_string("NAM1")) nam1 = index;
  }
  ASSERT_LT(nam0, rules.size());
  ASSERT_LT(nam1, rules.size());

  const std::vector<std::size_t> balanced(rules.size(), 0);
  auto added = balanced;
  ++added[nam0];
  EXPECT_TRUE(fo4::schema::worsened_subrecord_pair(signature, rules, balanced, added).has_value());

  std::vector<std::size_t> imbalanced(rules.size(), 0);
  imbalanced[nam0] = 2;
  auto paired = imbalanced;
  ++paired[nam1];
  EXPECT_FALSE(fo4::schema::worsened_subrecord_pair(signature, rules, imbalanced, paired).has_value());
  auto wider = imbalanced;
  ++wider[nam0];
  EXPECT_TRUE(fo4::schema::worsened_subrecord_pair(signature, rules, imbalanced, wider).has_value());

  auto unrelated = imbalanced;
  ++unrelated[0];
  EXPECT_FALSE(fo4::schema::worsened_subrecord_pair(signature, rules, imbalanced, unrelated).has_value());
}

TEST(ListPreflight, PackedKeywordArrayIsChecked4BytesPerElement) {
  fo4::Record record;
  record.header.signature = fo4::Signature::from_string("MISC");
  record.form_key = fo4::FormKey::parse("000800:Patch.esp");
  record.subrecords.push_back(u32_subrecord(fo4::Signature::from_string("KSIZ"), 2));
  fo4::Subrecord kwda;
  kwda.signature = fo4::Signature::from_string("KWDA");
  kwda.data.resize(8);
  record.subrecords.push_back(kwda);
  for (const auto& issue : fo4::schema::preflight_record(record)) {
    EXPECT_NE(issue.code, "invalid_formlink_list_storage") << issue.message;
  }

  record.subrecords.back().data.resize(6);
  bool flagged = false;
  for (const auto& issue : fo4::schema::preflight_record(record)) {
    flagged = flagged || issue.code == "invalid_formlink_list_storage";
  }
  EXPECT_TRUE(flagged);
}

TEST(ClimatePreflight, SunWindowSpanningA24HourDayIsFlaggedWithoutReordering) {
  const auto issues_for = [](std::vector<std::uint8_t> bytes) {
    fo4::Record record;
    record.header.signature = fo4::Signature::from_string("CLMT");
    record.form_key = fo4::FormKey::parse("000800:Patch.esp");
    fo4::Subrecord timing;
    timing.signature = fo4::Signature::from_string("TNAM");
    for (const auto byte : bytes) timing.data.push_back(std::byte{byte});
    record.subrecords.push_back(std::move(timing));
    return fo4::schema::preflight_record(record);
  };
  const auto sunrise = issues_for({0, 144, 10, 20, 0, 0});
  ASSERT_EQ(sunrise.size(), 1u);
  EXPECT_EQ(sunrise[0].code, "climate_sun_window_full_day");
  EXPECT_EQ(sunrise[0].path, "CLMT\\TNAM");
  EXPECT_EQ(sunrise[0].severity, fo4::schema::PreflightSeverity::warning);
  EXPECT_EQ(issues_for({0, 1, 10, 154, 0, 0}).size(), 1u);
  EXPECT_TRUE(issues_for({0, 143, 10, 20, 0, 0}).empty());
}

TEST(ClimatePreflight, ReversedOrZeroWidthSunWindowNeverOpensIsFlagged) {
  const auto issues_for = [](std::vector<std::uint8_t> bytes) {
    fo4::Record record;
    record.header.signature = fo4::Signature::from_string("CLMT");
    record.form_key = fo4::FormKey::parse("000800:Patch.esp");
    fo4::Subrecord timing;
    timing.signature = fo4::Signature::from_string("TNAM");
    for (const auto byte : bytes) timing.data.push_back(std::byte{byte});
    record.subrecords.push_back(std::move(timing));
    return fo4::schema::preflight_record(record);
  };
  const auto reversed_sunrise = issues_for({60, 10, 20, 30, 0, 0});
  ASSERT_EQ(reversed_sunrise.size(), 1u);
  EXPECT_EQ(reversed_sunrise[0].code, "climate_sun_window_never_opens");
  EXPECT_EQ(reversed_sunrise[0].path, "CLMT\\TNAM");
  EXPECT_EQ(reversed_sunrise[0].severity, fo4::schema::PreflightSeverity::warning);
  const auto zero_width_sunset = issues_for({10, 20, 40, 40, 0, 0});
  ASSERT_EQ(zero_width_sunset.size(), 1u);
  EXPECT_EQ(zero_width_sunset[0].code, "climate_sun_window_never_opens");
  EXPECT_TRUE(issues_for({10, 20, 40, 50, 0, 0}).empty());
}
