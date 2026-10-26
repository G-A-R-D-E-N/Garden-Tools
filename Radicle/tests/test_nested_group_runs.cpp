#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr auto k_innr = fo4::Signature::from_string("INNR");
constexpr auto k_vnam = fo4::Signature::from_string("VNAM");
constexpr auto k_wnam = fo4::Signature::from_string("WNAM");
constexpr auto k_ksiz = fo4::Signature::from_string("KSIZ");
constexpr auto k_kwda = fo4::Signature::from_string("KWDA");
constexpr auto k_ynam = fo4::Signature::from_string("YNAM");
constexpr auto k_nocm = fo4::Signature::from_string("NOCM");
constexpr auto k_indx = fo4::Signature::from_string("INDX");
constexpr auto k_data = fo4::Signature::from_string("DATA");
constexpr auto k_intv = fo4::Signature::from_string("INTV");
constexpr auto k_nam1 = fo4::Signature::from_string("NAM1");

struct Piece {
  std::string_view signature;
  std::vector<std::byte> data;
};

fo4::FormKey record_key() { return fo4::FormKey::parse("000816:Patch.esp"); }

fo4::schema::SubrecordGroupDescriptor naming_rules() {
  return fo4::schema::subrecord_group_descriptor(k_innr, "INNR\\NamingRules").value();
}

fo4::schema::SubrecordGroupDescriptor obstacle() {
  return fo4::schema::subrecord_group_descriptor(k_nocm, "NOCM\\Obstacle").value();
}

std::vector<std::byte> u16_bytes(std::uint16_t value) {
  fo4::test::ByteWriter out;
  out.u16(value);
  return out.bytes;
}

std::vector<std::byte> u32_bytes(std::uint32_t value) {
  fo4::test::ByteWriter out;
  out.u32(value);
  return out.bytes;
}

std::vector<std::byte> zstr(std::string_view text) {
  fo4::test::ByteWriter out;
  out.raw(text);
  out.u8(0);
  return out.bytes;
}

std::vector<Piece> name_run(std::uint16_t index) {
  return {{"WNAM", zstr("RadicleName")},
          {"KSIZ", u32_bytes(1)},
          {"KWDA", u32_bytes(0x911)},
          {"YNAM", u16_bytes(index)}};
}

std::vector<Piece> naming_body(std::size_t first_count, std::size_t second_count) {
  std::vector<Piece> pieces{{"EDID", zstr("RadicleNaming")}, {"UNAM", u32_bytes(29)}};
  pieces.push_back({"VNAM", u32_bytes(static_cast<std::uint32_t>(first_count))});
  for (std::size_t index = 0; index < first_count; ++index) {
    for (auto& piece : name_run(static_cast<std::uint16_t>(index))) pieces.push_back(std::move(piece));
  }
  pieces.push_back({"VNAM", u32_bytes(static_cast<std::uint32_t>(second_count))});
  for (std::size_t index = 0; index < second_count; ++index) {
    for (auto& piece : name_run(static_cast<std::uint16_t>(100 + index))) pieces.push_back(std::move(piece));
  }
  return pieces;
}

std::vector<std::byte> make_plugin(const std::vector<Piece>& innr) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x816);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  const auto group = fo4::test::begin_group(out, "INNR", 0);
  const auto record = fo4::test::begin_record(out, "INNR", 0, 0x00000816);
  for (const auto& piece : innr) {
    fo4::test::write_subrecord(out, piece.signature, piece.data);
  }
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

std::vector<std::byte> make_nocm_plugin(const std::vector<Piece>& nocm) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x816);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  const auto group = fo4::test::begin_group(out, "NOCM", 0);
  const auto record = fo4::test::begin_record(out, "NOCM", 0, 0x00000816);
  for (const auto& piece : nocm) fo4::test::write_subrecord(out, piece.signature, piece.data);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

void append_obstacle_runs(std::vector<Piece>& pieces, const std::vector<std::size_t>& data_counts) {
  for (const auto count : data_counts) {
    pieces.push_back({"INDX", u32_bytes(static_cast<std::uint32_t>(pieces.size()))});
    for (std::size_t data = 0; data < count; ++data) {
      pieces.push_back({"DATA", u32_bytes(static_cast<std::uint32_t>(data))});
    }
    pieces.push_back({"INTV", u32_bytes(0)});
  }
}

