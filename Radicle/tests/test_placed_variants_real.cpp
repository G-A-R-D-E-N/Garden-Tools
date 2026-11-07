#include "fo4/environment/base_records.hpp"
#include "fo4/environment/environment.hpp"
#include "fo4/environment/placed.hpp"
#include "fo4/forms/placed_reference.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/schema/preflight.hpp"
#include "fo4/schema/scalar.hpp"
#include "fo4/schema/record_constraints.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::filesystem::path official(const char* name, bool& skip) {
  const auto* dir = std::getenv("FO4_DATA_DIR");
  skip = dir == nullptr || !std::filesystem::exists(std::filesystem::path{dir} / name);
  return skip ? std::filesystem::path{} : std::filesystem::path{dir} / name;
}

bool tracked(fo4::Signature signature) {
  for (const auto* text : {"PARW", "PBAR", "PBEA", "PCON", "PFLA", "PGRE", "PHZD", "PMIS", "PLYR", "ADDN"}) {
    if (signature == fo4::Signature::from_string(text)) return true;
  }
  return false;
}

bool variant(fo4::Signature signature) {
  return tracked(signature) && signature != fo4::Signature::from_string("ADDN") &&
         signature != fo4::Signature::from_string("PLYR");
}

void check_authoring(const fo4::Record& record, std::vector<std::string>& violators) {
  const auto constraint = fo4::schema::record_authoring_constraint(record.header.signature);
  if (!constraint.has_value()) {
    violators.push_back(record.form_key.to_string() + " has no authoring schema");
    return;
  }
  const auto rules = fo4::schema::authoring_subrecords(*constraint);
  std::vector<std::size_t> counts(rules.size(), 0);
  std::optional<std::size_t> last;
  for (const auto& sub : record.subrecords) {
    for (std::size_t index = 0; index < rules.size(); ++index) {
      if (rules[index].signature != sub.signature) continue;
      ++counts[index];
      if (last.has_value() && index < *last) {
        violators.push_back(record.form_key.to_string() + " " + sub.signature.to_string() + " out of order");
      }
      last = index;
      break;
    }
  }
  for (std::size_t index = 0; index < rules.size(); ++index) {
    const auto& rule = rules[index];
    if (counts[index] < static_cast<std::size_t>(rule.minimum) ||
        counts[index] > static_cast<std::size_t>(rule.maximum)) {
      violators.push_back(record.form_key.to_string() + " " + rule.signature.to_string() + " count " +
                          std::to_string(counts[index]));
    }
  }
}

class PlacedVariantsReal : public testing::TestWithParam<const char*> {};

TEST_P(PlacedVariantsReal, EveryTrackedRecordDecodesAndMeetsAuthoringMinimums) {
  bool skip = false;
  const auto path = official(GetParam(), skip);
  if (skip) { GTEST_SKIP() << "set FO4_DATA_DIR to installed Fallout 4 data"; }
  record_game_build(detect_game_build(path.parent_path()));
  auto plugin = fo4::read_plugin_file(path);
  std::map<std::string, int> totals;
  std::map<std::string, int> preflighted;
  std::vector<std::string> violators;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    const auto signature = record.header.signature;
    if (!tracked(signature)) return;
    ++totals[signature.to_string()];
    for (const auto& field : fo4::schema::scalar_fields(signature)) {
      for (const auto& sub : record.subrecords) {
        if (sub.signature != field.scalar.subrecord) continue;
        const auto bytes = fo4::schema::scalar_field_bytes(sub.data, field.scalar);
        if (!bytes.empty()) static_cast<void>(fo4::schema::decode_scalar(bytes, field.scalar));
      }
    }
    if (variant(signature)) {
      const auto view = fo4::forms::decode_placed_reference(plugin, record);
      EXPECT_TRUE(view.has_value()) << record.form_key.to_string();
      if (view.has_value()) { EXPECT_EQ(view->kind, fo4::forms::PlacedReferenceKind::variant); }
      if (preflighted[signature.to_string()]++ < 64) {
        for (const auto& issue : fo4::schema::preflight_authoring_record(plugin, record.form_key)) {
          if (issue.code == "record_placement_invalid") violators.push_back(record.form_key.to_string() + " placement");
        }
      }
    }
    check_authoring(record, violators);
  });
  EXPECT_TRUE(violators.empty()) << violators.front();
  for (const auto& [type, count] : totals) RecordProperty(type, count);
  if (std::string_view(GetParam()) == "Fallout4.esm") {
    EXPECT_GT(totals["ADDN"], 0);
    EXPECT_GT(totals["PGRE"], 0);
    EXPECT_GT(totals["PHZD"], 0);
  }
}

