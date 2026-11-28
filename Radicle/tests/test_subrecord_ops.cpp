#include "fo4/plugin_core/subrecord_ops.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

std::vector<std::byte> bytes_of(std::initializer_list<unsigned> values) {
  std::vector<std::byte> out;
  out.reserve(values.size());
  for (const auto value : values) out.push_back(static_cast<std::byte>(value));
  return out;
}

std::vector<std::byte> zbytes(std::string_view text) {
  std::vector<std::byte> out;
  out.reserve(text.size() + 1);
  for (const char ch : text) out.push_back(std::byte{static_cast<unsigned char>(ch)});
  out.push_back(std::byte{0});
  return out;
}

fo4::Subrecord* push(fo4::Record& record, fo4::Signature signature, std::vector<std::byte> data) {
  record.subrecords.push_back(fo4::make_subrecord(signature, std::move(data)));
  return &record.subrecords[record.subrecords.size() - 1];
}

}

TEST(SubrecordOps, ZStringViewAcceptsExactlyOneTrailingNul) {
  const auto encoded = zbytes("Sound");
  const auto view = fo4::zstring_view(std::span<const std::byte>{encoded});
  ASSERT_TRUE(view.has_value());
  EXPECT_EQ(*view, "Sound");

  const std::vector<std::byte> unterminated = bytes_of({'S', 'o', 'u', 'n', 'd'});
  EXPECT_FALSE(fo4::zstring_view(std::span<const std::byte>{unterminated}).has_value());
  EXPECT_FALSE(fo4::zstring_view(std::span<const std::byte>{}).has_value());

  const std::vector<std::byte> just_nul = bytes_of({0});
  const auto empty = fo4::zstring_view(std::span<const std::byte>{just_nul});
  ASSERT_TRUE(empty.has_value());
  EXPECT_TRUE(empty->empty());
}

TEST(SubrecordOps, ZStringViewRejectsEmbeddedNul) {
  const std::vector<std::byte> data = bytes_of({'A', 0, 'b', 0});
  EXPECT_FALSE(fo4::zstring_view(std::span<const std::byte>{data}).has_value());
}

TEST(SubrecordOps, ZStringViewOfSubrecordUsesTheSameStrictness) {
  fo4::Record record;
  const auto* accepted = push(record, fo4::sig_full, zbytes("Door"));
  const auto view = fo4::zstring_view(*accepted);
  ASSERT_TRUE(view.has_value());
  EXPECT_EQ(*view, "Door");

  const auto* rejected = push(record, fo4::sig_modl, bytes_of({'D', 'o', 'o', 'r'}));
  EXPECT_FALSE(fo4::zstring_view(*rejected).has_value());
}

TEST(SubrecordOps, EncodeZStringAlwaysAppendsOneNul) {
  EXPECT_EQ(fo4::encode_zstring("Abc"), bytes_of({'A', 'b', 'c', 0}));
  EXPECT_EQ(fo4::encode_zstring(""), bytes_of({0}));

  const auto encoded = fo4::encode_zstring("Sound.wav");
  const auto view = fo4::zstring_view(std::span<const std::byte>{encoded});
  ASSERT_TRUE(view.has_value());
  EXPECT_EQ(*view, "Sound.wav");
}

TEST(SubrecordOps, FindSubrecordReturnsTheFirstMatchOrNull) {
  fo4::Record record;
  push(record, fo4::sig_edid, zbytes("First"));
  push(record, fo4::sig_full, zbytes("Name"));
  push(record, fo4::sig_edid, zbytes("Second"));

  EXPECT_EQ(fo4::find_subrecord(record, fo4::sig_edid), &record.subrecords[0]);
  EXPECT_EQ(fo4::find_subrecord(record, fo4::sig_full), &record.subrecords[1]);
  EXPECT_EQ(fo4::find_subrecord(record, fo4::sig_modl), nullptr);

  const fo4::Record& readonly = record;
  const auto* found = fo4::find_subrecord(readonly, fo4::sig_edid);
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(found->data, zbytes("First"));
}

