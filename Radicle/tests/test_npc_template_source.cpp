#include "npc_visual_fixture.hpp"

#include "fo4/c_api_env.h"
#include "fo4/c_api_cell.h"
#include "fo4/c_api_gameplay.h"
#include "fo4/forms/npc_gameplay.hpp"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

using fo4::FormKey;
using fo4::forms::GlobalValueReason;
using fo4::forms::NpcTemplateReason;
using fo4::forms::NpcTemplateSlot;
using npc_visual_fixture::Bytes;
using npc_visual_fixture::Rec;
using npc_visual_fixture::Subs;
using npc_visual_fixture::acbs;
using npc_visual_fixture::build;
using npc_visual_fixture::floats;
using npc_visual_fixture::key;
using npc_visual_fixture::make_env;
using npc_visual_fixture::words;
using npc_visual_fixture::zstr;

constexpr std::uint16_t k_bit_ai_packages = 0x20;
constexpr std::uint16_t k_bit_inventory = 0x100;
constexpr std::uint16_t k_bit_keywords = 0x1000;
constexpr std::uint32_t k_compressed_flag = 0x00040000;

constexpr std::uint32_t k_child = 0xA00;
constexpr std::uint32_t k_parent = 0xA01;
constexpr std::uint32_t k_grandparent = 0xA02;
constexpr std::uint32_t k_other = 0xA03;
constexpr std::uint32_t k_plain = 0xA04;
constexpr std::uint32_t k_dangling = 0xA05;
constexpr std::uint32_t k_to_race = 0xA06;
constexpr std::uint32_t k_to_lvln = 0xA07;
constexpr std::uint32_t k_loop = 0xA08;
constexpr std::uint32_t k_to_corrupt = 0xA09;
constexpr std::uint32_t k_no_link = 0xA0A;
constexpr std::uint32_t k_race = 0xA10;
constexpr std::uint32_t k_lvln = 0xA11;
constexpr std::uint32_t k_corrupt = 0xA12;
constexpr std::uint32_t k_missing = 0xAFF;

constexpr std::uint16_t k_bit_traits = 0x1;
constexpr std::uint16_t k_bit_def_pack_list = 0x400;
constexpr std::uint32_t k_glob_constant_flag = 0x40;
constexpr std::uint32_t k_pk_a = 0xC10;
constexpr std::uint32_t k_pk_b = 0xC11;
constexpr std::uint32_t k_pk_c = 0xC12;
constexpr std::uint32_t k_flst = 0xC20;
constexpr std::uint32_t k_pk_own = 0xC30;
constexpr std::uint32_t k_pk_child = 0xC31;
constexpr std::uint32_t k_pk_ai_only = 0xC32;
constexpr std::uint32_t k_pk_ai_stuck = 0xC33;
constexpr std::uint32_t k_pk_def_stuck = 0xC34;
constexpr std::uint32_t k_pk_dplt_not_flst = 0xC35;
constexpr std::uint32_t k_pk_dplt_missing = 0xC36;
constexpr std::uint32_t k_pk_bare = 0xC37;
constexpr std::uint32_t k_pk_bad_pkid = 0xC38;
constexpr std::uint32_t k_v_a = 0xC50;
constexpr std::uint32_t k_v_b = 0xC51;
constexpr std::uint32_t k_v_parent = 0xC60;
constexpr std::uint32_t k_v_own = 0xC61;
constexpr std::uint32_t k_v_child = 0xC62;
constexpr std::uint32_t k_v_flagged_own = 0xC63;
constexpr std::uint32_t k_v_tpta_split = 0xC64;
constexpr std::uint32_t k_v_none = 0xC65;
constexpr std::uint32_t k_v_to_lvln = 0xC66;
constexpr std::uint32_t k_v_loop = 0xC67;
constexpr std::uint32_t k_v_to_race = 0xC68;
constexpr std::uint32_t k_v_to_missing = 0xC69;
constexpr std::uint32_t k_v_null_vtck = 0xC6A;
constexpr std::uint32_t k_glob_const = 0xC70;
constexpr std::uint32_t k_glob_plain = 0xC71;
constexpr std::uint32_t k_glob_no_value = 0xC72;
constexpr std::uint32_t k_glob_bad_value = 0xC73;

