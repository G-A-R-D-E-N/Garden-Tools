#include "fo4/schema/form_links.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

namespace {

fo4::Subrecord raw_u32_subrecord(std::string_view signature, std::uint32_t value) {
  fo4::Subrecord subrecord;
  subrecord.signature = fo4::Signature::from_string(signature);
  subrecord.data.resize(4);
  std::memcpy(subrecord.data.data(), &value, sizeof(value));
  return subrecord;
}

std::uint32_t raw_u32(const fo4::Subrecord& subrecord) {
  EXPECT_EQ(subrecord.data.size(), 4u);
  if (subrecord.data.size() != 4) return 0;
  std::uint32_t value = 0;
  std::memcpy(&value, subrecord.data.data(), sizeof(value));
  return value;
}

fo4::PluginKey owner(std::string_view form_key) {
  return fo4::FormKey::parse(form_key).owner();
}

fo4::PluginFormIdContext context(bool swapped) {
  const auto master_a = owner("000001:MasterA.esm");
  const auto master_b = owner("000001:MasterB.esm");
  // Built with push_back: GCC's -Wdangling-pointer false-positives on the
  // braced-init vector inside the aggregate return.
  std::vector<fo4::MasterDescriptor> masters;
  if (swapped) {
    masters.push_back({master_b, fo4::PluginKind::full});
    masters.push_back({master_a, fo4::PluginKind::full});
  } else {
    masters.push_back({master_a, fo4::PluginKind::full});
    masters.push_back({master_b, fo4::PluginKind::full});
  }
  return {.current_plugin = owner("000001:Patch.esp"),
          .current_kind = fo4::PluginKind::full,
          .masters = std::move(masters)};
}

}

TEST(QuestAliasFormLinkRemap, RemapsRealLinksButLeavesQuestScalarsAndStringsUntouched) {
  fo4::Record record;
  record.header.signature = fo4::Signature::from_string("QUST");

  // These are real QUST FormLinks and point at MasterB before the reorder.
  // Hoisted into named locals: GCC's -Wdangling-pointer false-positives on
  // push_back of the returned unnamed temporary.
  auto alfr = raw_u32_subrecord("ALFR", 0x01000123u);
  auto alco = raw_u32_subrecord("ALCO", 0x01000234u);
  auto aleq = raw_u32_subrecord("ALEQ", 0x01000456u);
  record.subrecords.push_back(std::move(alfr));
  record.subrecords.push_back(std::move(alco));
  record.subrecords.push_back(std::move(aleq));

  // These signatures are FormIDs in other record types, but xEdit defines
  // them as QUST scalar/string data. Values deliberately look like a valid
  // MasterB FormID so a bad global-signature remapper would corrupt them.
  auto anam = raw_u32_subrecord("ANAM", 0x01000123u);
  auto fnam = raw_u32_subrecord("FNAM", 0x01000123u);
  auto nam2 = raw_u32_subrecord("NAM2", 0x01000123u);
  auto cnam = raw_u32_subrecord("CNAM", 0x01000123u);
  auto nnam = raw_u32_subrecord("NNAM", 0x01000123u);
  auto snam = raw_u32_subrecord("SNAM", 0x01000123u);
  record.subrecords.push_back(std::move(anam));
  record.subrecords.push_back(std::move(fnam));
  record.subrecords.push_back(std::move(nam2));
  record.subrecords.push_back(std::move(cnam));
  record.subrecords.push_back(std::move(nnam));
  record.subrecords.push_back(std::move(snam));

  fo4::remap_record_form_links(record, context(false), context(true));

  ASSERT_EQ(record.subrecords.size(), 9u);
  EXPECT_EQ(raw_u32(record.subrecords[0]), 0x00000123u);  // ALFR
  EXPECT_EQ(raw_u32(record.subrecords[1]), 0x00000234u);  // ALCO
  EXPECT_EQ(raw_u32(record.subrecords[2]), 0x00000456u);  // ALEQ
  for (std::size_t index = 3; index < record.subrecords.size(); ++index) {
    EXPECT_EQ(raw_u32(record.subrecords[index]), 0x01000123u)
        << record.subrecords[index].signature.to_string();
  }
}

fo4::Subrecord packed_alla(std::uint32_t keyword_a,
                           std::int32_t alias_a,
                           std::uint32_t keyword_b,
                           std::int32_t alias_b) {
  fo4::Subrecord subrecord;
  subrecord.signature = fo4::Signature::from_string("ALLA");
  subrecord.data.resize(16);
  std::memcpy(subrecord.data.data(), &keyword_a, 4);
  std::memcpy(subrecord.data.data() + 4, &alias_a, 4);
  std::memcpy(subrecord.data.data() + 8, &keyword_b, 4);
  std::memcpy(subrecord.data.data() + 12, &alias_b, 4);
  return subrecord;
}

TEST(QuestAliasFormLinkRemap, PackedAllaRemapsKeywordsAndLeavesAliasInts) {
  fo4::Record record;
  record.header.signature = fo4::Signature::from_string("QUST");
  record.subrecords.push_back(packed_alla(0x01000001u, 0x7F7F7F7F, 0x01000002u, 0x01020304));

  fo4::remap_record_form_links(record, context(false), context(true));

  ASSERT_EQ(record.subrecords.size(), 1u);
  ASSERT_EQ(record.subrecords[0].data.size(), 16u);
  std::uint32_t keyword_a = 0;
  std::memcpy(&keyword_a, record.subrecords[0].data.data(), 4);
  EXPECT_EQ(keyword_a, 0x00000001u);
  std::int32_t alias_a = 0;
  std::memcpy(&alias_a, record.subrecords[0].data.data() + 4, 4);
  EXPECT_EQ(alias_a, 0x7F7F7F7F);
  std::uint32_t keyword_b = 0;
  std::memcpy(&keyword_b, record.subrecords[0].data.data() + 8, 4);
  EXPECT_EQ(keyword_b, 0x00000002u);
  std::int32_t alias_b = 0;
  std::memcpy(&alias_b, record.subrecords[0].data.data() + 12, 4);
  EXPECT_EQ(alias_b, 0x01020304);

  const auto remapped = context(true);
  EXPECT_EQ(fo4::resolve_plugin_form_id(fo4::RawFormId{0x00000001u}, remapped),
            fo4::FormKey::parse("000001:MasterB.esm"));
  EXPECT_EQ(fo4::resolve_plugin_form_id(fo4::RawFormId{0x00000002u}, remapped),
            fo4::FormKey::parse("000002:MasterB.esm"));
}

TEST(QuestAliasFormLinkRemap, MalformedAllaFailsClosed) {
  fo4::Record record;
  record.header.signature = fo4::Signature::from_string("QUST");
  auto alla = raw_u32_subrecord("ALLA", 0x01000001u);
  record.subrecords.push_back(std::move(alla));
  EXPECT_THROW(fo4::remap_record_form_links(record, context(false), context(true)),
               fo4::FormIdError);
}
