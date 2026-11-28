#include "fo4/plugin/maintenance.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/graph/conflict_graph.hpp"
#include "fo4/schema/form_links.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <string_view>
#include <vector>

namespace {

struct Subspec {
  std::string_view signature;
  std::string_view data;
};

struct RecordSpec {
  std::string_view signature;
  std::uint32_t form_id;
  std::string_view editor_id;
  std::uint32_t keyword{0};
  std::uint32_t flags{0};
  std::vector<Subspec> extra{};
};

fo4::Plugin make_plugin(std::string_view name,
                        std::initializer_list<std::string_view> masters,
                        const std::vector<RecordSpec>& records,
                        float version = 1.0f) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(version);
  hedr.u32(static_cast<std::uint32_t>(records.size()));
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::ByteWriter data;
  data.u32(0);
  data.u32(0);
  for (const auto master : masters) {
    std::vector<std::byte> bytes;
    for (const char ch : master) bytes.push_back(std::byte{static_cast<std::uint8_t>(ch)});
    bytes.push_back(std::byte{0});
    fo4::test::write_subrecord(out, "MAST", bytes);
    fo4::test::write_subrecord(out, "DATA", data.bytes);
  }
  fo4::test::end_record(out, tes4);
  for (const auto& spec : records) {
    const auto group = fo4::test::begin_group(out, spec.signature, 0);
    const auto record = fo4::test::begin_record(out, spec.signature, spec.flags, spec.form_id);
    fo4::test::write_subrecord(out, "EDID", spec.editor_id);
    if (spec.keyword != 0) {
      fo4::test::ByteWriter ksiz;
      ksiz.u32(1);
      fo4::test::write_subrecord(out, "KSIZ", ksiz.bytes);
      fo4::test::ByteWriter kwda;
      kwda.u32(spec.keyword);
      fo4::test::write_subrecord(out, "KWDA", kwda.bytes);
    }
    for (const auto& extra : spec.extra) fo4::test::write_subrecord(out, extra.signature, extra.data);
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
  }
  return fo4::read_plugin(out.bytes, std::string(name));
}

}  // namespace

TEST(Maintenance, CleanupCopiesFormListToInjectionSourceAndRemovesOnlyInjectedEntries) {
  auto owner = make_plugin("Owner.esm", {}, {});
  auto referring = make_plugin("Referrer.esp", {"Owner.esm"},
      {{"FLST", 0x01000900, std::string_view{"List\0", 5}, 0, 0,
        {{"LNAM", std::string_view{"\x00\x08\x00\x00", 4}},
         {"LNAM", std::string_view{"\x01\x09\x00\x01", 4}}}},
       {"KYWD", 0x01000901, std::string_view{"Local\0", 6}}});
  auto injection = make_plugin("Injector.esp", {"Owner.esm"},
      {{"KYWD", 0x00000800, std::string_view{"Injected\0", 9}},
       {"KYWD", 0x01000801, std::string_view{"InjectorLocal\0", 14}}});
  injection = fo4::set_plugin_author(injection, "Author");
  injection = fo4::set_plugin_description(injection, "Description");
  auto noise = make_plugin("Noise.esp", {},
      {{"KYWD", 0x00000900, std::string_view{"One\0", 4}},
       {"KYWD", 0x00000900, std::string_view{"Two\0", 4}}});
  const auto noise_before = fo4::write_plugin(noise);
  std::vector<fo4::Plugin*> order{&owner, &referring, &injection, &noise};
  const auto list = fo4::FormKey::parse("000900:Referrer.esp");
  ASSERT_EQ(fo4::cleanup_injected_form_list(order, 1, list), fo4::InjectedCleanupStatus::updated);
  const auto after_referrer = fo4::read_plugin(fo4::write_plugin(referring), "Referrer.esp");
  const auto after_injector = fo4::read_plugin(fo4::write_plugin(injection), "Injector.esp");
  EXPECT_EQ(after_injector.header.author, "Author");
  EXPECT_EQ(after_injector.header.description, "Description");
  EXPECT_EQ(fo4::write_plugin(noise), noise_before);
  const auto* cleaned = fo4::find_record(after_referrer, list);
  const auto* copied = fo4::find_record(after_injector, list);
  ASSERT_NE(cleaned, nullptr);
  ASSERT_NE(copied, nullptr);
  ASSERT_EQ(cleaned->subrecords.size(), 2u);
  ASSERT_EQ(copied->subrecords.size(), 3u);
  EXPECT_EQ(cleaned->subrecords[1].data, (std::vector<std::byte>{std::byte{1}, std::byte{9}, std::byte{0}, std::byte{1}}));
  EXPECT_NE(fo4::find_record(after_referrer, fo4::FormKey::parse("000901:Referrer.esp")), nullptr);
  EXPECT_NE(fo4::find_record(after_injector, fo4::FormKey::parse("000801:Injector.esp")), nullptr);
  const auto copied_links = fo4::collect_form_links(after_injector);
  EXPECT_EQ(std::count_if(copied_links.begin(), copied_links.end(), [&](const auto& hit) {
    return hit.record == list && (hit.key == fo4::FormKey::parse("000800:Owner.esm") ||
                                  hit.key == fo4::FormKey::parse("000901:Referrer.esp"));
  }), 2);
}

