#include "fo4/environment/navmesh.hpp"
#include "fo4/plugin/navmesh_writer.hpp"
#include "fo4/plugin/subrecord_authoring.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/scalar.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>
#include <cstdlib>
#include <algorithm>
#include <map>
#include <set>

namespace {

class NavmeshWriterReal : public testing::TestWithParam<const char*> {};

TEST_P(NavmeshWriterReal, OfficialOrderAndCappedEditsPreserveEveryUntouchedByte) {
  const auto* directory = std::getenv("FO4_DATA_DIR");
  if (!directory) GTEST_SKIP() << "set FO4_DATA_DIR";
  record_game_build(detect_game_build(directory));
  const auto path = std::filesystem::path{directory} / GetParam();
  if (!std::filesystem::exists(path)) GTEST_SKIP() << "master missing";
  const std::filesystem::path paths[] = {path};
  const auto env = fo4::env::Environment::load(paths);
  auto plugin = env.plugin(0);
  std::set<fo4::FormKey> changed;
  std::size_t navm = 0, navi = 0, vertices = 0, triangles = 0, doors = 0, info_edits = 0;
  for (const auto* ref : env.plugin_records(0)) {
    const auto& record = *ref->record;
    const auto type = record.header.signature.to_string();
    if (type != "NAVM" && type != "NAVI") continue;
    SCOPED_TRACE(record.form_key.to_string());
    const std::vector<std::string> order = type == "NAVM"
        ? std::vector<std::string>{"EDID", "NVNM", "ONAM", "PNAM", "NNAM", "MNAM"}
        : std::vector<std::string>{"EDID", "NVER", "NVMI", "NVPP", "NVSI"};
    std::size_t last = 0;
    for (const auto& sub : record.subrecords) {
      if (sub.signature == fo4::sig_xxxx) { ASSERT_EQ(sub.data.size(), 4); continue; }
      const auto at = std::find(order.begin(), order.end(), sub.signature.to_string());
      ASSERT_NE(at, order.end()) << sub.signature.to_string();
      const auto position = static_cast<std::size_t>(at - order.begin());
      EXPECT_GE(position, last); last = position;
    }
    if (type == "NAVI") {
      ++navi;
      const auto infos = fo4::env::decode_navmesh_info(env, *ref);
      for (std::size_t i = 0; i < std::min<std::size_t>(infos.size(), 12); ++i) {
        EXPECT_EQ(fo4::set_record_scalar(plugin, record.form_key, *fo4::schema::scalar_field_descriptor("NAVI\\Preferred"), i, "0.375"), fo4::ScalarMutationStatus::updated);
        changed.insert(record.form_key); ++info_edits;
      }
      continue;
    }
    ++navm;
    if (vertices >= 12 && triangles >= 12 && doors >= 12) continue;
    const auto mesh = fo4::env::decode_navmesh(env, *ref);
    if (!mesh) continue;
    bool edited = false;
    if (!mesh->vertices.empty() && vertices < 12) {
      auto value = mesh->vertices.back(); value[0] = value[0] == 1.0f ? 2.0f : 1.0f;
      EXPECT_EQ(fo4::set_navmesh_vertex(plugin, record.form_key, mesh->vertices.size()-1, value), fo4::ScalarMutationStatus::updated); ++vertices; edited = true;
    }
    if (!mesh->triangles.empty() && triangles < 12) {
      auto value = mesh->triangles.back(); value.clearance = value.clearance == 1.0f ? 2.0f : 1.0f;
      EXPECT_EQ(fo4::set_navmesh_triangle(plugin, record.form_key, mesh->triangles.size()-1, value), fo4::ScalarMutationStatus::updated); ++triangles; edited = true;
    }
    if (!mesh->doors.empty() && doors < 12) {
      auto value = mesh->doors.back(); value.crc ^= 1;
      EXPECT_EQ(fo4::set_navmesh_door(plugin, record.form_key, mesh->doors.size()-1, value), fo4::ScalarMutationStatus::updated); ++doors; edited = true;
    }
    if (edited) changed.insert(record.form_key);
  }
  const auto bytes = fo4::write_plugin(plugin);
  const auto parsed = fo4::read_plugin(bytes, path);
  EXPECT_EQ(parsed.header.masters.size(), plugin.header.masters.size());
  EXPECT_EQ(parsed.header.next_form_id, plugin.header.next_form_id);
  const auto header_payloads = [](const fo4::Record& record) {
    std::vector<const fo4::Subrecord*> result;
    for (const auto& sub : record.subrecords) {
      if (sub.signature != fo4::sig_onam && sub.signature != fo4::sig_xxxx) result.push_back(&sub);
    }
    return result;
  };
  const auto a_header = header_payloads(env.plugin(0).tes4), b_header = header_payloads(parsed.tes4);
  ASSERT_EQ(a_header.size(), b_header.size());
  for (std::size_t i = 0; i < a_header.size(); ++i) {
    EXPECT_EQ(a_header[i]->signature, b_header[i]->signature); EXPECT_EQ(a_header[i]->data, b_header[i]->data);
  }
  std::map<fo4::FormKey, const fo4::Record*> by_key;
  for (const auto* record : fo4::collect_records(parsed)) by_key.emplace(record->form_key, record);
  std::size_t untouched = 0;
  for (const auto* before : fo4::collect_records(env.plugin(0))) {
    if (before->header.signature == fo4::Signature::from_string("TES4")) continue;
    const auto found = by_key.find(before->form_key); ASSERT_NE(found, by_key.end());
    const auto* after = found->second;
    if (!changed.contains(before->form_key)) {
      const auto a = fo4::record_original_bytes(env.plugin(0), *before), b = fo4::record_original_bytes(parsed, *after);
      ASSERT_EQ(a.size(), b.size()); ASSERT_TRUE(std::equal(a.begin(), a.end(), b.begin())); ++untouched;
      continue;
    }
    ASSERT_EQ(before->subrecords.size(), after->subrecords.size());
    const auto* edited = fo4::find_record(plugin, before->form_key); ASSERT_NE(edited, nullptr);
    for (std::size_t i = 0; i < before->subrecords.size(); ++i) {
      const auto& a = before->subrecords[i]; const auto& b = after->subrecords[i];
      ASSERT_EQ(a.signature, b.signature); ASSERT_EQ(a.data.size(), b.data.size());
      const auto tag = a.signature.to_string();
      if (tag != "NVNM" && tag != "NVMI") { EXPECT_EQ(a.data, b.data); continue; }
      EXPECT_EQ(b.data, edited->subrecords[i].data);
      if (tag == "NVMI") {
        for (std::size_t j = 0; j < a.data.size(); ++j) {
          if (j < 20 || j >= 24) { EXPECT_EQ(a.data[j], b.data[j]); }
        }
      } else {
        const auto* ref = env.winner(before->form_key); ASSERT_NE(ref, nullptr);
        const auto mesh = fo4::env::decode_navmesh(env, *ref); ASSERT_TRUE(mesh);
        const auto vertex_at = 20 + (mesh->vertices.empty() ? 0 : (mesh->vertices.size()-1)*12);
        const auto triangle_at = 24 + mesh->vertices.size()*12 + (mesh->triangles.empty() ? 0 : (mesh->triangles.size()-1)*21);
        const auto door_at = 32 + mesh->vertices.size()*12 + mesh->triangles.size()*21 + mesh->edges.size()*11 + (mesh->doors.empty() ? 0 : (mesh->doors.size()-1)*10);
        for (std::size_t j = 0; j < a.data.size(); ++j) {
          if ((!mesh->vertices.empty() && j >= vertex_at && j < vertex_at+4) ||
              (!mesh->triangles.empty() && j >= triangle_at+12 && j < triangle_at+16) ||
              (!mesh->doors.empty() && j >= door_at+2 && j < door_at+6)) continue;
          EXPECT_EQ(a.data[j], b.data[j]);
        }
      }
    }
  }
  RecordProperty("NAVM", static_cast<int>(navm)); RecordProperty("NAVI", static_cast<int>(navi));
  RecordProperty("vertices", static_cast<int>(vertices)); RecordProperty("triangles", static_cast<int>(triangles));
  RecordProperty("doors", static_cast<int>(doors)); RecordProperty("info_edits", static_cast<int>(info_edits));
  RecordProperty("untouched", static_cast<int>(untouched));
}

INSTANTIATE_TEST_SUITE_P(OfficialMasters, NavmeshWriterReal, testing::Values(
    "Fallout4.esm", "DLCRobot.esm", "DLCworkshop01.esm", "DLCCoast.esm", "DLCworkshop02.esm", "DLCworkshop03.esm", "DLCNukaWorld.esm"));

}
