#include "fo4/c_api_navmesh.h"
#include "fo4/environment/navmesh.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/schema/scalar.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

using fo4::test::ByteWriter;

ByteWriter geometry(bool exterior, std::uint32_t version = 15) {
  ByteWriter b;
  b.u32(version); b.u32(0x12345678); b.u32(exterior ? 0x810 : 0);
  if (exterior) { b.u16(0xFFFE); b.u16(7); } else b.u32(0x811);
  b.u32(1); b.f32(1.5f); b.f32(-2); b.f32(3);
  b.u32(1); b.u16(0); b.u16(1); b.u16(2); b.u16(0xFFFF); b.u16(0); b.u16(7);
  b.f32(4.5f); b.u8(0xAA); b.u16(0x8012); b.u16(0xABCD);
  b.u32(1); b.u32(0xDEADBEEF); b.u32(0x812); b.u16(0xFFFD); b.u8(2);
  b.u32(1); b.u16(0xFFFB); b.u32(0xABCDEF); b.u32(0x813);
  for (int i = 0; i < 4; ++i) b.u32(0);
  for (int i = 0; i < 8; ++i) b.f32(0);
  return b;
}

ByteWriter info(bool island) {
  ByteWriter b;
  b.u32(0x800); b.u32(0xFEDCBA98); b.f32(5); b.f32(6); b.f32(7); b.f32(0.25f);
  b.u32(1); b.u32(0x812); b.u32(1); b.u32(0x814);
  b.u32(1); b.u32(0xDEADBEEF); b.u32(0x813); b.u8(island);
  if (island) {
    for (int i = 0; i < 6; ++i) b.f32(static_cast<float>(i));
    b.u32(1); b.u16(0); b.u16(1); b.u16(2);
    b.u32(1); b.f32(1); b.f32(2); b.f32(3);
  }
  b.u32(0x87654321); b.u32(0); b.u32(0x811);
  return b;
}

ByteWriter extra_edge_geometry(std::uint32_t flags, std::array<std::uint16_t, 3> edges, std::uint32_t extra_count) {
  ByteWriter b;
  b.u32(15); b.u32(0x12345678); b.u32(0); b.u32(0x811);
  b.u32(1); b.f32(1.5f); b.f32(-2); b.f32(3);
  b.u32(1); b.u16(0); b.u16(1); b.u16(2);
  for (const auto edge : edges) b.u16(edge);
  b.f32(4.5f); b.u8(0xAA); b.u32(flags);
  b.u32(extra_count);
  for (std::uint32_t i = 0; i < extra_count; ++i) { b.u32(0xDEADBEEF); b.u32(0x812); b.u16(0xFFFD); b.u8(2); }
  b.u32(0);
  for (int i = 0; i < 4; ++i) b.u32(0);
  for (int i = 0; i < 8; ++i) b.f32(0);
  return b;
}

ByteWriter navi_paths() {
  ByteWriter b;
  b.u32(2); b.u32(2); b.u32(0x800); b.u32(0x810); b.u32(1); b.u32(0x810);
  b.u32(1); b.u32(0x800); b.u32(7);
  return b;
}

class NavmeshRead : public testing::Test {
protected:
  void SetUp() override {
    root = std::filesystem::temp_directory_path() /
        ("radicle_navmesh_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root); path = root / "Navigation.esm";
  }
  void TearDown() override { std::error_code error; std::filesystem::remove_all(root, error); }
  void write(const ByteWriter& nvnm, const ByteWriter& nvmi, bool duplicate = false, std::uint16_t form_version = 131,
             const ByteWriter* nvpp = nullptr, std::uint32_t navi_version = 15) {
    ByteWriter out;
    auto r = fo4::test::begin_record(out, "TES4", 1, 0);
    ByteWriter h; h.f32(1); h.u32(2); h.u32(0x900);
    fo4::test::write_subrecord(out, "HEDR", h.bytes); fo4::test::end_record(out, r);
    auto g = fo4::test::begin_group(out, "NAVM", 0); r = fo4::test::begin_record(out, "NAVM", 0, 0x800, form_version);
    if (!nvnm.bytes.empty()) fo4::test::write_subrecord(out, "NVNM", nvnm.bytes);
    if (duplicate) fo4::test::write_subrecord(out, "NVNM", nvnm.bytes);
    fo4::test::end_record(out, r); fo4::test::end_group(out, g);
    g = fo4::test::begin_group(out, "NAVI", 0); r = fo4::test::begin_record(out, "NAVI", 0, 0x801);
    ByteWriter v; v.u32(navi_version); fo4::test::write_subrecord(out, "NVER", v.bytes);
    fo4::test::write_subrecord(out, "NVMI", nvmi.bytes);
    if (nvpp) fo4::test::write_subrecord(out, "NVPP", nvpp->bytes);
    fo4::test::end_record(out, r); fo4::test::end_group(out, g);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(out.bytes.data()), static_cast<std::streamsize>(out.bytes.size()));
  }
  fo4::env::Environment load() { const std::filesystem::path paths[] = {path}; return fo4::env::Environment::load(paths); }
  const fo4::env::RecordRef& record(const fo4::env::Environment& env, std::uint32_t id) {
    return *env.winner(fo4::FormKey{fo4::PluginKey::from_filename("Navigation.esm"), fo4::LocalFormId{id}});
  }
  std::filesystem::path root, path;
};

