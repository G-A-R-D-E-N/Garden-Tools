#include "fo4/graph/conflict_graph.hpp"
#include "fo4/graph/record_search.hpp"
#include "fo4/plugin/reader.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Sub {
  std::string signature;
  std::string data;
};

struct Rec {
  std::string signature;
  std::uint32_t form_id;
  std::string editor_id;
  std::vector<Sub> subs;
};

std::string zstr(const std::string& text) {
  return text + std::string(1, '\0');
}

constexpr std::uint32_t k_localized_flag = 0x80;

std::string string_id(std::uint32_t id) {
  std::string bytes;
  for (int shift = 0; shift < 32; shift += 8) bytes.push_back(static_cast<char>((id >> shift) & 0xFFu));
  return bytes;
}

std::vector<std::byte> make_plugin(const std::vector<Rec>& records,
                                   const std::vector<std::string>& masters,
                                   std::uint32_t flags = 0) {
  fo4::test::ByteWriter out;
  out.bytes = fo4::test::make_tes4_plugin(flags, 1.0f, masters);
  for (const auto& spec : records) {
    const auto group = fo4::test::begin_group(out, spec.signature, 0);
    const auto record = fo4::test::begin_record(out, spec.signature, 0, spec.form_id);
    fo4::test::write_subrecord(out, "EDID", zstr(spec.editor_id));
    for (const auto& sub : spec.subs) fo4::test::write_subrecord(out, sub.signature, sub.data);
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
  }
  return out.bytes;
}

fo4::Plugin base_plugin() {
  return fo4::read_plugin(make_plugin({
                              {"KYWD", 0x800, "KwWidget", {{"DNAM", zstr("Hello Widget")}, {"FULL", zstr("Shiny Gadget")}}},
                              {"KYWD", 0x801, "KwPlain", {{"DNAM", zstr("Plain text")}}},
                              {"MISC", 0x802, "MiscBolt", {}},
                          },
                                                    {}),
                          "Base.esm");
}

fo4::Plugin patch_plugin() {
  return fo4::read_plugin(make_plugin({
                              {"KYWD", 0x800, "KwWidget", {{"DNAM", zstr("Hello Widget v2")}}},
                          },
                                                    {"Base.esm"}),
                          "Patch.esp");
}

std::vector<std::string> editors(const fo4::RecordSearchResult& result) {
  std::vector<std::string> out;
  for (const auto& hit : result.hits) out.push_back(hit.editor_id);
  return out;
}

}  // namespace

TEST(RecordSearch, SixDigitFormIdMatchesEveryVersionOfTheLocalId) {
  const auto base = base_plugin();
  const auto patch = patch_plugin();
  const std::vector<const fo4::Plugin*> order{&base, &patch};
  fo4::RecordSearchQuery query;
  query.form_id = "000800";
  const auto result = fo4::search_records(order, query);

  ASSERT_EQ(result.hits.size(), 2u);
  EXPECT_EQ(result.hits[0].plugin_index, 0u);
  EXPECT_EQ(result.hits[1].plugin_index, 1u);
  EXPECT_FALSE(result.truncated);
}

TEST(RecordSearch, EightDigitLoadOrderFormIdSelectsTheOriginPluginByPosition) {
  const auto base = base_plugin();
  const auto patch = patch_plugin();
  const std::vector<const fo4::Plugin*> order{&base, &patch};

  fo4::RecordSearchQuery origin;
  origin.form_id = "00000800";
  EXPECT_EQ(fo4::search_records(order, origin).hits.size(), 2u);

  fo4::RecordSearchQuery other;
  other.form_id = "01000800";
  EXPECT_TRUE(fo4::search_records(order, other).hits.empty());
}

namespace {

struct LightOrder {
  fo4::Plugin base = base_plugin();
  fo4::Plugin light_a = fo4::read_plugin(make_plugin({{"MISC", 0x801, "LightA", {}}}, {}), "LightA.esl");
  fo4::Plugin light_b = fo4::read_plugin(make_plugin({{"MISC", 0x802, "LightB", {}}}, {}), "LightB.esl");
  fo4::Plugin later = fo4::read_plugin(make_plugin({{"MISC", 0x803, "LaterFull", {}}}, {}), "Later.esp");
  std::vector<const fo4::Plugin*> order{&base, &light_a, &light_b, &later};
};

std::vector<std::string> form_id_editors(const LightOrder& fixture, const std::string& form_id) {
  fo4::RecordSearchQuery query;
  query.form_id = form_id;
  return editors(fo4::search_records(fixture.order, query));
}

}  // namespace