TEST(PlacedXemiReal, XemiReferencesMatchCensusInDlcRobotAndWorkshop03) {
  for (const auto& [name, expected] : {std::pair{"DLCRobot.esm", 482}, std::pair{"DLCworkshop03.esm", 207}}) {
    bool skip = false;
    const auto path = official(name, skip);
    if (skip) { GTEST_SKIP() << "set FO4_DATA_DIR to installed Fallout 4 data"; }
    record_game_build(detect_game_build(path.parent_path()));
    const std::filesystem::path paths[] = {path};
    const auto env = fo4::env::Environment::load(paths);
    int with_emittance = 0;
    for (const auto* ref : env.plugin_records(0)) {
      if (ref->record->header.signature != fo4::Signature::from_string("REFR")) continue;
      if (fo4::env::decode_placed_reference(env, *ref).emittance) ++with_emittance;
    }
    EXPECT_EQ(with_emittance, expected) << name;
  }
}

INSTANTIATE_TEST_SUITE_P(OfficialMasters, PlacedVariantsReal, testing::Values(
    "Fallout4.esm", "DLCRobot.esm", "DLCworkshop01.esm", "DLCCoast.esm",
    "DLCworkshop02.esm", "DLCworkshop03.esm", "DLCNukaWorld.esm"));

// The placed-light and ADDN censuses require the seven shipped masters in load order.
constexpr const char* k_official_masters[] = {
    "Fallout4.esm", "DLCRobot.esm", "DLCworkshop01.esm", "DLCCoast.esm",
    "DLCworkshop02.esm", "DLCworkshop03.esm", "DLCNukaWorld.esm"};
constexpr std::size_t k_official_master_count = 7;

bool load_official_masters(std::vector<std::filesystem::path>& paths) {
  const auto* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) return false;
  for (const auto* name : k_official_masters) {
    const auto path = std::filesystem::path(dir) / name;
    if (!std::filesystem::exists(path)) return false;
    paths.push_back(path);
  }
  return true;
}

bool has_subrecord(const fo4::Record& record, fo4::Signature signature, std::size_t* size = nullptr) {
  for (const auto& sub : record.subrecords) {
    if (sub.signature != signature) continue;
    if (size != nullptr) *size = sub.data.size();
    return true;
  }
  return false;
}

struct PlacedLightCounts {
  const char* plugin;
  int xrds;
  int xlig;
  int xemi;
};

constexpr PlacedLightCounts k_placed_light_counts[] = {
    {"Fallout4.esm", 43120, 42781, 23246},
    {"DLCRobot.esm", 1595, 1585, 482},
    {"DLCworkshop01.esm", 12, 0, 0},
    {"DLCCoast.esm", 7074, 7111, 1307},
    {"DLCworkshop02.esm", 6, 1, 0},
    {"DLCworkshop03.esm", 1077, 1020, 207},
    {"DLCNukaWorld.esm", 7499, 7065, 2921},
};

struct AddOnCounts {
  const char* plugin;
  int total;
  int with_model;
  int with_light;
  int with_both;
  int cap_positive;
};

constexpr AddOnCounts k_add_on_counts[] = {
    {"Fallout4.esm", 255, 185, 119, 50, 11},
    {"DLCRobot.esm", 10, 5, 5, 0, 0},
    {"DLCworkshop01.esm", 29, 3, 26, 0, 3},
    {"DLCCoast.esm", 25, 12, 13, 0, 0},
    {"DLCworkshop02.esm", 16, 9, 7, 0, 2},
    {"DLCworkshop03.esm", 2, 0, 2, 0, 0},
    {"DLCNukaWorld.esm", 32, 16, 17, 1, 2},
};

struct XligKnownAnswer {
  std::uint32_t reference;
  float radius;
  float values[6];
};

