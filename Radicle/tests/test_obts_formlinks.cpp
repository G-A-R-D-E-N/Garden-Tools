// OBTS carries OMOD include FormIDs and keyword FormIDs at offsets derived
// from counts in its own header. Without a walker they survive a master remap
// unchanged and end up naming a different plugin's records.

#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/forms/object_template.hpp"
#include "fo4/schema/form_links.hpp"

#include "plugin_bytes.hpp"
#include "fo4/plugin/record_edit.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstring>

namespace {

// One keyword and two includes, no properties.
std::vector<std::byte> obts(std::uint32_t keyword, std::uint32_t include_a,
                            std::uint32_t include_b) {
  fo4::test::ByteWriter o;
  o.u32(2);           // include count
  o.u32(0);           // property count
  o.u8(1); o.u8(0); o.u8(50); o.u8(0);   // level min / pad / level max / pad
  o.u16(0);           // addon index
  o.u8(0);            // default
  o.u8(1);            // keyword count
  o.u32(keyword);     // keyword FormID
  o.u8(0); o.u8(0);   // min level for ranks, alt levels per tier
  o.u32(include_a); o.u8(0); o.u8(0); o.u8(0);
  o.u32(include_b); o.u8(0); o.u8(0); o.u8(0);
  return o.bytes;
}

std::vector<std::byte> make_plugin() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f); hedr.u32(2); hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::ByteWriter data; data.u32(0); data.u32(0);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Alpha.esm\0", 10});
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Beta.esm\0", 9});
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::end_record(out, tes4);

  const auto grup = fo4::test::begin_group(out, "ARMO", 0);
  const auto rec = fo4::test::begin_record(out, "ARMO", 0, 0x02000800);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"A\0", 2});
  fo4::test::ByteWriter obte; obte.u32(1);
  fo4::test::write_subrecord(out, "OBTE", obte.bytes);
  // keyword owned by Alpha (index 0), includes owned by Beta (index 1).
  fo4::test::write_subrecord(out, "OBTS", obts(0x00000111, 0x01000222, 0x01000333));
  fo4::test::write_subrecord(out, "STOP", std::string_view{"", 0});
  fo4::test::end_record(out, rec);
  fo4::test::end_group(out, grup);
  return out.bytes;
}

std::vector<std::uint32_t> obts_form_ids(const fo4::Plugin& plugin) {
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse("000800:Mod.esp"));
  std::vector<std::uint32_t> out;
  for (const auto& sub : record->subrecords) {
    if (sub.signature.to_string() != "OBTS") continue;
    std::uint32_t v = 0;
    std::memcpy(&v, sub.data.data() + 0x10, 4);           // keyword
    out.push_back(v);
    std::memcpy(&v, sub.data.data() + 0x12 + 4, 4);       // include 0
    out.push_back(v);
    std::memcpy(&v, sub.data.data() + 0x12 + 4 + 7, 4);   // include 1
    out.push_back(v);
  }
  return out;
}

}  // namespace

TEST(ObtsFormLinks, TemplateFormIdsAreCollected) {
  auto plugin = fo4::read_plugin(make_plugin(), "Mod.esp");
  std::size_t keyword = 0;
  std::size_t includes = 0;
  for (const auto& hit : fo4::collect_form_links(plugin)) {
    if (hit.subrecord.to_string() != "OBTS") continue;
    if (hit.offset == 0x10) ++keyword;
    else ++includes;
  }
  EXPECT_EQ(keyword, 1u);
  EXPECT_EQ(includes, 2u);
}

TEST(ObtsFormLinks, TemplateFormIdsFollowAMasterRemap) {
  auto plugin = fo4::read_plugin(make_plugin(), "Mod.esp");
  EXPECT_EQ(obts_form_ids(plugin),
            (std::vector<std::uint32_t>{0x00000111, 0x01000222, 0x01000333}));

  // Swap the master order; Beta becomes index 0 and Alpha index 1.
  const auto remapped = fo4::remap_plugin_masters(
      plugin, std::vector<fo4::PluginKey>{fo4::PluginKey::from_filename("Beta.esm"),
                                          fo4::PluginKey::from_filename("Alpha.esm")});

  EXPECT_EQ(obts_form_ids(remapped),
            (std::vector<std::uint32_t>{0x01000111, 0x00000222, 0x00000333}))
      << "OBTS keyword/include FormIDs kept stale master indices";
}

// A body whose counts do not describe its length must fail closed rather than
// have arbitrary offsets rewritten.
TEST(ObtsFormLinks, MalformedTemplateBodyIsRefusedOnRemap) {
  auto bytes = make_plugin();
  auto plugin = fo4::read_plugin(bytes, "Mod.esp");
  for (auto& group : plugin.groups) {
    for (auto& child : group.children) {
      auto* record = std::get_if<fo4::Record>(&child);
      if (record == nullptr) continue;
      for (auto& sub : record->subrecords) {
        if (sub.signature.to_string() == "OBTS") sub.data.resize(sub.data.size() - 3);
      }
    }
  }
  EXPECT_THROW(static_cast<void>(fo4::remap_plugin_masters(
                   plugin, std::vector<fo4::PluginKey>{
                               fo4::PluginKey::from_filename("Beta.esm"),
                               fo4::PluginKey::from_filename("Alpha.esm")})),
               fo4::FormIdError);
}

