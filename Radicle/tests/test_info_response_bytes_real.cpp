#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/scalar.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace {

using fo4::Signature;

struct SubrecordBytes {
  Signature signature{};
  std::vector<std::byte> data;
};

std::vector<SubrecordBytes> snapshot(const fo4::Record& record) {
  std::vector<SubrecordBytes> out;
  for (const auto& subrecord : record.subrecords) {
    out.push_back({subrecord.signature, subrecord.data});
  }
  return out;
}

bool same_snapshot(const std::vector<SubrecordBytes>& left,
                   const std::vector<SubrecordBytes>& right) {
  if (left.size() != right.size()) return false;
  for (std::size_t index = 0; index < left.size(); ++index) {
    if (left[index].signature != right[index].signature) return false;
    if (left[index].data != right[index].data) return false;
  }
  return true;
}

bool same_record(const std::vector<SubrecordBytes>& before, const fo4::Record& after) {
  if (before.size() != after.subrecords.size()) return false;
  for (std::size_t index = 0; index < before.size(); ++index) {
    if (before[index].signature != after.subrecords[index].signature) return false;
    if (before[index].data != after.subrecords[index].data) return false;
  }
  return true;
}

std::optional<std::string> response_bytes(const fo4::Plugin& plugin,
                                          const fo4::FormKey& key,
                                          const fo4::schema::ScalarDescriptor& scalar) {
  const auto* record = fo4::find_record(plugin, key);
  if (record == nullptr) return std::nullopt;
  for (const auto& subrecord : record->subrecords) {
    if (subrecord.signature != Signature::from_string("NAM9")) continue;
    return fo4::schema::decode_scalar(subrecord.data, scalar);
  }
  return std::nullopt;
}

std::size_t response_byte_payload(const fo4::Plugin& plugin, const fo4::FormKey& key) {
  const auto* record = fo4::find_record(plugin, key);
  if (record == nullptr) return 0;
  for (const auto& subrecord : record->subrecords) {
    if (subrecord.signature == Signature::from_string("NAM9")) return subrecord.data.size();
  }
  return 0;
}

}  // namespace

TEST(RowRoundtripReal, InfoResponseBytesSurviveALengthChange) {
  const auto* directory = std::getenv("FO4_DATA_DIR");
  if (directory == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to run official-master coverage";
  }
  record_game_build(detect_game_build(directory));
  const auto path = std::filesystem::path(directory) / "Fallout4.esm";
  ASSERT_TRUE(std::filesystem::is_regular_file(path)) << path.string();
  const auto plugin = fo4::read_plugin_file(path);

  const auto field = fo4::schema::scalar_field_descriptor(
      Signature::from_string("INFO"), "INFO\\ResponseBytes");
  ASSERT_TRUE(field.has_value()) << "INFO has no NAM9 bytes row";
  ASSERT_EQ(field->scalar.storage, fo4::schema::ScalarStorage::bytes);

  std::optional<fo4::FormKey> chosen;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    if (chosen.has_value() || record.header.signature != Signature::from_string("INFO")) return;
    if (fo4::record_field_count(plugin, record.form_key, *field).value_or(0) > std::size_t{0}) {
      chosen = record.form_key;
    }
  });
  ASSERT_TRUE(chosen.has_value()) << "no INFO record carries a NAM9 response";
  const auto key = *chosen;

  auto edited = fo4::read_plugin_file(path);
  const auto* live = fo4::find_record(edited, key);
  ASSERT_NE(live, nullptr) << key.to_string();
  const auto before = snapshot(*live);
  const auto original = response_bytes(edited, key, field->scalar);
  ASSERT_TRUE(original.has_value()) << key.to_string();
  ASSERT_FALSE(original->empty());
  const auto original_size = response_byte_payload(edited, key);
  const auto longer = *original + "deadbeef";
  const auto shorter = original->substr(0, original->size() >= 2 ? 2 : 0);
  ASSERT_LT(shorter.size(), original->size());
  ASSERT_GT(longer.size(), original->size());

  ASSERT_EQ(fo4::set_record_scalar(edited, key, *field, 0, longer),
            fo4::ScalarMutationStatus::updated);
  const auto* grown = fo4::find_record(edited, key);
  ASSERT_NE(grown, nullptr);
  EXPECT_EQ(response_bytes(edited, key, field->scalar), std::optional<std::string>{longer});
  EXPECT_EQ(response_byte_payload(edited, key), original_size + 4)
      << "the longer value did not add exactly four bytes";
  const auto grown_snapshot = snapshot(*grown);

  auto bytes = fo4::write_plugin(edited);
  auto reread = fo4::read_plugin(bytes, "RowRoundtrip.esm");
  const auto* restored = fo4::find_record(reread, key);
  ASSERT_NE(restored, nullptr) << key.to_string();
  EXPECT_TRUE(same_record(grown_snapshot, *restored))
      << "the longer NAM9 payload did not re-read byte for byte";
  EXPECT_EQ(response_bytes(reread, key, field->scalar), std::optional<std::string>{longer});

  ASSERT_EQ(fo4::set_record_scalar(reread, key, *field, 0, shorter),
            fo4::ScalarMutationStatus::updated);
  const auto* shrunk = fo4::find_record(reread, key);
  ASSERT_NE(shrunk, nullptr);
  EXPECT_EQ(response_bytes(reread, key, field->scalar), std::optional<std::string>{shorter});
  EXPECT_EQ(response_byte_payload(reread, key), shorter.size() / 2)
      << "the shorter value did not replace the payload at its own length";
  const auto shrunk_snapshot = snapshot(*shrunk);

  bytes = fo4::write_plugin(reread);
  const auto second = fo4::read_plugin(bytes, "RowRoundtrip.esm");
  const auto* shortened = fo4::find_record(second, key);
  ASSERT_NE(shortened, nullptr) << key.to_string();
  EXPECT_TRUE(same_record(shrunk_snapshot, *shortened))
      << "the shorter NAM9 payload did not re-read byte for byte";

  ASSERT_EQ(fo4::set_record_scalar(edited, key, *field, 0, *original),
            fo4::ScalarMutationStatus::updated);
  const auto* reverted = fo4::find_record(edited, key);
  ASSERT_NE(reverted, nullptr);
  EXPECT_TRUE(same_snapshot(before, snapshot(*reverted)))
      << "the record was not restored after a longer and a shorter NAM9 write";
}