TEST_F(NavmeshRead, StoredGeometryLinksFlagsAndParentUnionMatchAbi) {
  for (const auto exterior : {false, true}) {
    write(geometry(exterior), info(exterior));
    const auto env = load(); const auto mesh = fo4::env::decode_navmesh(env, record(env, 0x800));
    ASSERT_TRUE(mesh); EXPECT_EQ(mesh->version, 15u);
    EXPECT_EQ(mesh->parent.crc, 0x12345678u);
    if (exterior) { EXPECT_EQ(mesh->parent.grid_y, -2); EXPECT_EQ(mesh->parent.grid_x, 7); }
    else { EXPECT_EQ(mesh->parent.cell.local().value(), 0x811u); }
    ASSERT_EQ(mesh->vertices.size(), 1); EXPECT_EQ(mesh->vertices[0][0], 1.5f);
    ASSERT_EQ(mesh->triangles.size(), 1); const auto& t = mesh->triangles[0];
    EXPECT_EQ(t.edges[0], -1); EXPECT_EQ(t.vertices[2], 2); EXPECT_EQ(t.clearance, 4.5f);
    EXPECT_EQ(t.flags, 0xABCD8012u); EXPECT_EQ(t.traversal, 0xAA);
    ASSERT_EQ(mesh->edges.size(), 1); EXPECT_EQ(mesh->edges[0].triangle, -3);
    EXPECT_EQ(mesh->edges[0].navmesh.local().value(), 0x812u);
    ASSERT_EQ(mesh->doors.size(), 1); EXPECT_EQ(mesh->doors[0].door.local().value(), 0x813u);
    EXPECT_EQ(mesh->opaque_tail.size(), 48u);
    const auto navi = fo4::env::decode_navmesh_info(env, record(env, 0x801)); ASSERT_EQ(navi.size(), 1);
    EXPECT_EQ(navi[0].preferred_edges[0].local().value(), 0x814u); EXPECT_EQ(navi[0].has_island, exterior);
    EXPECT_EQ(navi[0].island.size(), exterior ? 50u : 0u);
    const auto name = path.string(); const char* paths[] = {name.c_str()}; radicle_env* abi = nullptr;
    ASSERT_EQ(radicle_env_load(paths, 1, &abi), RADICLE_STATUS_OK);
    const radicle_record** records = nullptr; std::size_t count = 0;
    ASSERT_EQ(radicle_env_plugin_records(abi, 0, &records, &count), RADICLE_STATUS_OK);
    for (std::size_t i = 0; i < count; ++i) {
      if (env.plugin_records(0)[i]->record->header.signature == fo4::Signature::from_string("NAVM")) {
        const radicle_navmesh* value = nullptr;
        EXPECT_EQ(radicle_navmesh_decode(abi, records[i], &value), RADICLE_STATUS_OK);
        if (value) { EXPECT_EQ(value->triangles[0].flags, 0xABCD8012u); radicle_navmesh_free(value); }
      } else if (env.plugin_records(0)[i]->record->header.signature == fo4::Signature::from_string("NAVI")) {
        const radicle_navi* value = nullptr;
        EXPECT_EQ(radicle_navi_decode(abi, records[i], &value), RADICLE_STATUS_OK);
        if (value) { EXPECT_EQ(value->info_count, 1u); EXPECT_EQ(value->infos[0].island_size, exterior ? 50u : 0u); radicle_navi_free(value); }
      }
    }
    radicle_free_record_array(records); radicle_env_destroy(abi);
    const auto bytes = fo4::write_plugin(env.plugin(0));
    EXPECT_EQ(bytes, fo4::test::original_bytes(env.plugin(0)));
  }
}