// Detects: editing nested rows must preserve every unrelated byte across plugin save and reload.
TEST(ObtsFormLinks, NestedRowsSurviveEditingAndReload) {
  auto plugin = fo4::read_plugin(make_plugin(), "Mod.esp");
  const auto record_key = fo4::FormKey::parse("000800:Mod.esp");
  auto* record = fo4::detail::find_mutable_record(plugin, record_key);
  ASSERT_NE(record, nullptr);
  auto& body = record->subrecords[2].data;
  ASSERT_EQ(record->subrecords[2].signature.to_string(), "OBTS");
  body[4] = std::byte{1};
  std::array<std::byte, 24> property{};
  property[0] = std::byte{4};
  property[4] = std::byte{7};
  property[12] = std::byte{0x44};
  property[13] = std::byte{0x04};
  property[15] = std::byte{1};
  property[20] = std::byte{0x5a};
  body.insert(body.end(), property.begin(), property.end());
  fo4::detail::mark_record_modified(plugin, *record);
  const auto before = body;

  EXPECT_EQ(fo4::forms::set_object_template_keyword(plugin, record_key, 0, 0,
                                                    fo4::FormKey::parse("000555:Beta.esm")),
            fo4::forms::ObjectTemplateWrite::updated);
  EXPECT_EQ(fo4::forms::set_object_template_include(plugin, record_key, 0, 1,
                                                    fo4::FormKey::parse("000666:Alpha.esm")),
            fo4::forms::ObjectTemplateWrite::updated);
  EXPECT_EQ(fo4::forms::set_object_template_property_form(plugin, record_key, 0, 0,
                                                          fo4::FormKey::parse("000777:Alpha.esm")),
            fo4::forms::ObjectTemplateWrite::updated);
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Mod.esp");
  const auto* saved = fo4::find_record(reread, record_key);
  ASSERT_NE(saved, nullptr);
  const auto& actual = saved->subrecords[2].data;
  auto expected = before;
  const auto set_u32 = [&](std::size_t at, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) expected[at + i] = std::byte((value >> (i * 8)) & 0xff);
  };
  set_u32(16, 0x01000555);
  set_u32(29, 0x00000666);
  set_u32(48, 0x00000777);
  EXPECT_EQ(actual, expected);
}

// Detects: malformed or out-of-range nested rows must not change the plugin.
TEST(ObtsFormLinks, NestedRowFailuresPreserveTheOriginalPlugin) {
  auto plugin = fo4::read_plugin(make_plugin(), "Mod.esp");
  const auto key = fo4::FormKey::parse("000800:Mod.esp");
  const auto target = fo4::FormKey::parse("000555:Beta.esm");
  const auto original = fo4::write_plugin(plugin);
  EXPECT_EQ(fo4::forms::set_object_template_keyword(plugin, key, 0, 1, target),
            fo4::forms::ObjectTemplateWrite::index_out_of_range);
  EXPECT_EQ(fo4::forms::set_object_template_keyword(plugin, key, 0, 0, key),
            fo4::forms::ObjectTemplateWrite::invalid_value);
  EXPECT_EQ(fo4::forms::set_object_template_include(plugin, key, 0, 0, key),
            fo4::forms::ObjectTemplateWrite::invalid_value);
  EXPECT_EQ(fo4::forms::set_object_template_keyword(plugin, key, 0, 0,
      fo4::FormKey::parse("000999:Mod.esp")), fo4::forms::ObjectTemplateWrite::invalid_value);
  EXPECT_EQ(fo4::write_plugin(plugin), original);
  auto* record = fo4::detail::find_mutable_record(plugin, key);
  ASSERT_NE(record, nullptr);
  record->subrecords[2].data.pop_back();
  fo4::detail::mark_record_modified(plugin, *record);
  const auto malformed = fo4::write_plugin(plugin);
  EXPECT_EQ(fo4::forms::set_object_template_include(plugin, key, 0, 0, target),
            fo4::forms::ObjectTemplateWrite::malformed);
  EXPECT_EQ(fo4::write_plugin(plugin), malformed);
}

// Detects: ambiguous trailing markers across multiple combinations must not redirect an edit.
TEST(ObtsFormLinks, TrailingIncompleteCombinationIsRefused) {
  auto plugin = fo4::read_plugin(make_plugin(), "Mod.esp");
  const auto key = fo4::FormKey::parse("000800:Mod.esp");
  auto* record = fo4::detail::find_mutable_record(plugin, key);
  ASSERT_NE(record, nullptr);
  fo4::Subrecord orphan;
  orphan.signature = fo4::Signature::from_string("OBTF");
  record->subrecords[1].data[0] = std::byte{2};
  const auto second = record->subrecords[2];
  record->subrecords.insert(record->subrecords.end() - 1, second);
  record->subrecords.insert(record->subrecords.end() - 1, orphan);
  fo4::detail::mark_record_modified(plugin, *record);
  const auto original = fo4::write_plugin(plugin);
  EXPECT_EQ(fo4::forms::set_object_template_keyword(plugin, key, 0, 0,
      fo4::FormKey::parse("000555:Beta.esm")), fo4::forms::ObjectTemplateWrite::malformed);
  EXPECT_EQ(fo4::write_plugin(plugin), original);
}

// Detects: editing a later combination must not alter the first combination or its markers.
TEST(ObtsFormLinks, NestedEditTargetsOnlyTheSelectedCombination) {
  auto plugin = fo4::read_plugin(make_plugin(), "Mod.esp");
  const auto key = fo4::FormKey::parse("000800:Mod.esp");
  auto* record = fo4::detail::find_mutable_record(plugin, key);
  ASSERT_NE(record, nullptr);
  record->subrecords[1].data[0] = std::byte{2};
  const auto first = record->subrecords[2].data;
  fo4::Subrecord second;
  second.signature = fo4::Signature::from_string("OBTS");
  second.data = obts(0x00000444, 0x00000555, 0x00000666);
  record->subrecords.insert(record->subrecords.end() - 1, second);
  fo4::detail::mark_record_modified(plugin, *record);
  ASSERT_EQ(fo4::forms::set_object_template_keyword(plugin, key, 1, 0,
      fo4::FormKey::parse("000777:Beta.esm")), fo4::forms::ObjectTemplateWrite::updated);
  const auto saved = fo4::read_plugin(fo4::write_plugin(plugin), "Mod.esp");
  const auto* reread = fo4::find_record(saved, key);
  ASSERT_NE(reread, nullptr);
  ASSERT_EQ(reread->subrecords.size(), 5u);
  EXPECT_EQ(reread->subrecords[2].data, first);
  EXPECT_EQ(reread->subrecords[3].data[16], std::byte{0x77});
  EXPECT_EQ(reread->subrecords[3].data[17], std::byte{0x07});
  EXPECT_EQ(reread->subrecords[3].data[19], std::byte{0x01});
}