std::vector<Piece> obstacle_body(const std::vector<std::size_t>& first, const std::vector<std::size_t>& second) {
  std::vector<Piece> pieces{{"EDID", zstr("NavmeshObstacleCoverManager")}};
  append_obstacle_runs(pieces, first);
  pieces.push_back({"NAM1", zstr("ModelA")});
  append_obstacle_runs(pieces, second);
  pieces.push_back({"NAM1", zstr("ModelB")});
  return pieces;
}

void expect_obstacle_refusal(const std::vector<Piece>& body, std::string_view message) {
  auto plugin = fo4::read_plugin(make_nocm_plugin(body), "Patch.esp");
  try {
    static_cast<void>(fo4::subrecord_group_spans(plugin, record_key(), obstacle()));
    ADD_FAILURE() << "expected a refusal containing " << message;
  } catch (const fo4::schema::SubrecordGroupError& error) {
    EXPECT_NE(std::string(error.what()).find(message), std::string::npos) << error.what();
  }
}

template <typename Operation>
void expect_group_refusal(Operation operation, std::string_view message) {
  try {
    static_cast<void>(operation());
    ADD_FAILURE() << "expected a refusal containing " << message;
  } catch (const fo4::schema::SubrecordGroupError& error) {
    EXPECT_NE(std::string(error.what()).find(message), std::string::npos) << error.what();
  }
}

std::uint32_t read_u32(const std::vector<std::byte>& data, std::size_t offset) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[offset + i])) << (8 * i);
  }
  return value;
}

const fo4::Record& record_of(const fo4::Plugin& plugin) {
  return *fo4::find_record(plugin, record_key());
}

std::vector<std::string> signatures(const fo4::Plugin& plugin) {
  std::vector<std::string> names;
  for (const auto& subrecord : record_of(plugin).subrecords) {
    names.push_back(subrecord.signature.to_string());
  }
  return names;
}

std::vector<std::uint32_t> ruleset_counts(const fo4::Plugin& plugin) {
  std::vector<std::uint32_t> counts;
  for (const auto& subrecord : record_of(plugin).subrecords) {
    if (subrecord.signature == k_vnam && subrecord.data.size() == 4) {
      counts.push_back(read_u32(subrecord.data, 0));
    }
  }
  return counts;
}

std::vector<std::string> payloads(const fo4::Plugin& plugin, fo4::Signature signature) {
  std::vector<std::string> texts;
  for (const auto& subrecord : record_of(plugin).subrecords) {
    if (subrecord.signature != signature) continue;
    std::string text;
    for (const auto byte : subrecord.data) {
      text.push_back(static_cast<char>(std::to_integer<std::uint8_t>(byte)));
    }
    texts.push_back(text);
  }
  return texts;
}

}  // namespace

TEST(NestedGroupRuns, InnrNamingRulesDescriptorNamesItsNestedRunAndCount) {
  const auto group = naming_rules();
  EXPECT_EQ(group.record, k_innr);
  EXPECT_EQ(group.path, "INNR\\NamingRules");
  EXPECT_EQ(group.count_subrecord, fo4::Signature{});
  ASSERT_EQ(group.members.size(), 1u);
  EXPECT_EQ(group.members[0].subrecord, k_vnam);
  EXPECT_TRUE(group.members[0].required);
  EXPECT_FALSE(group.members[0].repeatable);
  EXPECT_EQ(group.members[0].payload_size, 4u);
  EXPECT_EQ(group.run.count_member, k_vnam);
  ASSERT_EQ(group.run.members.size(), 5u);
  EXPECT_EQ(group.run.members[0].subrecord, k_wnam);
  EXPECT_TRUE(group.run.members[0].required);
  EXPECT_TRUE(group.run.members[0].localized);
  EXPECT_EQ(group.run.members[0].default_payload.size(), 1u);
  EXPECT_EQ(group.run.members[1].subrecord, k_ksiz);
  EXPECT_FALSE(group.run.members[1].required);
  EXPECT_EQ(group.run.members[1].payload_size, 4u);
  EXPECT_EQ(group.run.members[2].subrecord, k_kwda);
  EXPECT_EQ(group.run.members[4].subrecord, k_ynam);
  EXPECT_TRUE(group.run.members[4].required);
  EXPECT_EQ(group.run.members[4].payload_size, 2u);
  const auto text_group = fo4::schema::subrecord_group_for_member(k_innr, k_wnam);
  ASSERT_TRUE(text_group.has_value());
  EXPECT_EQ(text_group->path, "INNR\\NamingRules");
  const auto index_group = fo4::schema::subrecord_group_for_member(k_innr, k_ynam);
  ASSERT_TRUE(index_group.has_value());
  EXPECT_EQ(index_group->path, "INNR\\NamingRules");
  EXPECT_TRUE(fo4::schema::is_subrecord_group_count_subrecord(k_innr, k_vnam));
}

