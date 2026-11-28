#include "fo4/plugin/batch_patch.hpp"
#include "fo4/plugin/localized_flag.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/semantic_fields.hpp"
#include "fo4/plugin/strip_masters.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"
#include "temp_dir.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using fo4::test::ByteWriter;

std::vector<std::byte> header(const std::vector<std::string>& masters = {}) {
  return fo4::test::make_tes4_plugin(0, 1.0f, masters);
}

std::vector<std::byte> misc_with_full(std::string_view full) {
  auto bytes = header();
  ByteWriter out;
  const auto group = fo4::test::begin_group(out, "MISC", 0);
  const auto record = fo4::test::begin_record(out, "MISC", 0, 0x800u);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Named\0", 6});
  fo4::test::write_subrecord(out, "FULL", full);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  bytes.insert(bytes.end(), out.bytes.begin(), out.bytes.end());
  return bytes;
}

std::vector<std::byte> weapons(std::size_t count = 3) {
  auto bytes = header();
  ByteWriter out;
  const auto group = fo4::test::begin_group(out, "WEAP", 0);
  std::uint32_t id = 0x810;
  for (const auto& [name, max_x] : {std::pair{std::string_view{"Alpha\0", 6}, std::uint16_t{10}},
                                    std::pair{std::string_view{"Beta\0", 5}, std::uint16_t{20}},
                                    std::pair{std::string_view{"Gamma\0", 6}, std::uint16_t{30}}}) {
    if (id - 0x810 >= count) break;
    const auto record = fo4::test::begin_record(out, "WEAP", 0, id++);
    fo4::test::write_subrecord(out, "EDID", name);
    ByteWriter bounds;
    for (const auto value : {std::uint16_t{0}, std::uint16_t{0}, std::uint16_t{0}, max_x, std::uint16_t{0},
                             std::uint16_t{0}}) {
      bounds.u16(value);
    }
    fo4::test::write_subrecord(out, "OBND", bounds.bytes);
    fo4::test::end_record(out, record);
  }
  fo4::test::end_group(out, group);
  bytes.insert(bytes.end(), out.bytes.begin(), out.bytes.end());
  return bytes;
}

fo4::BatchPatchHost null_host() {
  return {
      [](std::string_view) { return std::nullopt; },
      [](const fo4::FormKey&) -> std::optional<fo4::Signature> { return std::nullopt; },
      [](fo4::Plugin&, const fo4::PluginKey&) {},
  };
}

std::int16_t max_x_of(const fo4::Record& record) {
  for (const auto& sub : record.subrecords) {
    if (sub.signature == fo4::Signature::from_string("OBND")) {
      return static_cast<std::int16_t>(static_cast<std::uint16_t>(sub.data[6]) |
                                       (static_cast<std::uint16_t>(sub.data[7]) << 8));
    }
  }
  return -1;
}

}  // namespace

TEST(LocalizedFlag, InlineShapedFourBytePayloadIsInlineText) {
  const auto inline_text = fo4::read_plugin(misc_with_full(std::string_view{"Axe\0", 4}), "P.esp");
  EXPECT_EQ(fo4::check_localized_flag_change(inline_text, true).conflict, fo4::LocalizedFlagConflict::inline_text);

  const auto short_text = fo4::read_plugin(misc_with_full(std::string_view{"A\0", 2}), "P.esp");
  const auto check = fo4::check_localized_flag_change(short_text, true);
  EXPECT_EQ(check.conflict, fo4::LocalizedFlagConflict::inline_text);
  ASSERT_TRUE(check.record.has_value());
}

TEST(LocalizedFlag, StringIdPayloadPassesSetAndBlocksClear) {
  const auto id = fo4::read_plugin(misc_with_full(std::string_view{"\x05\x00\x01\x00", 4}), "P.esp");
  EXPECT_EQ(fo4::check_localized_flag_change(id, true).conflict, fo4::LocalizedFlagConflict::none);
  EXPECT_EQ(fo4::check_localized_flag_change(id, false).conflict, fo4::LocalizedFlagConflict::string_id);

  const auto text = fo4::read_plugin(misc_with_full(std::string_view{"Hello\0", 6}), "P.esp");
  EXPECT_EQ(fo4::check_localized_flag_change(text, false).conflict, fo4::LocalizedFlagConflict::none);
}