// Detects: optional leading editor and name markers must survive a nested link edit.
TEST(ObtsFormLinks, NestedEditPreservesLeadingCombinationMarkers) {
  auto plugin = fo4::read_plugin(make_plugin(), "Mod.esp");
  const auto key = fo4::FormKey::parse("000800:Mod.esp");
  auto* record = fo4::detail::find_mutable_record(plugin, key);
  ASSERT_NE(record, nullptr);
  fo4::Subrecord marker;
  marker.signature = fo4::Signature::from_string("OBTF");
  fo4::Subrecord name;
  name.signature = fo4::Signature::from_string("FULL");
  name.data = {std::byte{'A'}, std::byte{0}};
  {
    const std::array<fo4::Subrecord, 2> added{marker, name};
    record->subrecords.insert(record->subrecords.begin() + 2, added.begin(), added.end());
  }
  fo4::detail::mark_record_modified(plugin, *record);
  ASSERT_EQ(fo4::forms::set_object_template_include(plugin, key, 0, 0,
      fo4::FormKey::parse("000777:Beta.esm")), fo4::forms::ObjectTemplateWrite::updated);
  const auto saved = fo4::read_plugin(fo4::write_plugin(plugin), "Mod.esp");
  const auto* reread = fo4::find_record(saved, key);
  ASSERT_NE(reread, nullptr);
  ASSERT_EQ(reread->subrecords.size(), 6u);
  EXPECT_EQ(reread->subrecords[2].signature, marker.signature);
  EXPECT_EQ(reread->subrecords[3].signature, name.signature);
  EXPECT_EQ(reread->subrecords[3].data, name.data);
  EXPECT_EQ(reread->subrecords[4].data[22], std::byte{0x77});
  EXPECT_EQ(reread->subrecords[4].data[23], std::byte{0x07});
}

// Detects: typed reads and value edits must preserve all unrelated bytes across serialization.
TEST(ObtsFormLinks, NestedRowsReadAndEditNonReferenceValues) {
  auto plugin = fo4::read_plugin(make_plugin(), "Mod.esp");
  const auto key = fo4::FormKey::parse("000800:Mod.esp");
  auto* record = fo4::detail::find_mutable_record(plugin, key);
  ASSERT_NE(record, nullptr);
  auto& body = record->subrecords[2].data;
  body[4] = std::byte{1};
  std::array<std::byte, 24> property{};
  property[0] = std::byte{2};
  property[4] = std::byte{1};
  property[8] = std::byte{7};
  property[12] = std::byte{1};
  property[20] = std::byte{0x5a};
  body.insert(body.end(), property.begin(), property.end());
  fo4::detail::mark_record_modified(plugin, *record);
  const auto before = body;
  fo4::forms::ObjectTemplateRows rows;
  ASSERT_EQ(fo4::forms::read_object_template_rows(plugin, key, 0, rows),
            fo4::forms::ObjectTemplateWrite::updated);
  ASSERT_EQ(rows.keywords.size(), 1u);
  EXPECT_EQ(rows.keywords[0], fo4::FormKey::parse("000111:Alpha.esm"));
  ASSERT_EQ(rows.includes.size(), 2u);
  EXPECT_EQ(rows.includes[1].mod, fo4::FormKey::parse("000333:Beta.esm"));
  ASSERT_EQ(rows.properties.size(), 1u);
  EXPECT_EQ(rows.properties[0].value_type, 2u);
  EXPECT_EQ(rows.properties[0].value1, 1u);
  EXPECT_FALSE(rows.properties[0].form_value.has_value());
  EXPECT_EQ(fo4::forms::set_object_template_include_options(plugin, key, 0, 1, 9, 1, 1),
            fo4::forms::ObjectTemplateWrite::updated);
  EXPECT_EQ(fo4::forms::set_object_template_include_options(plugin, key, 0, 1, 9, 2, 1),
            fo4::forms::ObjectTemplateWrite::invalid_value);
  EXPECT_EQ(fo4::forms::set_object_template_property_value(plugin, key, 0, 0, 0, 0),
            fo4::forms::ObjectTemplateWrite::updated);
  EXPECT_EQ(fo4::forms::set_object_template_property_value(plugin, key, 0, 0, 2, 0x3f800000),
            fo4::forms::ObjectTemplateWrite::updated);
  EXPECT_EQ(fo4::forms::set_object_template_property_value(plugin, key, 0, 0, 0, 2),
            fo4::forms::ObjectTemplateWrite::invalid_value);
  const auto saved = fo4::read_plugin(fo4::write_plugin(plugin), "Mod.esp");
  const auto* reread = fo4::find_record(saved, key);
  ASSERT_NE(reread, nullptr);
  auto expected = before;
  expected[33] = std::byte{9}; expected[34] = std::byte{1}; expected[35] = std::byte{1};
  expected[48] = std::byte{0};
  expected[56] = std::byte{0}; expected[57] = std::byte{0};
  expected[58] = std::byte{0x80}; expected[59] = std::byte{0x3f};
  EXPECT_EQ(reread->subrecords[2].data, expected);
}

