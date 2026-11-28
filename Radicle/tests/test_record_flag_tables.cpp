#include "fo4/c_api.h"
#include "fo4/c_api_cell.h"
#include "fo4/c_api_env.h"

#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace {

using Rows = std::vector<std::pair<std::uint32_t, std::string>>;

Rows rows_of(const radicle_placed_ref_flag_row* rows, std::size_t count) {
  Rows out;
  for (std::size_t i = 0; i < count; ++i) out.emplace_back(rows[i].bit, rows[i].name);
  return out;
}

Rows record_table(const char* signature) {
  const radicle_placed_ref_flag_row* rows = nullptr;
  std::size_t count = 0;
  EXPECT_EQ(radicle_record_flag_table(signature, &rows, &count), RADICLE_STATUS_OK);
  return rows_of(rows, count);
}

// Constraint: masks and names are copied from xEdit Core/wbDefinitionsFO4.pas @93cc0bc5 (ACTI 5611-5633, DOOR 6477-6483, FURN 6848-6860, STAT 12045-12066, plus the Deleted 0x20 and Ignored 0x1000 rows wbFlagsList adds); the bit must equal the mask's single set bit.
Rows from_masks(const std::vector<std::pair<std::uint32_t, std::string>>& masks) {
  Rows out;
  for (const auto& [mask, name] : masks) {
    EXPECT_EQ(std::popcount(mask), 1) << name;
    out.emplace_back(static_cast<std::uint32_t>(std::countr_zero(mask)), name);
  }
  return out;
}

const Rows& xedit_acti() {
  static const Rows rows = from_masks({
      {0x00000004, "Never Fades"},
      {0x00000010, "Non Occluder"},
      {0x00000020, "Deleted"},
      {0x00000040, "Unknown 6"},
      {0x00000080, "Heading Marker"},
      {0x00000100, "Must Update Anims"},
      {0x00000200, "Hidden From Local Map"},
      {0x00000400, "Headtrack Marker"},
      {0x00000800, "Used as Platform"},
      {0x00001000, "Ignored"},
      {0x00002000, "Pack-In Use Only"},
      {0x00008000, "Has Distant LOD"},
      {0x00010000, "Random Anim Start"},
      {0x00020000, "Dangerous"},
      {0x00100000, "Ignore Object Interaction"},
      {0x00800000, "Is Marker"},
      {0x02000000, "Obstacle"},
      {0x04000000, "Navmesh - Filter"},
      {0x08000000, "Navmesh - Bounding Box"},
      {0x20000000, "Child Can Use"},
      {0x40000000, "Navmesh - Ground"},
  });
  return rows;
}

const Rows& xedit_door() {
  static const Rows rows = from_masks({
      {0x00000010, "Non Occluder"},
      {0x00000020, "Deleted"},
      {0x00001000, "Ignored"},
      {0x00008000, "Has Distant LOD"},
      {0x00010000, "Random Anim Start"},
      {0x00800000, "Is Marker"},
  });
  return rows;
}

const Rows& xedit_furn() {
  static const Rows rows = from_masks({
      {0x00000004, "Has Container"},
      {0x00000010, "Unknown 4"},
      {0x00000020, "Deleted"},
      {0x00000080, "Is Perch"},
      {0x00001000, "Ignored"},
      {0x00002000, "Unknown 13"},
      {0x00008000, "Has Distant LOD"},
      {0x00010000, "Random Anim Start"},
      {0x00800000, "Is Marker"},
      {0x02000000, "Power Armor"},
      {0x10000000, "Must Exit To Talk"},
      {0x20000000, "Child Can Use"},
  });
  return rows;
}

const Rows& xedit_stat() {
  static const Rows rows = from_masks({
      {0x00000004, "Heading Marker"},
      {0x00000010, "Non Occluder"},
      {0x00000020, "Deleted"},
      {0x00000040, "Has Tree LOD"},
      {0x00000080, "Add-On LOD Object"},
      {0x00000200, "Hidden From Local Map"},
      {0x00000400, "Headtrack Marker"},
      {0x00000800, "Used as Platform"},
      {0x00001000, "Ignored"},
      {0x00002000, "Pack-In Use Only"},
      {0x00008000, "Has Distant LOD"},
      {0x00020000, "Uses HD LOD Texture"},
      {0x00080000, "Has Currents"},
      {0x00800000, "Is Marker"},
      {0x02000000, "Obstacle"},
      {0x04000000, "NavMesh - Filter"},
      {0x08000000, "NavMesh - Bounding Box"},
      {0x10000000, "Show In World Map (Sky Cell Only)"},
      {0x40000000, "NavMesh - Ground"},
  });
  return rows;
}