TEST(LocalizedFlag, StringsFolderPresenceIsReported) {
  fo4::test::TempDir dir;
  const auto plugin_path = dir.path / "P.esp";
  EXPECT_FALSE(fo4::strings_folder_present(plugin_path));
  std::filesystem::create_directories(dir.path / "Strings");
  std::ofstream{dir.path / "Strings" / "P_en.STRINGS", std::ios::binary} << "x";
  EXPECT_TRUE(fo4::strings_folder_present(plugin_path));
}

TEST(LocalizedFlag, EmptyOrBarePluginPathDoesNotProbeWorkingDirectory) {
  fo4::test::TempDir dir;
  std::filesystem::create_directories(dir.path / "Strings");
  const auto previous = std::filesystem::current_path();
  std::filesystem::current_path(dir.path);
  const bool bare = fo4::strings_folder_present("P.esp");
  const bool empty = fo4::strings_folder_present({});
  std::filesystem::current_path(previous);
  EXPECT_FALSE(bare);
  EXPECT_FALSE(empty);
}

TEST(LocalizedFlag, UndecodableRecordRefusesBothDirections) {
  auto bytes = header();
  fo4::test::ByteWriter out;
  const auto group = fo4::test::begin_group(out, "MISC", 0);
  const auto record = fo4::test::begin_record(out, "MISC", fo4::RecordFlags::compressed, 0x802u);
  out.u32(32);
  out.raw(std::string_view{"not-zlib-data-at-all"});
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  bytes.insert(bytes.end(), out.bytes.begin(), out.bytes.end());
  const auto plugin = fo4::read_plugin(bytes, "P.esp");

  for (const bool localized : {true, false}) {
    const auto check = fo4::check_localized_flag_change(plugin, localized);
    EXPECT_EQ(check.conflict, fo4::LocalizedFlagConflict::undecodable_record);
    ASSERT_TRUE(check.record.has_value());
  }
}

TEST(BatchPatch, PlanRejectsBadInputBeforeTouchingRecords) {
  const std::vector<fo4::BatchOperationSpec> mul{{"mul", "WEAP\\BoundMaxX", "1"}};
  const auto host = null_host();
  EXPECT_EQ(fo4::plan_batch_patch("WEAP", mul, {}, {}, host).error().code, "InvalidOperations");

  const std::vector<fo4::BatchOperationSpec> wrong_type{{"set", "AMMO\\BoundMaxX", "1"}};
  EXPECT_EQ(fo4::plan_batch_patch("WEAP", wrong_type, {}, {}, host).error().code, "BatchFieldRecordMismatch");

  const std::vector<fo4::BatchOperationSpec> ok{{"set", "WEAP\\BoundMaxX", "99"}};
  EXPECT_EQ(fo4::plan_batch_patch("WEAP", ok, "NotAField", "x", host).error().code, "FilterFieldUnknown");
  EXPECT_TRUE(fo4::plan_batch_patch("WEAP", ok, {}, {}, host).has_value());
}

TEST(BatchPatch, SelectFiltersByEditorIdAndHonoursLimit) {
  const auto source = fo4::read_plugin(weapons(), "Source.esp");
  const std::vector<fo4::BatchOperationSpec> ops{{"set", "WEAP\\BoundMaxX", "99"}};
  const auto host = null_host();

  const auto all = fo4::plan_batch_patch("WEAP", ops, {}, {}, host);
  ASSERT_TRUE(all.has_value());
  const auto everything = fo4::select_batch_records(source, *all, 10);
  EXPECT_EQ(everything.total, 3u);
  EXPECT_EQ(everything.matched.size(), 3u);
  EXPECT_EQ(fo4::select_batch_records(source, *all, 2).matched.size(), 2u);

  const auto by_id = fo4::plan_batch_patch("WEAP", ops, "EditorID", "eta", host);
  ASSERT_TRUE(by_id.has_value());
  const auto beta = fo4::select_batch_records(source, *by_id, 10);
  ASSERT_EQ(beta.matched.size(), 1u);
  EXPECT_EQ(fo4::record_editor_id(*beta.matched.front()), "Beta");
  EXPECT_EQ(beta.total, 3u);
}