TEST(NestedGroupRuns, SpansScopeEachNestedRunToItsOwnCountSubrecord) {
  auto plugin = fo4::read_plugin(make_plugin(naming_body(2, 1)), "Patch.esp");
  const auto spans = fo4::subrecord_group_spans(plugin, record_key(), naming_rules());
  ASSERT_TRUE(spans.has_value());
  ASSERT_EQ(spans->size(), 2u);
  EXPECT_EQ((*spans)[0].first, 2u);
  EXPECT_EQ((*spans)[0].end, 11u);
  EXPECT_EQ((*spans)[1].first, 11u);
  EXPECT_EQ((*spans)[1].end, 16u);
  ASSERT_EQ((*spans)[0].runs.size(), 2u);
  EXPECT_EQ((*spans)[0].runs[0].first, 3u);
  EXPECT_EQ((*spans)[0].runs[0].end, 7u);
  EXPECT_EQ((*spans)[0].runs[1].first, 7u);
  EXPECT_EQ((*spans)[0].runs[1].end, 11u);
  ASSERT_EQ((*spans)[1].runs.size(), 1u);
  EXPECT_EQ((*spans)[1].runs[0].first, 12u);
  EXPECT_EQ((*spans)[1].runs[0].end, 16u);
  EXPECT_EQ(signatures(plugin)[2], "VNAM");
  EXPECT_EQ(signatures(plugin)[11], "VNAM");
}

TEST(NestedGroupRuns, CountMismatchAndMissingLeaderFailClosed) {
  const std::vector<std::vector<Piece>> malformed{
      {{"EDID", zstr("RadicleNaming")}, {"UNAM", u32_bytes(29)}, {"VNAM", u32_bytes(2)},
       {"WNAM", zstr("RadicleName")}, {"YNAM", u16_bytes(0)}},
      {{"EDID", zstr("RadicleNaming")}, {"UNAM", u32_bytes(29)}, {"VNAM", u32_bytes(1)},
       {"YNAM", u16_bytes(0)}},
      {{"EDID", zstr("RadicleNaming")}, {"UNAM", u32_bytes(29)}, {"VNAM", u32_bytes(1)},
       {"WNAM", zstr("RadicleName")}, {"KSIZ", u16_bytes(1)}, {"YNAM", u16_bytes(0)}},
      {{"EDID", zstr("RadicleNaming")}, {"UNAM", u32_bytes(29)}, {"VNAM", u32_bytes(1)},
       {"WNAM", zstr("RadicleName")}, {"KSIZ", u32_bytes(1)}, {"KWDA", u32_bytes(0x911)}, {"YNAM", u16_bytes(0)},
       {"WNAM", zstr("RadicleName")}, {"KSIZ", u32_bytes(1)}, {"KWDA", u32_bytes(0x911)}, {"YNAM", u16_bytes(1)}}};
  for (const auto& body : malformed) {
    auto plugin = fo4::read_plugin(make_plugin(body), "Patch.esp");
    EXPECT_THROW(static_cast<void>(fo4::subrecord_group_spans(plugin, record_key(), naming_rules())),
                 fo4::schema::SubrecordGroupError);
  }
}

