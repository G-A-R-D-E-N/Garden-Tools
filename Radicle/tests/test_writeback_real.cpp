#include "fo4/environment/environment.hpp"
#include "fo4/environment/placed.hpp"
#include "fo4/forms/placed_reference.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/plugin/writer.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace {

std::string text(const fo4::FormKey& key) { return key.to_string(); }
std::string text(const std::optional<fo4::FormKey>& key) { return key ? key->to_string() : "-"; }

std::vector<std::string> links(const fo4::env::PlacedReferenceData& d) {
  std::vector<std::string> out{"base " + text(d.base), "layer " + text(d.layer), "zone " + text(d.encounter_zone),
                               "location " + text(d.location_reference), "attach " + text(d.attach_ref)};
  for (const auto& key : d.location_ref_types) out.push_back("XLRT " + text(key));
  if (d.enable_parent) out.push_back("XESP " + text(d.enable_parent->reference));
  if (d.ownership) out.push_back("XOWN " + text(d.ownership->owner));
  for (const auto& link : d.linked_references) out.push_back("XLKR " + text(link.keyword) + " " + text(link.reference));
  for (const auto& parent : d.activate_parents) out.push_back("XAPR " + text(parent.reference));
  for (const auto& portal : d.portals) out.push_back("XPOD " + text(portal.origin) + " " + text(portal.destination));
  if (d.teleport) out.push_back("XTEL " + text(d.teleport->door) + " " + text(d.teleport->transition_interior));
  if (d.room) {
    out.push_back("XRMR " + text(d.room->lighting_template) + " " + text(d.room->image_space));
    for (const auto& room : d.room->linked_rooms) out.push_back("XLRM " + text(room));
  }
  for (const auto& patrol : d.patrols) {
    out.push_back("patrol " + text(patrol.idle) + " " + text(patrol.topic));
    for (const auto& topic : patrol.topics) out.push_back("PDTO " + text(topic.topic));
  }
  if (d.scripts) {
    for (const auto& script : d.scripts->scripts) {
      for (const auto& property : script.properties) out.push_back("VMAD " + property.name + " " + text(property.object));
    }
  }
  return out;
}

}  // namespace

TEST(WritebackRealData, CopiedDlcReferencesKeepEveryLinkWhenMastersMove) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder";
  const std::filesystem::path data(dir);
  const std::vector<std::filesystem::path> order{data / "Fallout4.esm", data / "DLCRobot.esm"};
  for (const auto& path : order) {
    if (!std::filesystem::exists(path)) GTEST_SKIP() << path.filename().string() << " is not present";
  }
  const auto env = fo4::env::Environment::load(order);
  const auto& dlc = env.plugin(1);

  const std::set<std::string> added{"XPOD", "XORD", "XLRT", "XPWR", "XAPR", "XPLK", "XWPN"};
  const std::set<std::string> linked{"XESP", "XLKR", "XOWN", "XTEL", "XLRM", "XEZN", "XLCN",
                                     "XLYR", "XATR", "VMAD", "XRMR", "XRFG"};
  std::vector<fo4::FormKey> refs;
  std::size_t other = 0;
  fo4::visit_records(dlc, [&](const fo4::Record& record) {
    if (!fo4::env::is_placed_reference_signature(record.header.signature)) return;
    bool has_added = false;
    bool has_linked = false;
    for (const auto& sub : record.subrecords) {
      has_added = has_added || added.contains(sub.signature.to_string());
      has_linked = has_linked || linked.contains(sub.signature.to_string());
    }
    if (has_added || (has_linked && other++ % 25 == 0)) refs.push_back(record.form_key);
  });
  ASSERT_FALSE(refs.empty());

  auto patch = fo4::make_empty_plugin("RemapProbe.esp");
  std::size_t copied = 0;
  for (const auto& ref : refs) {
    const auto status = fo4::forms::copy_placed_reference_as_override(patch, dlc, ref);
    if (status == fo4::forms::PlacedReferenceCopyStatus::inserted) ++copied;
  }
  ASSERT_EQ(copied, refs.size());
  ASSERT_EQ(patch.header.masters.size(), 2u);
  ASSERT_NE(patch.header.masters[0].key, dlc.header.masters[0].key) << "the copy must move the masters";

  const auto dir_out = std::filesystem::temp_directory_path() /
                       ("radicle_writeback_real_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(dir_out);
  const auto patch_path = dir_out / "RemapProbe.esp";
  fo4::write_plugin_file(patch, patch_path);
  auto with_patch = order;
  with_patch.push_back(patch_path);
  const auto reloaded = fo4::env::Environment::load(with_patch);

  std::size_t mismatched = 0;
  for (const auto& ref : refs) {
    const auto source = links(fo4::env::decode_placed_reference(env, *env.winner(ref)));
    const auto patched = links(fo4::env::decode_placed_reference(reloaded, *reloaded.winner(ref)));
    if (source != patched && ++mismatched <= 20) {
      for (std::size_t i = 0; i < std::max(source.size(), patched.size()); ++i) {
        const auto a = i < source.size() ? source[i] : std::string("<none>");
        const auto b = i < patched.size() ? patched[i] : std::string("<none>");
        if (a != b) ADD_FAILURE() << ref.to_string() << ": " << a << " became " << b;
      }
    }
  }
  std::error_code ec;
  std::filesystem::remove_all(dir_out, ec);
  EXPECT_EQ(mismatched, 0u) << "of " << refs.size() << " copied references";
}
