#include "fo4/c_api_authoring.h"
#include "fo4/c_api_destructible.h"
#include "fo4/plugin/reader.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

struct Plugin {
  radicle_plugin* handle{nullptr};
  explicit Plugin(const char* filename = "Destructible.esp") {
    EXPECT_EQ(radicle_plugin_create(filename, &handle), RADICLE_STATUS_OK);
  }
  ~Plugin() { radicle_plugin_destroy(handle); }
  Plugin(const Plugin&) = delete;
  Plugin& operator=(const Plugin&) = delete;

  fo4::Plugin snapshot() const {
    std::uint8_t* bytes = nullptr;
    std::size_t size = 0;
    EXPECT_EQ(radicle_plugin_write_bytes(handle, &bytes, &size), RADICLE_STATUS_OK);
    std::vector<std::byte> copy(reinterpret_cast<std::byte*>(bytes),
                               reinterpret_cast<std::byte*>(bytes) + size);
    radicle_free_bytes(bytes);
    return fo4::read_plugin(copy, "Destructible.esp");
  }
};

radicle_form_key make_armor(Plugin& plugin) {
  radicle_form_key key{};
  EXPECT_EQ(radicle_plugin_create_record(plugin.handle, "ARMO", "DestrArmor", &key),
            RADICLE_STATUS_OK);
  return key;
}

const fo4::Subrecord* find_subrecord(const fo4::Record& record, std::string_view signature) {
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature.to_string() == signature) return &subrecord;
  }
  return nullptr;
}

std::uint8_t dest_count(const fo4::Plugin& plugin, std::uint32_t local) {
  for (const auto* record : fo4::collect_records(plugin)) {
    if (record->header.form_id.value() != local) continue;
    const auto* dest = find_subrecord(*record, "DEST");
    if (dest == nullptr || dest->data.size() < 5) return 0xFF;
    return static_cast<std::uint8_t>(dest->data[4]);
  }
  return 0xFF;
}

bool has_subrecord(const fo4::Plugin& plugin, std::uint32_t local, std::string_view signature) {
  for (const auto* record : fo4::collect_records(plugin)) {
    if (record->header.form_id.value() != local) continue;
    if (find_subrecord(*record, signature) != nullptr) return true;
  }
  return false;
}

}  // namespace