// Detects: an invalid nested owner must not publish a partial read or change plugin bytes.
TEST(ObtsFormLinks, NestedReadRejectsInvalidMasterWithoutPublishingRows) {
  auto plugin = fo4::read_plugin(make_plugin(), "Mod.esp");
  const auto key = fo4::FormKey::parse("000800:Mod.esp");
  auto* record = fo4::detail::find_mutable_record(plugin, key);
  ASSERT_NE(record, nullptr);
  record->subrecords[2].data[19] = std::byte{0x7f};
  fo4::detail::mark_record_modified(plugin, *record);
  const auto original = fo4::write_plugin(plugin);
  fo4::forms::ObjectTemplateRows rows;
  rows.keywords.push_back(key);
  EXPECT_EQ(fo4::forms::read_object_template_rows(plugin, key, 0, rows),
            fo4::forms::ObjectTemplateWrite::invalid_value);
  ASSERT_EQ(rows.keywords.size(), 1u);
  EXPECT_EQ(rows.keywords[0], key);
  EXPECT_EQ(fo4::write_plugin(plugin), original);
}

// Detects: cloning, moving and removing each nested row must update counts without changing unrelated bytes.
TEST(ObtsFormLinks, NestedRowCloneMoveAndRemoveRoundTrip) {
  auto plugin = fo4::read_plugin(make_plugin(), "Mod.esp");
  const auto key = fo4::FormKey::parse("000800:Mod.esp");
  auto* record = fo4::detail::find_mutable_record(plugin, key);
  ASSERT_NE(record, nullptr);
  auto& body = record->subrecords[2].data;
  body[4] = std::byte{1};
  std::array<std::byte, 24> property{};
  property[0] = std::byte{4}; property[12] = std::byte{0x55}; property[20] = std::byte{0x5a};
  body.insert(body.end(), property.begin(), property.end());
  fo4::detail::mark_record_modified(plugin, *record);
  const auto original = body;
  for (const auto kind : {fo4::forms::ObjectTemplateRowKind::keyword,
                           fo4::forms::ObjectTemplateRowKind::include,
                           fo4::forms::ObjectTemplateRowKind::property}) {
    EXPECT_EQ(fo4::forms::clone_object_template_row(plugin, key, 0, kind, 0, 1),
              fo4::forms::ObjectTemplateWrite::updated);
    fo4::forms::ObjectTemplateRows rows;
    ASSERT_EQ(fo4::forms::read_object_template_rows(plugin, key, 0, rows),
              fo4::forms::ObjectTemplateWrite::updated);
    EXPECT_EQ(rows.keywords.size(), kind == fo4::forms::ObjectTemplateRowKind::keyword ? 2u : 1u);
    EXPECT_EQ(rows.includes.size(), kind == fo4::forms::ObjectTemplateRowKind::include ? 3u : 2u);
    EXPECT_EQ(rows.properties.size(), kind == fo4::forms::ObjectTemplateRowKind::property ? 2u : 1u);
    EXPECT_EQ(fo4::forms::move_object_template_row(plugin, key, 0, kind, 1, 0),
              fo4::forms::ObjectTemplateWrite::updated);
    EXPECT_EQ(fo4::forms::remove_object_template_row(plugin, key, 0, kind, 0),
              fo4::forms::ObjectTemplateWrite::updated);
  }
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Mod.esp");
  const auto* saved = fo4::find_record(reread, key);
  ASSERT_NE(saved, nullptr);
  EXPECT_EQ(saved->subrecords[2].data, original);
}

// Detects: invalid row indices must not alter any nested section or its counts.
TEST(ObtsFormLinks, NestedRowStructuralErrorsLeavePayloadUntouched) {
  auto plugin = fo4::read_plugin(make_plugin(), "Mod.esp");
  const auto key = fo4::FormKey::parse("000800:Mod.esp");
  const auto original = fo4::write_plugin(plugin);
  using Kind = fo4::forms::ObjectTemplateRowKind;
  EXPECT_EQ(fo4::forms::clone_object_template_row(plugin, key, 0, Kind::keyword, 1, 0),
            fo4::forms::ObjectTemplateWrite::index_out_of_range);
  EXPECT_EQ(fo4::forms::move_object_template_row(plugin, key, 0, Kind::include, 0, 2),
            fo4::forms::ObjectTemplateWrite::index_out_of_range);
  EXPECT_EQ(fo4::forms::remove_object_template_row(plugin, key, 0, Kind::property, 0),
            fo4::forms::ObjectTemplateWrite::index_out_of_range);
  EXPECT_EQ(fo4::forms::remove_object_template_row(plugin, key, 0, static_cast<Kind>(256), 0),
            fo4::forms::ObjectTemplateWrite::invalid_value);
  EXPECT_EQ(fo4::write_plugin(plugin), original);
}

// Detects: a full keyword array must not wrap its one-byte count when cloned.
TEST(ObtsFormLinks, FullKeywordArrayRejectsInsertionWithoutChanges) {
  auto plugin = fo4::read_plugin(make_plugin(), "Mod.esp");
  const auto key = fo4::FormKey::parse("000800:Mod.esp");
  auto* record = fo4::detail::find_mutable_record(plugin, key);
  ASSERT_NE(record, nullptr);
  auto& body = record->subrecords[2].data;
  body[15] = std::byte{0xff};
  body.insert(body.begin() + 20, 254 * 4, std::byte{0});
  fo4::detail::mark_record_modified(plugin, *record);
  const auto original = fo4::write_plugin(plugin);
  EXPECT_EQ(fo4::forms::clone_object_template_row(plugin, key, 0,
      fo4::forms::ObjectTemplateRowKind::keyword, 0, 255), fo4::forms::ObjectTemplateWrite::invalid_value);
  EXPECT_EQ(fo4::write_plugin(plugin), original);
}