TEST(SubrecordOps, UniqueSubrecordCallsTheHandlerOncePerExtraCopy) {
  fo4::Record record;
  push(record, fo4::sig_edid, zbytes("One"));
  push(record, fo4::sig_full, zbytes("Name"));
  push(record, fo4::sig_edid, zbytes("Two"));

  int duplicates = 0;
  fo4::Subrecord* found = fo4::unique_subrecord(record, fo4::sig_edid, [&] { ++duplicates; });
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(duplicates, 1);
  EXPECT_EQ(fo4::unique_subrecord(record, fo4::sig_modl, [&] { ++duplicates; }), nullptr);
  EXPECT_EQ(duplicates, 1);

  fo4::Record single;
  push(single, fo4::sig_edid, zbytes("Only"));
  EXPECT_EQ(fo4::unique_subrecord(single, fo4::sig_edid, [&] { ++duplicates; }),
            &single.subrecords[0]);
  EXPECT_EQ(duplicates, 1);
}

TEST(SubrecordOps, MakeSubrecordKeepsSignatureAndClearsExtendedSize) {
  const auto payload = zbytes("Sound.wav");
  const auto explicit_data = fo4::make_subrecord(fo4::sig_full, payload);
  EXPECT_EQ(explicit_data.signature, fo4::sig_full);
  EXPECT_EQ(explicit_data.data, payload);
  EXPECT_FALSE(explicit_data.extended_size);

  const auto zero_filled = fo4::make_subrecord(fo4::sig_modl, std::size_t{4});
  EXPECT_EQ(zero_filled.signature, fo4::sig_modl);
  ASSERT_EQ(zero_filled.data.size(), 4u);
  EXPECT_TRUE(std::all_of(zero_filled.data.begin(), zero_filled.data.end(),
                          [](std::byte value) { return value == std::byte{0}; }));
  EXPECT_FALSE(zero_filled.extended_size);

  const auto empty = fo4::make_subrecord(fo4::sig_edid);
  EXPECT_TRUE(empty.data.empty());
}

TEST(SubrecordOps, EraseSubrecordsRemovesEveryCopyAndKeepsOrder) {
  fo4::Record record;
  push(record, fo4::sig_edid, zbytes("One"));
  push(record, fo4::sig_full, zbytes("Name"));
  push(record, fo4::sig_edid, zbytes("Two"));
  push(record, fo4::sig_modl, zbytes("Mesh.nif"));

  EXPECT_EQ(fo4::erase_subrecords(record, fo4::sig_edid), 2u);
  ASSERT_EQ(record.subrecords.size(), 2u);
  EXPECT_EQ(record.subrecords[0].signature, fo4::sig_full);
  EXPECT_EQ(record.subrecords[1].signature, fo4::sig_modl);
  EXPECT_EQ(fo4::erase_subrecords(record, fo4::sig_edid), 0u);
}

TEST(SubrecordOps, EditorIdOfIsLenientAboutTermination) {
  fo4::Record absent;
  EXPECT_FALSE(fo4::editor_id_of(absent).has_value());

  fo4::Record terminated;
  push(terminated, fo4::sig_edid, zbytes("Thing"));
  const auto id = fo4::editor_id_of(terminated);
  ASSERT_TRUE(id.has_value());
  EXPECT_EQ(*id, "Thing");

  fo4::Record unterminated;
  push(unterminated, fo4::sig_edid, bytes_of({'T', 'h', 'i', 'n', 'g'}));
  EXPECT_EQ(fo4::editor_id_of(unterminated), "Thing");

  fo4::Record embedded;
  push(embedded, fo4::sig_edid, bytes_of({'A', 0, 'b'}));
  EXPECT_EQ(fo4::editor_id_of(embedded), "A");
}