TEST(Maintenance, CleanupRefusesDuplicateInjectedTargetWithoutMutation) {
  auto owner = make_plugin("Owner.esm", {}, {});
  auto referring = make_plugin("Referrer.esp", {"Owner.esm"},
      {{"FLST", 0x01000900, std::string_view{"List\0", 5}, 0, 0,
        {{"LNAM", std::string_view{"\x00\x08\x00\x00", 4}}}}});
  auto injection = make_plugin("Injector.esp", {"Owner.esm"},
      {{"KYWD", 0x00000800, std::string_view{"One\0", 4}},
       {"KYWD", 0x00000800, std::string_view{"Two\0", 4}}});
  const auto before_referring = fo4::write_plugin(referring);
  const auto before_injection = fo4::write_plugin(injection);
  std::vector<fo4::Plugin*> order{&owner, &referring, &injection};
  EXPECT_EQ(fo4::cleanup_injected_form_list(order, 1, fo4::FormKey::parse("000900:Referrer.esp")),
            fo4::InjectedCleanupStatus::unsupported);
  EXPECT_EQ(fo4::write_plugin(referring), before_referring);
  EXPECT_EQ(fo4::write_plugin(injection), before_injection);
}

static std::vector<std::string_view> master_names(const fo4::Plugin& plugin) {
  std::vector<std::string_view> names;
  for (const auto& master : plugin.header.masters) names.push_back(master.key.filename());
  return names;
}

TEST(Maintenance, CleanupSortsExistingMastersWhenAppendingWouldBreakLoadOrder) {
  auto owner = make_plugin("Owner.esm", {}, {});
  auto referring = make_plugin("Referrer.esp", {"Owner.esm"},
      {{"FLST", 0x01000900, std::string_view{"List\0", 5}, 0, 0,
        {{"LNAM", std::string_view{"\0\x08\0\0", 4}}}}});
  auto later = make_plugin("Later.esp", {"Owner.esm"}, {});
  auto injection = make_plugin("Injector.esp", {"Owner.esm", "Later.esp"},
      {{"KYWD", 0x00000800, std::string_view{"Injected\0", 9}}});
  std::vector<fo4::Plugin*> order{&owner, &referring, &later, &injection};
  const auto list = fo4::FormKey::parse("000900:Referrer.esp");
  ASSERT_EQ(fo4::cleanup_injected_form_list(order, 1, list), fo4::InjectedCleanupStatus::updated);
  const auto after_injector = fo4::read_plugin(fo4::write_plugin(injection), "Injector.esp");
  EXPECT_EQ(master_names(after_injector),
            (std::vector<std::string_view>{"Owner.esm", "Referrer.esp", "Later.esp"}));
  EXPECT_NE(fo4::find_record(after_injector, list), nullptr);
  EXPECT_NE(fo4::find_record(after_injector, fo4::FormKey::parse("000800:Owner.esm")), nullptr);
}

TEST(Maintenance, CleanupKeepsSeveralMastersSortedWhenReferrerIsAppended) {
  auto base = make_plugin("Base.esm", {}, {});
  auto zeta = make_plugin("Zeta.esm", {}, {{"KYWD", 0x00000801, std::string_view{"Zeta\0", 5}}});
  auto referring = make_plugin("Referrer.esp", {"Base.esm", "Zeta.esm"},
      {{"FLST", 0x02000900, std::string_view{"List\0", 5}, 0, 0,
        {{"LNAM", std::string_view{"\0\x08\0\0", 4}}}}});
  auto injection = make_plugin("Injector.esp", {"Base.esm", "Zeta.esm"},
      {{"KYWD", 0x00000800, std::string_view{"Injected\0", 9}}});
  std::vector<fo4::Plugin*> order{&base, &zeta, &referring, &injection};
  const auto list = fo4::FormKey::parse("000900:Referrer.esp");
  ASSERT_EQ(fo4::cleanup_injected_form_list(order, 2, list), fo4::InjectedCleanupStatus::updated);
  const auto after_injector = fo4::read_plugin(fo4::write_plugin(injection), "Injector.esp");
  EXPECT_EQ(master_names(after_injector),
            (std::vector<std::string_view>{"Base.esm", "Zeta.esm", "Referrer.esp"}));
}