TEST(RecordSearch, EightDigitFormIdNumbersFullPluginsWithoutCountingLightOnes) {
  const LightOrder fixture;
  EXPECT_EQ(form_id_editors(fixture, "01000803"), std::vector<std::string>{"LaterFull"});
  EXPECT_TRUE(form_id_editors(fixture, "03000803").empty());
  EXPECT_TRUE(form_id_editors(fixture, "02000803").empty());
}

TEST(RecordSearch, LightRuntimeFormIdSelectsTheLightPluginBySlot) {
  const LightOrder fixture;
  EXPECT_EQ(form_id_editors(fixture, "FE000801"), std::vector<std::string>{"LightA"});
  EXPECT_EQ(form_id_editors(fixture, "FE001802"), std::vector<std::string>{"LightB"});
  EXPECT_TRUE(form_id_editors(fixture, "FE000802").empty());
  EXPECT_TRUE(form_id_editors(fixture, "FE001801").empty());
}

TEST(RecordSearch, LightRuntimeFormIdWithNoSuchSlotMatchesNothing) {
  const LightOrder fixture;
  EXPECT_TRUE(form_id_editors(fixture, "FE002801").empty());
  EXPECT_TRUE(form_id_editors(fixture, "FEFFF801").empty());
  EXPECT_TRUE(form_id_editors(fixture, "FF000801").empty());
}

TEST(RecordSearch, FormKeyTextMatchesTheOwningPluginOnly) {
  const auto base = base_plugin();
  const auto patch = patch_plugin();
  const std::vector<const fo4::Plugin*> order{&base, &patch};

  fo4::RecordSearchQuery exact;
  exact.form_id = "000800:Base.esm";
  EXPECT_EQ(fo4::search_records(order, exact).hits.size(), 2u);

  fo4::RecordSearchQuery wrong;
  wrong.form_id = "000800:Patch.esp";
  EXPECT_TRUE(fo4::search_records(order, wrong).hits.empty());
}

TEST(RecordSearch, MalformedFormIdIsRejected) {
  const auto base = base_plugin();
  const std::vector<const fo4::Plugin*> order{&base};
  fo4::RecordSearchQuery query;
  query.form_id = "ZZ";
  EXPECT_THROW(fo4::search_records(order, query), std::invalid_argument);
  query.form_id = "1234567";
  EXPECT_THROW(fo4::search_records(order, query), std::invalid_argument);
}

TEST(RecordSearch, SignatureSetFiltersByExactType) {
  const auto base = base_plugin();
  const auto patch = patch_plugin();
  const std::vector<const fo4::Plugin*> order{&base, &patch};
  fo4::RecordSearchQuery query;
  query.signatures = {fo4::Signature::from_string("MISC")};
  const auto result = fo4::search_records(order, query);

  EXPECT_EQ(editors(result), std::vector<std::string>{"MiscBolt"});
}

TEST(RecordSearch, EditorIdSubstringAndPatternAreCaseInsensitive) {
  const auto base = base_plugin();
  const auto patch = patch_plugin();
  const std::vector<const fo4::Plugin*> order{&base, &patch};

  fo4::RecordSearchQuery substring;
  substring.editor_id = "WIDGET";
  EXPECT_EQ(fo4::search_records(order, substring).hits.size(), 2u);

  fo4::RecordSearchQuery pattern;
  pattern.editor_id_pattern = "KWPL*N";
  EXPECT_EQ(editors(fo4::search_records(order, pattern)), std::vector<std::string>{"KwPlain"});

  fo4::RecordSearchQuery single;
  single.editor_id_pattern = "KwWid?et";
  EXPECT_EQ(editors(fo4::search_records(order, single)), (std::vector<std::string>{"KwWidget", "KwWidget"}));

  fo4::RecordSearchQuery literal;
  literal.editor_id_pattern = "([";
  EXPECT_TRUE(fo4::search_records(order, literal).hits.empty());
}