TEST_F(NavmeshRead, RejectsUnsupportedTruncatedDuplicateAndOverflowPayloads) {
  auto bad = geometry(false); bad.bytes.pop_back();
  auto overflow = geometry(false); overflow.patch_u32(16, 0xFFFFFFFFu);
  for (const auto& b : {bad, overflow, geometry(false, 16)}) {
    write(b, info(false)); const auto env = load();
    EXPECT_THROW(fo4::env::decode_navmesh(env, record(env, 0x800)), fo4::env::WorldDecodeError);
  }
  write(geometry(false), info(false), true); const auto env = load();
  EXPECT_THROW(fo4::env::decode_navmesh(env, record(env, 0x800)), fo4::env::WorldDecodeError);
  bad = info(true); bad.bytes.pop_back(); write(geometry(false), bad); const auto bad_env = load();
  EXPECT_THROW(fo4::env::decode_navmesh_info(bad_env, record(bad_env, 0x801)), fo4::env::WorldDecodeError);
}

TEST_F(NavmeshRead, MissingGeometryStaysAbsent) {
  write(ByteWriter{}, info(false)); const auto env = load();
  EXPECT_FALSE(fo4::env::decode_navmesh(env, record(env, 0x800)));
}

TEST_F(NavmeshRead, InfoPrefixWritesReparseAndMalformedLayoutsStayAbsent) {
  write(geometry(false), info(true)); const auto env = load(); auto plugin = env.plugin(0);
  const auto key = record(env, 0x801).record->form_key;
  for (const auto* name : {"NAVI\\LocationX", "NAVI\\LocationY", "NAVI\\LocationZ", "NAVI\\Preferred"}) {
    EXPECT_EQ(fo4::set_record_scalar(plugin, key, *fo4::schema::scalar_field_descriptor(name), 0, "1.25"), fo4::ScalarMutationStatus::updated);
  }
  const auto field = fo4::schema::formlink_field_descriptor("NAVI\\Navmeshes"); ASSERT_TRUE(field);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key, *field, 0, record(env, 0x800).record->form_key), fo4::FormLinkFieldMutationStatus::unchanged);
  EXPECT_THROW(fo4::set_record_formlink_field(plugin, key, *field, 0, key), fo4::schema::FormLinkFieldError);
  fo4::write_plugin_file(plugin, path); const auto reread = load();
  const auto values = fo4::env::decode_navmesh_info(reread, record(reread, 0x801)); ASSERT_EQ(values.size(), 1);
  EXPECT_EQ(values[0].location, (std::array<float,3>{1.25f,1.25f,1.25f})); EXPECT_EQ(values[0].preferred, 1.25f);
  EXPECT_EQ(values[0].island.size(), 50);
  auto malformed = info(true); malformed.bytes.pop_back(); write(geometry(false), malformed);
  const auto bad_env = load(); auto bad = bad_env.plugin(0); const auto before = fo4::write_plugin(bad);
  EXPECT_EQ(fo4::set_record_scalar(bad, key, *fo4::schema::scalar_field_descriptor("NAVI\\Preferred"), 0, "1"), fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::write_plugin(bad), before);
}