TEST(Maintenance, CleanupSortsAppendedMastersIntoLoadOrder) {
  auto base = make_plugin("Base.esm", {}, {});
  auto zeta = make_plugin("Zeta.esm", {}, {{"KYWD", 0x00000801, std::string_view{"Zeta\0", 5}}});
  auto referring = make_plugin("Referrer.esp", {"Base.esm", "Zeta.esm"},
      {{"FLST", 0x02000900, std::string_view{"List\0", 5}, 0, 0,
        {{"LNAM", std::string_view{"\0\x08\0\0", 4}},
         {"LNAM", std::string_view{"\x01\x08\0\x01", 4}}}}});
  auto injection = make_plugin("Injector.esp", {"Base.esm"},
      {{"KYWD", 0x00000800, std::string_view{"Injected\0", 9}}});
  std::vector<fo4::Plugin*> order{&base, &zeta, &referring, &injection};
  const auto list = fo4::FormKey::parse("000900:Referrer.esp");
  ASSERT_EQ(fo4::cleanup_injected_form_list(order, 2, list), fo4::InjectedCleanupStatus::updated);
  const auto after_injector = fo4::read_plugin(fo4::write_plugin(injection), "Injector.esp");
  EXPECT_EQ(master_names(after_injector),
            (std::vector<std::string_view>{"Base.esm", "Zeta.esm", "Referrer.esp"}));
  const auto copied_links = fo4::collect_form_links(after_injector);
  EXPECT_EQ(std::count_if(copied_links.begin(), copied_links.end(), [&](const auto& hit) {
    return hit.record == list && (hit.key == fo4::FormKey::parse("000800:Base.esm") ||
                                  hit.key == fo4::FormKey::parse("000801:Zeta.esm"));
  }), 2);
}

TEST(Maintenance, CleanupRefusesWhenInterveningFormListOverrideWouldBeOverwritten) {
  auto owner = make_plugin("Owner.esm", {}, {});
  auto referring = make_plugin("Referrer.esp", {"Owner.esm"},
      {{"FLST", 0x01000900, std::string_view{"Old\0", 4}, 0, 0,
        {{"LNAM", std::string_view{"\x00\x08\x00\x00", 4}},
         {"LNAM", std::string_view{"\x01\x09\x00\x01", 4}}}},
       {"KYWD", 0x01000901, std::string_view{"Local\0", 6}}});
  auto intervening = make_plugin("Intervening.esp", {"Referrer.esp", "Owner.esm"},
      {{"FLST", 0x00000900, std::string_view{"Edited\0", 7}, 0, 0,
        {{"LNAM", std::string_view{"\x01\x09\x00\x00", 4}}}}});
  auto injection = make_plugin("Injector.esp", {"Owner.esm"},
      {{"KYWD", 0x00000800, std::string_view{"Injected\0", 9}}});
  const auto list = fo4::FormKey::parse("000900:Referrer.esp");
  auto baseline_referring = referring;
  auto baseline_injection = injection;
  std::vector<fo4::Plugin*> baseline{&owner, &baseline_referring, &baseline_injection};
  EXPECT_EQ(fo4::cleanup_injected_form_list(baseline, 1, list), fo4::InjectedCleanupStatus::updated);

  const auto intervening_links = fo4::collect_form_links(intervening);
  EXPECT_TRUE(std::any_of(intervening_links.begin(), intervening_links.end(), [&](const auto& hit) {
    return hit.record == list && hit.key == fo4::FormKey::parse("000901:Referrer.esp");
  }));
  const std::vector<const fo4::Plugin*> graph_plugins{&owner, &referring, &intervening, &injection};
  const auto graph = fo4::ConflictGraph::build(graph_plugins);
  const auto* winner = graph.winner(list);
  ASSERT_NE(winner, nullptr);
  EXPECT_EQ(winner->plugin, intervening.key);
  EXPECT_EQ(winner->load_order, 2u);
  std::vector<fo4::Plugin*> order{&owner, &referring, &intervening, &injection};
  const auto before_referring = fo4::write_plugin(referring);
  const auto before_intervening = fo4::write_plugin(intervening);
  const auto before_injection = fo4::write_plugin(injection);
  EXPECT_EQ(fo4::cleanup_injected_form_list(order, 1, list),
            fo4::InjectedCleanupStatus::unsupported);
  EXPECT_EQ(fo4::write_plugin(referring), before_referring);
  EXPECT_EQ(fo4::write_plugin(intervening), before_intervening);
  EXPECT_EQ(fo4::write_plugin(injection), before_injection);
}