Bytes tpta_at(std::size_t slot, std::uint32_t target) {
  std::array<std::uint32_t, 13> links{};
  links[slot] = target;
  fo4::test::ByteWriter out;
  for (const auto link : links) out.u32(link);
  return out.bytes;
}

Rec npc_with(std::uint32_t id, std::uint16_t template_flags, Subs extra = {}) {
  Subs subs{{"EDID", zstr("Npc" + std::to_string(id))}, {"ACBS", acbs(0, template_flags)}};
  subs.insert(subs.end(), extra.begin(), extra.end());
  return Rec{"NPC_", id, std::move(subs)};
}

std::vector<Rec> records() {
  return {
      Rec{"RACE", k_race, {{"EDID", zstr("Race")}}},
      Rec{"LVLN", k_lvln, {{"EDID", zstr("Leveled")}}},
      Rec{"NPC_", k_corrupt, {{"EDID", zstr("Corrupt")}}, k_compressed_flag},
      npc_with(k_grandparent, 0),
      npc_with(k_parent, k_bit_keywords, {{"TPLT", words({k_grandparent})}}),
      npc_with(k_other, 0),
      npc_with(k_plain, 0, {{"TPLT", words({k_parent})}}),
      npc_with(k_child, k_bit_keywords, {{"TPLT", words({k_parent})}}),
      npc_with(k_dangling, k_bit_keywords, {{"TPLT", words({k_missing})}}),
      npc_with(k_to_race, k_bit_keywords, {{"TPLT", words({k_race})}}),
      npc_with(k_to_lvln, k_bit_keywords, {{"TPLT", words({k_lvln})}}),
      npc_with(k_loop, k_bit_keywords, {{"TPLT", words({k_loop})}}),
      npc_with(k_to_corrupt, k_bit_keywords, {{"TPLT", words({k_corrupt})}}),
      npc_with(k_no_link, k_bit_keywords),
      Rec{"FLST", k_flst, {{"EDID", zstr("Defaults")}, {"LNAM", words({k_pk_b})}, {"LNAM", words({0})}, {"LNAM", words({k_pk_c})}}},
      npc_with(k_pk_own, 0, {{"PKID", words({k_pk_a})}, {"PKID", words({k_pk_a})}, {"DPLT", words({k_flst})}}),
      npc_with(k_pk_child, k_bit_ai_packages | k_bit_def_pack_list, {{"TPLT", words({k_pk_own})}}),
      npc_with(k_pk_ai_only, k_bit_ai_packages, {{"TPLT", words({k_pk_own})}, {"DPLT", words({k_flst})}}),
      npc_with(k_pk_ai_stuck, k_bit_ai_packages, {{"TPLT", words({k_lvln})}}),
      npc_with(k_pk_def_stuck, k_bit_def_pack_list, {{"PKID", words({k_pk_a})}}),
      npc_with(k_pk_dplt_not_flst, 0, {{"PKID", words({k_pk_b})}, {"DPLT", words({k_race})}}),
      npc_with(k_pk_dplt_missing, 0, {{"PKID", words({k_pk_b})}, {"DPLT", words({k_missing})}}),
      npc_with(k_pk_bare, 0),
      npc_with(k_pk_bad_pkid, 0, {{"PKID", Bytes(3)}}),
      npc_with(k_v_parent, 0, {{"VTCK", words({k_v_b})}}),
      npc_with(k_v_own, 0, {{"VTCK", words({k_v_a})}, {"TPLT", words({k_v_parent})}}),
      npc_with(k_v_child, 0, {{"TPLT", words({k_v_parent})}}),
      npc_with(k_v_flagged_own, k_bit_traits, {{"VTCK", words({k_v_a})}, {"TPLT", words({k_v_parent})}}),
      npc_with(k_v_tpta_split, k_bit_traits, {{"TPLT", words({k_v_parent})}, {"TPTA", tpta_at(0, k_other)}}),
      npc_with(k_v_none, 0),
      npc_with(k_v_to_lvln, 0, {{"TPLT", words({k_lvln})}}),
      npc_with(k_v_loop, 0, {{"TPLT", words({k_v_loop})}}),
      npc_with(k_v_to_race, 0, {{"TPLT", words({k_race})}}),
      npc_with(k_v_to_missing, 0, {{"TPLT", words({k_missing})}}),
      npc_with(k_v_null_vtck, 0, {{"VTCK", words({0})}, {"TPLT", words({k_v_parent})}}),
      Rec{"GLOB", k_glob_const, {{"EDID", zstr("Const")}, {"FNAM", Bytes{std::byte{'f'}}}, {"FLTV", floats({0.1f})}}, k_glob_constant_flag},
      Rec{"GLOB", k_glob_plain, {{"EDID", zstr("Plain")}, {"FNAM", Bytes{std::byte{'f'}}}, {"FLTV", floats({-2.25f})}}},
      Rec{"GLOB", k_glob_no_value, {{"EDID", zstr("NoValue")}, {"FNAM", Bytes{std::byte{'f'}}}}, k_glob_constant_flag},
      Rec{"GLOB", k_glob_bad_value, {{"EDID", zstr("BadValue")}, {"FLTV", Bytes(3)}}, k_glob_constant_flag},
  };
}