constexpr XligKnownAnswer k_xlig_known_answers[] = {
    {0x10E976, 365.46771f, {0.0f, 3.0043764f, 0.0f, 24.983643f, 0.0f, 0.0f}},
    {0x10E977, 504.70557f, {0.0f, -0.7109257f, 0.0f, 2.0f, 0.0f, 0.0f}},
    {0x10E978, 595.02039f, {0.0f, 1.2068334f, 0.0f, 24.983643f, 0.0f, 0.0f}},
    {0x017C4E, 817.30359f, {-39.220001f, -0.25f, 0.0f, 50.0f, 0.0f, 0.0f}},
    {0x017C49, 630.36810f, {-39.219955f, 0.8570247f, 0.0f, 50.0f, 0.0f, 0.0f}},
};

struct AddOnKnownAnswer {
  std::uint32_t addon;
  std::uint32_t node_index;
  bool model;
  bool light;
};

constexpr AddOnKnownAnswer k_add_on_known_answers[] = {
    {0x24A1C7, 283, true, true},
    {0x24A19F, 282, false, true},
    {0x24922A, 248, true, false},
};

fo4::FormKey master_form(const char* plugin, std::uint32_t local) {
  return fo4::FormKey{fo4::PluginKey::from_filename(plugin), fo4::LocalFormId{local}};
}

// Regression: an XRDS/XLIG parsed from the wrong offset drops or shifts records; these are the raw per-plugin counts.
TEST(PlacedLightFieldsReal, XrdsXligAndXemiRawCountsMatchTheCensus) {
  std::vector<std::filesystem::path> paths;
  if (!load_official_masters(paths)) {
    GTEST_SKIP() << "set FO4_DATA_DIR to the seven shipped masters";
  }
  record_game_build(detect_game_build(paths.front().parent_path()));
  const auto env = fo4::env::Environment::load(paths);
  ASSERT_EQ(env.plugins().size(), k_official_master_count);
  const auto refr = fo4::Signature::from_string("REFR");
  const auto xrds = fo4::Signature::from_string("XRDS");
  const auto xlig = fo4::Signature::from_string("XLIG");
  const auto xemi = fo4::Signature::from_string("XEMI");
  int xrds_total = 0;
  int xlig_total = 0;
  int xemi_total = 0;

  for (std::size_t i = 0; i < k_official_master_count; ++i) {
    const auto& expected = k_placed_light_counts[i];
    ASSERT_EQ(env.plugins()[i].key.filename(), std::string_view(expected.plugin));
    int with_xrds = 0;
    int with_xlig = 0;
    int with_xemi = 0;
    int xlig24 = 0;
    int xlig20 = 0;
    int xlig16 = 0;
    for (const auto* ref : env.plugin_records(i)) {
      if (ref->record->header.signature != refr) continue;
      std::size_t xlig_size = 0;
      const bool record_xrds = has_subrecord(*ref->record, xrds);
      const bool record_xlig = has_subrecord(*ref->record, xlig, &xlig_size);
      const bool record_xemi = has_subrecord(*ref->record, xemi);
      with_xrds += record_xrds ? 1 : 0;
      with_xlig += record_xlig ? 1 : 0;
      with_xemi += record_xemi ? 1 : 0;
      if (record_xlig) {
        xlig24 += xlig_size == 24u ? 1 : 0;
        xlig20 += xlig_size == 20u ? 1 : 0;
        xlig16 += xlig_size == 16u ? 1 : 0;
      }
    }
    EXPECT_EQ(with_xrds, expected.xrds) << expected.plugin;
    EXPECT_EQ(with_xlig, expected.xlig) << expected.plugin;
    EXPECT_EQ(with_xemi, expected.xemi) << expected.plugin;
    if (std::string_view(expected.plugin) == "Fallout4.esm") {
      EXPECT_EQ(xlig24, 41678) << expected.plugin;
      EXPECT_EQ(xlig20, 1101) << expected.plugin;
      EXPECT_EQ(xlig16, 2) << expected.plugin;
    } else {
      EXPECT_EQ(xlig24, with_xlig) << expected.plugin;
      EXPECT_EQ(xlig20, 0) << expected.plugin;
      EXPECT_EQ(xlig16, 0) << expected.plugin;
    }
    xrds_total += with_xrds;
    xlig_total += with_xlig;
    xemi_total += with_xemi;
  }
  EXPECT_EQ(xrds_total, 60383);
  EXPECT_EQ(xlig_total, 59563);
  EXPECT_EQ(xemi_total, 28163);
}