TEST(Maintenance, CleanupRefusesUnknownFormListMembersWithoutMutatingEitherPlugin) {
  auto owner = make_plugin("Owner.esm", {}, {});
  auto referring = make_plugin("Referrer.esp", {"Owner.esm"},
      {{"FLST", 0x01000900, std::string_view{"List\0", 5}, 0, 0,
        {{"LNAM", std::string_view{"\x00\x08\x00\x00", 4}}, {"ZZZZ", "opaque"}}}});
  auto injection = make_plugin("Injector.esp", {"Owner.esm"},
      {{"KYWD", 0x00000800, std::string_view{"Injected\0", 9}}});
  std::vector<fo4::Plugin*> order{&owner, &referring, &injection};
  const auto before_referring = fo4::write_plugin(referring);
  const auto before_injection = fo4::write_plugin(injection);
  EXPECT_EQ(fo4::cleanup_injected_form_list(order, 1, fo4::FormKey::parse("000900:Referrer.esp")),
            fo4::InjectedCleanupStatus::unsupported);
  EXPECT_EQ(fo4::write_plugin(referring), before_referring);
  EXPECT_EQ(fo4::write_plugin(injection), before_injection);
}

TEST(Maintenance, CleanupRefusesOpaqueDestinationBeforeChangingEitherPlugin) {
  auto owner = make_plugin("Owner.esm", {}, {});
  auto referring = make_plugin("Referrer.esp", {"Owner.esm"},
      {{"FLST", 0x01000900, std::string_view{"List\0", 5}, 0, 0,
        {{"LNAM", std::string_view{"\x00\x08\x00\x00", 4}}}}});
  auto injection = make_plugin("Injector.esp", {"Owner.esm"},
      {{"KYWD", 0x00000800, std::string_view{"Injected\0", 9}, 0, 0, {{"ZZZZ", "opaque"}}}});
  std::vector<fo4::Plugin*> order{&owner, &referring, &injection};
  const auto before_referring = fo4::write_plugin(referring);
  const auto before_injection = fo4::write_plugin(injection);
  EXPECT_EQ(fo4::cleanup_injected_form_list(order, 1, fo4::FormKey::parse("000900:Referrer.esp")),
            fo4::InjectedCleanupStatus::unsupported);
  EXPECT_EQ(fo4::write_plugin(referring), before_referring);
  EXPECT_EQ(fo4::write_plugin(injection), before_injection);
}

TEST(Maintenance, CleanupRefusesDifferentInjectionSourcesWithoutChangingAnyPlugin) {
  auto owner = make_plugin("Owner.esm", {}, {});
  auto referring = make_plugin("Referrer.esp", {"Owner.esm"},
      {{"FLST", 0x01000900, std::string_view{"List\0", 5}, 0, 0,
        {{"LNAM", std::string_view{"\x00\x08\x00\x00", 4}},
         {"LNAM", std::string_view{"\x01\x08\x00\x00", 4}}}}});
  auto first = make_plugin("First.esp", {"Owner.esm"},
      {{"KYWD", 0x00000800, std::string_view{"One\0", 4}}});
  auto second = make_plugin("Second.esp", {"Owner.esm"},
      {{"KYWD", 0x00000801, std::string_view{"Two\0", 4}}});
  std::vector<fo4::Plugin*> order{&owner, &referring, &first, &second};
  const auto before_referring = fo4::write_plugin(referring);
  const auto before_first = fo4::write_plugin(first);
  const auto before_second = fo4::write_plugin(second);
  EXPECT_EQ(fo4::cleanup_injected_form_list(order, 1, fo4::FormKey::parse("000900:Referrer.esp")),
            fo4::InjectedCleanupStatus::unsupported);
  EXPECT_EQ(fo4::write_plugin(referring), before_referring);
  EXPECT_EQ(fo4::write_plugin(first), before_first);
  EXPECT_EQ(fo4::write_plugin(second), before_second);
}

TEST(Maintenance, CleanupReportsNoInjectedReferencesAndLeavesBothPluginsUntouched) {
  auto owner = make_plugin("Owner.esm", {}, {{"KYWD", 0x00000800, std::string_view{"OwnerKw\0", 8}}});
  auto referring = make_plugin("Referrer.esp", {"Owner.esm"},
      {{"FLST", 0x01000900, std::string_view{"List\0", 5}, 0, 0,
        {{"LNAM", std::string_view{"\x00\x08\x00\x00", 4}},
         {"LNAM", std::string_view{"\x01\x09\x00\x01", 4}}}},
       {"KYWD", 0x01000901, std::string_view{"Local\0", 6}}});
  auto injection = make_plugin("Injector.esp", {"Owner.esm"},
      {{"KYWD", 0x01000801, std::string_view{"InjectorLocal\0", 14}}});
  std::vector<fo4::Plugin*> order{&owner, &referring, &injection};
  const auto before_owner = fo4::write_plugin(owner);
  const auto before_referring = fo4::write_plugin(referring);
  const auto before_injection = fo4::write_plugin(injection);
  EXPECT_EQ(fo4::cleanup_injected_form_list(order, 1, fo4::FormKey::parse("000900:Referrer.esp")),
            fo4::InjectedCleanupStatus::no_injected_references);
  EXPECT_EQ(fo4::write_plugin(owner), before_owner);
  EXPECT_EQ(fo4::write_plugin(referring), before_referring);
  EXPECT_EQ(fo4::write_plugin(injection), before_injection);
}

