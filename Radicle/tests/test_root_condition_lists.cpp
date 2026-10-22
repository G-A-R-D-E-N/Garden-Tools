#include "fo4/c_api.h"
#include "fo4/c_api_conditions.h"
#include "fo4/c_api_env.h"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/condition_scopes.hpp"
#include "fo4/schema/ctda.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Body = std::vector<std::pair<std::string, std::vector<std::byte>>>;

std::vector<std::byte> zeros(std::size_t count) { return std::vector<std::byte>(count); }

std::vector<std::byte> u32_bytes(std::uint32_t value) {
  fo4::test::ByteWriter writer;
  writer.u32(value);
  return writer.bytes;
}

std::vector<std::byte> string_bytes(std::string_view text) {
  fo4::test::ByteWriter writer;
  writer.raw(text);
  return writer.bytes;
}

std::vector<std::byte> ctda_bytes(std::uint16_t function) {
  fo4::test::ByteWriter ctda;
  ctda.u32(0);
  ctda.u32(0);
  ctda.u16(function);
  ctda.u16(0);
  ctda.i32(0);
  ctda.i32(0);
  ctda.u32(0);
  ctda.u32(0);
  ctda.i32(-1);
  return ctda.bytes;
}

fo4::CtdaCondition function_condition(std::uint16_t function) {
  fo4::CtdaCondition condition;
  condition.function = function;
  return condition;
}

fo4::Plugin make_plugin(std::string_view signature, const Body& body) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, signature, 0);
  const auto record = fo4::test::begin_record(out, signature, 0, 0x00000800);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"CondRecord\0", 12});
  for (const auto& [sig, data] : body) {
    fo4::test::write_subrecord(out, sig, std::span<const std::byte>{data});
  }
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return fo4::read_plugin(out.bytes, "Cond.esp");
}

const fo4::Record* cond_record(const fo4::Plugin& plugin) {
  return fo4::find_record(plugin, fo4::FormKey::parse("000800:Cond.esp"));
}

struct Snapshot {
  std::vector<std::string> signatures;
  std::vector<std::uint32_t> citc;
  std::vector<std::uint16_t> functions;
};

Snapshot snapshot(const fo4::Plugin& plugin) {
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Cond.esp");
  Snapshot out;
  const auto* record = cond_record(reread);
  if (record == nullptr) return out;
  for (const auto& sub : record->subrecords) out.signatures.push_back(sub.signature.to_string());
  for (const auto& sub : record->subrecords) {
    if (sub.signature != fo4::Signature::from_string("CITC")) continue;
    std::uint32_t value = 0;
    std::memcpy(&value, sub.data.data(), sizeof(value));
    out.citc.push_back(value);
  }
  const auto read = fo4::schema::read_root_conditions(*record);
  if (read.ok) {
    for (const auto& condition : read.conditions) out.functions.push_back(condition.function);
  }
  return out;
}

fo4::ConditionMutationStatus set_root(fo4::Plugin& plugin,
                                      const std::vector<fo4::CtdaCondition>& conditions) {
  return fo4::set_record_conditions(plugin, fo4::FormKey::parse("000800:Cond.esp"), conditions);
}

struct TempRoot {
  std::filesystem::path path;
  ~TempRoot() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
};

}  // namespace

TEST(RootConditionLists, CobjCreatesListBeforeCnamThenReplacesAndClears) {
  auto plugin = make_plugin("COBJ", {{"DESC", zeros(4)}, {"CNAM", zeros(4)}, {"BNAM", zeros(4)}});
  EXPECT_EQ(set_root(plugin, {function_condition(9)}), fo4::ConditionMutationStatus::updated);
  auto after = snapshot(plugin);
  EXPECT_EQ(after.signatures, (std::vector<std::string>{"EDID", "DESC", "CTDA", "CNAM", "BNAM"}));
  EXPECT_EQ(after.functions, (std::vector<std::uint16_t>{9}));

  EXPECT_EQ(set_root(plugin, {function_condition(9), function_condition(10)}),
            fo4::ConditionMutationStatus::updated);
  after = snapshot(plugin);
  EXPECT_EQ(after.signatures,
            (std::vector<std::string>{"EDID", "DESC", "CTDA", "CTDA", "CNAM", "BNAM"}));
  EXPECT_EQ(after.functions, (std::vector<std::uint16_t>{9, 10}));

  EXPECT_EQ(set_root(plugin, std::vector<fo4::CtdaCondition>{}),
            fo4::ConditionMutationStatus::updated);
  after = snapshot(plugin);
  EXPECT_EQ(after.signatures, (std::vector<std::string>{"EDID", "DESC", "CNAM", "BNAM"}));
  EXPECT_TRUE(after.functions.empty());
}