TEST(BatchPatch, ApplyWritesOverridesIntoTargetAndLeavesSourceAlone) {
  const auto source = fo4::read_plugin(weapons(), "Source.esp");
  auto target = fo4::read_plugin(header(), "Patch.esp");
  const std::vector<fo4::BatchOperationSpec> ops{{"set", "WEAP\\BoundMaxX", "99"}};
  const auto host = null_host();
  const auto plan = fo4::plan_batch_patch("WEAP", ops, {}, {}, host);
  ASSERT_TRUE(plan.has_value());
  const auto selection = fo4::select_batch_records(source, *plan, 10);

  const auto result = fo4::apply_batch_patch(target, source, selection.matched, *plan, host);
  EXPECT_EQ(result.failed, 0u);
  EXPECT_EQ(result.done, 3u);
  EXPECT_EQ(result.inserted, 3u);
  EXPECT_EQ(result.operations_applied, 3u);
  EXPECT_TRUE(result.changed);
  ASSERT_TRUE(result.plugin.has_value());

  std::size_t base_weapons = 0;
  fo4::visit_records(target, [&](const fo4::Record& record) {
    if (record.header.signature == fo4::Signature::from_string("WEAP")) ++base_weapons;
  });
  EXPECT_EQ(base_weapons, 0u);

  std::size_t patched = 0;
  fo4::visit_records(*result.plugin, [&](const fo4::Record& record) {
    if (record.header.signature == fo4::Signature::from_string("WEAP")) {
      ++patched;
      EXPECT_EQ(max_x_of(record), 99);
    }
  });
  EXPECT_EQ(patched, 3u);
  fo4::visit_records(source, [&](const fo4::Record& record) { EXPECT_NE(max_x_of(record), 99); });

  const auto again = fo4::apply_batch_patch(*result.plugin, source, selection.matched, *plan, host);
  EXPECT_EQ(again.failed, 0u);
  EXPECT_FALSE(again.changed);
  EXPECT_FALSE(again.plugin.has_value());
}

TEST(BatchPatch, PartialFailureReturnsNoPlugin) {
  const auto source = fo4::read_plugin(weapons(), "Source.esp");
  const auto smaller = fo4::read_plugin(weapons(2), "Source.esp");
  const auto target = fo4::read_plugin(header(), "Patch.esp");
  const std::vector<fo4::BatchOperationSpec> ops{{"set", "WEAP\\BoundMaxX", "99"}};
  const auto host = null_host();
  const auto plan = fo4::plan_batch_patch("WEAP", ops, {}, {}, host);
  ASSERT_TRUE(plan.has_value());
  const auto selection = fo4::select_batch_records(source, *plan, 10);
  ASSERT_EQ(selection.matched.size(), 3u);

  const auto result = fo4::apply_batch_patch(target, smaller, selection.matched, *plan, host);
  EXPECT_EQ(result.failed, 1u);
  EXPECT_EQ(result.done, 2u);
  EXPECT_FALSE(result.plugin.has_value());
}

TEST(BatchPatch, PlanRequiresFourLetterTypeAndCompleteHost) {
  const std::vector<fo4::BatchOperationSpec> ops{{"set", "WEAP\\BoundMaxX", "99"}};
  const auto host = null_host();
  EXPECT_EQ(fo4::plan_batch_patch("WEA", ops, {}, {}, host).error().code, "InvalidRecordType");
  EXPECT_EQ(fo4::plan_batch_patch("WEAPX", ops, {}, {}, host).error().code, "InvalidRecordType");
  EXPECT_EQ(fo4::plan_batch_patch("", ops, {}, {}, host).error().code, "InvalidRecordType");

  auto missing = null_host();
  missing.ensure_master = nullptr;
  EXPECT_EQ(fo4::plan_batch_patch("WEAP", ops, {}, {}, missing).error().code, "BatchHostIncomplete");
  missing = null_host();
  missing.resolve_value = nullptr;
  EXPECT_EQ(fo4::plan_batch_patch("WEAP", ops, {}, {}, missing).error().code, "BatchHostIncomplete");
  missing = null_host();
  missing.winner_signature = nullptr;
  EXPECT_EQ(fo4::plan_batch_patch("WEAP", ops, {}, {}, missing).error().code, "BatchHostIncomplete");
}