TEST(Maintenance, CleanupSkipsEntriesWhoseFirstHolderIsAMasterOfTheReferringPlugin) {
  auto owner = make_plugin("Owner.esm", {}, {});
  auto source = make_plugin("Source.esp", {"Owner.esm"},
      {{"KYWD", 0x00000800, std::string_view{"Overridden\0", 11}}});
  auto referring = make_plugin("Referrer.esp", {"Owner.esm", "Source.esp"},
      {{"FLST", 0x02000900, std::string_view{"List\0", 5}, 0, 0,
        {{"LNAM", std::string_view{"\x00\x08\x00\x00", 4}}}}});
  std::vector<fo4::Plugin*> order{&owner, &source, &referring};
  const auto before_owner = fo4::write_plugin(owner);
  const auto before_source = fo4::write_plugin(source);
  const auto before_referring = fo4::write_plugin(referring);
  EXPECT_EQ(fo4::cleanup_injected_form_list(order, 2, fo4::FormKey::parse("000900:Referrer.esp")),
            fo4::InjectedCleanupStatus::no_injected_references);
  EXPECT_EQ(fo4::write_plugin(owner), before_owner);
  EXPECT_EQ(fo4::write_plugin(source), before_source);
  EXPECT_EQ(fo4::write_plugin(referring), before_referring);
}

TEST(Maintenance, CleanupKeepsNullEntriesWhileRemovingInjectedOnes) {
  auto owner = make_plugin("Owner.esm", {}, {});
  auto referring = make_plugin("Referrer.esp", {"Owner.esm"},
      {{"FLST", 0x01000900, std::string_view{"List\0", 5}, 0, 0,
        {{"LNAM", std::string_view{"\x00\x00\x00\x00", 4}},
         {"LNAM", std::string_view{"\x00\x08\x00\x00", 4}}}}});
  auto injection = make_plugin("Injector.esp", {"Owner.esm"},
      {{"KYWD", 0x00000800, std::string_view{"Injected\0", 9}}});
  std::vector<fo4::Plugin*> order{&owner, &referring, &injection};
  const auto list = fo4::FormKey::parse("000900:Referrer.esp");
  ASSERT_EQ(fo4::cleanup_injected_form_list(order, 1, list), fo4::InjectedCleanupStatus::updated);
  const auto after_referrer = fo4::read_plugin(fo4::write_plugin(referring), "Referrer.esp");
  const auto* cleaned = fo4::find_record(after_referrer, list);
  ASSERT_NE(cleaned, nullptr);
  ASSERT_EQ(cleaned->subrecords.size(), 2u);
  EXPECT_EQ(cleaned->subrecords[1].data, (std::vector<std::byte>(4, std::byte{0})));
}

TEST(Maintenance, CleanupRefusesTheAllOnesEntryWithoutChangingEitherPlugin) {
  auto owner = make_plugin("Owner.esm", {}, {});
  auto referring = make_plugin("Referrer.esp", {"Owner.esm"},
      {{"FLST", 0x01000900, std::string_view{"List\0", 5}, 0, 0,
        {{"LNAM", std::string_view{"\x00\x08\x00\x00", 4}},
         {"LNAM", std::string_view{"\xff\xff\xff\xff", 4}}}}});
  auto injection = make_plugin("Injector.esp", {"Owner.esm"},
      {{"KYWD", 0x00000800, std::string_view{"Injected\0", 9}}});
  std::vector<fo4::Plugin*> order{&owner, &referring, &injection};
  const auto before_referring = fo4::write_plugin(referring);
  const auto before_injection = fo4::write_plugin(injection);
  EXPECT_EQ(fo4::cleanup_injected_form_list(order, 1, fo4::FormKey::parse("000900:Referrer.esp")),
            fo4::InjectedCleanupStatus::unsupported);
  EXPECT_EQ(fo4::write_plugin(referring), before_referring);
  EXPECT_EQ(fo4::write_plugin(injection), before_injection);
}