// Regression: a 16/20-byte XLIG must not be rejected and must not read absent members; every stored field decodes.
TEST(PlacedLightFieldsReal, EveryPlacedReferenceWithLightFieldsDecodes) {
  std::vector<std::filesystem::path> paths;
  if (!load_official_masters(paths)) {
    GTEST_SKIP() << "set FO4_DATA_DIR to the seven shipped masters";
  }
  record_game_build(detect_game_build(paths.front().parent_path()));
  const auto env = fo4::env::Environment::load(paths);
  ASSERT_EQ(env.plugins().size(), k_official_master_count);
  const auto xrds = fo4::Signature::from_string("XRDS");
  const auto xlig = fo4::Signature::from_string("XLIG");
  const auto xemi = fo4::Signature::from_string("XEMI");
  std::string first_failure;
  std::size_t checked = 0;
  std::size_t decoded_xrds = 0;
  std::size_t decoded_xlig = 0;
  std::size_t decoded_xemi = 0;

  for (std::size_t i = 0; i < k_official_master_count; ++i) {
    int plugin_xemi = 0;
    for (const auto* ref : env.plugin_records(i)) {
      const auto signature = ref->record->header.signature;
      if (!fo4::is_placed_reference_signature(signature)) continue;
      const bool record_xrds = has_subrecord(*ref->record, xrds);
      const bool record_xlig = has_subrecord(*ref->record, xlig);
      const bool record_xemi = has_subrecord(*ref->record, xemi);
      if (!record_xrds && !record_xlig && !record_xemi) continue;
      ++checked;
      try {
        const auto data = fo4::env::decode_placed_reference(env, *ref);
        if (record_xrds) {
          if (data.radius) {
            ++decoded_xrds;
          } else if (first_failure.empty()) {
            first_failure = ref->record->form_key.to_string() + " dropped XRDS";
          }
        }
        if (record_xlig) {
          if (data.light_data) {
            ++decoded_xlig;
          } else if (first_failure.empty()) {
            first_failure = ref->record->form_key.to_string() + " dropped XLIG";
          }
        }
        if (record_xemi) {
          if (data.emittance) {
            ++plugin_xemi;
            ++decoded_xemi;
          } else if (first_failure.empty()) {
            first_failure = ref->record->form_key.to_string() + " dropped XEMI";
          }
        }
      } catch (const fo4::env::WorldDecodeError& error) {
        if (first_failure.empty()) first_failure = error.what();
      }
    }
    EXPECT_EQ(plugin_xemi, k_placed_light_counts[i].xemi) << k_official_masters[i];
  }
  EXPECT_TRUE(first_failure.empty()) << first_failure;
  EXPECT_GT(checked, 0u);
  EXPECT_EQ(decoded_xrds, 60383u);
  EXPECT_EQ(decoded_xlig, 59563u);
  EXPECT_EQ(decoded_xemi, 28163u);
}

// Invariant: the six XLIG floats and XRDS are stored values in that order, not base-record values or converted units.
TEST(PlacedLightFieldsReal, XrdsAndXligKnownAnswersMatchFallout4References) {
  bool skip = false;
  const auto path = official("Fallout4.esm", skip);
  if (skip) GTEST_SKIP() << "set FO4_DATA_DIR to installed Fallout 4 data";
  record_game_build(detect_game_build(path.parent_path()));
  const std::filesystem::path paths[] = {path};
  const auto env = fo4::env::Environment::load(paths);
  for (const auto& known : k_xlig_known_answers) {
    SCOPED_TRACE(known.reference);
    const auto* ref = env.winner(master_form("Fallout4.esm", known.reference));
    ASSERT_NE(ref, nullptr);
    const auto data = fo4::env::decode_placed_reference(env, *ref);
    ASSERT_TRUE(data.radius.has_value());
    ASSERT_TRUE(data.light_data.has_value());
    EXPECT_FLOAT_EQ(*data.radius, known.radius);
    EXPECT_FLOAT_EQ(data.light_data->fov, known.values[0]);
    EXPECT_FLOAT_EQ(data.light_data->fade, known.values[1]);
    EXPECT_FLOAT_EQ(data.light_data->end_distance_cap, known.values[2]);
    EXPECT_FLOAT_EQ(data.light_data->shadow_depth_bias, known.values[3]);
    ASSERT_TRUE(data.light_data->near_clip.has_value());
    EXPECT_FLOAT_EQ(*data.light_data->near_clip, known.values[4]);
    ASSERT_TRUE(data.light_data->volumetric_intensity.has_value());
    EXPECT_FLOAT_EQ(*data.light_data->volumetric_intensity, known.values[5]);
  }
}