TEST(NestedGroupRuns, InsertingAndRemovingANameKeepsItsRulesetCount) {
  auto plugin = fo4::read_plugin(make_plugin(naming_body(2, 1)), "Patch.esp");
  const auto original = fo4::write_plugin(plugin);
  EXPECT_EQ(fo4::insert_record_subrecord_group_run(plugin, record_key(), naming_rules(), 0, 1),
            fo4::SubrecordGroupMutationStatus::updated);
  EXPECT_EQ(ruleset_counts(plugin), (std::vector<std::uint32_t>{3, 1}));
  const auto names = signatures(plugin);
  EXPECT_EQ(names[2], "VNAM");
  EXPECT_EQ(names[3], "WNAM");
  EXPECT_EQ(names[7], "WNAM");
  EXPECT_EQ(names[8], "YNAM");
  EXPECT_EQ(names[9], "WNAM");
  EXPECT_EQ(names[10], "KSIZ");
  EXPECT_EQ(names[12], "YNAM");
  EXPECT_EQ(names[13], "VNAM");
  EXPECT_EQ(payloads(plugin, k_wnam).size(), 4u);
  EXPECT_EQ(payloads(plugin, k_ynam).size(), payloads(plugin, k_wnam).size());
  EXPECT_EQ(payloads(plugin, k_wnam)[1], std::string("\0", 1));
  EXPECT_EQ(payloads(plugin, k_ynam)[1], std::string("\0\0", 2));

  EXPECT_EQ(fo4::remove_record_subrecord_group_run(plugin, record_key(), naming_rules(), 0, 1),
            fo4::SubrecordGroupMutationStatus::updated);
  EXPECT_EQ(fo4::write_plugin(plugin), original);
}

TEST(NestedGroupRuns, InsertingAndRemovingARulesetRoundTripsTheBytes) {
  auto plugin = fo4::read_plugin(make_plugin(naming_body(2, 1)), "Patch.esp");
  const auto original = fo4::write_plugin(plugin);
  EXPECT_EQ(fo4::insert_record_subrecord_group(plugin, record_key(), naming_rules(), 1),
            fo4::SubrecordGroupMutationStatus::updated);
  EXPECT_EQ(ruleset_counts(plugin), (std::vector<std::uint32_t>{2, 0, 1}));
  const auto names = signatures(plugin);
  EXPECT_EQ(names[11], "VNAM");
  EXPECT_EQ(names[12], "VNAM");

  EXPECT_EQ(fo4::remove_record_subrecord_group(plugin, record_key(), naming_rules(), 1),
            fo4::SubrecordGroupMutationStatus::updated);
  EXPECT_EQ(fo4::write_plugin(plugin), original);
}

TEST(NestedGroupRuns, NameRowsAddressOneNameAtATime) {
  auto plugin = fo4::read_plugin(make_plugin(naming_body(2, 1)), "Patch.esp");
  const auto text = fo4::schema::scalar_field_descriptor("INNR\\NameText");
  ASSERT_TRUE(text.has_value());
  const auto index = fo4::schema::scalar_field_descriptor("INNR\\NameIndex");
  ASSERT_TRUE(index.has_value());

  EXPECT_EQ(fo4::set_record_string(plugin, record_key(), "INNR\\NameText", 1, "Edited"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), *index, 0, "7"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::record_field_count(plugin, record_key(), *text), 3u);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(payloads(reread, k_wnam)[0], std::string("RadicleName\0", 12));
  EXPECT_EQ(payloads(reread, k_wnam)[1], std::string("Edited\0", 7));
  EXPECT_EQ(payloads(reread, k_ynam)[0], std::string("\x07\0", 2));
  EXPECT_EQ(payloads(reread, k_ynam)[1], std::string("\x01\0", 2));
}