TEST(CApiDestructible, HeaderStagesAndMembersRoundTrip) {
  Plugin plugin;
  const auto key = make_armor(plugin);
  ASSERT_NE(key.plugin, nullptr);

  radicle_destructible_header_status header_status = RADICLE_DESTRUCTIBLE_HEADER_MISSING;
  EXPECT_EQ(radicle_plugin_insert_destructible_header(plugin.handle, key, &header_status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(header_status, RADICLE_DESTRUCTIBLE_HEADER_UPDATED);
  EXPECT_EQ(radicle_plugin_insert_destructible_header(plugin.handle, key, &header_status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(header_status, RADICLE_DESTRUCTIBLE_HEADER_PRESENT);

  radicle_destructible_stage_status stage_status =
      RADICLE_DESTRUCTIBLE_STAGE_INDEX_OUT_OF_RANGE;
  EXPECT_EQ(radicle_plugin_insert_destructible_stage(plugin.handle, key, 1, &stage_status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(stage_status, RADICLE_DESTRUCTIBLE_STAGE_INDEX_OUT_OF_RANGE);
  EXPECT_EQ(radicle_plugin_insert_destructible_stage(plugin.handle, key, 0, &stage_status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(stage_status, RADICLE_DESTRUCTIBLE_STAGE_UPDATED);
  EXPECT_EQ(dest_count(plugin.snapshot(), key.local_id), 1u);

  radicle_destructible_stage_member_status member_status =
      RADICLE_DESTRUCTIBLE_STAGE_MEMBER_ABSENT;
  EXPECT_EQ(
      radicle_plugin_insert_destructible_stage_member(plugin.handle, key, 0, "DMDL", &member_status),
      RADICLE_STATUS_OK);
  EXPECT_EQ(member_status, RADICLE_DESTRUCTIBLE_STAGE_MEMBER_UPDATED);
  EXPECT_EQ(
      radicle_plugin_insert_destructible_stage_member(plugin.handle, key, 0, "DMDC", &member_status),
      RADICLE_STATUS_OK);
  EXPECT_EQ(member_status, RADICLE_DESTRUCTIBLE_STAGE_MEMBER_UPDATED);
  EXPECT_TRUE(has_subrecord(plugin.snapshot(), key.local_id, "DMDC"));

  EXPECT_EQ(
      radicle_plugin_remove_destructible_stage_member(plugin.handle, key, 0, "DMDC", &member_status),
      RADICLE_STATUS_OK);
  EXPECT_EQ(member_status, RADICLE_DESTRUCTIBLE_STAGE_MEMBER_UPDATED);
  EXPECT_EQ(
      radicle_plugin_remove_destructible_stage_member(plugin.handle, key, 0, "DMDC", &member_status),
      RADICLE_STATUS_OK);
  EXPECT_EQ(member_status, RADICLE_DESTRUCTIBLE_STAGE_MEMBER_ABSENT);

  EXPECT_EQ(radicle_plugin_remove_destructible_stage(plugin.handle, key, 0, &stage_status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(stage_status, RADICLE_DESTRUCTIBLE_STAGE_UPDATED);
  EXPECT_EQ(dest_count(plugin.snapshot(), key.local_id), 0u);

  EXPECT_EQ(radicle_plugin_remove_destructible_header(plugin.handle, key, &header_status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(header_status, RADICLE_DESTRUCTIBLE_HEADER_UPDATED);
  EXPECT_EQ(radicle_plugin_remove_destructible_header(plugin.handle, key, &header_status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(header_status, RADICLE_DESTRUCTIBLE_HEADER_MISSING);
  EXPECT_EQ(radicle_plugin_insert_destructible_stage(plugin.handle, key, 0, &stage_status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(stage_status, RADICLE_DESTRUCTIBLE_STAGE_HEADER_MISSING);
}

TEST(CApiDestructible, MapsEveryRefusalToAStatus) {
  Plugin plugin;
  const auto key = make_armor(plugin);
  radicle_form_key list{};
  EXPECT_EQ(radicle_plugin_create_record(plugin.handle, "LVSP", "List", &list),
            RADICLE_STATUS_OK);

  radicle_destructible_header_status header_status = RADICLE_DESTRUCTIBLE_HEADER_UPDATED;
  radicle_destructible_stage_status stage_status = RADICLE_DESTRUCTIBLE_STAGE_UPDATED;
  radicle_destructible_stage_member_status member_status =
      RADICLE_DESTRUCTIBLE_STAGE_MEMBER_UPDATED;
  const radicle_form_key missing{key.plugin, 0x8FF};

  EXPECT_EQ(radicle_plugin_insert_destructible_header(plugin.handle, missing, &header_status),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_remove_destructible_stage(plugin.handle, missing, 0, &stage_status),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(
      radicle_plugin_remove_destructible_stage_member(plugin.handle, missing, 0, "DMDL",
                                                      &member_status),
      RADICLE_STATUS_NOT_FOUND);

  EXPECT_EQ(radicle_plugin_insert_destructible_header(plugin.handle, list, &header_status),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_insert_destructible_stage(plugin.handle, list, 0, &stage_status),
            RADICLE_STATUS_INVALID_ARGUMENT);

  EXPECT_EQ(radicle_plugin_insert_destructible_stage(plugin.handle, key, 0, &stage_status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(stage_status, RADICLE_DESTRUCTIBLE_STAGE_HEADER_MISSING);
  EXPECT_EQ(
      radicle_plugin_insert_destructible_stage_member(plugin.handle, key, 0, "DSTA", &member_status),
      RADICLE_STATUS_OK);
  EXPECT_EQ(member_status, RADICLE_DESTRUCTIBLE_STAGE_MEMBER_HEADER_MISSING);
  EXPECT_EQ(
      radicle_plugin_insert_destructible_stage_member(plugin.handle, key, 9, "DSTA", &member_status),
      RADICLE_STATUS_OK);
  EXPECT_EQ(member_status, RADICLE_DESTRUCTIBLE_STAGE_MEMBER_HEADER_MISSING);

  EXPECT_EQ(radicle_plugin_insert_destructible_header(plugin.handle, key, &header_status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(header_status, RADICLE_DESTRUCTIBLE_HEADER_UPDATED);
  // Constraint: a stage index is valid before a member can be refused for its own signature.
  EXPECT_EQ(radicle_plugin_insert_destructible_stage(plugin.handle, key, 0, &stage_status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(stage_status, RADICLE_DESTRUCTIBLE_STAGE_UPDATED);
  EXPECT_EQ(
      radicle_plugin_insert_destructible_stage_member(plugin.handle, key, 3, "DSTA", &member_status),
      RADICLE_STATUS_OK);
  EXPECT_EQ(member_status, RADICLE_DESTRUCTIBLE_STAGE_MEMBER_STAGE_INDEX_OUT_OF_RANGE);
  EXPECT_EQ(
      radicle_plugin_insert_destructible_stage_member(plugin.handle, key, 0, "DSTD", &member_status),
      RADICLE_STATUS_OK);
  EXPECT_EQ(member_status, RADICLE_DESTRUCTIBLE_STAGE_MEMBER_NOT_OPTIONAL);
  EXPECT_EQ(
      radicle_plugin_insert_destructible_stage_member(plugin.handle, key, 0, "DMDT", &member_status),
      RADICLE_STATUS_OK);
  EXPECT_EQ(member_status, RADICLE_DESTRUCTIBLE_STAGE_MEMBER_NO_DEFAULT);

  EXPECT_EQ(radicle_plugin_insert_destructible_header(nullptr, key, &header_status),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_insert_destructible_header(plugin.handle, key, nullptr),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_insert_destructible_stage(plugin.handle, {nullptr, 0}, 0, &stage_status),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_insert_destructible_stage_member(plugin.handle, key, 0, "DMD",
                                                            &member_status),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_insert_destructible_stage_member(plugin.handle, key, 0, nullptr,
                                                            &member_status),
            RADICLE_STATUS_INVALID_ARGUMENT);

  EXPECT_EQ(radicle_plugin_remove_destructible_header(plugin.handle, key, &header_status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(header_status, RADICLE_DESTRUCTIBLE_HEADER_UPDATED);
}

namespace {

using Members = std::vector<std::pair<const char*, std::vector<std::byte>>>;

std::filesystem::path scratch_root() {
  return std::filesystem::temp_directory_path() /
         ("radicle_c_destructible_" +
          std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
}

radicle_plugin* load_tangled(const std::filesystem::path& root, std::uint8_t stages,
                             const Members& members, std::int32_t health = 0,
                             std::uint8_t flags = 0) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Fallout4.esm\0", 13});
  fo4::test::write_subrecord(out, "DATA", std::vector<std::byte>(8));
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, "PROJ", 0);
  const auto record = fo4::test::begin_record(out, "PROJ", 0, 0x01000800);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Tangled\0", 8});
  std::vector<std::byte> dest(8);
  dest[0] = static_cast<std::byte>(health & 0xFF);
  dest[1] = static_cast<std::byte>((health >> 8) & 0xFF);
  dest[2] = static_cast<std::byte>((health >> 16) & 0xFF);
  dest[3] = static_cast<std::byte>((health >> 24) & 0xFF);
  dest[4] = static_cast<std::byte>(stages);
  dest[5] = static_cast<std::byte>(flags);
  fo4::test::write_subrecord(out, "DEST", dest);
  for (const auto& [name, data] : members) fo4::test::write_subrecord(out, name, data);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);

  std::filesystem::create_directories(root);
  const auto path = root / "Tangled.esp";
  {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(out.bytes.data()),
                 static_cast<std::streamsize>(out.bytes.size()));
  }
  radicle_plugin* handle = nullptr;
  EXPECT_EQ(radicle_plugin_read_file(path.string().c_str(), &handle), RADICLE_STATUS_OK)
      << radicle_last_error();
  return handle;
}

std::vector<std::byte> write_bytes(radicle_plugin* handle) {
  std::uint8_t* raw = nullptr;
  std::size_t size = 0;
  EXPECT_EQ(radicle_plugin_write_bytes(handle, &raw, &size), RADICLE_STATUS_OK);
  std::vector<std::byte> out(reinterpret_cast<std::byte*>(raw),
                             reinterpret_cast<std::byte*>(raw) + size);
  radicle_free_bytes(raw);
  return out;
}

}  // namespace

TEST(CApiDestructible, OrphanStageMembersAreRefusedWithoutChangingBytes) {
  const auto root = scratch_root();
  radicle_plugin* handle =
      load_tangled(root, 2,
                   {{"DSTD", std::vector<std::byte>(20)},
                    {"DSTF", {}},
                    {"DMDL", std::vector<std::byte>(2)},
                    {"DSTD", std::vector<std::byte>(20)},
                    {"DSTF", {}}});
  ASSERT_NE(handle, nullptr);
  const auto before = write_bytes(handle);
  const radicle_form_key key{"Tangled.esp", 0x800};

  radicle_destructible_stage_status stage_status = RADICLE_DESTRUCTIBLE_STAGE_UPDATED;
  EXPECT_EQ(radicle_plugin_insert_destructible_stage(handle, key, 0, &stage_status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(stage_status, RADICLE_DESTRUCTIBLE_STAGE_INTERLEAVED);
  EXPECT_EQ(radicle_plugin_remove_destructible_stage(handle, key, 0, &stage_status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(stage_status, RADICLE_DESTRUCTIBLE_STAGE_INTERLEAVED);
  radicle_destructible_stage_member_status member_status =
      RADICLE_DESTRUCTIBLE_STAGE_MEMBER_UPDATED;
  EXPECT_EQ(
      radicle_plugin_insert_destructible_stage_member(handle, key, 0, "DSTA", &member_status),
      RADICLE_STATUS_OK);
  EXPECT_EQ(member_status, RADICLE_DESTRUCTIBLE_STAGE_MEMBER_INTERLEAVED);
  EXPECT_EQ(
      radicle_plugin_remove_destructible_stage_member(handle, key, 0, "DMDL", &member_status),
      RADICLE_STATUS_OK);
  EXPECT_EQ(member_status, RADICLE_DESTRUCTIBLE_STAGE_MEMBER_INTERLEAVED);

  EXPECT_EQ(write_bytes(handle), before);
  radicle_plugin_destroy(handle);
  std::error_code error;
  std::filesystem::remove_all(root, error);
}

TEST(CApiDestructible, CountThatDisagreesWithTheStageRunIsRefusedAsMalformed) {
  const auto root = scratch_root();
  radicle_plugin* handle =
      load_tangled(root, 0,
                   {{"DSTD", std::vector<std::byte>(20)},
                    {"DMDL", std::vector<std::byte>(2)},
                    {"DSTF", {}}});
  ASSERT_NE(handle, nullptr);
  const auto before = write_bytes(handle);
  const radicle_form_key key{"Tangled.esp", 0x800};

  radicle_destructible_stage_status stage_status = RADICLE_DESTRUCTIBLE_STAGE_UPDATED;
  EXPECT_EQ(radicle_plugin_insert_destructible_stage(handle, key, 0, &stage_status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(stage_status, RADICLE_DESTRUCTIBLE_STAGE_MALFORMED);
  EXPECT_EQ(radicle_plugin_remove_destructible_stage(handle, key, 0, &stage_status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(stage_status, RADICLE_DESTRUCTIBLE_STAGE_MALFORMED);
  radicle_destructible_stage_member_status member_status =
      RADICLE_DESTRUCTIBLE_STAGE_MEMBER_UPDATED;
  EXPECT_EQ(
      radicle_plugin_insert_destructible_stage_member(handle, key, 0, "DSTA", &member_status),
      RADICLE_STATUS_OK);
  EXPECT_EQ(member_status, RADICLE_DESTRUCTIBLE_STAGE_MEMBER_MALFORMED);

  EXPECT_EQ(write_bytes(handle), before);
  radicle_plugin_destroy(handle);
  std::error_code error;
  std::filesystem::remove_all(root, error);
}

TEST(CApiDestructible, WholeGroupReadExportsHeaderResistancesAndInterleavedStages) {
  const auto root = scratch_root();
  fo4::test::ByteWriter damage;
  damage.u32(0x00000A10);
  damage.u32(5);
  damage.u32(0x00000A11);
  damage.u32(7);
  radicle_plugin* handle = load_tangled(
      root, 2,
      {{"DAMC", damage.bytes},
       {"DSTD", std::vector<std::byte>(20, std::byte{0x20})},
       {"DMDL", std::vector<std::byte>{std::byte{0x11}, std::byte{0x22}}},
       {"DSTF", {}},
       {"DATA", std::vector<std::byte>(4)},
       {"DSTD", std::vector<std::byte>(20, std::byte{0x21})},
       {"DSTA", std::vector<std::byte>{std::byte{0x33}, std::byte{0x44}, std::byte{0x55}}},
       {"DSTF", {}},
       {"NAM1", std::vector<std::byte>(2)}},
      1200, 3);
  ASSERT_NE(handle, nullptr);

  radicle_destructible_group group{};
  radicle_destructible_stage_status status = RADICLE_DESTRUCTIBLE_STAGE_UPDATED;
  EXPECT_EQ(radicle_plugin_destructible_group_read(handle, {"Tangled.esp", 0x800}, &group,
                                                   &status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_DESTRUCTIBLE_STAGE_PRESENT);
  EXPECT_EQ(group.header.health, 1200);
  EXPECT_EQ(group.header.stage_count, 2u);
  EXPECT_EQ(group.header.flags, 3u);

  ASSERT_EQ(group.resistance_count, 2u);
  EXPECT_STREQ(group.resistances[0].damage_type.plugin, "Fallout4.esm");
  EXPECT_EQ(group.resistances[0].damage_type.local_id, 0xA10u);
  EXPECT_EQ(group.resistances[0].value, 5u);
  EXPECT_STREQ(group.resistances[1].damage_type.plugin, "Fallout4.esm");
  EXPECT_EQ(group.resistances[1].damage_type.local_id, 0xA11u);
  EXPECT_EQ(group.resistances[1].value, 7u);

  ASSERT_EQ(group.stage_count, 2u);
  const auto signatures = [](const radicle_destructible_stage& stage) {
    std::string text;
    for (size_t index = 0; index < stage.member_count; ++index) {
      if (!text.empty()) text.push_back(' ');
      text += stage.members[index].signature;
    }
    return text;
  };
  const auto member_bytes = [](const radicle_destructible_member& member) {
    return std::vector<std::byte>(reinterpret_cast<const std::byte*>(member.data),
                                  reinterpret_cast<const std::byte*>(member.data) + member.size);
  };
  EXPECT_EQ(signatures(group.stages[0]), "DSTD DMDL DSTF");
  EXPECT_EQ(signatures(group.stages[1]), "DSTD DSTA DSTF");
  EXPECT_EQ(member_bytes(group.stages[0].members[0]),
            std::vector<std::byte>(20, std::byte{0x20}));
  EXPECT_EQ(member_bytes(group.stages[0].members[1]),
            (std::vector<std::byte>{std::byte{0x11}, std::byte{0x22}}));
  EXPECT_TRUE(member_bytes(group.stages[0].members[2]).empty());
  EXPECT_EQ(member_bytes(group.stages[1].members[0]),
            std::vector<std::byte>(20, std::byte{0x21}));
  EXPECT_EQ(member_bytes(group.stages[1].members[1]),
            (std::vector<std::byte>{std::byte{0x33}, std::byte{0x44}, std::byte{0x55}}));
  EXPECT_TRUE(member_bytes(group.stages[1].members[2]).empty());

  radicle_free_destructible_group(&group);
  EXPECT_EQ(group.stages, nullptr);
  EXPECT_EQ(group.resistances, nullptr);
  radicle_plugin_destroy(handle);
  std::error_code error;
  std::filesystem::remove_all(root, error);
}

TEST(CApiDestructible, WholeGroupReadReturnsNoPartialGroupOnMalformedOrInterleavedRuns) {
  const auto check_refusal = [](std::uint8_t count, const Members& members,
                                radicle_destructible_stage_status expected) {
    const auto root = scratch_root();
    radicle_plugin* handle = load_tangled(root, count, members);
    ASSERT_NE(handle, nullptr);
    radicle_destructible_group group{};
    group.header.health = 1234;
    group.resistance_count = 2;
    radicle_destructible_stage_status status = RADICLE_DESTRUCTIBLE_STAGE_PRESENT;
    EXPECT_EQ(radicle_plugin_destructible_group_read(handle, {"Tangled.esp", 0x800}, &group,
                                                     &status),
              RADICLE_STATUS_OK);
    EXPECT_EQ(status, expected);
    EXPECT_EQ(group.header.health, 0);
    EXPECT_EQ(group.header.stage_count, 0u);
    EXPECT_EQ(group.header.flags, 0u);
    EXPECT_EQ(group.resistances, nullptr);
    EXPECT_EQ(group.resistance_count, 0u);
    EXPECT_EQ(group.stages, nullptr);
    EXPECT_EQ(group.stage_count, 0u);
    radicle_free_destructible_group(&group);
    radicle_plugin_destroy(handle);
    std::error_code error;
    std::filesystem::remove_all(root, error);
  };

  check_refusal(0,
                {{"DSTD", std::vector<std::byte>(20)}, {"DSTF", {}}},
                RADICLE_DESTRUCTIBLE_STAGE_MALFORMED);
  check_refusal(2,
                {{"DMDL", std::vector<std::byte>(2)}, {"DSTD", std::vector<std::byte>(20)},
                 {"DSTF", {}}, {"DSTD", std::vector<std::byte>(20)}, {"DSTF", {}}},
                RADICLE_DESTRUCTIBLE_STAGE_INTERLEAVED);
}

namespace {

// Constraint: The eighteen wbDEST carriers minus the three the C ABI tests already walked (ACTI and ARMO in CreateInsertRemoveAndReloadOnTwoCarriers, PROJ in the projectile fixture).
constexpr std::string_view k_remaining_carriers[] = {
    "ALCH", "AMMO", "BOOK", "CONT", "DOOR", "FLOR", "FURN", "INGR",
    "KEYM", "LIGH", "MISC", "MSTT", "NPC_", "TACT", "WEAP",
};

std::vector<std::vector<std::byte>> payloads(const fo4::Plugin& plugin, std::uint32_t local,
                                             std::string_view signature) {
  std::vector<std::vector<std::byte>> out;
  for (const auto* record : fo4::collect_records(plugin)) {
    if (record->header.form_id.value() != local) continue;
    for (const auto& subrecord : record->subrecords) {
      if (subrecord.signature.to_string() == signature) out.push_back(subrecord.data);
    }
  }
  return out;
}

std::uint32_t little_u32(const std::vector<std::byte>& data, std::size_t offset) {
  std::uint32_t value = 0;
  for (std::size_t index = 0; index < 4; ++index) {
    value |= static_cast<std::uint32_t>(data[offset + index]) << (8 * index);
  }
  return value;
}

}  // namespace

// Constraint: Regression guard: before this test only ARMO, ACTI and PROJ reached the C ABI, so a carrier whose authoring rules or DEST slot differ could regress unnoticed. Each of the remaining fifteen signatures now creates the group, edits it, reloads it from disk and removes it.
TEST(CApiDestructible, CrudAndReloadOnEveryRemainingCarrier) {
  const auto root = scratch_root();
  std::filesystem::create_directories(root);

  for (const auto signature_text : k_remaining_carriers) {
    const std::string signature(signature_text);
    // Constraint: a radicle_form_key carries the plugin filename, so the name has to outlive every
// Constraint: handle the loop destroys and reopens.
    const std::string plugin_name = signature + ".esp";
    const auto path = root / plugin_name;

    // Constraint: every step round-trips through the file, so the assertions read the bytes a
// Constraint: later process would load rather than the in-memory record the mutator touched.
    const auto persist = [&](radicle_plugin* handle) {
      const auto bytes = write_bytes(handle);
      {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        EXPECT_TRUE(stream.good()) << signature;
        stream.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
      }
      radicle_plugin_destroy(handle);
      radicle_plugin* reopened = nullptr;
      EXPECT_EQ(radicle_plugin_read_file(path.string().c_str(), &reopened), RADICLE_STATUS_OK)
          << signature << ": " << radicle_last_error();
      return reopened;
    };

    radicle_plugin* handle = nullptr;
    ASSERT_EQ(radicle_plugin_create(plugin_name.c_str(), &handle), RADICLE_STATUS_OK)
        << signature;
    radicle_form_key key{};
    ASSERT_EQ(radicle_plugin_create_record(handle, signature.c_str(), "Destr", &key),
              RADICLE_STATUS_OK)
        << signature;

    radicle_destructible_header_status header_status = RADICLE_DESTRUCTIBLE_HEADER_MISSING;
    ASSERT_EQ(radicle_plugin_insert_destructible_header(handle, key, &header_status),
              RADICLE_STATUS_OK)
        << signature;
    ASSERT_EQ(header_status, RADICLE_DESTRUCTIBLE_HEADER_UPDATED) << signature;

    radicle_destructible_stage_status stage_status = RADICLE_DESTRUCTIBLE_STAGE_UPDATED;
    for (std::size_t index = 0; index < 2; ++index) {
      ASSERT_EQ(radicle_plugin_insert_destructible_stage(handle, key, index, &stage_status),
                RADICLE_STATUS_OK)
          << signature;
      ASSERT_EQ(stage_status, RADICLE_DESTRUCTIBLE_STAGE_UPDATED) << signature;
    }

    const auto health = signature + "\\DestructionHealth";
    ASSERT_EQ(radicle_plugin_set_record_scalar_at(handle, key, health.c_str(), 0, "750"),
              RADICLE_STATUS_OK)
        << signature;
    const auto percent = signature + "\\DestructionStageHealthPercent";
    ASSERT_EQ(radicle_plugin_set_record_scalar_at(handle, key, percent.c_str(), 0, "25"),
              RADICLE_STATUS_OK)
        << signature;
    ASSERT_EQ(radicle_plugin_set_record_scalar_at(handle, key, percent.c_str(), 1, "60"),
              RADICLE_STATUS_OK)
        << signature;

    radicle_destructible_stage_member_status member_status =
        RADICLE_DESTRUCTIBLE_STAGE_MEMBER_UPDATED;
    ASSERT_EQ(
        radicle_plugin_insert_destructible_stage_member(handle, key, 0, "DSTA", &member_status),
        RADICLE_STATUS_OK)
        << signature;
    ASSERT_EQ(member_status, RADICLE_DESTRUCTIBLE_STAGE_MEMBER_UPDATED) << signature;

    handle = persist(handle);
    ASSERT_NE(handle, nullptr) << signature;
// Constraint: The reloaded plugin owns a new handle; keep the key pointing at the stable filename.
    key.plugin = plugin_name.c_str();
    {
      const auto parsed = fo4::read_plugin(write_bytes(handle), signature + ".esp");
      const auto dest = payloads(parsed, key.local_id, "DEST");
      ASSERT_EQ(dest.size(), 1u) << signature;
      ASSERT_EQ(dest[0].size(), 8u) << signature;
      EXPECT_EQ(little_u32(dest[0], 0), 750u) << signature;
      EXPECT_EQ(static_cast<std::uint8_t>(dest[0][4]), 2u) << signature;
      const auto stages = payloads(parsed, key.local_id, "DSTD");
      ASSERT_EQ(stages.size(), 2u) << signature;
      ASSERT_EQ(stages[0].size(), 20u) << signature;
      EXPECT_EQ(static_cast<std::uint8_t>(stages[0][0]), 25u) << signature;
      EXPECT_EQ(static_cast<std::uint8_t>(stages[1][0]), 60u) << signature;
      EXPECT_EQ(payloads(parsed, key.local_id, "DSTA").size(), 1u) << signature;
    }

    ASSERT_EQ(radicle_plugin_remove_destructible_stage(handle, key, 0, &stage_status),
              RADICLE_STATUS_OK)
        << signature;
    ASSERT_EQ(stage_status, RADICLE_DESTRUCTIBLE_STAGE_UPDATED) << signature;

    handle = persist(handle);
    ASSERT_NE(handle, nullptr) << signature;
// Constraint: The reloaded plugin owns a new handle; keep the key pointing at the stable filename.
    key.plugin = plugin_name.c_str();
    {
      const auto parsed = fo4::read_plugin(write_bytes(handle), signature + ".esp");
      const auto stages = payloads(parsed, key.local_id, "DSTD");
      ASSERT_EQ(stages.size(), 1u) << signature;
      EXPECT_EQ(static_cast<std::uint8_t>(stages[0][0]), 60u) << signature;
      EXPECT_EQ(payloads(parsed, key.local_id, "DSTA").size(), 0u) << signature;
    }

    ASSERT_EQ(radicle_plugin_remove_destructible_header(handle, key, &header_status),
              RADICLE_STATUS_OK)
        << signature;
    ASSERT_EQ(header_status, RADICLE_DESTRUCTIBLE_HEADER_UPDATED) << signature;

    handle = persist(handle);
    ASSERT_NE(handle, nullptr) << signature;
// Constraint: The reloaded plugin owns a new handle; keep the key pointing at the stable filename.
    key.plugin = plugin_name.c_str();
    {
      const auto parsed = fo4::read_plugin(write_bytes(handle), signature + ".esp");
      EXPECT_EQ(payloads(parsed, key.local_id, "DEST").size(), 0u) << signature;
      EXPECT_EQ(payloads(parsed, key.local_id, "DSTD").size(), 0u) << signature;
    }
    ASSERT_EQ(radicle_plugin_insert_destructible_stage(handle, key, 0, &stage_status),
              RADICLE_STATUS_OK)
        << signature;
    EXPECT_EQ(stage_status, RADICLE_DESTRUCTIBLE_STAGE_HEADER_MISSING) << signature;
    radicle_plugin_destroy(handle);
  }

  std::error_code error;
  std::filesystem::remove_all(root, error);
}