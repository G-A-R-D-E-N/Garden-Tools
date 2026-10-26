#include "fo4/c_api_env.h"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using fo4::Signature;
using fo4::test::ByteWriter;

constexpr std::string_view k_key = "000800:Patch.esp";

Bytes zstr(std::string_view text) {
  ByteWriter out;
  out.raw(text);
  out.u8(0);
  return out.bytes;
}

Bytes response(std::uint8_t number) {
  ByteWriter out;
  out.u32(0x810);
  out.u8(number);
  out.u32(0x811);
  out.u8(0xAB);
  out.u16(90);
  out.u32(0xFFFFFFFF);
  out.u32(42);
  return out.bytes;
}

Bytes make_plugin(bool with_bytes, bool with_marker) {
  ByteWriter out;
  out.bytes = fo4::test::make_tes4_plugin(0, 1.0f, {"Fallout4.esm"});
  const auto top = fo4::test::begin_group(out, "DIAL", 0);
  const auto record = fo4::test::begin_record(out, "INFO", 0, 0x01000800u);
  fo4::test::write_subrecord(out, "EDID", zstr("RadicleInfo"));
  fo4::test::write_subrecord(out, "TRDA", response(1));
  for (const auto* member : {"NAM1", "NAM2", "NAM3", "NAM4"}) {
    fo4::test::write_subrecord(out, member, Bytes{std::byte{0}});
  }
  if (with_bytes) {
    fo4::test::write_subrecord(out, "NAM9", Bytes{std::byte{0xDE}, std::byte{0xAD}});
  }
  if (with_marker) fo4::test::write_subrecord(out, "WZMD", Bytes{});
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, top);
  return out.bytes;
}

fo4::schema::SubrecordGroupDescriptor response_group() {
  const auto descriptor = fo4::schema::subrecord_group_descriptor(
      Signature::from_string("INFO"), "INFO\\Response");
  EXPECT_TRUE(descriptor.has_value());
  return descriptor.value_or(fo4::schema::SubrecordGroupDescriptor{});
}

std::vector<Signature> payload_signatures(const fo4::Plugin& plugin, const fo4::FormKey& record_key) {
  std::vector<Signature> out;
  const auto* record = fo4::find_record(plugin, record_key);
  if (record == nullptr) return out;
  for (const auto& subrecord : record->subrecords) out.push_back(subrecord.signature);
  return out;
}

std::size_t count_of(const std::vector<Signature>& signatures, Signature signature) {
  return static_cast<std::size_t>(
      std::count(signatures.begin(), signatures.end(), signature));
}

// Constraint: the temporary plugin directory is removed even when an ASSERT returns early.
struct TempDirectory {
  explicit TempDirectory(std::filesystem::path directory) : path(std::move(directory)) {
    std::filesystem::create_directories(path);
  }
  ~TempDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
  TempDirectory(const TempDirectory&) = delete;
  TempDirectory& operator=(const TempDirectory&) = delete;
  TempDirectory(TempDirectory&&) = delete;
  TempDirectory& operator=(TempDirectory&&) = delete;
  std::filesystem::path path;
};

template <typename Fn>
std::string thrown_message(Fn&& fn) {
  try {
    fn();
  } catch (const std::exception& error) {
    return error.what();
  }
  return {};
}

}  // namespace

TEST(SubrecordGroupMembers, MarkerIsInsertedAtItsDeclarationPositionAndClearedAgain) {
  // Constraint: a zero-byte marker must be insertable and removable alone, and its insertion must not disturb the required response members.
  auto plugin = fo4::read_plugin(make_plugin(false, false), "Patch.esp");
  const auto record_key = fo4::FormKey::parse(k_key);
  const auto group = response_group();
  const auto marker = Signature::from_string("WZMD");
  const auto before = payload_signatures(plugin, record_key);
  ASSERT_EQ(count_of(before, marker), std::size_t{0});

  ASSERT_EQ(fo4::set_record_subrecord_group_member(plugin, record_key, group, 0, marker, true),
            fo4::SubrecordGroupMutationStatus::updated);
  const auto after = payload_signatures(plugin, record_key);
  ASSERT_EQ(after.size(), before.size() + 1);
  EXPECT_EQ(after.back(), marker);
  const auto* record = fo4::find_record(plugin, record_key);
  ASSERT_NE(record, nullptr);
  EXPECT_TRUE(record->subrecords.back().data.empty());

  EXPECT_EQ(fo4::set_record_subrecord_group_member(plugin, record_key, group, 0, marker, true),
            fo4::SubrecordGroupMutationStatus::unchanged);
  EXPECT_EQ(payload_signatures(plugin, record_key), after);

  EXPECT_EQ(fo4::set_record_subrecord_group_member(plugin, record_key, group, 0, marker, false),
            fo4::SubrecordGroupMutationStatus::updated);
  EXPECT_EQ(payload_signatures(plugin, record_key), before);
  EXPECT_EQ(fo4::set_record_subrecord_group_member(plugin, record_key, group, 0, marker, false),
            fo4::SubrecordGroupMutationStatus::unchanged);
}