// Invariant: these names must stay the identifiers the engine writes today, not xEdit labels such as "Diamond City"; a split marker 65 or a numeric fallback turned into a name is the regression they detect.
std::string vocab_name(std::uint32_t table, std::uint32_t value, radicle_status* status = nullptr) {
  std::uint8_t* bytes = nullptr;
  std::size_t size = 0;
  const radicle_status result = radicle_vocab_name(table, value, &bytes, &size);
  if (status != nullptr) *status = result;
  std::string text;
  if (result == RADICLE_STATUS_OK) text.assign(reinterpret_cast<const char*>(bytes), size);
  radicle_free_bytes(bytes);
  return text;
}

constexpr std::uint32_t pack_signature(const char (&s)[5]) {
  return static_cast<std::uint32_t>(static_cast<unsigned char>(s[0])) |
         static_cast<std::uint32_t>(static_cast<unsigned char>(s[1])) << 8 |
         static_cast<std::uint32_t>(static_cast<unsigned char>(s[2])) << 16 |
         static_cast<std::uint32_t>(static_cast<unsigned char>(s[3])) << 24;
}

bool vocab_missing(std::uint32_t table, std::uint32_t value) {
  radicle_status status = RADICLE_STATUS_OK;
  EXPECT_TRUE(vocab_name(table, value, &status).empty());
  return status == RADICLE_STATUS_NOT_FOUND;
}

std::uint32_t traits_of(std::uint32_t signature) {
  std::uint32_t traits = 99;
  EXPECT_EQ(radicle_vocab_signature_traits(signature, &traits), RADICLE_STATUS_OK);
  return traits;
}

}  // namespace

TEST(RecordFlagTables, ActivatorRowsMatchXedit) { EXPECT_EQ(record_table("ACTI"), xedit_acti()); }
TEST(RecordFlagTables, DoorRowsMatchXedit) { EXPECT_EQ(record_table("DOOR"), xedit_door()); }
TEST(RecordFlagTables, FurnitureRowsMatchXedit) { EXPECT_EQ(record_table("FURN"), xedit_furn()); }
TEST(RecordFlagTables, StaticRowsMatchXedit) { EXPECT_EQ(record_table("STAT"), xedit_stat()); }

TEST(RecordFlagTables, IsMarkerIsBitTwentyThreeOnEveryTableThatHasIt) {
  for (const char* signature : {"ACTI", "DOOR", "FURN", "STAT"}) {
    bool found = false;
    for (const auto& [bit, name] : record_table(signature)) {
      if (name == "Is Marker") {
        found = true;
        EXPECT_EQ(1u << bit, 0x00800000u) << signature;
      }
    }
    EXPECT_TRUE(found) << signature;
  }
}

TEST(RecordFlagTables, OtherSignaturesGetOnlyTheCommonRows) {
  const Rows common = from_masks({{0x00000020, "Deleted"}, {0x00001000, "Ignored"}});
  EXPECT_EQ(record_table("MSTT"), common);
  EXPECT_EQ(record_table("acti"), common);
  EXPECT_EQ(record_table(""), common);
}

TEST(RecordFlagTables, EnableParentRowsMatchXedit) {
  const radicle_placed_ref_flag_row* rows = nullptr;
  std::size_t count = 0;
  ASSERT_EQ(radicle_enable_parent_flag_table(&rows, &count), RADICLE_STATUS_OK);
  EXPECT_EQ(rows_of(rows, count), (Rows{{0, "Set Enable State to Opposite of Parent"}, {1, "Pop In"}}));
}