TEST(RootConditionLists, ActiCreatesCitcBeforeNvnmAndDropsItWhenCleared) {
  auto plugin = make_plugin("ACTI", {{"KNAM", u32_bytes(0)}, {"NVNM", zeros(4)}});
  EXPECT_EQ(set_root(plugin, {function_condition(9)}), fo4::ConditionMutationStatus::updated);
  auto after = snapshot(plugin);
  EXPECT_EQ(after.signatures,
            (std::vector<std::string>{"EDID", "KNAM", "CITC", "CTDA", "NVNM"}));
  EXPECT_EQ(after.citc, (std::vector<std::uint32_t>{1}));

  EXPECT_EQ(set_root(plugin, {function_condition(9), function_condition(10)}),
            fo4::ConditionMutationStatus::updated);
  after = snapshot(plugin);
  EXPECT_EQ(after.citc, (std::vector<std::uint32_t>{2}));
  EXPECT_EQ(after.functions, (std::vector<std::uint16_t>{9, 10}));

  EXPECT_EQ(set_root(plugin, std::vector<fo4::CtdaCondition>{}),
            fo4::ConditionMutationStatus::updated);
  after = snapshot(plugin);
  EXPECT_EQ(after.signatures, (std::vector<std::string>{"EDID", "KNAM", "NVNM"}));
  EXPECT_TRUE(after.citc.empty());
}

TEST(RootConditionLists, FurnCreatesCitcBeforeMnamAndDropsItWhenCleared) {
  auto plugin = make_plugin("FURN", {{"FNAM", zeros(2)},
                                     {"MNAM", u32_bytes(0)},
                                     {"ENAM", zeros(4)},
                                     {"XMRK", string_bytes(std::string_view{"m.nif\0", 6})}});
  EXPECT_EQ(set_root(plugin, {function_condition(9)}), fo4::ConditionMutationStatus::updated);
  auto after = snapshot(plugin);
  EXPECT_EQ(after.signatures,
            (std::vector<std::string>{"EDID", "FNAM", "CITC", "CTDA", "MNAM", "ENAM", "XMRK"}));
  EXPECT_EQ(after.citc, (std::vector<std::uint32_t>{1}));
  EXPECT_EQ(after.functions, (std::vector<std::uint16_t>{9}));

  EXPECT_EQ(set_root(plugin, {function_condition(9), function_condition(10)}),
            fo4::ConditionMutationStatus::updated);
  after = snapshot(plugin);
  EXPECT_EQ(after.signatures,
            (std::vector<std::string>{"EDID", "FNAM", "CITC", "CTDA", "CTDA", "MNAM", "ENAM",
                                      "XMRK"}));
  EXPECT_EQ(after.citc, (std::vector<std::uint32_t>{2}));
  EXPECT_EQ(after.functions, (std::vector<std::uint16_t>{9, 10}));

  EXPECT_EQ(set_root(plugin, std::vector<fo4::CtdaCondition>{}),
            fo4::ConditionMutationStatus::updated);
  after = snapshot(plugin);
  EXPECT_EQ(after.signatures,
            (std::vector<std::string>{"EDID", "FNAM", "MNAM", "ENAM", "XMRK"}));
  EXPECT_TRUE(after.citc.empty());
  EXPECT_TRUE(after.functions.empty());
}

TEST(RootConditionLists, CamsListLandsBeforeDataWithoutCitc) {
  auto plugin = make_plugin("CAMS", {{"DATA", zeros(4)}, {"MNAM", zeros(4)}});
  EXPECT_EQ(set_root(plugin, {function_condition(9)}), fo4::ConditionMutationStatus::updated);
  auto after = snapshot(plugin);
  EXPECT_EQ(after.signatures, (std::vector<std::string>{"EDID", "CTDA", "DATA", "MNAM"}));
  EXPECT_TRUE(after.citc.empty());
  EXPECT_EQ(set_root(plugin, std::vector<fo4::CtdaCondition>{}),
            fo4::ConditionMutationStatus::updated);
  after = snapshot(plugin);
  EXPECT_EQ(after.signatures, (std::vector<std::string>{"EDID", "DATA", "MNAM"}));
}

TEST(RootConditionLists, LscrListLandsBeforeNnam) {
  auto plugin = make_plugin("LSCR", {{"DESC", zeros(4)}, {"NNAM", zeros(4)}});
  EXPECT_EQ(set_root(plugin, {function_condition(9)}), fo4::ConditionMutationStatus::updated);
  const auto after = snapshot(plugin);
  EXPECT_EQ(after.signatures, (std::vector<std::string>{"EDID", "DESC", "CTDA", "NNAM"}));
  EXPECT_EQ(after.functions, (std::vector<std::uint16_t>{9}));
}