std::vector<FormKey> keys_of(std::initializer_list<std::uint32_t> locals) {
  std::vector<FormKey> keys;
  for (const auto local : locals) keys.push_back(key(local));
  return keys;
}

fo4::forms::NpcTemplateSource resolve(const fo4::env::Environment& env, std::uint32_t local, NpcTemplateSlot slot) {
  return fo4::forms::resolve_npc_template_source(env, *env.winner(key(local)), slot);
}

fo4::forms::NpcPackageList packages(const fo4::env::Environment& env, std::uint32_t local) {
  return fo4::forms::npc_package_list(env, *env.winner(key(local)));
}

fo4::forms::NpcVoiceType voice(const fo4::env::Environment& env, std::uint32_t local) {
  return fo4::forms::resolve_npc_voice_type(env, *env.winner(key(local)));
}

fo4::forms::GlobalValue glob(const fo4::env::Environment& env, std::uint32_t local, bool constant_only) {
  return fo4::forms::read_global_value(*env.winner(key(local)), constant_only);
}

fo4::env::Environment env_with(std::vector<Rec> extra = {}) {
  auto all = records();
  for (auto& rec : extra) all.push_back(std::move(rec));
  return make_env({{"Base.esm", build({}, 0x1, all)}});
}

class NpcTemplateSourceCApi : public ::testing::Test {
protected:
  void SetUp() override {
    root_ = std::filesystem::temp_directory_path() /
            ("radicle_npc_template_source_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root_);
    const auto path = (root_ / "Base.esm").string();
    const auto bytes = build({}, 0x1, records());
    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    stream.close();
    const char* paths[] = {path.c_str()};
    radicle_env* env = nullptr;
    ASSERT_EQ(radicle_env_load(paths, 1, &env), RADICLE_STATUS_OK);
    env_.reset(env);
  }

  void TearDown() override {
    env_.reset();
    std::error_code error;
    std::filesystem::remove_all(root_, error);
  }

  const radicle_record* winner(std::uint32_t local) {
    const radicle_record* record = nullptr;
    EXPECT_EQ(radicle_env_winner(env_.get(), radicle_form_key{"Base.esm", local}, &record), RADICLE_STATUS_OK);
    return record;
  }

  std::filesystem::path root_;
  std::unique_ptr<radicle_env, decltype(&radicle_env_destroy)> env_{nullptr, radicle_env_destroy};
};

}  // namespace

TEST(NpcTemplateSource, ClearFlagMeansTheNpcOwnsTheSlotEvenWithATemplateLink) {
  const auto env = env_with();
  const auto result = resolve(env, k_plain, NpcTemplateSlot::keywords);
  EXPECT_EQ(result.reason, NpcTemplateReason::own);
  EXPECT_EQ(result.source, key(k_plain));
  EXPECT_EQ(result.hops, 0u);
  EXPECT_FALSE(result.blocker.has_value());
}