TEST(BatchPatch, ApplyReportsPerRecordFailureForUnparsableValue) {
  const auto source = fo4::read_plugin(weapons(), "Source.esp");
  auto target = fo4::read_plugin(header(), "Patch.esp");
  const std::vector<fo4::BatchOperationSpec> ops{{"set", "WEAP\\BoundMaxX", "not a number"}};
  const auto host = null_host();
  const auto plan = fo4::plan_batch_patch("WEAP", ops, {}, {}, host);
  ASSERT_TRUE(plan.has_value());
  const auto selection = fo4::select_batch_records(source, *plan, 10);
  const auto result = fo4::apply_batch_patch(target, source, selection.matched, *plan, host);
  EXPECT_EQ(result.failed, 3u);
  EXPECT_FALSE(result.errors.empty());
  EXPECT_LE(result.errors.size(), fo4::k_batch_max_errors);
  EXPECT_FALSE(result.plugin.has_value());
}

TEST(SemanticFields, CellsExposeScalarValuesAndAddresses) {
  const auto source = fo4::read_plugin(weapons(), "Source.esp");
  const fo4::Record* alpha = nullptr;
  fo4::visit_records(source, [&](const fo4::Record& record) {
    if (alpha == nullptr && record.header.signature == fo4::Signature::from_string("WEAP")) alpha = &record;
  });
  ASSERT_NE(alpha, nullptr);
  const auto cells = fo4::semantic_field_cells(source, *alpha);
  ASSERT_TRUE(cells.has_value());
  const auto bound = std::find_if(cells->begin(), cells->end(), [](const auto& cell) {
    return cell.path == "WEAP\\BoundMaxX";
  });
  ASSERT_NE(bound, cells->end());
  EXPECT_EQ(bound->value, "10");
  EXPECT_TRUE(bound->available);
  EXPECT_FALSE(bound->reference);
  EXPECT_TRUE(fo4::semantic_field_address("WEAP\\BoundMaxX").has_value());
  EXPECT_FALSE(fo4::semantic_field_address("NoSuchField").has_value());
}


namespace {

constexpr std::uint16_t k_has_perk = 448;
constexpr std::uint16_t k_get_level = 80;

struct Cond {
  std::uint16_t function;
  std::uint32_t param1;
  std::uint8_t type;
  std::uint32_t compare;
};

std::vector<std::byte> recipe_with(const std::vector<Cond>& conditions) {
  auto bytes = header({"Gone.esm", "Keep.esm"});
  ByteWriter out;
  const auto group = fo4::test::begin_group(out, "COBJ", 0);
  const auto record = fo4::test::begin_record(out, "COBJ", 0, 0x02000800u);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Recipe\0", 7});
  for (const auto& cond : conditions) {
    ByteWriter body;
    body.u8(cond.type);
    body.u8(0);
    body.u8(0);
    body.u8(0);
    body.u32(cond.compare);
    body.u16(cond.function);
    body.u16(0);
    body.u32(cond.param1);
    body.u32(0);
    body.u32(0);
    body.u32(0);
    body.u32(0xFFFFFFFFu);
    fo4::test::write_subrecord(out, "CTDA", body.bytes);
  }
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  bytes.insert(bytes.end(), out.bytes.begin(), out.bytes.end());
  return bytes;
}

std::size_t ctda_rows(const fo4::Plugin& plugin) {
  std::size_t rows = 0;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    for (const auto& sub : record.subrecords) {
      if (sub.signature == fo4::Signature::from_string("CTDA")) ++rows;
    }
  });
  return rows;
}

std::filesystem::path write_source(const fo4::test::TempDir& dir, const std::vector<std::byte>& bytes) {
  const auto path = dir.path / "Source.esp";
  std::ofstream file{path, std::ios::binary};
  file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  return path;
}

const std::set<fo4::PluginKey>& strip_gone() {
  static const std::set<fo4::PluginKey> strip{fo4::PluginKey::from_filename("Gone.esm")};
  return strip;
}

}  // namespace