// Detects: moving different include rows must preserve their individual links and option bytes.
TEST(ObtsFormLinks, NestedMoveKeepsDistinctRowsTogether) {
  auto plugin = fo4::read_plugin(make_plugin(), "Mod.esp");
  const auto key = fo4::FormKey::parse("000800:Mod.esp");
  ASSERT_EQ(fo4::forms::set_object_template_include_options(plugin, key, 0, 1, 7, 1, 0),
            fo4::forms::ObjectTemplateWrite::updated);
  ASSERT_EQ(fo4::forms::move_object_template_row(plugin, key, 0,
      fo4::forms::ObjectTemplateRowKind::include, 1, 0), fo4::forms::ObjectTemplateWrite::updated);
  auto saved = fo4::read_plugin(fo4::write_plugin(plugin), "Mod.esp");
  fo4::forms::ObjectTemplateRows rows;
  ASSERT_EQ(fo4::forms::read_object_template_rows(saved, key, 0, rows),
            fo4::forms::ObjectTemplateWrite::updated);
  ASSERT_EQ(rows.includes.size(), 2u);
  EXPECT_EQ(rows.includes[0].mod, fo4::FormKey::parse("000333:Beta.esm"));
  EXPECT_EQ(rows.includes[0].attach_point, 7u);
  EXPECT_EQ(rows.includes[0].optional, 1u);
  EXPECT_EQ(rows.includes[1].mod, fo4::FormKey::parse("000222:Beta.esm"));
  EXPECT_EQ(rows.includes[1].attach_point, 0u);
}

// Detects: explicit rows must seed empty nested sections with valid counts and links after reload.
TEST(ObtsFormLinks, ExplicitRowsSeedEmptySectionsWithoutChangingHeader) {
  auto plugin = fo4::read_plugin(make_plugin(), "Mod.esp");
  const auto key = fo4::FormKey::parse("000800:Mod.esp");
  auto* record = fo4::detail::find_mutable_record(plugin, key);
  ASSERT_NE(record, nullptr);
  auto& body = record->subrecords[2].data;
  body = std::vector<std::byte>(18, std::byte{0});
  body[8] = std::byte{9}; body[16] = std::byte{17}; body[17] = std::byte{23};
  fo4::detail::mark_record_modified(plugin, *record);
  fo4::forms::ObjectTemplateInsertRow row{};
  row.link = fo4::FormKey::parse("000111:Alpha.esm");
  ASSERT_EQ(fo4::forms::insert_object_template_row(plugin, key, 0,
      fo4::forms::ObjectTemplateRowKind::keyword, 0, row), fo4::forms::ObjectTemplateWrite::updated);
  row.link = fo4::FormKey::parse("000222:Beta.esm");
  row.attach_point = 5; row.optional = 1; row.dont_use_all = 0;
  ASSERT_EQ(fo4::forms::insert_object_template_row(plugin, key, 0,
      fo4::forms::ObjectTemplateRowKind::include, 0, row), fo4::forms::ObjectTemplateWrite::updated);
  row.property.value_type = 0; row.property.function_type = 2; row.property.property = 7;
  row.property.value1 = 42; row.property.value2 = 99; row.property.step_bits = 0x3f800000;
  ASSERT_EQ(fo4::forms::insert_object_template_row(plugin, key, 0,
      fo4::forms::ObjectTemplateRowKind::property, 0, row), fo4::forms::ObjectTemplateWrite::updated);
  row.property.value_type = 4;
  row.property.form_value = fo4::FormKey::parse("000333:Beta.esm");
  ASSERT_EQ(fo4::forms::insert_object_template_row(plugin, key, 0,
      fo4::forms::ObjectTemplateRowKind::property, 1, row), fo4::forms::ObjectTemplateWrite::updated);
  auto saved = fo4::read_plugin(fo4::write_plugin(plugin), "Mod.esp");
  fo4::forms::ObjectTemplateRows rows;
  ASSERT_EQ(fo4::forms::read_object_template_rows(saved, key, 0, rows), fo4::forms::ObjectTemplateWrite::updated);
  ASSERT_EQ(rows.keywords.size(), 1u);
  EXPECT_EQ(rows.keywords[0], fo4::FormKey::parse("000111:Alpha.esm"));
  ASSERT_EQ(rows.includes.size(), 1u);
  EXPECT_EQ(rows.includes[0].mod, fo4::FormKey::parse("000222:Beta.esm"));
  EXPECT_EQ(rows.includes[0].attach_point, 5u);
  EXPECT_EQ(rows.includes[0].optional, 1u);
  ASSERT_EQ(rows.properties.size(), 2u);
  EXPECT_EQ(rows.properties[0].property, 7u);
  EXPECT_EQ(rows.properties[0].value1, 42u);
  EXPECT_EQ(rows.properties[0].value2, 99u);
  EXPECT_EQ(rows.properties[0].step_bits, 0x3f800000u);
  ASSERT_TRUE(rows.properties[1].form_value.has_value());
  EXPECT_EQ(*rows.properties[1].form_value, fo4::FormKey::parse("000333:Beta.esm"));
  const auto* reread = fo4::find_record(saved, key);
  ASSERT_NE(reread, nullptr);
  EXPECT_EQ(reread->subrecords[2].data[8], std::byte{9});
  EXPECT_EQ(reread->subrecords[2].data[20], std::byte{17});
  EXPECT_EQ(reread->subrecords[2].data[21], std::byte{23});
}