TEST(SubrecordGroupMembers, OptionalMemberKeepsItsDeclarationSlotWhenReinserted) {
  auto plugin = fo4::read_plugin(make_plugin(true, true), "Patch.esp");
  const auto record_key = fo4::FormKey::parse(k_key);
  const auto group = response_group();
  const auto bytes_member = Signature::from_string("NAM9");
  const auto marker = Signature::from_string("WZMD");
  const auto before = payload_signatures(plugin, record_key);
  ASSERT_EQ(count_of(before, bytes_member), std::size_t{1});
  ASSERT_EQ(before.back(), marker);

  ASSERT_EQ(fo4::set_record_subrecord_group_member(plugin, record_key, group, 0, bytes_member, false),
            fo4::SubrecordGroupMutationStatus::updated);
  const auto removed = payload_signatures(plugin, record_key);
  EXPECT_EQ(count_of(removed, bytes_member), std::size_t{0});
  EXPECT_EQ(removed.size(), before.size() - 1);

  ASSERT_EQ(fo4::set_record_subrecord_group_member(plugin, record_key, group, 0, bytes_member, true),
            fo4::SubrecordGroupMutationStatus::updated);
  const auto restored = payload_signatures(plugin, record_key);
  EXPECT_EQ(restored, before);
  EXPECT_EQ(count_of(restored, bytes_member), std::size_t{1});
  ASSERT_EQ(restored.size(), std::size_t{8}) << "the reinserted element lost a member";
  EXPECT_EQ(restored[5], Signature::from_string("NAM4"));
  EXPECT_EQ(restored[6], bytes_member);
  EXPECT_EQ(restored[7], marker) << "the reinserted member landed after the marker";
  const auto* record = fo4::find_record(plugin, record_key);
  ASSERT_NE(record, nullptr);
  for (const auto& subrecord : record->subrecords) {
    if (subrecord.signature == bytes_member) {
      EXPECT_TRUE(subrecord.data.empty());
    }
  }
}

TEST(SubrecordGroupMembers, ClearingARepeatedMemberRefusesInsteadOfDroppingOneCopy) {
  // Constraint: a clear names one member, so a member that repeats inside one element refuses rather than losing an arbitrary copy.
  const Signature sculpt_lead = Signature::from_string("BSMP");
  const Signature sculpt_mesh = Signature::from_string("BSMB");
  const std::array<fo4::schema::SubrecordGroupMemberDescriptor, 2> members{{
      {sculpt_lead, true, false, 4},
      {sculpt_mesh, false, true, 0},
  }};
  fo4::schema::SubrecordGroupDescriptor group{};
  group.record = Signature::from_string("ARMA");
  group.path = "ARMA\\SculptData";
  group.label = "Sculpt data";
  group.members = members;

  ByteWriter out;
  out.bytes = fo4::test::make_tes4_plugin(0, 1.0f, {"Fallout4.esm"});
  const auto top = fo4::test::begin_group(out, "ARMA", 0);
  const auto record = fo4::test::begin_record(out, "ARMA", 0, 0x01000800u);
  fo4::test::write_subrecord(out, "EDID", zstr("RadicleArmor"));
  fo4::test::write_subrecord(out, "BSMP", Bytes{std::byte{1}, std::byte{0}, std::byte{0}, std::byte{0}});
  fo4::test::write_subrecord(out, "BSMB", Bytes{std::byte{0x01}});
  fo4::test::write_subrecord(out, "BSMB", Bytes{std::byte{0x02}});
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, top);

  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  const auto record_key = fo4::FormKey::parse(k_key);
  const auto before = payload_signatures(plugin, record_key);
  ASSERT_EQ(count_of(before, sculpt_mesh), std::size_t{2});

  EXPECT_EQ(thrown_message([&] {
              fo4::set_record_subrecord_group_member(plugin, record_key, group, 0, sculpt_mesh, false);
            }),
            "ARMA\\SculptData member BSMB appears 2 times and cannot be cleared as one");
  EXPECT_EQ(payload_signatures(plugin, record_key), before);
}