TEST(StripMasters, DropsParameterAndUseGlobalConditionsNamingStrippedMasterKeepsOthers) {
  fo4::test::TempDir dir;
  const auto source = write_source(dir, recipe_with({
      {k_has_perk, 0x00000801u, 0, 0x3F800000u},
      {k_get_level, 0, 0x04, 0x00000802u},
      {k_has_perk, 0x01000801u, 0, 0x3F800000u},
  }));

  const auto dry = fo4::strip_masters_clean(source, strip_gone(), std::nullopt);
  ASSERT_TRUE(dry.has_value());
  EXPECT_FALSE(dry->written);
  EXPECT_EQ(dry->dropped_conditions, 2u);
  EXPECT_EQ(dry->touched_records, 1u);
  ASSERT_EQ(dry->removed.size(), 1u);
  EXPECT_TRUE(dry->survivors.empty());

  const auto output = dir.path / "Clean.esp";
  const auto wet = fo4::strip_masters_clean(source, strip_gone(), output);
  ASSERT_TRUE(wet.has_value());
  EXPECT_TRUE(wet->written);
  const auto cleaned = fo4::read_plugin_file(output);
  ASSERT_EQ(cleaned.header.masters.size(), 1u);
  EXPECT_EQ(cleaned.header.masters.front().key, fo4::PluginKey::from_filename("Keep.esm"));
  EXPECT_EQ(ctda_rows(cleaned), 1u);
}

TEST(StripMasters, ConditionNamingOtherMasterIsKeptWhileUnusedStrippedMasterIsRemoved) {
  fo4::test::TempDir dir;
  const auto source = write_source(dir, recipe_with({
      {k_has_perk, 0x01000801u, 0, 0x3F800000u},
      {k_get_level, 0, 0x04, 0x01000802u},
  }));
  const auto output = dir.path / "Clean.esp";
  const auto result = fo4::strip_masters_clean(source, strip_gone(), output);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->dropped_conditions, 0u);
  EXPECT_EQ(result->touched_records, 0u);
  EXPECT_EQ(result->removed.size(), 1u);
  EXPECT_TRUE(result->written);
  EXPECT_EQ(ctda_rows(fo4::read_plugin_file(output)), 2u);
}

namespace {

constexpr std::uint32_t k_own_old = 0x02000800u;
constexpr std::uint32_t k_own_new = 0x01000800u;

void write_u32_sub(ByteWriter& out, std::string_view sig, std::initializer_list<std::uint32_t> words) {
  ByteWriter body;
  for (const auto word : words) body.u32(word);
  fo4::test::write_subrecord(out, sig, body.bytes);
}

struct LinkIds {
  std::uint32_t alfr;
  std::uint32_t alla;
  std::uint32_t pdto;
  std::uint32_t pldt;
  std::uint32_t ptda;
};

std::vector<std::byte> alias_package_plugin(const LinkIds& ids, std::uint32_t quest_form_id = k_own_old) {
  auto bytes = header({"Gone.esm", "Keep.esm"});
  ByteWriter out;
  {
    const auto group = fo4::test::begin_group(out, "QUST", 0);
    const auto record = fo4::test::begin_record(out, "QUST", 0, quest_form_id);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"Quest\0", 6});
    write_u32_sub(out, "ALST", {7});
    fo4::test::write_subrecord(out, "ALID", std::string_view{"Alias\0", 6});
    write_u32_sub(out, "ALFR", {ids.alfr});
    write_u32_sub(out, "ALLA", {ids.alla, 7});
    write_u32_sub(out, "ALED", {});
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
  }
  {
    const auto group = fo4::test::begin_group(out, "PACK", 0);
    const auto record = fo4::test::begin_record(out, "PACK", 0, 0x02000801u);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"Package\0", 8});
    write_u32_sub(out, "PDTO", {0, ids.pdto});
    write_u32_sub(out, "PLDT", {0, ids.pldt, 0});
    write_u32_sub(out, "PTDA", {0, ids.ptda, 0});
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
  }
  bytes.insert(bytes.end(), out.bytes.begin(), out.bytes.end());
  return bytes;
}

std::uint32_t u32_at(const fo4::Subrecord& sub, std::size_t offset) {
  if (offset > sub.data.size() || sub.data.size() - offset < 4) throw std::runtime_error("short subrecord");
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    value |= static_cast<std::uint32_t>(sub.data[offset + i]) << (8 * i);
  }
  return value;
}