// Detects: malformed explicit rows must not change counts or any other plugin bytes.
TEST(ObtsFormLinks, InvalidExplicitRowsAreRefusedWithoutMutation) {
  auto plugin = fo4::read_plugin(make_plugin(), "Mod.esp");
  const auto key = fo4::FormKey::parse("000800:Mod.esp");
  const auto original = fo4::write_plugin(plugin);
  fo4::forms::ObjectTemplateInsertRow row{};
  row.link = fo4::FormKey::parse("000111:Missing.esm");
  EXPECT_EQ(fo4::forms::insert_object_template_row(plugin, key, 0,
      fo4::forms::ObjectTemplateRowKind::keyword, 0, row), fo4::forms::ObjectTemplateWrite::invalid_value);
  row.link = fo4::FormKey::parse("000222:Beta.esm"); row.optional = 2;
  EXPECT_EQ(fo4::forms::insert_object_template_row(plugin, key, 0,
      fo4::forms::ObjectTemplateRowKind::include, 0, row), fo4::forms::ObjectTemplateWrite::invalid_value);
  row.property.value_type = 2; row.property.value1 = 2;
  EXPECT_EQ(fo4::forms::insert_object_template_row(plugin, key, 0,
      fo4::forms::ObjectTemplateRowKind::property, 0, row), fo4::forms::ObjectTemplateWrite::invalid_value);
  row.property.value_type = 4; row.property.form_value.reset();
  EXPECT_EQ(fo4::forms::insert_object_template_row(plugin, key, 0,
      fo4::forms::ObjectTemplateRowKind::property, 0, row), fo4::forms::ObjectTemplateWrite::invalid_value);
  EXPECT_EQ(fo4::write_plugin(plugin), original);
}

// Detects: type-3 edits must reject writes to the unused slot and preserve existing nonzero bytes.
TEST(ObtsFormLinks, OpaqueStringPropertySlotsSurviveInsertEditAndReload) {
  auto plugin = fo4::read_plugin(make_plugin(), "Mod.esp");
  const auto key = fo4::FormKey::parse("000800:Mod.esp");
  fo4::forms::ObjectTemplateInsertRow row{};
  row.property.value_type = 3;
  row.property.function_type = 1;
  row.property.property = 7;
  row.property.value1 = 0x0042ff41;
  row.property.value2 = 0xdeadbeef;
  row.property.step_bits = 0x3f800000;
  const auto original = fo4::write_plugin(plugin);
  EXPECT_EQ(fo4::forms::insert_object_template_row(plugin, key, 0,
      fo4::forms::ObjectTemplateRowKind::property, 0, row), fo4::forms::ObjectTemplateWrite::invalid_value);
  EXPECT_EQ(fo4::write_plugin(plugin), original);
  row.property.value2 = 0;
  ASSERT_EQ(fo4::forms::insert_object_template_row(plugin, key, 0,
      fo4::forms::ObjectTemplateRowKind::property, 0, row), fo4::forms::ObjectTemplateWrite::updated);
  auto* record = fo4::detail::find_mutable_record(plugin, key);
  ASSERT_NE(record, nullptr);
  auto& body = record->subrecords[2].data;
  body[52] = std::byte{0xef}; body[53] = std::byte{0xbe};
  body[54] = std::byte{0xad}; body[55] = std::byte{0xde};
  fo4::detail::mark_record_modified(plugin, *record);
  ASSERT_EQ(fo4::forms::set_object_template_property_value(plugin, key, 0, 0, 0, 0xff004241),
            fo4::forms::ObjectTemplateWrite::updated);
  EXPECT_EQ(fo4::forms::set_object_template_property_value(plugin, key, 0, 0, 1, 0x89abcdef),
            fo4::forms::ObjectTemplateWrite::invalid_value);
  ASSERT_EQ(fo4::forms::set_object_template_property_value(plugin, key, 0, 0, 2, 0x40000000),
            fo4::forms::ObjectTemplateWrite::updated);
  auto saved = fo4::read_plugin(fo4::write_plugin(plugin), "Mod.esp");
  fo4::forms::ObjectTemplateRows rows;
  ASSERT_EQ(fo4::forms::read_object_template_rows(saved, key, 0, rows), fo4::forms::ObjectTemplateWrite::updated);
  ASSERT_EQ(rows.properties.size(), 1u);
  EXPECT_EQ(rows.properties[0].value_type, 3u);
  EXPECT_EQ(rows.properties[0].function_type, 1u);
  EXPECT_EQ(rows.properties[0].property, 7u);
  EXPECT_EQ(rows.properties[0].value1, 0xff004241u);
  EXPECT_EQ(rows.properties[0].value2, 0xdeadbeefu);
  EXPECT_EQ(rows.properties[0].step_bits, 0x40000000u);
  EXPECT_FALSE(rows.properties[0].form_value.has_value());
}