TEST(NpcTemplateSource, SetFlagFollowsTpltAndStopsAtTheFirstTemplateThatOwnsTheSlot) {
  const auto env = env_with();
  const auto result = resolve(env, k_child, NpcTemplateSlot::keywords);
  EXPECT_EQ(result.reason, NpcTemplateReason::template_npc);
  EXPECT_EQ(result.source, key(k_grandparent));
  EXPECT_EQ(result.hops, 2u);
  const auto other_slot = resolve(env, k_child, NpcTemplateSlot::traits);
  EXPECT_EQ(other_slot.reason, NpcTemplateReason::own);
  EXPECT_EQ(other_slot.source, key(k_child));
}

TEST(NpcTemplateSource, EachSlotReadsItsOwnFlagBitAndItsOwnTptaLink) {
  const auto env = env_with({npc_with(0xA20, k_bit_ai_packages | k_bit_inventory,
                                       {{"TPLT", words({k_other})}, {"TPTA", tpta_at(5, k_parent)}})});
  const auto packages = resolve(env, 0xA20, NpcTemplateSlot::ai_packages);
  EXPECT_EQ(packages.reason, NpcTemplateReason::template_npc);
  EXPECT_EQ(packages.source, key(k_parent));
  EXPECT_EQ(packages.hops, 1u);
  const auto inventory = resolve(env, 0xA20, NpcTemplateSlot::inventory);
  EXPECT_EQ(inventory.source, key(k_other));
  const auto def_pack = resolve(env, 0xA20, NpcTemplateSlot::def_pack_list);
  EXPECT_EQ(def_pack.reason, NpcTemplateReason::own);
}

TEST(NpcTemplateSource, ZeroTptaEntryFallsBackToTplt) {
  const auto env = env_with({npc_with(0xA21, k_bit_ai_packages, {{"TPLT", words({k_other})}, {"TPTA", tpta_at(4, k_parent)}})});
  const auto result = resolve(env, 0xA21, NpcTemplateSlot::ai_packages);
  EXPECT_EQ(result.source, key(k_other));
}

TEST(NpcTemplateSource, BrokenChainsKeepTheLastNpcAndNameTheReason) {
  const auto env = env_with();
  const auto no_link = resolve(env, k_no_link, NpcTemplateSlot::keywords);
  EXPECT_EQ(no_link.reason, NpcTemplateReason::no_link);
  EXPECT_EQ(no_link.source, key(k_no_link));
  EXPECT_FALSE(no_link.blocker.has_value());

  const auto dangling = resolve(env, k_dangling, NpcTemplateSlot::keywords);
  EXPECT_EQ(dangling.reason, NpcTemplateReason::not_found);
  EXPECT_EQ(dangling.source, key(k_dangling));
  EXPECT_EQ(dangling.blocker, key(k_missing));

  const auto race = resolve(env, k_to_race, NpcTemplateSlot::keywords);
  EXPECT_EQ(race.reason, NpcTemplateReason::not_npc);
  EXPECT_EQ(race.blocker, key(k_race));

  const auto leveled = resolve(env, k_to_lvln, NpcTemplateSlot::keywords);
  EXPECT_EQ(leveled.reason, NpcTemplateReason::leveled_list);
  EXPECT_EQ(leveled.source, key(k_to_lvln));
  EXPECT_EQ(leveled.blocker, key(k_lvln));

  const auto corrupt = resolve(env, k_to_corrupt, NpcTemplateSlot::keywords);
  EXPECT_EQ(corrupt.reason, NpcTemplateReason::undecodable);
  EXPECT_EQ(corrupt.blocker, key(k_corrupt));
}

TEST(NpcTemplateSource, ACycleStopsAfterEightHops) {
  const auto env = env_with();
  const auto result = resolve(env, k_loop, NpcTemplateSlot::keywords);
  EXPECT_EQ(result.reason, NpcTemplateReason::too_deep);
  EXPECT_EQ(result.hops, 8u);
  EXPECT_EQ(result.source, key(k_loop));
  EXPECT_EQ(result.blocker, key(k_loop));
}

TEST(NpcTemplateSource, ANonNpcRecordIsRejected) {
  const auto env = env_with();
  EXPECT_THROW(resolve(env, k_race, NpcTemplateSlot::keywords), fo4::forms::GameplayError);
}