const fo4::Record& record_of(const fo4::Plugin& plugin, std::string_view type) {
  const fo4::Record* found = nullptr;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    if (record.header.signature == fo4::Signature::from_string(type)) found = &record;
  });
  if (!found) throw std::runtime_error("missing record");
  return *found;
}

const fo4::Subrecord& sub_of(const fo4::Record& record, std::string_view type) {
  for (const auto& sub : record.subrecords) {
    if (sub.signature == fo4::Signature::from_string(type)) return sub;
  }
  throw std::runtime_error("missing subrecord " + std::string(type));
}

std::vector<std::string> master_names(const fo4::Plugin& plugin) {
  std::vector<std::string> names;
  for (const auto& master : plugin.header.masters) names.emplace_back(master.key.filename());
  return names;
}

}  // namespace

TEST(StripMasters, AliasSpecificReferenceNamingStrippedMasterBlocksRemoval) {
  fo4::test::TempDir dir;
  const auto source = write_source(dir, alias_package_plugin({0x00000801u, 0x01000802u, 0x01000803u, 0x01000804u, 0x01000805u}));
  const auto output = dir.path / "Clean.esp";
  const auto result = fo4::strip_masters_clean(source, strip_gone(), output);
  ASSERT_TRUE(result.has_value()) << result.error().message;
  EXPECT_TRUE(result->removed.empty());
  EXPECT_EQ(result->survivors, (std::vector<std::string>{"Gone.esm"}));
  EXPECT_EQ(result->masters_after, (std::vector<std::string>{"Gone.esm", "Keep.esm"}));
  EXPECT_FALSE(result->written);
  EXPECT_FALSE(std::filesystem::exists(output));
}

TEST(StripMasters, AliasLinkedKeywordNamingStrippedMasterBlocksRemoval) {
  fo4::test::TempDir dir;
  const auto source = write_source(dir, alias_package_plugin({0x01000801u, 0x00000802u, 0x01000803u, 0x01000804u, 0x01000805u}));
  const auto result = fo4::strip_masters_clean(source, strip_gone(), dir.path / "Clean.esp");
  ASSERT_TRUE(result.has_value()) << result.error().message;
  EXPECT_TRUE(result->removed.empty());
  EXPECT_EQ(result->survivors, (std::vector<std::string>{"Gone.esm"}));
}

TEST(StripMasters, PackageDataTopicNamingStrippedMasterBlocksRemoval) {
  fo4::test::TempDir dir;
  const auto source = write_source(dir, alias_package_plugin({0x01000801u, 0x01000802u, 0x00000803u, 0x01000804u, 0x01000805u}));
  const auto result = fo4::strip_masters_clean(source, strip_gone(), dir.path / "Clean.esp");
  ASSERT_TRUE(result.has_value()) << result.error().message;
  EXPECT_TRUE(result->removed.empty());
  EXPECT_EQ(result->survivors, (std::vector<std::string>{"Gone.esm"}));
}

TEST(StripMasters, PackageLocationNamingStrippedMasterBlocksRemoval) {
  fo4::test::TempDir dir;
  const auto source = write_source(dir, alias_package_plugin({0x01000801u, 0x01000802u, 0x01000803u, 0x00000804u, 0x01000805u}));
  const auto result = fo4::strip_masters_clean(source, strip_gone(), dir.path / "Clean.esp");
  ASSERT_TRUE(result.has_value()) << result.error().message;
  EXPECT_TRUE(result->removed.empty());
  EXPECT_EQ(result->survivors, (std::vector<std::string>{"Gone.esm"}));
}

TEST(StripMasters, PackageTargetNamingStrippedMasterBlocksRemoval) {
  fo4::test::TempDir dir;
  const auto source = write_source(dir, alias_package_plugin({0x01000801u, 0x01000802u, 0x01000803u, 0x01000804u, 0x00000805u}));
  const auto result = fo4::strip_masters_clean(source, strip_gone(), dir.path / "Clean.esp");
  ASSERT_TRUE(result.has_value()) << result.error().message;
  EXPECT_TRUE(result->removed.empty());
  EXPECT_EQ(result->survivors, (std::vector<std::string>{"Gone.esm"}));
}