// Detects: property metadata replacement must preserve reserved bytes and reject incompatible values atomically.
TEST(ObtsFormLinks, PropertyMetadataReplacementPreservesReservedBytes) {
  auto plugin = fo4::read_plugin(make_plugin(), "Mod.esp");
  const auto key = fo4::FormKey::parse("000800:Mod.esp");
  fo4::forms::ObjectTemplateInsertRow row{};
  row.property.value_type = 3;
  row.property.value1 = 0x11223344;
  ASSERT_EQ(fo4::forms::insert_object_template_row(plugin, key, 0,
      fo4::forms::ObjectTemplateRowKind::property, 0, row), fo4::forms::ObjectTemplateWrite::updated);
  auto* record = fo4::detail::find_mutable_record(plugin, key);
  ASSERT_NE(record, nullptr);
  auto& bytes = record->subrecords[2].data;
  bytes[37] = std::byte{0xa1}; bytes[41] = std::byte{0xb2}; bytes[46] = std::byte{0xc3};
  bytes[52] = std::byte{0xef}; bytes[53] = std::byte{0xbe};
  bytes[54] = std::byte{0xad}; bytes[55] = std::byte{0xde};
  fo4::detail::mark_record_modified(plugin, *record);
  const auto original = fo4::write_plugin(plugin);
  auto invalid = row.property;
  invalid.value2 = 0;
  EXPECT_EQ(fo4::forms::set_object_template_property(plugin, key, 0, 0, invalid),
            fo4::forms::ObjectTemplateWrite::invalid_value);
  invalid.value_type = 2; invalid.value1 = 2; invalid.value2 = 0;
  EXPECT_EQ(fo4::forms::set_object_template_property(plugin, key, 0, 0, invalid),
            fo4::forms::ObjectTemplateWrite::invalid_value);
  invalid.value_type = 4; invalid.value1 = 0; invalid.form_value.reset();
  EXPECT_EQ(fo4::forms::set_object_template_property(plugin, key, 0, 0, invalid),
            fo4::forms::ObjectTemplateWrite::invalid_value);
  EXPECT_EQ(fo4::write_plugin(plugin), original);
  auto retained = row.property;
  retained.property = 9; retained.value2 = 0xdeadbeef;
  ASSERT_EQ(fo4::forms::set_object_template_property(plugin, key, 0, 0, retained),
            fo4::forms::ObjectTemplateWrite::updated);
  EXPECT_EQ(bytes[52], std::byte{0xef}); EXPECT_EQ(bytes[55], std::byte{0xde});
  auto updated = row.property;
  updated.value_type = 4; updated.function_type = 2; updated.property = 0x1234;
  updated.value2 = 0x76543210; updated.step_bits = 0x40000000;
  updated.form_value = fo4::FormKey::parse("000777:Beta.esm");
  ASSERT_EQ(fo4::forms::set_object_template_property(plugin, key, 0, 0, updated),
            fo4::forms::ObjectTemplateWrite::updated);
  const auto saved = fo4::read_plugin(fo4::write_plugin(plugin), "Mod.esp");
  const auto* reread = fo4::find_record(saved, key);
  ASSERT_NE(reread, nullptr);
  const auto& out = reread->subrecords[2].data;
  EXPECT_EQ(out[37], std::byte{0xa1}); EXPECT_EQ(out[41], std::byte{0xb2});
  EXPECT_EQ(out[46], std::byte{0xc3});
  EXPECT_EQ(out[36], std::byte{4}); EXPECT_EQ(out[40], std::byte{2});
  EXPECT_EQ(out[44], std::byte{0x34}); EXPECT_EQ(out[45], std::byte{0x12});
  EXPECT_EQ(out[48], std::byte{0x77}); EXPECT_EQ(out[49], std::byte{7});
  EXPECT_EQ(out[51], std::byte{1}); EXPECT_EQ(out[52], std::byte{0x10});
}

// Detects: rank/tier edits must follow the keyword count and preserve neighboring rows across reload.
TEST(ObtsFormLinks, RankTierFollowsKeywordResizingAndRoundTrips) {
  auto plugin = fo4::read_plugin(make_plugin(), "Mod.esp");
  const auto key = fo4::FormKey::parse("000800:Mod.esp");
  auto* record = fo4::detail::find_mutable_record(plugin, key);
  ASSERT_NE(record, nullptr);
  record->subrecords[2].data[20] = std::byte{3};
  record->subrecords[2].data[21] = std::byte{4};
  fo4::detail::mark_record_modified(plugin, *record);
  const auto original = record->subrecords[2].data;
  ASSERT_EQ(fo4::forms::clone_object_template_row(plugin, key, 0,
      fo4::forms::ObjectTemplateRowKind::keyword, 0, 1), fo4::forms::ObjectTemplateWrite::updated);
  fo4::forms::ObjectTemplateRows rows;
  ASSERT_EQ(fo4::forms::read_object_template_rows(plugin, key, 0, rows), fo4::forms::ObjectTemplateWrite::updated);
  EXPECT_EQ(rows.min_level_for_ranks, 3u);
  EXPECT_EQ(rows.alt_levels_per_tier, 4u);
  ASSERT_EQ(fo4::forms::set_object_template_rank_tier(plugin, key, 0, 7, 8),
            fo4::forms::ObjectTemplateWrite::updated);
  ASSERT_EQ(fo4::forms::remove_object_template_row(plugin, key, 0,
      fo4::forms::ObjectTemplateRowKind::keyword, 1), fo4::forms::ObjectTemplateWrite::updated);
  auto saved = fo4::read_plugin(fo4::write_plugin(plugin), "Mod.esp");
  ASSERT_EQ(fo4::forms::read_object_template_rows(saved, key, 0, rows), fo4::forms::ObjectTemplateWrite::updated);
  EXPECT_EQ(rows.min_level_for_ranks, 7u);
  EXPECT_EQ(rows.alt_levels_per_tier, 8u);
  const auto* reread = fo4::find_record(saved, key);
  ASSERT_NE(reread, nullptr);
  auto expected = original;
  expected[20] = std::byte{7}; expected[21] = std::byte{8};
  EXPECT_EQ(reread->subrecords[2].data, expected);
}

