#include "fo4/schema/form_links.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
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

}

TEST(QuestAliasFormLinkRemap, RemapsALFRButLeavesAliasIdsUntouched) {
  fo4::Record record;
  record.header.signature = fo4::Signature::from_string("QUST");
  record.subrecords.push_back(raw_u32_subrecord("ALFR", 0x01000123u));
  record.subrecords.push_back(raw_u32_subrecord("ALFA", 0x01000123u));
  record.subrecords.push_back(raw_u32_subrecord("ALEA", 0x01000123u));

  const fo4::PluginFormIdContext old_context{
      .plugin = owner("000001:Patch.esp"),
      .masters = {
          owner("000001:MasterA.esm"),
          owner("000001:MasterB.esm"),
      },
  };
  const fo4::PluginFormIdContext new_context{
      .plugin = owner("000001:Patch.esp"),
      .masters = {
          owner("000001:MasterB.esm"),
          owner("000001:MasterA.esm"),
      },
  };

  fo4::remap_record_form_links(record, old_context, new_context);

  ASSERT_EQ(record.subrecords.size(), 3u);
  EXPECT_EQ(raw_u32(record.subrecords[0]), 0x00000123u);
  EXPECT_EQ(raw_u32(record.subrecords[1]), 0x01000123u);
  EXPECT_EQ(raw_u32(record.subrecords[2]), 0x01000123u);
}