TEST(NestedGroupRuns, NameRowsRefuseARecordWhoseRulesetCountIsWrong) {
  auto plugin = fo4::read_plugin(
      make_plugin({{"EDID", zstr("RadicleNaming")}, {"UNAM", u32_bytes(29)}, {"VNAM", u32_bytes(2)},
                   {"WNAM", zstr("RadicleName")}, {"YNAM", u16_bytes(0)}}),
      "Patch.esp");
  const auto before = fo4::write_plugin(plugin);
  const auto index = fo4::schema::scalar_field_descriptor("INNR\\NameIndex");
  ASSERT_TRUE(index.has_value());
  EXPECT_THROW(fo4::set_record_scalar(plugin, record_key(), *index, 0, "7"),
               fo4::schema::ScalarError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(NestedGroupRuns, InsertingANameIntoALocalizedNameRefuses) {
  auto plugin = fo4::read_plugin(make_plugin(naming_body(2, 1)), "Patch.esp");
  plugin.header.tes4_flags = fo4::RecordFlags{fo4::RecordFlags::localized};
  const auto before = fo4::write_plugin(plugin);
  EXPECT_THROW(fo4::insert_record_subrecord_group_run(plugin, record_key(), naming_rules(), 0, 0),
               fo4::schema::SubrecordGroupError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(NestedGroupRuns, NocmObstacleDescriptorNamesItsUncountedRuns) {
  const auto group = obstacle();
  EXPECT_EQ(group.record, k_nocm);
  EXPECT_EQ(group.path, "NOCM\\Obstacle");
  EXPECT_EQ(group.count_subrecord, fo4::Signature{});
  ASSERT_EQ(group.members.size(), 1u);
  EXPECT_EQ(group.members[0].subrecord, k_nam1);
  EXPECT_TRUE(group.members[0].required);
  EXPECT_FALSE(group.members[0].repeatable);
  EXPECT_EQ(group.members[0].payload_size, 0u);
  EXPECT_EQ(group.run.count_member, fo4::Signature{});
  ASSERT_EQ(group.run.members.size(), 3u);
  EXPECT_EQ(group.run.members[0].subrecord, k_indx);
  EXPECT_TRUE(group.run.members[0].required);
  EXPECT_FALSE(group.run.members[0].repeatable);
  EXPECT_EQ(group.run.members[0].payload_size, 4u);
  EXPECT_EQ(group.run.members[1].subrecord, k_data);
  EXPECT_FALSE(group.run.members[1].required);
  EXPECT_TRUE(group.run.members[1].repeatable);
  EXPECT_EQ(group.run.members[2].subrecord, k_intv);
  EXPECT_TRUE(group.run.members[2].required);
  const auto run_group = fo4::schema::subrecord_group_for_member(k_nocm, k_indx);
  ASSERT_TRUE(run_group.has_value());
  EXPECT_EQ(run_group->path, "NOCM\\Obstacle");
  EXPECT_FALSE(fo4::schema::is_subrecord_group_count_subrecord(k_nocm, k_indx));
  EXPECT_FALSE(fo4::schema::is_subrecord_group_count_subrecord(k_nocm, k_nam1));
}

TEST(NestedGroupRuns, NocmSpansGroupEveryUncountedRunUnderItsModel) {
  auto plugin = fo4::read_plugin(make_nocm_plugin(obstacle_body({2, 0}, {1})), "Patch.esp");
  const auto spans = fo4::subrecord_group_spans(plugin, record_key(), obstacle());
  ASSERT_TRUE(spans.has_value());
  ASSERT_EQ(spans->size(), 2u);
  EXPECT_EQ((*spans)[0].first, 1u);
  EXPECT_EQ((*spans)[0].end, 8u);
  EXPECT_EQ((*spans)[1].first, 8u);
  EXPECT_EQ((*spans)[1].end, 12u);
  ASSERT_EQ((*spans)[0].runs.size(), 2u);
  EXPECT_EQ((*spans)[0].runs[0].first, 1u);
  EXPECT_EQ((*spans)[0].runs[0].end, 5u);
  EXPECT_EQ((*spans)[0].runs[1].first, 5u);
  EXPECT_EQ((*spans)[0].runs[1].end, 7u);
  ASSERT_EQ((*spans)[1].runs.size(), 1u);
  EXPECT_EQ((*spans)[1].runs[0].first, 8u);
  EXPECT_EQ((*spans)[1].runs[0].end, 11u);
  const auto names = signatures(plugin);
  EXPECT_EQ(names[7], "NAM1");
  EXPECT_EQ(names[11], "NAM1");
  EXPECT_EQ(names[8], "INDX");
}

TEST(NestedGroupRuns, NocmSingleModelSpansCoverItsRunsAndItsModel) {
  const Piece edid{"EDID", zstr("NavmeshObstacleCoverManager")};
  const auto one_run = fo4::read_plugin(
      make_nocm_plugin({edid, {"INDX", u32_bytes(0)}, {"INTV", u32_bytes(0)}, {"NAM1", zstr("ModelA")}}),
      "Patch.esp");
  const auto spans = fo4::subrecord_group_spans(one_run, record_key(), obstacle());
  ASSERT_TRUE(spans.has_value());
  ASSERT_EQ(spans->size(), 1u);
  EXPECT_EQ((*spans)[0].first, 1u);
  EXPECT_EQ((*spans)[0].end, 4u);
  ASSERT_EQ((*spans)[0].runs.size(), 1u);
  EXPECT_EQ((*spans)[0].runs[0].first, 1u);
  EXPECT_EQ((*spans)[0].runs[0].end, 3u);

  const auto two_runs = fo4::read_plugin(
      make_nocm_plugin({edid, {"INDX", u32_bytes(0)}, {"DATA", u32_bytes(0)}, {"DATA", u32_bytes(1)},
                        {"INTV", u32_bytes(0)}, {"INDX", u32_bytes(1)}, {"INTV", u32_bytes(0)},
                        {"NAM1", zstr("ModelA")}}),
      "Patch.esp");
  const auto pair = fo4::subrecord_group_spans(two_runs, record_key(), obstacle());
  ASSERT_TRUE(pair.has_value());
  ASSERT_EQ(pair->size(), 1u);
  EXPECT_EQ((*pair)[0].first, 1u);
  EXPECT_EQ((*pair)[0].end, 8u);
  ASSERT_EQ((*pair)[0].runs.size(), 2u);
  EXPECT_EQ((*pair)[0].runs[0].first, 1u);
  EXPECT_EQ((*pair)[0].runs[0].end, 5u);
  EXPECT_EQ((*pair)[0].runs[1].first, 5u);
  EXPECT_EQ((*pair)[0].runs[1].end, 7u);
}

TEST(NestedGroupRuns, NocmModelWithoutRunsStartsItsOwnSpan) {
  auto plugin = fo4::read_plugin(
      make_nocm_plugin({{"EDID", zstr("NavmeshObstacleCoverManager")}, {"INDX", u32_bytes(0)},
                        {"INTV", u32_bytes(0)}, {"NAM1", zstr("ModelA")}, {"NAM1", zstr("ModelB")}}),
      "Patch.esp");
  const auto spans = fo4::subrecord_group_spans(plugin, record_key(), obstacle());
  ASSERT_TRUE(spans.has_value());
  ASSERT_EQ(spans->size(), 2u);
  EXPECT_EQ((*spans)[0].first, 1u);
  EXPECT_EQ((*spans)[0].end, 4u);
  ASSERT_EQ((*spans)[0].runs.size(), 1u);
  EXPECT_EQ((*spans)[0].runs[0].first, 1u);
  EXPECT_EQ((*spans)[0].runs[0].end, 3u);
  EXPECT_EQ((*spans)[1].first, 4u);
  EXPECT_EQ((*spans)[1].end, 5u);
  EXPECT_TRUE((*spans)[1].runs.empty());
}

TEST(NestedGroupRuns, NocmShapeFailuresFailClosed) {
  const Piece edid{"EDID", zstr("NavmeshObstacleCoverManager")};
  const Piece indx{"INDX", u32_bytes(0)};
  const Piece intv{"INTV", u32_bytes(0)};
  const Piece nam1{"NAM1", zstr("ModelA")};
  expect_obstacle_refusal({edid, indx, {"DATA", u32_bytes(0)}, nam1}, "has a run without required INTV");
  expect_obstacle_refusal({edid, indx, intv, {"DATA", u32_bytes(0)}, nam1}, "has DATA out of order or repeated");
  expect_obstacle_refusal({edid, indx, intv}, "has a run without required NAM1");
  expect_obstacle_refusal({edid, indx, intv, intv}, "has INTV out of order or repeated");
}

TEST(NestedGroupRuns, NocmUncountedRunsRefuseWholeSpanMutations) {
  auto plugin = fo4::read_plugin(make_nocm_plugin(obstacle_body({2}, {1})), "Patch.esp");
  const auto before = fo4::write_plugin(plugin);
  expect_group_refusal(
      [&] { static_cast<void>(fo4::insert_record_subrecord_group(plugin, record_key(), obstacle(), 0)); },
      "spans uncounted sub-runs");
  expect_group_refusal(
      [&] { static_cast<void>(fo4::remove_record_subrecord_group(plugin, record_key(), obstacle(), 0)); },
      "spans uncounted sub-runs");
  expect_group_refusal(
      [&] { static_cast<void>(fo4::insert_record_subrecord_group_run(plugin, record_key(), obstacle(), 0, 0)); },
      "has no counted sub-run");
  expect_group_refusal(
      [&] { static_cast<void>(fo4::remove_record_subrecord_group_run(plugin, record_key(), obstacle(), 0, 0)); },
      "has no counted sub-run");
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(NestedGroupRuns, NocmMalformedRecordRefusesItsRowWrites) {
  auto plugin = fo4::read_plugin(
      make_nocm_plugin({{"EDID", zstr("NavmeshObstacleCoverManager")}, {"INDX", u32_bytes(0)},
                        {"INTV", u32_bytes(0)}}),
      "Patch.esp");
  const auto index = fo4::schema::scalar_field_descriptor("NOCM\\Index");
  const auto model = fo4::schema::scalar_field_descriptor("NOCM\\Model");
  ASSERT_TRUE(index.has_value());
  ASSERT_TRUE(model.has_value());
  const auto before = fo4::write_plugin(plugin);
  try {
    static_cast<void>(fo4::set_record_scalar(plugin, record_key(), *index, 0, "1"));
    ADD_FAILURE() << "a malformed NOCM accepted an Index write";
  } catch (const fo4::schema::ScalarError& error) {
    EXPECT_NE(std::string(error.what()).find("has a run without required NAM1"), std::string::npos)
        << error.what();
  }
  try {
    static_cast<void>(fo4::set_record_string(plugin, record_key(), *model, 0, "ModelB"));
    ADD_FAILURE() << "a malformed NOCM accepted a Model write";
  } catch (const fo4::schema::ScalarError& error) {
    EXPECT_NE(std::string(error.what()).find("has a run without required NAM1"), std::string::npos)
        << error.what();
  }
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(NestedGroupRuns, NocmIndexRowReadsEachRunsIndex) {
  auto plugin = fo4::read_plugin(make_nocm_plugin(obstacle_body({1, 0}, {1})), "Patch.esp");
  const auto index = fo4::schema::scalar_field_descriptor("NOCM\\Index");
  ASSERT_TRUE(index.has_value());
  EXPECT_EQ(fo4::record_field_count(plugin, record_key(), *index), 3u);
  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), *index, 1, "1056"),
            fo4::ScalarMutationStatus::updated);
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  std::vector<std::uint32_t> indexes;
  for (const auto& subrecord : record_of(reread).subrecords) {
    if (subrecord.signature == k_indx) indexes.push_back(read_u32(subrecord.data, 0));
  }
  ASSERT_EQ(indexes.size(), 3u);
  EXPECT_EQ(indexes[0], 1u);
  EXPECT_EQ(indexes[1], 1056u);
  EXPECT_EQ(indexes[2], 7u);
}

TEST(NestedGroupRuns, NestedMutationsReportMissingRecordsAndRanges) {
  auto plugin = fo4::read_plugin(make_plugin(naming_body(2, 1)), "Patch.esp");
  const auto before = fo4::write_plugin(plugin);
  EXPECT_EQ(fo4::insert_record_subrecord_group_run(plugin, record_key(), naming_rules(), 2, 0),
            fo4::SubrecordGroupMutationStatus::index_out_of_range);
  EXPECT_EQ(fo4::insert_record_subrecord_group_run(plugin, record_key(), naming_rules(), 0, 3),
            fo4::SubrecordGroupMutationStatus::index_out_of_range);
  EXPECT_EQ(fo4::remove_record_subrecord_group_run(plugin, record_key(), naming_rules(), 0, 2),
            fo4::SubrecordGroupMutationStatus::index_out_of_range);
  EXPECT_EQ(fo4::insert_record_subrecord_group_run(plugin, fo4::FormKey::parse("000999:Patch.esp"),
                                                   naming_rules(), 0, 0),
            fo4::SubrecordGroupMutationStatus::record_missing);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}
