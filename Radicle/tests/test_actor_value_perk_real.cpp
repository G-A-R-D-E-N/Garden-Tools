#include "fo4/c_api_records.h"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/plugin/writer.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>

namespace {

class ActorValuePerkReal : public testing::TestWithParam<const char*> {};

TEST_P(ActorValuePerkReal, NativeAndCAbiPayloadsAgreeAndReparseIdempotently) {
  const auto* directory = std::getenv("FO4_DATA_DIR");
  if (directory == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to run the official-master payload checks";
  }
  record_game_build(detect_game_build(directory));
  const auto path = std::filesystem::path{directory} / GetParam();
  ASSERT_TRUE(std::filesystem::is_regular_file(path)) << path;
  const auto native = fo4::read_plugin_file(path);
  std::map<fo4::FormKey, const fo4::Record*> expected;
  fo4::visit_records(native, [&](const fo4::Record& record) {
    const auto signature = record.header.signature.to_string();
    if (signature == "AVIF" || signature == "PERK") {
      ASSERT_TRUE(expected.emplace(record.form_key, &record).second);
    }
  });
  ASSERT_FALSE(HasFatalFailure());

  radicle_env* raw_env = nullptr;
  const auto filename = path.string();
  const char* paths[] = {filename.c_str()};
  ASSERT_EQ(radicle_env_load(paths, 1, &raw_env), RADICLE_STATUS_OK)
      << radicle_last_error();
  const std::unique_ptr<radicle_env, decltype(&radicle_env_destroy)>
      env(raw_env, &radicle_env_destroy);
  ASSERT_NE(env, nullptr);
  const radicle_record** raw_records = nullptr;
  std::size_t count = 0;
  ASSERT_EQ(radicle_env_plugin_records(env.get(), 0, &raw_records, &count),
            RADICLE_STATUS_OK) << radicle_last_error();
  const std::unique_ptr<const radicle_record*, decltype(&radicle_free_record_array)>
      records(raw_records, &radicle_free_record_array);
  std::set<fo4::FormKey> seen;
  std::map<std::string, std::size_t> families;
  std::map<std::string, std::size_t> shapes;

  for (std::size_t index = 0; index < count; ++index) {
    radicle_record_info info{};
    ASSERT_EQ(radicle_record_get_info(env.get(), raw_records[index], &info),
              RADICLE_STATUS_OK) << radicle_last_error();
    const std::string signature{info.signature};
    if (signature != "AVIF" && signature != "PERK") {
      continue;
    }
    ASSERT_NE(info.owner_plugin, nullptr);
    const fo4::FormKey key{fo4::PluginKey::from_filename(info.owner_plugin),
                          fo4::LocalFormId{info.local_id}};
    SCOPED_TRACE(filename + " " + key.to_string());
    ASSERT_TRUE(seen.insert(key).second);
    const auto found = expected.find(key);
    ASSERT_NE(found, expected.end());
    const auto& record = *found->second;
    EXPECT_EQ(signature, record.header.signature.to_string());
    EXPECT_EQ(info.record_flags, record.header.flags.value());
    std::uint16_t version = 0;
    std::size_t subrecord_count = 0;
    ASSERT_EQ(radicle_record_payload_info(env.get(), raw_records[index],
                                          &version, &subrecord_count),
              RADICLE_STATUS_OK) << radicle_last_error();
    EXPECT_EQ(version, record.header.form_version);
    ASSERT_EQ(subrecord_count, record.subrecords.size());
    bool after_first_prke = false;
    for (std::size_t at = 0; at < subrecord_count; ++at) {
      SCOPED_TRACE(at);
      const auto& sub = record.subrecords[at];
      std::uint8_t c_signature[4]{};
      std::uint8_t* raw_bytes = nullptr;
      std::size_t size = 0;
      std::int32_t extended = 0;
      ASSERT_EQ(radicle_record_copy_subrecord(env.get(), raw_records[index], at,
                                               c_signature, &raw_bytes, &size,
                                               &extended), RADICLE_STATUS_OK)
          << radicle_last_error();
      const std::unique_ptr<std::uint8_t, decltype(&radicle_free_bytes)>
          bytes(raw_bytes, &radicle_free_bytes);
      EXPECT_EQ(std::memcmp(c_signature, sub.signature.bytes().data(), 4), 0);
      EXPECT_EQ(extended != 0, sub.extended_size);
      ASSERT_EQ(size, sub.data.size());
      if (size != 0) {
        ASSERT_NE(bytes, nullptr);
        EXPECT_EQ(std::memcmp(bytes.get(), sub.data.data(), size), 0);
      } else {
        EXPECT_EQ(bytes, nullptr);
      }
      const auto sub_signature = sub.signature.to_string();
      if (sub_signature == "PRKE") {
        after_first_prke = true;
      }
      const auto region = signature == "PERK" && sub_signature == "DATA"
          ? (after_first_prke ? "/after-first-PRKE" : "/before-first-PRKE")
          : "";
      ++shapes[signature + "/" + sub_signature + region + "/" + std::to_string(size)];
    }
    ++families[signature];
  }
  EXPECT_EQ(seen.size(), expected.size());
  const auto encoded = fo4::write_plugin(native);
  const auto reread = fo4::read_plugin(encoded, path);
  for (const auto& [key, record] : expected) {
    SCOPED_TRACE(key.to_string());
    const auto* restored = fo4::find_record(reread, key);
    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(restored->header.signature, record->header.signature);
    EXPECT_EQ(restored->header.form_version, record->header.form_version);
    EXPECT_EQ(restored->header.flags, record->header.flags);
    ASSERT_EQ(restored->subrecords.size(), record->subrecords.size());
    for (std::size_t at = 0; at < record->subrecords.size(); ++at) {
      EXPECT_EQ(restored->subrecords[at].signature, record->subrecords[at].signature);
      EXPECT_EQ(restored->subrecords[at].data, record->subrecords[at].data);
      EXPECT_EQ(restored->subrecords[at].extended_size, record->subrecords[at].extended_size);
    }
  }
  RecordProperty("AVIF_records", std::to_string(families["AVIF"]));
  RecordProperty("PERK_records", std::to_string(families["PERK"]));
  std::cout << GetParam() << ": AVIF=" << families["AVIF"]
            << " PERK=" << families["PERK"] << '\n';
  for (const auto& [shape, occurrences] : shapes) {
    std::cout << shape << "=" << occurrences << '\n';
  }
}

INSTANTIATE_TEST_SUITE_P(OfficialMasters, ActorValuePerkReal,
    testing::Values("Fallout4.esm", "DLCRobot.esm", "DLCworkshop01.esm",
                    "DLCCoast.esm", "DLCworkshop02.esm", "DLCworkshop03.esm",
                    "DLCNukaWorld.esm"));

}