TEST(RootConditionLists, SmqnCountsIntoRequiredCitcAndKeepsItAtZero) {
  auto plugin = make_plugin("SMQN", {{"PNAM", u32_bytes(0)},
                                     {"SNAM", u32_bytes(0)},
                                     {"CITC", u32_bytes(0)},
                                     {"DNAM", zeros(4)},
                                     {"XNAM", zeros(4)}});
  EXPECT_EQ(set_root(plugin, {function_condition(9), function_condition(10)}),
            fo4::ConditionMutationStatus::updated);
  auto after = snapshot(plugin);
  EXPECT_EQ(after.signatures,
            (std::vector<std::string>{"EDID", "PNAM", "SNAM", "CITC", "CTDA", "CTDA", "DNAM",
                                      "XNAM"}));
  EXPECT_EQ(after.citc, (std::vector<std::uint32_t>{2}));

  EXPECT_EQ(set_root(plugin, std::vector<fo4::CtdaCondition>{}),
            fo4::ConditionMutationStatus::updated);
  after = snapshot(plugin);
  EXPECT_EQ(after.signatures,
            (std::vector<std::string>{"EDID", "PNAM", "SNAM", "CITC", "DNAM", "XNAM"}));
  EXPECT_EQ(after.citc, (std::vector<std::uint32_t>{0}));
}

TEST(RootConditionLists, MgefListAppendsAtEndWhenNothingFollows) {
  auto plugin = make_plugin("MGEF", {{"DATA", zeros(4)}, {"DNAM", zeros(4)}});
  EXPECT_EQ(set_root(plugin, {function_condition(9)}), fo4::ConditionMutationStatus::updated);
  const auto after = snapshot(plugin);
  EXPECT_EQ(after.signatures, (std::vector<std::string>{"EDID", "DATA", "DNAM", "CTDA"}));
  EXPECT_EQ(after.functions, (std::vector<std::uint16_t>{9}));
}

TEST(RootConditionLists, FactListAppendsAtEndAndDropsItsCitcWhenCleared) {
  auto plugin = make_plugin("FACT", {{"PLVD", zeros(4)}, {"CITC", u32_bytes(0)}});
  EXPECT_EQ(set_root(plugin, {function_condition(9)}), fo4::ConditionMutationStatus::updated);
  auto after = snapshot(plugin);
  EXPECT_EQ(after.signatures, (std::vector<std::string>{"EDID", "PLVD", "CITC", "CTDA"}));
  EXPECT_EQ(after.citc, (std::vector<std::uint32_t>{1}));
  EXPECT_EQ(after.functions, (std::vector<std::uint16_t>{9}));

  EXPECT_EQ(set_root(plugin, std::vector<fo4::CtdaCondition>{}),
            fo4::ConditionMutationStatus::updated);
  after = snapshot(plugin);
  EXPECT_EQ(after.signatures, (std::vector<std::string>{"EDID", "PLVD"}));
  EXPECT_TRUE(after.citc.empty());
}

TEST(RootConditionLists, IdleListLandsAfterEdidBeforeDnamWithoutCitc) {
  auto plugin = make_plugin("IDLE", {{"DNAM", zeros(4)}, {"ENAM", zeros(4)}});
  EXPECT_EQ(set_root(plugin, {function_condition(9)}), fo4::ConditionMutationStatus::updated);
  const auto after = snapshot(plugin);
  EXPECT_EQ(after.signatures, (std::vector<std::string>{"EDID", "CTDA", "DNAM", "ENAM"}));
  EXPECT_TRUE(after.citc.empty());
  EXPECT_EQ(after.functions, (std::vector<std::uint16_t>{9}));
}

TEST(RootConditionLists, MustListLandsBeforeSnamAndCountsIntoItsCitc) {
  auto plugin =
      make_plugin("MUST", {{"FNAM", zeros(4)}, {"CITC", u32_bytes(0)}, {"SNAM", u32_bytes(0)}});
  EXPECT_EQ(set_root(plugin, {function_condition(9)}), fo4::ConditionMutationStatus::updated);
  auto after = snapshot(plugin);
  EXPECT_EQ(after.signatures,
            (std::vector<std::string>{"EDID", "FNAM", "CITC", "CTDA", "SNAM"}));
  EXPECT_EQ(after.citc, (std::vector<std::uint32_t>{1}));
  EXPECT_EQ(after.functions, (std::vector<std::uint16_t>{9}));

  EXPECT_EQ(set_root(plugin, std::vector<fo4::CtdaCondition>{}),
            fo4::ConditionMutationStatus::updated);
  after = snapshot(plugin);
  EXPECT_EQ(after.signatures, (std::vector<std::string>{"EDID", "FNAM", "SNAM"}));
  EXPECT_TRUE(after.citc.empty());
}