TEST(RecordFlagTables, NullArgumentsAreRefused) {
  const radicle_placed_ref_flag_row* rows = nullptr;
  std::size_t count = 0;
  EXPECT_EQ(radicle_record_flag_table(nullptr, &rows, &count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_record_flag_table("STAT", nullptr, &count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_record_flag_table("STAT", &rows, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_enable_parent_flag_table(nullptr, &count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_enable_parent_flag_table(&rows, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST(RecordFlagTables, RecordClassIsKeyedByTheSignatureBytesPackedLittleEndian) {
  EXPECT_EQ(pack_signature("REFR"), 0x52464552u);
  EXPECT_EQ(vocab_name(RADICLE_VOCAB_RECORD_CLASS, pack_signature("REFR")), "PlacedObject");
  EXPECT_EQ(vocab_name(RADICLE_VOCAB_RECORD_CLASS, pack_signature("ACHR")), "PlacedNpc");
  EXPECT_EQ(vocab_name(RADICLE_VOCAB_RECORD_CLASS, pack_signature("NPC_")), "Npc");
  EXPECT_EQ(vocab_name(RADICLE_VOCAB_RECORD_CLASS, pack_signature("NOTE")), "Holotape");
  EXPECT_EQ(vocab_name(RADICLE_VOCAB_RECORD_CLASS, pack_signature("ALCH")), "Ingestible");
  EXPECT_EQ(vocab_name(RADICLE_VOCAB_RECORD_CLASS, pack_signature("PGRE")), "PlacedTrap");
  EXPECT_EQ(vocab_name(RADICLE_VOCAB_RECORD_CLASS, pack_signature("ZOOM")), "Zoom");
  EXPECT_TRUE(vocab_missing(RADICLE_VOCAB_RECORD_CLASS, pack_signature("XXXX")));
  EXPECT_TRUE(vocab_missing(RADICLE_VOCAB_RECORD_CLASS, 0));
}

TEST(RecordFlagTables, OmodHasNoClassNameButIsNamedSoTheEnginesOutputIsUnchanged) {
  EXPECT_TRUE(vocab_missing(RADICLE_VOCAB_RECORD_CLASS, pack_signature("OMOD")));
  EXPECT_EQ(traits_of(pack_signature("OMOD")), static_cast<std::uint32_t>(RADICLE_VOCAB_TRAIT_NAMED));
}

TEST(RecordFlagTables, MarkerTypesAreIdentifiersWithOneMergedEntryAtSixtyFive) {
  EXPECT_EQ(vocab_name(RADICLE_VOCAB_MARKER_TYPE, 0), "Cave");
  EXPECT_EQ(vocab_name(RADICLE_VOCAB_MARKER_TYPE, 2), "DiamondCity");
  EXPECT_EQ(vocab_name(RADICLE_VOCAB_MARKER_TYPE, 4), "FactoryOrIndustrialSite");
  EXPECT_EQ(vocab_name(RADICLE_VOCAB_MARKER_TYPE, 5), "GovtBuildingOrMonument");
  EXPECT_EQ(vocab_name(RADICLE_VOCAB_MARKER_TYPE, 60), "RailroadFaction");
  EXPECT_EQ(vocab_name(RADICLE_VOCAB_MARKER_TYPE, 61), "Railroad");
  EXPECT_EQ(vocab_name(RADICLE_VOCAB_MARKER_TYPE, 64), "USSConstitution");
  EXPECT_EQ(vocab_name(RADICLE_VOCAB_MARKER_TYPE, 65),
            "MechanistLairRaidersettlementVassalsettlementPotentialVassalsettlement");
  for (std::uint32_t value = 0; value <= 65; ++value) {
    EXPECT_FALSE(vocab_name(RADICLE_VOCAB_MARKER_TYPE, value).empty()) << value;
  }
  for (std::uint32_t value = 66; value <= 99; ++value) {
    EXPECT_TRUE(vocab_missing(RADICLE_VOCAB_MARKER_TYPE, value)) << value;
  }
  EXPECT_TRUE(vocab_missing(RADICLE_VOCAB_MARKER_TYPE, 0xFFFFFFFFu));
}

TEST(RecordFlagTables, SmallValueTablesAnswerOnlyTheirListedValues) {
  const char* levels[] = {"Easy", "Medium", "Hard", "VeryHard"};
  for (std::uint32_t i = 0; i < 4; ++i) EXPECT_EQ(vocab_name(RADICLE_VOCAB_LEVEL, i), levels[i]);
  EXPECT_TRUE(vocab_missing(RADICLE_VOCAB_LEVEL, 4));
  const char* primitives[] = {"None", "Box", "Sphere", "Plane", "Line", "Ellipsoid"};
  for (std::uint32_t i = 0; i < 6; ++i) EXPECT_EQ(vocab_name(RADICLE_VOCAB_PRIMITIVE_TYPE, i), primitives[i]);
  EXPECT_TRUE(vocab_missing(RADICLE_VOCAB_PRIMITIVE_TYPE, 6));
  EXPECT_EQ(vocab_name(RADICLE_VOCAB_ACTIVATE_PARENT_FLAGS, 1), "ParentActivateOnly");
  EXPECT_TRUE(vocab_missing(RADICLE_VOCAB_ACTIVATE_PARENT_FLAGS, 0));
  EXPECT_TRUE(vocab_missing(RADICLE_VOCAB_ACTIVATE_PARENT_FLAGS, 2));
}

TEST(RecordFlagTables, ScriptPropertyTypesHaveTheFourteenListedValues) {
  const std::pair<std::uint32_t, const char*> listed[] = {
      {1, "Object"},         {2, "String"},          {3, "Int"},           {4, "Float"},
      {5, "Bool"},           {6, "Variable"},        {7, "Struct"},        {11, "ArrayOfObject"},
      {12, "ArrayOfString"}, {13, "ArrayOfInt"},     {14, "ArrayOfFloat"}, {15, "ArrayOfBool"},
      {16, "ArrayOfVariable"}, {17, "ArrayOfStruct"},
  };
  std::uint32_t named = 0;
  for (std::uint32_t value = 0; value < 64; ++value) {
    radicle_status status = RADICLE_STATUS_OK;
    vocab_name(RADICLE_VOCAB_SCRIPT_PROPERTY_TYPE, value, &status);
    if (status == RADICLE_STATUS_OK) ++named;
    else EXPECT_EQ(status, RADICLE_STATUS_NOT_FOUND) << value;
  }
  EXPECT_EQ(named, 14u);
  for (const auto& [value, name] : listed) EXPECT_EQ(vocab_name(RADICLE_VOCAB_SCRIPT_PROPERTY_TYPE, value), name);
}

TEST(RecordFlagTables, SignatureTraitsAreTheEnginesNamedAndTrapLikeSets) {
  const std::uint32_t named[] = {
      pack_signature("AACT"), pack_signature("ACTI"), pack_signature("AVIF"), pack_signature("AMMO"),
      pack_signature("OMOD"), pack_signature("ARMO"), pack_signature("BOOK"), pack_signature("CELL"),
      pack_signature("CLAS"), pack_signature("CLFM"), pack_signature("CMPO"), pack_signature("CONT"),
      pack_signature("DIAL"), pack_signature("DOOR"), pack_signature("EXPL"), pack_signature("FACT"),
      pack_signature("FLST"), pack_signature("FURN"), pack_signature("HAZD"), pack_signature("HDPT"),
      pack_signature("NOTE"), pack_signature("ALCH"), pack_signature("INGR"), pack_signature("INNR"),
      pack_signature("KYWD"), pack_signature("LIGH"), pack_signature("LCTN"), pack_signature("MGEF"),
      pack_signature("MATT"), pack_signature("MESG"), pack_signature("MISC"), pack_signature("MSTT"),
      pack_signature("MOVT"), pack_signature("NPC_"), pack_signature("ENCH"), pack_signature("PERK"),
      pack_signature("PROJ"), pack_signature("QUST"), pack_signature("RACE"), pack_signature("RFGP"),
      pack_signature("SNCT"), pack_signature("SPEL"), pack_signature("STAT"), pack_signature("SCOL"),
      pack_signature("TACT"), pack_signature("TERM"), pack_signature("TREE"), pack_signature("WATR"),
      pack_signature("WEAP"), pack_signature("WRLD"),
  };
  EXPECT_EQ(std::size(named), 50u);
  for (const std::uint32_t signature : named) {
    EXPECT_EQ(traits_of(signature), static_cast<std::uint32_t>(RADICLE_VOCAB_TRAIT_NAMED));
  }
  const std::uint32_t traps[] = {pack_signature("PARW"), pack_signature("PBAR"), pack_signature("PBEA"),
                                 pack_signature("PCON"), pack_signature("PFLA"), pack_signature("PHZD"),
                                 pack_signature("PMIS"), pack_signature("PGRE")};
  for (const std::uint32_t signature : traps) {
    EXPECT_EQ(traits_of(signature), static_cast<std::uint32_t>(RADICLE_VOCAB_TRAIT_TRAP_LIKE));
  }
  EXPECT_EQ(traits_of(pack_signature("REFR")), 0u);
  EXPECT_EQ(traits_of(pack_signature("XXXX")), 0u);
}

TEST(RecordFlagTables, VocabularyCallsRefuseBadArguments) {
  std::uint8_t* bytes = nullptr;
  std::size_t size = 0;
  EXPECT_EQ(radicle_vocab_name(RADICLE_VOCAB_LEVEL, 0, nullptr, &size), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_vocab_name(RADICLE_VOCAB_LEVEL, 0, &bytes, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_vocab_name(6, 0, &bytes, &size), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(bytes, nullptr);
  EXPECT_EQ(radicle_vocab_signature_traits(pack_signature("STAT"), nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
}