TEST(Maintenance, FindsIdenticalOverrideAndIgnoresChangedOne) {
  const auto base = make_plugin("Base.esm", {}, {{"KYWD", 0x000800, std::string_view{"Kw\0", 3}},
                                                 {"KYWD", 0x000801, std::string_view{"Other\0", 6}}});
  const auto patch = make_plugin("Patch.esp", {"Base.esm"},
                                 {{"KYWD", 0x00000800, std::string_view{"Kw\0", 3}},
                                  {"KYWD", 0x00000801, std::string_view{"Changed\0", 8}}});
  const std::vector<const fo4::Plugin*> load_order{&base, &patch};
  const auto itm = fo4::find_identical_to_master(load_order, 1);
  ASSERT_EQ(itm.size(), 1u);
  EXPECT_EQ(itm[0].key, fo4::FormKey::parse("000800:Base.esm"));
  EXPECT_EQ(itm[0].previous.filename(), "Base.esm");
}

TEST(Maintenance, IdenticalAcrossDifferentMasterOrder) {
  const auto base = make_plugin("Base.esm", {}, {{"KYWD", 0x000800, std::string_view{"Kw\0", 3}}});
  const auto mod = make_plugin("Mod.esp", {"Base.esm"},
                               {{"MISC", 0x01000900, std::string_view{"Item\0", 5}, 0x00000800}});
  const auto patch = make_plugin("Patch.esp", {"Mod.esp", "Base.esm"},
                                 {{"MISC", 0x00000900, std::string_view{"Item\0", 5}, 0x01000800}});
  const std::vector<const fo4::Plugin*> load_order{&base, &mod, &patch};
  const auto itm = fo4::find_identical_to_master(load_order, 2);
  ASSERT_EQ(itm.size(), 1u);
  EXPECT_EQ(itm[0].key, fo4::FormKey::parse("000900:Mod.esp"));
  EXPECT_EQ(itm[0].previous.filename(), "Mod.esp");
}

TEST(Maintenance, IgnoredSubrecordDifferenceStillIdenticalToMaster) {
  const auto base = make_plugin("Base.esm", {},
                                {{"TACT", 0x000800, std::string_view{"Item\0", 5}, 0, 0,
                                  {{"PNAM", std::string_view{"\x01\0\0\0", 4}}}}});
  const auto patch = make_plugin("Patch.esp", {"Base.esm"},
                                 {{"TACT", 0x00000800, std::string_view{"Item\0", 5}, 0, 0,
                                   {{"PNAM", std::string_view{"\x02\0\0\0", 4}}}}});
  const std::vector<const fo4::Plugin*> load_order{&base, &patch};
  const auto itm = fo4::find_identical_to_master(load_order, 1);
  ASSERT_EQ(itm.size(), 1u);
  EXPECT_EQ(itm[0].key, fo4::FormKey::parse("000800:Base.esm"));
  EXPECT_EQ(itm[0].previous.filename(), "Base.esm");
}

TEST(Maintenance, ExactScanKeepsOverrideThatDiffersInAnIgnoredSubrecord) {
  const auto base = make_plugin("Base.esm", {},
                                {{"TACT", 0x000800, std::string_view{"Item\0", 5}, 0, 0,
                                  {{"PNAM", std::string_view{"\x01\0\0\0", 4}}}}});
  auto patch = make_plugin("Patch.esp", {"Base.esm"},
                           {{"TACT", 0x00000800, std::string_view{"Item\0", 5}, 0, 0,
                             {{"PNAM", std::string_view{"\x02\0\0\0", 4}}}}});
  const std::vector<const fo4::Plugin*> load_order{&base, &patch};
  ASSERT_EQ(fo4::find_identical_to_master(load_order, 1).size(), 1u);
  EXPECT_TRUE(fo4::find_identical_to_master(load_order, 1, 0, true).empty());
}

TEST(Maintenance, ExactScanStillRemovesByteIdenticalOverride) {
  const auto base = make_plugin("Base.esm", {},
                                {{"TACT", 0x000800, std::string_view{"Item\0", 5}, 0, 0,
                                  {{"PNAM", std::string_view{"\x01\0\0\0", 4}}}}});
  auto patch = make_plugin("Patch.esp", {"Base.esm"},
                           {{"TACT", 0x00000800, std::string_view{"Item\0", 5}, 0, 0,
                             {{"PNAM", std::string_view{"\x01\0\0\0", 4}}}}});
  const std::vector<const fo4::Plugin*> load_order{&base, &patch};
  const auto itm = fo4::find_identical_to_master(load_order, 1, 0, true);
  ASSERT_EQ(itm.size(), 1u);
  const std::vector<fo4::FormKey> keys{itm[0].key};
  EXPECT_EQ(fo4::remove_records(patch, keys), 1u);
  EXPECT_EQ(fo4::find_record(patch, itm[0].key), nullptr);
}