TEST_F(NavmeshRead, CAbiDecodeRejectsRecordsFromAnotherEnvironmentWithoutWriting) {
  write(geometry(false), info(false));
  const auto name = path.string(); const char* paths[] = {name.c_str()};
  radicle_env* env_a = nullptr; radicle_env* env_b = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 1, &env_a), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_env_load(paths, 1, &env_b), RADICLE_STATUS_OK);
  const radicle_record* navm = nullptr; const radicle_record* navi = nullptr;
  ASSERT_EQ(radicle_env_winner(env_a, radicle_form_key{"Navigation.esm", 0x800}, &navm), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_env_winner(env_a, radicle_form_key{"Navigation.esm", 0x801}, &navi), RADICLE_STATUS_OK);
  radicle_navmesh navmesh_sentinel{};
  const radicle_navmesh* navmesh_out = &navmesh_sentinel;
  EXPECT_EQ(radicle_navmesh_decode(env_b, navm, &navmesh_out), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_NE(std::string(radicle_last_error()).find("does not belong"), std::string::npos);
  EXPECT_EQ(navmesh_out, &navmesh_sentinel);
  radicle_navi navi_sentinel{};
  const radicle_navi* navi_out = &navi_sentinel;
  EXPECT_EQ(radicle_navi_decode(env_b, navi, &navi_out), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_NE(std::string(radicle_last_error()).find("does not belong"), std::string::npos);
  EXPECT_EQ(navi_out, &navi_sentinel);
  radicle_env_destroy(env_b); radicle_env_destroy(env_a);
}

TEST_F(NavmeshRead, ExtraEdgeFlagIndexesExtraEdgeArray) {
  write(extra_edge_geometry(0x5, {0, 0xFFFF, 1}, 2), info(false));
  const auto env = load(); const auto mesh = fo4::env::decode_navmesh(env, record(env, 0x800));
  ASSERT_TRUE(mesh); ASSERT_EQ(mesh->edges.size(), 2u); ASSERT_EQ(mesh->triangles.size(), 1u);
  const auto& t = mesh->triangles[0];
  EXPECT_TRUE(t.edge_is_extra(0)); EXPECT_FALSE(t.edge_is_extra(1)); EXPECT_TRUE(t.edge_is_extra(2));
  EXPECT_EQ(t.edges[1], -1);
}

TEST_F(NavmeshRead, FlaggedEdgeIndexPastExtraEdgeCountFailsClosed) {
  for (const auto& edges : {std::array<std::uint16_t, 3>{1, 0, 0}, std::array<std::uint16_t, 3>{0xFFFF, 0, 0}}) {
    write(extra_edge_geometry(0x1, edges, 1), info(false)); const auto env = load();
    EXPECT_THROW(fo4::env::decode_navmesh(env, record(env, 0x800)), fo4::env::WorldDecodeError);
  }
}

TEST(NavmeshAbi, TriangleEdgeAccessorReadsFlagBitsAndRefusesOutOfRange) {
  radicle_navmesh_triangle triangle{};
  triangle.flags = 0x5;
  int32_t is_extra = -1;
  EXPECT_EQ(radicle_navmesh_triangle_edge_is_extra(&triangle, 0, &is_extra), RADICLE_STATUS_OK); EXPECT_EQ(is_extra, 1);
  EXPECT_EQ(radicle_navmesh_triangle_edge_is_extra(&triangle, 1, &is_extra), RADICLE_STATUS_OK); EXPECT_EQ(is_extra, 0);
  EXPECT_EQ(radicle_navmesh_triangle_edge_is_extra(&triangle, 3, &is_extra), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_navmesh_triangle_edge_is_extra(nullptr, 0, &is_extra), RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST_F(NavmeshRead, PathPayloadDecodesListsAndAnchorsAndFailsClosed) {
  write(geometry(false), info(false)); const auto absent = load();
  EXPECT_TRUE(fo4::env::decode_navmesh_paths(absent, record(absent, 0x801)).paths.empty());
  const auto good = navi_paths();
  auto truncated = good; truncated.bytes.pop_back();
  auto trailing = good; trailing.bytes.push_back(std::byte{0});
  write(geometry(false), info(false), false, 131, &good); const auto env = load();
  const auto decoded = fo4::env::decode_navmesh_paths(env, record(env, 0x801));
  ASSERT_EQ(decoded.paths.size(), 2u);
  ASSERT_EQ(decoded.paths[0].size(), 2u); EXPECT_EQ(decoded.paths[0][0].local().value(), 0x800u); EXPECT_EQ(decoded.paths[0][1].local().value(), 0x810u);
  ASSERT_EQ(decoded.paths[1].size(), 1u); EXPECT_EQ(decoded.paths[1][0].local().value(), 0x810u);
  ASSERT_EQ(decoded.anchors.size(), 1u);
  EXPECT_EQ(decoded.anchors[0].first.local().value(), 0x800u); EXPECT_EQ(decoded.anchors[0].second, 7u);
  const auto name = path.string(); const char* files[] = {name.c_str()}; radicle_env* abi = nullptr;
  ASSERT_EQ(radicle_env_load(files, 1, &abi), RADICLE_STATUS_OK);
  const radicle_record* navi = nullptr;
  ASSERT_EQ(radicle_env_winner(abi, radicle_form_key{"Navigation.esm", 0x801}, &navi), RADICLE_STATUS_OK);
  const radicle_navi_path_list* list = nullptr;
  ASSERT_EQ(radicle_navi_path_list_decode(abi, navi, &list), RADICLE_STATUS_OK);
  ASSERT_EQ(list->path_count, 2u); EXPECT_EQ(list->paths[0].navmesh_count, 2u); EXPECT_EQ(list->paths[0].navmeshes[1].local_id, 0x810u);
  ASSERT_EQ(list->anchor_count, 1u); EXPECT_EQ(list->anchors[0].value, 7u);
  radicle_navi_path_list_free(list); radicle_env_destroy(abi);
  for (const auto* bad : {&truncated, &trailing}) {
    write(geometry(false), info(false), false, 131, bad); const auto bad_env = load();
    EXPECT_THROW(fo4::env::decode_navmesh_paths(bad_env, record(bad_env, 0x801)), fo4::env::WorldDecodeError);
  }
}

TEST_F(NavmeshRead, PathListAbiRefusesForeignEnvironmentNullArgumentsAndNonNaviRecords) {
  const auto good = navi_paths();
  write(geometry(false), info(false), false, 131, &good);
  const auto name = path.string(); const char* paths[] = {name.c_str()};
  radicle_env* env_a = nullptr; radicle_env* env_b = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 1, &env_a), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_env_load(paths, 1, &env_b), RADICLE_STATUS_OK);
  const radicle_record* navm = nullptr; const radicle_record* navi = nullptr;
  ASSERT_EQ(radicle_env_winner(env_a, radicle_form_key{"Navigation.esm", 0x800}, &navm), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_env_winner(env_a, radicle_form_key{"Navigation.esm", 0x801}, &navi), RADICLE_STATUS_OK);
  const radicle_navi_path_list* list = nullptr;
  EXPECT_EQ(radicle_navi_path_list_decode(env_b, navi, &list), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_NE(std::string(radicle_last_error()).find("does not belong"), std::string::npos);
  EXPECT_EQ(list, nullptr);
  EXPECT_EQ(radicle_navi_path_list_decode(nullptr, navi, &list), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_navi_path_list_decode(env_a, nullptr, &list), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_navi_path_list_decode(env_a, navi, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_navi_path_list_decode(env_a, navm, &list), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(list, nullptr);
  radicle_navi_path_list_free(nullptr);
  radicle_env_destroy(env_b); radicle_env_destroy(env_a);
}

TEST_F(NavmeshRead, PathPayloadRequiresNaviVersionFifteen) {
  const auto good = navi_paths();
  write(geometry(false), info(false), false, 131, &good, 14);
  const auto env = load();
  try {
    fo4::env::decode_navmesh_paths(env, record(env, 0x801));
    ADD_FAILURE() << "path payload decoded at NVER 14";
  } catch (const fo4::env::WorldDecodeError& error) {
    EXPECT_NE(std::string(error.what()).find("version 15 is required"), std::string::npos);
  }
  EXPECT_THROW(fo4::env::decode_navmesh_info(env, record(env, 0x801)), fo4::env::WorldDecodeError);
}

TEST_F(NavmeshRead, LegacyFormVersionIsRejectedWithoutChangingBytes) {
  for (const auto version : {127u, 128u}) {
    write(geometry(false), info(false), false, static_cast<std::uint16_t>(version));
    const auto env = load();
    if (version < 128) {
      EXPECT_THROW(fo4::env::decode_navmesh(env, record(env, 0x800)), fo4::env::WorldDecodeError);
    } else {
      EXPECT_TRUE(fo4::env::decode_navmesh(env, record(env, 0x800)));
    }
    EXPECT_EQ(fo4::write_plugin(env.plugin(0)), fo4::test::original_bytes(env.plugin(0)));
  }
}

}