// Invariant: each ADDN field must come from its own subrecord so the raw per-plugin census cannot shift.
TEST(AddOnNodeReal, AddOnNodeCountsMatchTheCensus) {
  std::vector<std::filesystem::path> paths;
  if (!load_official_masters(paths)) {
    GTEST_SKIP() << "set FO4_DATA_DIR to the seven shipped masters";
  }
  record_game_build(detect_game_build(paths.front().parent_path()));
  const auto env = fo4::env::Environment::load(paths);
  ASSERT_EQ(env.plugins().size(), k_official_master_count);
  const auto addn = fo4::Signature::from_string("ADDN");
  const auto ligh = fo4::Signature::from_string("LIGH");
  std::set<std::uint32_t> node_indices;
  std::string first_failure;
  int total = 0;
  int with_light = 0;
  int resolved_light = 0;

  for (std::size_t i = 0; i < k_official_master_count; ++i) {
    const auto& expected = k_add_on_counts[i];
    ASSERT_EQ(env.plugins()[i].key.filename(), std::string_view(expected.plugin));
    int plugin_total = 0;
    int plugin_model = 0;
    int plugin_light = 0;
    int plugin_both = 0;
    int plugin_cap_positive = 0;
    for (const auto* ref : env.plugin_records(i)) {
      if (ref->record->header.signature != addn) continue;
      ++plugin_total;
      try {
        const auto data = fo4::env::decode_add_on_node(env, *ref);
        const bool model = data.model.has_value();
        const bool light = data.light.has_value();
        plugin_model += model ? 1 : 0;
        plugin_light += light ? 1 : 0;
        plugin_both += (model && light) ? 1 : 0;
        if (data.master_particle_cap && *data.master_particle_cap > 0) ++plugin_cap_positive;
        if (data.node_index) node_indices.insert(*data.node_index);
        if (light) {
          ++with_light;
          const auto* target = env.winner(*data.light);
          if (target != nullptr && target->record->header.signature == ligh) ++resolved_light;
        }
      } catch (const fo4::env::WorldDecodeError& error) {
        if (first_failure.empty()) first_failure = error.what();
      }
    }
    EXPECT_EQ(plugin_total, expected.total) << expected.plugin;
    EXPECT_EQ(plugin_model, expected.with_model) << expected.plugin;
    EXPECT_EQ(plugin_light, expected.with_light) << expected.plugin;
    EXPECT_EQ(plugin_both, expected.with_both) << expected.plugin;
    EXPECT_EQ(plugin_cap_positive, expected.cap_positive) << expected.plugin;
    total += plugin_total;
  }
  EXPECT_TRUE(first_failure.empty()) << first_failure;
  EXPECT_EQ(total, 369);
  EXPECT_EQ(node_indices.size(), 369u);
  EXPECT_EQ(with_light, 189);
  EXPECT_EQ(resolved_light, 189);
}

// Regression: a missing LNAM must stay a null light and a missing MODL a null model, not an empty string.
TEST(AddOnNodeReal, AddOnNodeKnownAnswersMatchFallout4Records) {
  bool skip = false;
  const auto path = official("Fallout4.esm", skip);
  if (skip) GTEST_SKIP() << "set FO4_DATA_DIR to installed Fallout 4 data";
  record_game_build(detect_game_build(path.parent_path()));
  const std::filesystem::path paths[] = {path};
  const auto env = fo4::env::Environment::load(paths);
  for (const auto& known : k_add_on_known_answers) {
    SCOPED_TRACE(known.addon);
    const auto* ref = env.winner(master_form("Fallout4.esm", known.addon));
    ASSERT_NE(ref, nullptr);
    const auto data = fo4::env::decode_add_on_node(env, *ref);
    ASSERT_TRUE(data.node_index.has_value());
    EXPECT_EQ(*data.node_index, known.node_index);
    EXPECT_EQ(data.model.has_value(), known.model);
    EXPECT_EQ(data.light.has_value(), known.light);
    if (data.model) {
      EXPECT_FALSE(data.model->empty());
    }
    if (data.light) {
      EXPECT_EQ(data.light->owner(), fo4::PluginKey::from_filename("Fallout4.esm"));
    }
  }
}

}  // namespace