TEST(Maintenance, RealFieldDifferenceIsNotIdenticalEvenWithIgnoredSubrecord) {
  const auto base = make_plugin("Base.esm", {},
                                {{"TACT", 0x000800, std::string_view{"Item\0", 5}, 0, 0,
                                  {{"PNAM", std::string_view{"\x01\0\0\0", 4}}}}});
  const auto patch = make_plugin("Patch.esp", {"Base.esm"},
                                 {{"TACT", 0x00000800, std::string_view{"Other\0", 6}, 0, 0,
                                   {{"PNAM", std::string_view{"\x02\0\0\0", 4}}}}});
  const std::vector<const fo4::Plugin*> load_order{&base, &patch};
  EXPECT_TRUE(fo4::find_identical_to_master(load_order, 1).empty());
}

TEST(Maintenance, CellWaterHeightIsComparedForInteriorAndExteriorCells) {
  const std::string_view interior{"\x01\x00", 2};
  const std::string_view exterior{"\x00\x00", 2};
  const std::string_view water_low{"\x00\x00\x80\x3f", 4};
  const std::string_view water_high{"\x00\x00\x00\x40", 4};
  const auto itm_count = [](std::string_view data, std::string_view base_water, std::string_view patch_water) {
    const auto base = make_plugin("Base.esm", {},
                                  {{"CELL", 0x000800, std::string_view{"Cell\0", 5}, 0, 0,
                                    {{"DATA", data}, {"XCLW", base_water}}}});
    const auto patch = make_plugin("Patch.esp", {"Base.esm"},
                                   {{"CELL", 0x00000800, std::string_view{"Cell\0", 5}, 0, 0,
                                     {{"DATA", data}, {"XCLW", patch_water}}}});
    const std::vector<const fo4::Plugin*> load_order{&base, &patch};
    return fo4::find_identical_to_master(load_order, 1).size();
  };
  EXPECT_EQ(itm_count(interior, water_low, water_low), 1u);
  EXPECT_EQ(itm_count(interior, water_low, water_high), 0u);
  EXPECT_EQ(itm_count(exterior, water_low, water_low), 1u);
  EXPECT_EQ(itm_count(exterior, water_low, water_high), 0u);
}

TEST(Maintenance, OverrideIdenticalToMasterIsNotItmWhenAnInterveningOverrideDiffers) {
  const auto master = make_plugin("A.esm", {}, {{"MISC", 0x000800, std::string_view{"Item\0", 5}}});
  const auto intervening = make_plugin("B.esp", {"A.esm"}, {{"MISC", 0x00000800, std::string_view{"Changed\0", 8}}});
  const auto last = make_plugin("C.esp", {"A.esm"}, {{"MISC", 0x00000800, std::string_view{"Item\0", 5}}});
  const std::vector<const fo4::Plugin*> load_order{&master, &intervening, &last};
  EXPECT_TRUE(fo4::find_identical_to_master(load_order, 2).empty());
}

TEST(Maintenance, OverrideIdenticalToAnInterveningOverrideIsNotItmWhenItDiffersFromMaster) {
  const auto master = make_plugin("A.esm", {}, {{"MISC", 0x000800, std::string_view{"Item\0", 5}}});
  const auto intervening = make_plugin("B.esp", {"A.esm"}, {{"MISC", 0x00000800, std::string_view{"Changed\0", 8}}});
  const auto last = make_plugin("C.esp", {"A.esm"}, {{"MISC", 0x00000800, std::string_view{"Changed\0", 8}}});
  const std::vector<const fo4::Plugin*> load_order{&master, &intervening, &last};
  EXPECT_TRUE(fo4::find_identical_to_master(load_order, 2).empty());
}

TEST(Maintenance, OverrideIdenticalToMasterIsItmWhileALaterOverrideStillHoldsTheRecord) {
  const auto master = make_plugin("A.esm", {}, {{"MISC", 0x000800, std::string_view{"Item\0", 5}}});
  const auto first = make_plugin("B.esp", {"A.esm"}, {{"MISC", 0x00000800, std::string_view{"Changed\0", 8}}});
  const auto middle = make_plugin("C.esp", {"A.esm"}, {{"MISC", 0x00000800, std::string_view{"Item\0", 5}}});
  const auto last = make_plugin("D.esp", {"A.esm"}, {{"MISC", 0x00000800, std::string_view{"Item\0", 5}}});
  const std::vector<const fo4::Plugin*> load_order{&master, &first, &middle, &last};
  const auto itm = fo4::find_identical_to_master(load_order, 2);
  ASSERT_EQ(itm.size(), 1u);
  EXPECT_EQ(itm[0].previous.filename(), "A.esm");
  EXPECT_TRUE(fo4::find_identical_to_master(load_order, 3).empty());
}