TEST(RootConditionLists, ScenRootListIgnoresNestedListAndLandsBeforeTnam) {
  auto plugin = make_plugin("SCEN", {{"ANAM", u32_bytes(0)},
                                     {"CTDA", ctda_bytes(1)},
                                     {"PNAM", zeros(4)},
                                     {"INAM", u32_bytes(0)},
                                     {"TNAM", zeros(4)}});
  const auto before = snapshot(plugin);
  EXPECT_TRUE(before.functions.empty());
  EXPECT_EQ(set_root(plugin, {function_condition(9)}), fo4::ConditionMutationStatus::updated);
  auto after = snapshot(plugin);
  EXPECT_EQ(after.signatures,
            (std::vector<std::string>{"EDID", "ANAM", "CTDA", "PNAM", "INAM", "CTDA", "TNAM"}));
  EXPECT_EQ(after.functions, (std::vector<std::uint16_t>{9}));

  EXPECT_EQ(set_root(plugin, std::vector<fo4::CtdaCondition>{}),
            fo4::ConditionMutationStatus::updated);
  after = snapshot(plugin);
  EXPECT_EQ(after.signatures,
            (std::vector<std::string>{"EDID", "ANAM", "CTDA", "PNAM", "INAM", "TNAM"}));
  EXPECT_TRUE(after.functions.empty());
}

TEST(RootConditionLists, ScenWithoutPnamRefusesRootListAndIsUnchanged) {
  auto plugin = make_plugin("SCEN", {{"ANAM", u32_bytes(0)},
                                     {"CTDA", ctda_bytes(1)},
                                     {"TNAM", zeros(4)}});
  const auto before = fo4::write_plugin(plugin);
  const auto read = fo4::schema::read_root_conditions(*cond_record(plugin));
  EXPECT_FALSE(read.ok);
  EXPECT_EQ(read.error, fo4::schema::ScopedConditionStatus::alias_structure_invalid);
  EXPECT_EQ(set_root(plugin, {function_condition(9)}),
            fo4::ConditionMutationStatus::alias_structure_invalid);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(RootConditionLists, CApiCreatesCobjListThroughRadicleExport) {
  TempRoot root{std::filesystem::temp_directory_path() /
                ("radicle_root_lists_" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))};
  std::filesystem::create_directories(root.path / "out");
  const auto input = root.path / "Cond.esp";
  const auto plugin = make_plugin("COBJ", {{"DESC", zeros(4)}, {"CNAM", zeros(4)}});
  const auto bytes = fo4::write_plugin(plugin);
  {
    std::ofstream stream(input, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
  }

  radicle_plugin* handle = nullptr;
  const auto input_text = input.string();
  ASSERT_EQ(radicle_plugin_read_file(input_text.c_str(), &handle), RADICLE_STATUS_OK);
  std::unique_ptr<radicle_plugin, decltype(&radicle_plugin_destroy)> owned(handle,
                                                                          radicle_plugin_destroy);
  const auto row = radicle_condition{0x00, 0u, 9, 0, 0, 0, 0, 0, -1, nullptr, nullptr};
  radicle_condition_write_status status{};
  ASSERT_EQ(radicle_plugin_set_conditions(owned.get(), radicle_form_key{"Cond.esp", 0x800},
                                          nullptr, &row, 1, &status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_CONDITION_UPDATED);

  const auto output = (root.path / "out" / "Cond.esp").string();
  ASSERT_EQ(radicle_plugin_write_file(owned.get(), output.c_str()), RADICLE_STATUS_OK);
  const char* paths[] = {output.c_str()};
  radicle_env* env_raw = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 1, &env_raw), RADICLE_STATUS_OK);
  std::unique_ptr<radicle_env, decltype(&radicle_env_destroy)> env(env_raw, radicle_env_destroy);
  const radicle_record* record = nullptr;
  ASSERT_EQ(radicle_env_winner(env.get(), radicle_form_key{"Cond.esp", 0x800}, &record),
            RADICLE_STATUS_OK);
  const radicle_condition_list* list = nullptr;
  ASSERT_EQ(radicle_record_conditions(env.get(), record, nullptr, &list), RADICLE_STATUS_OK);
  std::unique_ptr<const radicle_condition_list, decltype(&radicle_condition_list_free)> owned_list(
      list, radicle_condition_list_free);
  ASSERT_EQ(owned_list->count, 1u);
  EXPECT_EQ(owned_list->rows[0].function, 9);
}