TEST(NpcTemplateSource, EveryOfTheThirteenSlotsPairsItsFlagBitWithItsTptaLink) {
  constexpr std::uint32_t k_target_base = 0xB00;
  constexpr std::uint32_t k_host_base = 0xB20;
  std::vector<Rec> extra;
  for (std::uint32_t slot = 0; slot < 13; ++slot) {
    extra.push_back(npc_with(k_target_base + slot, 0));
    extra.push_back(npc_with(k_host_base + slot, static_cast<std::uint16_t>(1u << slot),
                             {{"TPTA", tpta_at(slot, k_target_base + slot)}}));
  }
  const auto env = env_with(std::move(extra));
  for (std::uint32_t host = 0; host < 13; ++host) {
    for (std::uint32_t slot = 0; slot < 13; ++slot) {
      const auto result = resolve(env, k_host_base + host, static_cast<NpcTemplateSlot>(slot));
      if (slot == host) {
        EXPECT_EQ(result.reason, NpcTemplateReason::template_npc) << "slot " << slot;
        EXPECT_EQ(result.source, key(k_target_base + slot)) << "slot " << slot;
        EXPECT_EQ(result.hops, 1u) << "slot " << slot;
      } else {
        EXPECT_EQ(result.reason, NpcTemplateReason::own) << "host " << host << " slot " << slot;
        EXPECT_EQ(result.source, key(k_host_base + host)) << "host " << host << " slot " << slot;
      }
    }
  }
}

TEST_F(NpcTemplateSourceCApi, ReportsSourceHopsAndBlocker) {
  radicle_npc_template_source out{};
  ASSERT_EQ(radicle_npc_template_source_resolve(env_.get(), winner(k_child), RADICLE_NPC_TEMPLATE_KEYWORDS, &out),
            RADICLE_STATUS_OK);
  EXPECT_STREQ(out.source.plugin, "Base.esm");
  EXPECT_EQ(out.source.local_id, k_grandparent);
  EXPECT_EQ(out.reason, RADICLE_NPC_TEMPLATE_REASON_TEMPLATE_NPC);
  EXPECT_EQ(out.hops, 2u);
  EXPECT_EQ(out.has_blocker, 0);

  ASSERT_EQ(radicle_npc_template_source_resolve(env_.get(), winner(k_to_lvln), RADICLE_NPC_TEMPLATE_KEYWORDS, &out),
            RADICLE_STATUS_OK);
  EXPECT_EQ(out.reason, RADICLE_NPC_TEMPLATE_REASON_LEVELED_LIST);
  EXPECT_EQ(out.has_blocker, 1);
  EXPECT_EQ(out.blocker.local_id, k_lvln);
}