// Detects: every order of one unordered combination must preserve its name and marker during nested edits.
TEST(ObtsFormLinks, SingleCombinationAcceptsUnorderedMarkersWithoutRewritingThem) {
  std::array<int, 3> order{0, 1, 2};
  do {
    auto plugin = fo4::read_plugin(make_plugin(), "Mod.esp");
    const auto key = fo4::FormKey::parse("000800:Mod.esp");
    auto* record = fo4::detail::find_mutable_record(plugin, key);
    ASSERT_NE(record, nullptr);
    fo4::Subrecord marker;
    marker.signature = fo4::Signature::from_string("OBTF");
    fo4::Subrecord name;
    name.signature = fo4::Signature::from_string("FULL");
    name.data = {std::byte{'A'}, std::byte{0}};
    const std::array<fo4::Subrecord, 3> members{record->subrecords[2], marker, name};
    record->subrecords.erase(record->subrecords.begin() + 2);
    for (int i = 0; i < 3; ++i) record->subrecords.insert(record->subrecords.begin() + 2 + i, members[order[i]]);
    fo4::detail::mark_record_modified(plugin, *record);
    ASSERT_EQ(fo4::forms::set_object_template_keyword(plugin, key, 0, 0,
        fo4::FormKey::parse("000777:Beta.esm")), fo4::forms::ObjectTemplateWrite::updated);
    auto expected = members;
    expected[0].data[16] = std::byte{0x77}; expected[0].data[17] = std::byte{0x07};
    expected[0].data[18] = std::byte{0}; expected[0].data[19] = std::byte{1};
    const auto saved = fo4::read_plugin(fo4::write_plugin(plugin), "Mod.esp");
    const auto* reread = fo4::find_record(saved, key);
    ASSERT_NE(reread, nullptr);
    ASSERT_EQ(reread->subrecords.size(), 6u);
    for (int i = 0; i < 3; ++i) {
      EXPECT_EQ(reread->subrecords[2 + i].signature, expected[order[i]].signature);
      EXPECT_EQ(reread->subrecords[2 + i].data, expected[order[i]].data);
    }
  } while (std::next_permutation(order.begin(), order.end()));
}

// Detects: duplicate unordered members must remain rejected without changing the record.
TEST(ObtsFormLinks, SingleCombinationDuplicateMarkersRemainRejected) {
  for (const auto signature : {"OBTF", "FULL"}) {
    auto plugin = fo4::read_plugin(make_plugin(), "Mod.esp");
    const auto key = fo4::FormKey::parse("000800:Mod.esp");
    auto* record = fo4::detail::find_mutable_record(plugin, key);
    ASSERT_NE(record, nullptr);
    fo4::Subrecord marker;
    marker.signature = fo4::Signature::from_string(signature);
    if (marker.signature == fo4::Signature::from_string("FULL")) marker.data = {std::byte{'A'}, std::byte{0}};
    record->subrecords.insert(record->subrecords.begin() + 2, marker);
    record->subrecords.insert(record->subrecords.begin() + 3, marker);
    fo4::detail::mark_record_modified(plugin, *record);
    const auto before = fo4::write_plugin(plugin);
    EXPECT_EQ(fo4::forms::set_object_template_keyword(plugin, key, 0, 0,
        fo4::FormKey::parse("000777:Beta.esm")), fo4::forms::ObjectTemplateWrite::malformed) << signature;
    EXPECT_EQ(fo4::write_plugin(plugin), before) << signature;
  }
}

// Detects: a two-combination template used to answer ambiguous, so it must read and edit.
TEST(ObtsFormLinks, MarkersBetweenCombinationsLeadTheNextCombination) {
  for (const auto signature : {"OBTF", "FULL"}) {
    auto plugin = fo4::read_plugin(make_plugin(), "Mod.esp");
    const auto key = fo4::FormKey::parse("000800:Mod.esp");
    auto* record = fo4::detail::find_mutable_record(plugin, key);
    ASSERT_NE(record, nullptr);
    record->subrecords[1].data[0] = std::byte{2};
    const auto second = record->subrecords[2];
    fo4::Subrecord marker;
    marker.signature = fo4::Signature::from_string(signature);
    if (marker.signature == fo4::Signature::from_string("FULL")) marker.data = {std::byte{'A'}, std::byte{0}};
    {
    const std::array<fo4::Subrecord, 2> added{marker, second};
    record->subrecords.insert(record->subrecords.end() - 1, added.begin(), added.end());
  }
    fo4::detail::mark_record_modified(plugin, *record);

    for (std::size_t combination = 0; combination < 2; ++combination) {
      fo4::forms::ObjectTemplateRows rows;
      EXPECT_EQ(fo4::forms::read_object_template_rows(plugin, key, combination, rows),
                fo4::forms::ObjectTemplateWrite::updated) << signature << ' ' << combination;
      ASSERT_EQ(rows.keywords.size(), 1u) << signature << ' ' << combination;
      EXPECT_EQ(rows.keywords[0], fo4::FormKey::parse("000111:Alpha.esm")) << signature;
    }
    EXPECT_EQ(fo4::forms::set_object_template_keyword(plugin, key, 1, 0,
        fo4::FormKey::parse("000777:Beta.esm")), fo4::forms::ObjectTemplateWrite::updated) << signature;

    const auto* reread = fo4::find_record(plugin, key);
    ASSERT_NE(reread, nullptr);
    std::size_t seen = 0;
    for (const auto& sub : reread->subrecords) {
      if (sub.signature != fo4::Signature::from_string("OBTS")) continue;
      std::uint32_t keyword = 0;
      std::memcpy(&keyword, sub.data.data() + 0x10, 4);
      if (seen == 1) {
        EXPECT_EQ(keyword, 0x01000777u) << signature << " second combination kept the wrong keyword";
      } else {
        EXPECT_EQ(keyword, 0x00000111u) << signature << " first combination changed";
      }
      ++seen;
    }
    EXPECT_EQ(seen, 2u) << signature;
  }
}