TEST(StripMasters, RemapRewritesSurvivingRecordIdsAndAliasPackageLinks) {
  fo4::test::TempDir dir;
  const auto source = write_source(dir, alias_package_plugin({0x01000811u, 0x01000812u, 0x01000813u, 0x01000814u, 0x01000815u}));
  const auto output = dir.path / "Clean.esp";
  const auto result = fo4::strip_masters_clean(source, strip_gone(), output);
  ASSERT_TRUE(result.has_value()) << result.error().message;
  ASSERT_TRUE(result->written);
  EXPECT_EQ(result->removed, (std::vector<std::string>{"Gone.esm"}));

  const auto cleaned = fo4::read_plugin_file(output);
  EXPECT_EQ(master_names(cleaned), (std::vector<std::string>{"Keep.esm"}));
  const auto& quest = record_of(cleaned, "QUST");
  const auto& package = record_of(cleaned, "PACK");
  EXPECT_EQ(quest.header.form_id.value(), k_own_new);
  EXPECT_EQ(package.header.form_id.value(), 0x01000801u);
  EXPECT_EQ(u32_at(sub_of(quest, "ALFR"), 0), 0x00000811u);
  EXPECT_EQ(u32_at(sub_of(quest, "ALLA"), 0), 0x00000812u);
  EXPECT_EQ(u32_at(sub_of(quest, "ALLA"), 4), 7u);
  EXPECT_EQ(u32_at(sub_of(quest, "ALST"), 0), 7u);
  EXPECT_EQ(u32_at(sub_of(package, "PDTO"), 4), 0x00000813u);
  EXPECT_EQ(u32_at(sub_of(package, "PLDT"), 4), 0x00000814u);
  EXPECT_EQ(u32_at(sub_of(package, "PTDA"), 4), 0x00000815u);
}

TEST(StripMasters, RemapMovesLinksToOwnRecordsToTheShiftedLocalIndex) {
  fo4::test::TempDir dir;
  const auto source = write_source(dir, alias_package_plugin({0x02000801u, 0x01000812u, 0x02000800u, 0x01000814u, 0x02000801u}));
  const auto output = dir.path / "Clean.esp";
  const auto result = fo4::strip_masters_clean(source, strip_gone(), output);
  ASSERT_TRUE(result.has_value()) << result.error().message;
  ASSERT_TRUE(result->written);
  const auto cleaned = fo4::read_plugin_file(output);
  const auto& quest = record_of(cleaned, "QUST");
  const auto& package = record_of(cleaned, "PACK");
  EXPECT_EQ(u32_at(sub_of(quest, "ALFR"), 0), 0x01000801u);
  EXPECT_EQ(u32_at(sub_of(package, "PDTO"), 4), 0x01000800u);
  EXPECT_EQ(u32_at(sub_of(package, "PTDA"), 4), 0x01000801u);
}

TEST(StripMasters, RecordWithMasterIndexBeyondTheTableRefusesAndWritesNothing) {
  fo4::test::TempDir dir;
  const auto source = write_source(
      dir, alias_package_plugin({0x01000801u, 0x01000802u, 0x01000803u, 0x01000804u, 0x01000805u}, 0x07000800u));
  const auto output = dir.path / "Clean.esp";
  const auto result = fo4::strip_masters_clean(source, strip_gone(), output);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, "StripMastersFailed");
  EXPECT_NE(result.error().message.find("master index 7"), std::string::npos) << result.error().message;
  EXPECT_FALSE(std::filesystem::exists(output));
}

TEST(StripMasters, LinkIndexBeyondTableFoldsToTheOwnPluginAcrossTheRemap) {
  fo4::test::TempDir dir;
  const auto source = write_source(dir, alias_package_plugin({0x09000801u, 0x01000802u, 0x01000803u, 0x01000804u, 0x01000805u}));
  const auto output = dir.path / "Clean.esp";
  const auto result = fo4::strip_masters_clean(source, strip_gone(), output);
  ASSERT_TRUE(result.has_value()) << result.error().message;
  ASSERT_TRUE(result->written);
  EXPECT_EQ(u32_at(sub_of(record_of(fo4::read_plugin_file(output), "QUST"), "ALFR"), 0), 0x01000801u);
}