TEST(SubrecordGroupMembers, RefusesRequiredUnknownAndOffRangeRequests) {
  auto plugin = fo4::read_plugin(make_plugin(false, false), "Patch.esp");
  const auto record_key = fo4::FormKey::parse(k_key);
  const auto group = response_group();
  const auto before = payload_signatures(plugin, record_key);

  EXPECT_EQ(thrown_message([&] {
              fo4::set_record_subrecord_group_member(plugin, record_key, group, 0,
                                                     Signature::from_string("TRDA"), false);
            }),
            "INFO\\Response member TRDA is required");
  EXPECT_EQ(thrown_message([&] {
              fo4::set_record_subrecord_group_member(plugin, record_key, group, 0,
                                                     Signature::from_string("ZZZZ"), true);
            }),
            "INFO\\Response has no ZZZZ member");
  EXPECT_EQ(fo4::set_record_subrecord_group_member(plugin, record_key, group, 1,
                                                   Signature::from_string("WZMD"), true),
            fo4::SubrecordGroupMutationStatus::index_out_of_range);
  EXPECT_EQ(payload_signatures(plugin, record_key), before);
}

TEST(SubrecordGroupMembers, CApiSetsAndClearsTheMarker) {
  const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
  TempDirectory root(std::filesystem::temp_directory_path() / ("radicle_c_group_" + std::to_string(id)));
  const auto path = root.path / "CGroup.esp";
  const auto bytes = make_plugin(true, false);
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  stream.close();

  radicle_plugin* plugin = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(path.string().c_str(), &plugin), RADICLE_STATUS_OK)
      << radicle_last_error();
  const radicle_form_key key{"CGroup.esp", 0x800};
  const auto marker = Signature::from_string("WZMD");
  const auto saved_count = [&](std::string_view name) {
    const auto saved_path = root.path / std::string(name);
    EXPECT_EQ(radicle_plugin_write_file(plugin, saved_path.string().c_str()), RADICLE_STATUS_OK)
        << radicle_last_error();
    // Constraint: a plugin read from a file takes its owner from that file, so the copy is keyed by its own name.
    const auto saved = fo4::read_plugin_file(saved_path);
    const auto saved_key = fo4::FormKey::parse(std::string("000800:") + std::string(name));
    return count_of(payload_signatures(saved, saved_key), marker);
  };

  EXPECT_EQ(radicle_plugin_subrecord_group_member_set(plugin, key, "INFO\\Response", 0, "WZMD", 1),
            RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(radicle_plugin_subrecord_group_member_set(plugin, key, "INFO\\Response", 0, "WZMD", 1),
            RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(saved_count("Saved.esp"), std::size_t{1}) << "the marker did not reach the saved plugin";

  EXPECT_EQ(radicle_plugin_subrecord_group_member_set(plugin, key, "INFO\\Response", 0, "WZMD", 0),
            RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(saved_count("Cleared.esp"), std::size_t{0}) << "the cleared marker is still in the saved plugin";

  EXPECT_EQ(radicle_plugin_subrecord_group_member_set(plugin, key, "INFO\\Response", 0, "WZMD", 1),
            RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(saved_count("Reset.esp"), std::size_t{1}) << "the re-set marker did not reach the saved plugin";
  EXPECT_EQ(radicle_plugin_subrecord_group_member_set(plugin, key, "INFO\\Response", 0, "TRDA", 0),
            RADICLE_STATUS_INVALID_ARGUMENT) << radicle_last_error();
  EXPECT_EQ(radicle_plugin_subrecord_group_member_set(plugin, key, "INFO\\Response", 0, "WZM", 1),
            RADICLE_STATUS_INVALID_ARGUMENT) << radicle_last_error();
  EXPECT_EQ(radicle_plugin_subrecord_group_member_set(plugin, key, "NOPE\\Response", 0, "WZMD", 1),
            RADICLE_STATUS_INVALID_ARGUMENT) << radicle_last_error();
  EXPECT_EQ(radicle_plugin_subrecord_group_member_set(plugin, key, "INFO\\Response", 4, "WZMD", 1),
            RADICLE_STATUS_NOT_FOUND) << radicle_last_error();
  EXPECT_EQ(saved_count("Refused.esp"), std::size_t{1}) << "a refused call changed the marker";

  radicle_plugin_destroy(plugin);
}
