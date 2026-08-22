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
  return {
      .current_plugin = owner("000001:Patch.esp"),
      .current_kind = fo4::PluginKind::full,
      .masters = swapped
          ? std::vector<fo4::MasterDescriptor>{{master_b, fo4::PluginKind::full},
                                                {master_a, fo4::PluginKind::full}}
          : std::vector<fo4::MasterDescriptor>{{master_a, fo4::PluginKind::full},
                                                {master_b, fo4::PluginKind::full}},
  };
}

}

TEST(QuestAliasFormLinkRemap, RemapsRealLinksButLeavesQuestScalarsAndStringsUntouched) {
  fo4::Record record;
  record.header.signature = fo4::Signature::from_string("QUST");

  // These are real QUST FormLinks and point at MasterB before the reorder.
  record.subrecords.push_back(raw_u32_subrecord("ALFR", 0x01000123u));
  record.subrecords.push_back(raw_u32_subrecord("ALCO", 0x01000234u));
  record.subrecords.push_back(raw_u32_subrecord("ALEQ", 0x01000456u));

  // These signatures are FormIDs in other record types, but xEdit defines
  // them as QUST scalar/string data. Values deliberately look like a valid
  // MasterB FormID so a bad global-signature remapper would corrupt them.
  record.subrecords.push_back(raw_u32_subrecord("ANAM", 0x01000123u));
  record.subrecords.push_back(raw_u32_subrecord("FNAM", 0x01000123u));
  record.subrecords.push_back(raw_u32_subrecord("NAM2", 0x01000123u));
  record.subrecords.push_back(raw_u32_subrecord("CNAM", 0x01000123u));
  record.subrecords.push_back(raw_u32_subrecord("NNAM", 0x01000123u));
  record.subrecords.push_back(raw_u32_subrecord("SNAM", 0x01000123u));

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