TEST(RecordSearch, EditorIdPatternIsWholeIdGlobAndRejectsLongPatterns) {
  const auto base = base_plugin();
  const std::vector<const fo4::Plugin*> order{&base};

  fo4::RecordSearchQuery partial;
  partial.editor_id_pattern = "Widget";
  EXPECT_TRUE(fo4::search_records(order, partial).hits.empty());

  fo4::RecordSearchQuery at_limit;
  at_limit.editor_id_pattern = std::string(128, '*');
  EXPECT_EQ(fo4::search_records(order, at_limit).hits.size(), 3u);

  fo4::RecordSearchQuery too_long;
  too_long.editor_id_pattern = std::string(129, '*');
  EXPECT_THROW(fo4::search_records(order, too_long), std::invalid_argument);
}

TEST(RecordSearch, PathologicalEditorIdPatternReturnsQuickly) {
  const auto plugin = fo4::read_plugin(make_plugin({{"KYWD", 0x900, std::string(120, 'a'), {}}}, {}), "Long.esp");
  const std::vector<const fo4::Plugin*> order{&plugin};

  fo4::RecordSearchQuery query;
  query.editor_id_pattern = "*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*b";
  const auto started = std::chrono::steady_clock::now();
  EXPECT_TRUE(fo4::search_records(order, query).hits.empty());
  EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(1));
}

TEST(RecordSearch, DisplayNameMatchesInlineFullText) {
  const auto base = base_plugin();
  const auto patch = patch_plugin();
  const std::vector<const fo4::Plugin*> order{&base, &patch};
  fo4::RecordSearchQuery query;
  query.display_name = "shiny";
  const auto result = fo4::search_records(order, query);

  ASSERT_EQ(result.hits.size(), 1u);
  EXPECT_EQ(result.hits[0].plugin_index, 0u);
  EXPECT_EQ(result.hits[0].editor_id, "KwWidget");
}

TEST(RecordSearch, GeneralTextMatchesInlineDisplayName) {
  const auto base = base_plugin();
  const std::vector<const fo4::Plugin*> order{&base};
  fo4::RecordSearchQuery query;
  query.text = "SHINY";
  EXPECT_EQ(editors(fo4::search_records(order, query)), std::vector<std::string>{"KwWidget"});
}

namespace {

fo4::Plugin localized_plugin() {
  return fo4::read_plugin(make_plugin({
                              {"MISC", 0x800, "LocNamed", {{"FULL", string_id(5)}}},
                              {"MISC", 0x801, "LocMissing", {{"FULL", string_id(6)}}},
                          },
                                      {}, k_localized_flag),
                          "Loc.esp");
}

fo4::LocalizedNameResolver gleaming_table(std::size_t expected_plugin) {
  return [expected_plugin](std::size_t plugin_index, std::uint32_t id) -> std::optional<std::string> {
    if (plugin_index == expected_plugin && id == 5) return "Gleaming Blade";
    return std::nullopt;
  };
}

}  // namespace

TEST(RecordSearch, LocalizedDisplayNameResolvesThroughTheStringTable) {
  const auto base = base_plugin();
  const auto loc = localized_plugin();
  const std::vector<const fo4::Plugin*> order{&base, &loc};
  const auto resolver = gleaming_table(1);

  fo4::RecordSearchQuery by_name;
  by_name.display_name = "gleaming";
  const auto named = fo4::search_records(order, by_name, nullptr, {}, resolver);
  ASSERT_EQ(named.hits.size(), 1u);
  EXPECT_EQ(named.hits[0].editor_id, "LocNamed");
  EXPECT_EQ(named.hits[0].plugin_index, 1u);

  fo4::RecordSearchQuery by_text;
  by_text.text = "BLADE";
  EXPECT_EQ(editors(fo4::search_records(order, by_text, nullptr, {}, resolver)),
            std::vector<std::string>{"LocNamed"});

  fo4::RecordSearchQuery inline_name;
  inline_name.display_name = "shiny";
  EXPECT_EQ(editors(fo4::search_records(order, inline_name, nullptr, {}, resolver)),
            std::vector<std::string>{"KwWidget"});
}

TEST(RecordSearch, LocalizedDisplayNameWithoutATableMatchesNothingAndDoesNotThrow) {
  const auto loc = localized_plugin();
  const std::vector<const fo4::Plugin*> order{&loc};

  fo4::RecordSearchQuery by_name;
  by_name.display_name = "gleaming";
  EXPECT_NO_THROW(EXPECT_TRUE(fo4::search_records(order, by_name).hits.empty()));

  fo4::RecordSearchQuery by_text;
  by_text.text = "gleaming";
  EXPECT_NO_THROW(EXPECT_TRUE(fo4::search_records(order, by_text).hits.empty()));

  fo4::RecordSearchQuery by_editor;
  by_editor.text = "locnamed";
  EXPECT_EQ(editors(fo4::search_records(order, by_editor)), std::vector<std::string>{"LocNamed"});
}

