#include "fo4/forms/npc.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/schema/scalar.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace {

class NpcReal : public testing::TestWithParam<const char*> {};

TEST_P(NpcReal, EveryNpcDecodesScalarsAndLists) {
  const auto* directory = std::getenv("FO4_DATA_DIR");
  if (!directory) {
    GTEST_SKIP() << "set FO4_DATA_DIR to run official-master coverage";
  }
  record_game_build(detect_game_build(directory));
  const auto path = std::filesystem::path(directory) / GetParam();
  ASSERT_TRUE(std::filesystem::is_regular_file(path));
  const auto plugin = fo4::read_plugin_file(path);
  const auto npc_signature = fo4::Signature::from_string("NPC_");

  std::vector<fo4::FormKey> npcs;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    if (record.header.signature == npc_signature) npcs.push_back(record.form_key);
  });
  if (std::string_view(GetParam()) == "Fallout4.esm") {
    EXPECT_GT(npcs.size(), 0u);
  }

  const auto fields = fo4::schema::scalar_fields(npc_signature);
  for (const auto& key : npcs) {
    SCOPED_TRACE(GetParam() + std::string(" ") + key.to_string());
    try {
      const auto* record = fo4::find_record(plugin, key);
      ASSERT_NE(record, nullptr);
      for (const auto& field : fields) {
        for (const auto& subrecord : record->subrecords) {
          if (subrecord.signature != field.scalar.subrecord) continue;
          const auto bytes = fo4::schema::scalar_field_bytes(subrecord.data, field.scalar);
          if (!bytes.empty()) static_cast<void>(fo4::schema::decode_scalar(bytes, field.scalar));
          break;
        }
      }
      static_cast<void>(fo4::forms::decode_npc_factions(plugin, key));
      static_cast<void>(fo4::forms::decode_npc_perks(plugin, key));
      static_cast<void>(fo4::forms::decode_npc_inventory(plugin, key));
    } catch (const std::exception& error) {
      ADD_FAILURE() << GetParam() << ' ' << key.to_string() << ": " << error.what();
    }
  }
}

INSTANTIATE_TEST_SUITE_P(OfficialMasters, NpcReal, testing::Values(
    "Fallout4.esm", "DLCRobot.esm", "DLCworkshop01.esm", "DLCCoast.esm",
    "DLCworkshop02.esm", "DLCworkshop03.esm", "DLCNukaWorld.esm"));

}