TEST(Maintenance, CountsDeletedRecordsAndNavmeshes) {
  auto plugin = make_plugin(
      "Dirty.esp", {}, {{"REFR", 0x000800, std::string_view{"A\0", 2}, 0, fo4::RecordFlags::deleted},
                        {"NAVM", 0x000801, std::string_view{"B\0", 2}, 0, fo4::RecordFlags::deleted},
                        {"MISC", 0x000802, std::string_view{"C\0", 2}}});
  const auto counts = fo4::count_deleted_records(plugin);
  EXPECT_EQ(counts.records, 2u);
  EXPECT_EQ(counts.references, 1u);
  EXPECT_EQ(counts.navmeshes, 1u);
}

TEST(Maintenance, EslEligibilityCountsNativeRecordsOutsideTheLightRange) {
  auto plugin = make_plugin("Mod.esp", {"Base.esm"},
                                  {{"KYWD", 0x00000800, std::string_view{"Override\0", 9}},
                                   {"MISC", 0x01000900, std::string_view{"Small\0", 6}},
                                   {"MISC", 0x01001000, std::string_view{"Large\0", 6}}});
  const auto esl = fo4::esl_eligibility(plugin);
  EXPECT_EQ(esl.native_records, 2u);
  EXPECT_EQ(esl.out_of_range, 1u);
  EXPECT_EQ(esl.capacity, 0xFFEu);
  EXPECT_TRUE(esl.can_fit());
  EXPECT_FALSE(esl.ids_fit_light_range());
}

TEST(Maintenance, EslEligibilityFollowsTheXEditRecordLimitAndIdCeiling) {
  const auto native = [](std::uint32_t count, std::uint32_t first) {
    std::vector<RecordSpec> records;
    for (std::uint32_t i = 0; i < count; ++i) {
      records.push_back({"MISC", 0x01000000u | (first + i), std::string_view{"M\0", 2}});
    }
    return fo4::esl_eligibility(make_plugin("Mod.esp", {"Base.esm"}, records));
  };
  EXPECT_TRUE(native(2049, 0x1000).can_fit());
  EXPECT_TRUE(native(0xFFE, 0x1000).can_fit());
  EXPECT_FALSE(native(0xFFF, 0x1000).can_fit());

  const auto ceiling = native(1, 0xFFF);
  EXPECT_EQ(ceiling.out_of_range, 0u);
  EXPECT_TRUE(ceiling.ids_fit_light_range());
  const auto past = native(1, 0x1000);
  EXPECT_EQ(past.out_of_range, 1u);
  EXPECT_FALSE(past.ids_fit_light_range());
}

TEST(Maintenance, EslEligibilityKeepsTheFullFloorWithoutHedrOneAndAMaster) {
  const std::vector<RecordSpec> own{{"MISC", 0x00001000, std::string_view{"M\0", 2}}};
  EXPECT_EQ(fo4::esl_eligibility(make_plugin("Mod.esp", {}, own)).capacity, 0x800u);
  const std::vector<RecordSpec> mastered{{"MISC", 0x01001000, std::string_view{"M\0", 2}}};
  EXPECT_EQ(fo4::esl_eligibility(make_plugin("Mod.esp", {"Base.esm"}, mastered, 0.95f)).capacity, 0x800u);
  EXPECT_EQ(fo4::esl_eligibility(make_plugin("Mod.esp", {"Base.esm"}, mastered)).capacity, 0xFFEu);
}

TEST(Maintenance, EslEligibilityCountsNativeIdsBelowTheTargetFloor) {
  const auto masterless = [](std::uint32_t id) {
    return fo4::esl_eligibility(make_plugin("Mod.esp", {}, {{"MISC", id, std::string_view{"M\0", 2}}}));
  };
  EXPECT_EQ(masterless(0x7FF).out_of_range, 1u);
  EXPECT_EQ(masterless(0x7FF).capacity, 0x800u);
  EXPECT_EQ(masterless(0x800).out_of_range, 0u);
  EXPECT_EQ(masterless(0xFFF).out_of_range, 0u);
  EXPECT_EQ(masterless(0x1000).out_of_range, 1u);

  const auto mastered = [](std::uint32_t id, float version = 1.0f) {
    return fo4::esl_eligibility(
        make_plugin("Mod.esp", {"Base.esm"}, {{"MISC", 0x01000000u | id, std::string_view{"M\0", 2}}}, version));
  };
  EXPECT_EQ(mastered(0x000).out_of_range, 1u);
  EXPECT_EQ(mastered(0x001).out_of_range, 0u);
  EXPECT_EQ(mastered(0x7FF).out_of_range, 0u);
  EXPECT_EQ(mastered(0x7FF, 0.95f).out_of_range, 1u);
  EXPECT_EQ(mastered(0x800, 0.95f).out_of_range, 0u);
}