TEST(RecordSearch, FieldValueMatchesDecodedScalarText) {
  const auto base = base_plugin();
  const auto patch = patch_plugin();
  const std::vector<const fo4::Plugin*> order{&base, &patch};

  fo4::RecordSearchQuery hello;
  hello.field_value = "hello";
  EXPECT_EQ(fo4::search_records(order, hello).hits.size(), 2u);

  fo4::RecordSearchQuery plain;
  plain.field_value = "plain text";
  const auto plain_result = fo4::search_records(order, plain);
  ASSERT_EQ(plain_result.hits.size(), 1u);
  EXPECT_EQ(plain_result.hits[0].editor_id, "KwPlain");

  fo4::RecordSearchQuery missing;
  missing.field_value = "absent";
  EXPECT_TRUE(fo4::search_records(order, missing).hits.empty());
}

TEST(RecordSearch, TextMatchesFormKeyEditorIdOrSignatureAsBefore) {
  const auto base = base_plugin();
  const auto patch = patch_plugin();
  const std::vector<const fo4::Plugin*> order{&base, &patch};

  fo4::RecordSearchQuery by_editor;
  by_editor.text = "miscbolt";
  EXPECT_EQ(editors(fo4::search_records(order, by_editor)), std::vector<std::string>{"MiscBolt"});

  fo4::RecordSearchQuery by_type;
  by_type.text = "misc";
  EXPECT_EQ(editors(fo4::search_records(order, by_type)), std::vector<std::string>{"MiscBolt"});

  fo4::RecordSearchQuery by_key;
  by_key.text = "000800:base";
  const auto key_result = fo4::search_records(order, by_key);
  ASSERT_EQ(key_result.hits.size(), 2u);
  EXPECT_EQ(key_result.hits[0].plugin_index, 0u);
  EXPECT_EQ(key_result.hits[1].plugin_index, 1u);
}

TEST(RecordSearch, LimitBoundsResultsAndReportsTruncation) {
  const auto base = base_plugin();
  const auto patch = patch_plugin();
  const std::vector<const fo4::Plugin*> order{&base, &patch};

  fo4::RecordSearchQuery two;
  two.limit = 2;
  const auto bounded = fo4::search_records(order, two);
  EXPECT_EQ(bounded.hits.size(), 2u);
  EXPECT_TRUE(bounded.truncated);

  fo4::RecordSearchQuery all;
  all.limit = 4;
  const auto full = fo4::search_records(order, all);
  EXPECT_EQ(full.hits.size(), 4u);
  EXPECT_FALSE(full.truncated);

  fo4::RecordSearchQuery zero;
  zero.limit = 0;
  EXPECT_THROW(fo4::search_records(order, zero), std::invalid_argument);
}

TEST(RecordSearch, ConflictFilterNeedsAGraph) {
  const auto base = base_plugin();
  const std::vector<const fo4::Plugin*> order{&base};
  fo4::RecordSearchQuery query;
  query.conflict_categories = {fo4::ConflictCategory::only_one};
  EXPECT_THROW(fo4::search_records(order, query), std::invalid_argument);
}

TEST(RecordSearch, ConflictFilterSeparatesSingleVersionRecordsFromOverrides) {
  const auto base = base_plugin();
  const auto patch = patch_plugin();
  const std::vector<const fo4::Plugin*> order{&base, &patch};
  const auto graph = fo4::ConflictGraph::build(order);

  fo4::RecordSearchQuery only;
  only.conflict_categories = {fo4::ConflictCategory::only_one};
  EXPECT_EQ(editors(fo4::search_records(order, only, &graph)),
            (std::vector<std::string>{"KwPlain", "MiscBolt"}));

  fo4::RecordSearchQuery contested;
  contested.conflict_categories = {fo4::ConflictCategory::no_conflict, fo4::ConflictCategory::benign,
                                   fo4::ConflictCategory::override_, fo4::ConflictCategory::conflict,
                                   fo4::ConflictCategory::critical};
  const auto contested_result = fo4::search_records(order, contested, &graph);
  EXPECT_EQ(editors(contested_result), (std::vector<std::string>{"KwWidget", "KwWidget"}));
}