TEST_F(NpcTemplateSourceCApi, RejectedCallsLeaveAClearedOutput) {
  radicle_npc_template_source out{};
  out.hops = 99;
  out.has_blocker = 1;
  EXPECT_EQ(radicle_npc_template_source_resolve(env_.get(), winner(k_child), 13, &out), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(out.hops, 0u);
  EXPECT_EQ(out.has_blocker, 0);
  EXPECT_EQ(out.source.plugin, nullptr);
}

TEST_F(NpcTemplateSourceCApi, RejectsBadArgumentsAndNonNpcRecords) {
  radicle_npc_template_source out{};
  EXPECT_EQ(radicle_npc_template_source_resolve(env_.get(), winner(k_child), 13, &out), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_npc_template_source_resolve(env_.get(), winner(k_race), 0, &out), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_npc_template_source_resolve(nullptr, winner(k_child), 0, &out), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_npc_template_source_resolve(env_.get(), nullptr, 0, &out), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_npc_template_source_resolve(env_.get(), winner(k_child), 0, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST(NpcPackageList, OwnPkidThenFlstMembersInStoredOrderWithoutDedupeAndNullLinksDropped) {
  const auto env = env_with();
  const auto result = packages(env, k_pk_own);
  EXPECT_EQ(result.source.reason, NpcTemplateReason::own);
  EXPECT_EQ(result.keys, keys_of({k_pk_a, k_pk_a, k_pk_b, k_pk_c}));
}

TEST(NpcPackageList, BothTemplateFlagsTakeTheListFromTheTemplateNpc) {
  const auto env = env_with();
  const auto result = packages(env, k_pk_child);
  EXPECT_EQ(result.source.reason, NpcTemplateReason::template_npc);
  EXPECT_EQ(result.source.source, key(k_pk_own));
  EXPECT_EQ(result.keys, keys_of({k_pk_a, k_pk_a, k_pk_b, k_pk_c}));
}

TEST(NpcPackageList, EachSlotIsResolvedAloneSoTheAiFlagAloneKeepsTheOwnDefPackListOfTheNpc) {
  const auto env = env_with();
  EXPECT_EQ(packages(env, k_pk_ai_only).keys, keys_of({k_pk_a, k_pk_a, k_pk_b, k_pk_c}));
  const auto bare = packages(env, k_pk_bare);
  EXPECT_EQ(bare.source.reason, NpcTemplateReason::own);
  EXPECT_TRUE(bare.keys.empty());
}

TEST(NpcPackageList, EitherSlotStoppingMakesTheWholeListUndecidedAndNamesTheBlocker) {
  const auto env = env_with();
  const auto ai = packages(env, k_pk_ai_stuck);
  EXPECT_EQ(ai.source.reason, NpcTemplateReason::leveled_list);
  EXPECT_EQ(ai.source.blocker, key(k_lvln));
  EXPECT_TRUE(ai.keys.empty());
  const auto def = packages(env, k_pk_def_stuck);
  EXPECT_EQ(def.source.reason, NpcTemplateReason::no_link);
  EXPECT_TRUE(def.keys.empty());
}

TEST(NpcPackageList, ADefaultListThatIsMissingOrNotAnFlstContributesNothingAndStaysDecided) {
  const auto env = env_with();
  EXPECT_EQ(packages(env, k_pk_dplt_not_flst).source.reason, NpcTemplateReason::own);
  EXPECT_EQ(packages(env, k_pk_dplt_not_flst).keys, keys_of({k_pk_b}));
  EXPECT_EQ(packages(env, k_pk_dplt_missing).keys, keys_of({k_pk_b}));
}

TEST(NpcPackageList, AMalformedPkidIsAParseFailureNotAShortList) {
  const auto env = env_with();
  EXPECT_THROW(packages(env, k_pk_bad_pkid), fo4::forms::GameplayError);
}

TEST(NpcVoiceType, OwnVtckWinsEvenWhenTheTraitsFlagWouldSendTheNpcToItsTemplate) {
  const auto env = env_with();
  const auto result = voice(env, k_v_flagged_own);
  EXPECT_EQ(result.voice, key(k_v_a));
  EXPECT_EQ(result.origin.reason, NpcTemplateReason::own);
  EXPECT_EQ(resolve(env, k_v_flagged_own, NpcTemplateSlot::traits).source, key(k_v_parent));
}

TEST(NpcVoiceType, TpltIsFollowedWithoutTemplateFlagsAndTheFirstVtckOnTheChainWins) {
  const auto env = env_with();
  const auto result = voice(env, k_v_child);
  EXPECT_EQ(result.voice, key(k_v_b));
  EXPECT_EQ(result.origin.reason, NpcTemplateReason::template_npc);
  EXPECT_EQ(result.origin.source, key(k_v_parent));
  EXPECT_EQ(result.origin.hops, 1u);
  EXPECT_EQ(voice(env, k_v_own).voice, key(k_v_a));
  EXPECT_EQ(voice(env, k_v_null_vtck).voice, key(k_v_b));
}

TEST(NpcVoiceType, TpltNotTptaDrivesTheWalkEvenWithTheTraitsFlagSet) {
  const auto env = env_with();
  EXPECT_EQ(voice(env, k_v_tpta_split).voice, key(k_v_b));
  EXPECT_EQ(resolve(env, k_v_tpta_split, NpcTemplateSlot::traits).source, key(k_other));
}

TEST(NpcVoiceType, ABrokenOrAbsentChainLeavesNoVoiceAndNamesWhyAndWhere) {
  const auto env = env_with();
  EXPECT_EQ(voice(env, k_v_none).origin.reason, NpcTemplateReason::no_link);
  const auto lvln = voice(env, k_v_to_lvln);
  EXPECT_FALSE(lvln.voice.has_value());
  EXPECT_EQ(lvln.origin.reason, NpcTemplateReason::leveled_list);
  EXPECT_EQ(lvln.origin.blocker, key(k_lvln));
  EXPECT_EQ(voice(env, k_v_to_race).origin.reason, NpcTemplateReason::not_npc);
  EXPECT_EQ(voice(env, k_v_to_missing).origin.reason, NpcTemplateReason::not_found);
}

TEST(NpcVoiceType, ACycleEndsAtTheHopLimitWithoutAVoice) {
  const auto env = env_with();
  const auto result = voice(env, k_v_loop);
  EXPECT_FALSE(result.voice.has_value());
  EXPECT_EQ(result.origin.reason, NpcTemplateReason::too_deep);
  EXPECT_EQ(result.origin.hops, 8u);
}

TEST(NpcVoiceType, AVoiceEightHopsAwayIsFoundAndNineHopsAwayIsNot) {
  std::vector<Rec> extra;
  for (std::uint32_t i = 0; i <= 9; ++i) {
    Subs near_subs{{"TPLT", words({0xD01 + i})}};
    Subs far_subs{{"TPLT", words({0xD21 + i})}};
    if (i == 8) near_subs.push_back({"VTCK", words({k_v_a})});
    if (i == 9) far_subs.push_back({"VTCK", words({k_v_a})});
    extra.push_back(npc_with(0xD00 + i, 0, near_subs));
    extra.push_back(npc_with(0xD20 + i, 0, far_subs));
  }
  const auto env = env_with(std::move(extra));
  const auto near_result = voice(env, 0xD00);
  EXPECT_EQ(near_result.voice, key(k_v_a));
  EXPECT_EQ(near_result.origin.hops, 8u);
  const auto far_result = voice(env, 0xD20);
  EXPECT_FALSE(far_result.voice.has_value());
  EXPECT_EQ(far_result.origin.reason, NpcTemplateReason::too_deep);
  EXPECT_EQ(far_result.origin.source, key(0xD28));
}

TEST(GlobalValue, ConstantOnlyRequiresTheConstantRecordFlagAndOtherwiseTheFlagIsIgnored) {
  const auto env = env_with();
  EXPECT_EQ(glob(env, k_glob_const, true).reason, GlobalValueReason::ok);
  EXPECT_EQ(glob(env, k_glob_const, true).value, static_cast<double>(0.1f));
  EXPECT_EQ(glob(env, k_glob_plain, true).reason, GlobalValueReason::not_constant);
  EXPECT_EQ(glob(env, k_glob_plain, false).reason, GlobalValueReason::ok);
  EXPECT_EQ(glob(env, k_glob_plain, false).value, -2.25);
}

TEST(GlobalValue, MissingFltvIsNoValueNotAnErrorAndAWrongSizedFltvIsAParseFailure) {
  const auto env = env_with();
  EXPECT_EQ(glob(env, k_glob_no_value, true).reason, GlobalValueReason::no_value);
  EXPECT_THROW(glob(env, k_glob_bad_value, true), fo4::forms::GameplayError);
  EXPECT_EQ(glob(env, k_race, false).reason, GlobalValueReason::not_glob);
}

TEST_F(NpcTemplateSourceCApi, PackageListReturnsKeysOrAnUndecidedReasonAndFreesWithFreeFormKeys) {
  radicle_form_key* keys = nullptr;
  size_t count = 0;
  radicle_npc_template_source reason{};
  ASSERT_EQ(radicle_npc_package_list(env_.get(), winner(k_pk_child), &keys, &count, &reason), RADICLE_STATUS_OK);
  EXPECT_EQ(reason.reason, RADICLE_NPC_TEMPLATE_REASON_TEMPLATE_NPC);
  ASSERT_EQ(count, 4u);
  EXPECT_EQ(keys[0].local_id, k_pk_a);
  EXPECT_EQ(keys[3].local_id, k_pk_c);
  EXPECT_STREQ(keys[2].plugin, "Base.esm");
  radicle_free_form_keys(keys);

  ASSERT_EQ(radicle_npc_package_list(env_.get(), winner(k_pk_ai_stuck), &keys, &count, &reason), RADICLE_STATUS_OK);
  EXPECT_EQ(reason.reason, RADICLE_NPC_TEMPLATE_REASON_LEVELED_LIST);
  EXPECT_EQ(reason.has_blocker, 1);
  EXPECT_EQ(keys, nullptr);
  EXPECT_EQ(count, 0u);

  ASSERT_EQ(radicle_npc_package_list(env_.get(), winner(k_pk_bare), &keys, &count, &reason), RADICLE_STATUS_OK);
  EXPECT_EQ(reason.reason, RADICLE_NPC_TEMPLATE_REASON_OWN);
  EXPECT_EQ(keys, nullptr);
  EXPECT_EQ(count, 0u);

  EXPECT_EQ(radicle_npc_package_list(env_.get(), winner(k_race), &keys, &count, &reason), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_npc_package_list(env_.get(), winner(k_pk_bad_pkid), &keys, &count, &reason), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(radicle_npc_package_list(env_.get(), winner(k_pk_own), nullptr, &count, &reason), RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST_F(NpcTemplateSourceCApi, GlobValueReportsReasonAndSetsTheValueOnlyWhenOk) {
  double value = 7.0;
  radicle_glob_reason reason = RADICLE_GLOB_REASON_OK;
  ASSERT_EQ(radicle_glob_value(env_.get(), winner(k_glob_const), 1, &value, &reason), RADICLE_STATUS_OK);
  EXPECT_EQ(reason, RADICLE_GLOB_REASON_OK);
  EXPECT_EQ(value, static_cast<double>(0.1f));
  ASSERT_EQ(radicle_glob_value(env_.get(), winner(k_glob_plain), 1, &value, &reason), RADICLE_STATUS_OK);
  EXPECT_EQ(reason, RADICLE_GLOB_REASON_NOT_CONSTANT);
  EXPECT_EQ(value, 0.0);
  ASSERT_EQ(radicle_glob_value(env_.get(), winner(k_glob_plain), 0, &value, &reason), RADICLE_STATUS_OK);
  EXPECT_EQ(value, -2.25);
  ASSERT_EQ(radicle_glob_value(env_.get(), winner(k_race), 0, &value, &reason), RADICLE_STATUS_OK);
  EXPECT_EQ(reason, RADICLE_GLOB_REASON_NOT_GLOB);
  ASSERT_EQ(radicle_glob_value(env_.get(), winner(k_glob_no_value), 0, &value, &reason), RADICLE_STATUS_OK);
  EXPECT_EQ(reason, RADICLE_GLOB_REASON_NO_VALUE);
  EXPECT_EQ(radicle_glob_value(env_.get(), winner(k_glob_bad_value), 0, &value, &reason), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(radicle_glob_value(env_.get(), nullptr, 0, &value, &reason), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_glob_value(env_.get(), winner(k_glob_const), 0, nullptr, &reason), RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST_F(NpcTemplateSourceCApi, VoiceTypeReturnsTheKeyAndTheOriginOrAZeroedKeyWithTheReason) {
  radicle_form_key voice_key{};
  radicle_npc_template_source reason{};
  ASSERT_EQ(radicle_npc_voice_type(env_.get(), winner(k_v_child), &voice_key, &reason), RADICLE_STATUS_OK);
  EXPECT_EQ(voice_key.local_id, k_v_b);
  EXPECT_STREQ(voice_key.plugin, "Base.esm");
  EXPECT_EQ(reason.reason, RADICLE_NPC_TEMPLATE_REASON_TEMPLATE_NPC);
  EXPECT_EQ(reason.source.local_id, k_v_parent);
  ASSERT_EQ(radicle_npc_voice_type(env_.get(), winner(k_v_to_lvln), &voice_key, &reason), RADICLE_STATUS_OK);
  EXPECT_EQ(voice_key.plugin, nullptr);
  EXPECT_EQ(reason.reason, RADICLE_NPC_TEMPLATE_REASON_LEVELED_LIST);
  EXPECT_EQ(radicle_npc_voice_type(env_.get(), winner(k_race), &voice_key, &reason), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_npc_voice_type(env_.get(), winner(k_v_child), nullptr, &reason), RADICLE_STATUS_INVALID_ARGUMENT);
}
