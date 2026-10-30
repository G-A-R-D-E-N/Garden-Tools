#include "fo4/formid.hpp"
#include "fo4/plugin/container.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/schema/condition_scopes.hpp"
#include "fo4/signature.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using fo4::Signature;
using fo4::schema::ConditionScopeDescriptor;
using fo4::schema::ScopedConditionStatus;

struct Span {
  std::size_t begin{0};
  std::size_t end{0};
};

struct Tally {
  std::size_t packs{0};
  std::size_t quests{0};
  std::size_t branches{0};
  std::size_t targets{0};
  std::size_t conditions{0};
  std::size_t read_failures{0};
  std::size_t citc_mismatches{0};
  std::size_t writeback_changes{0};
  std::size_t coverage_offenders{0};
  std::size_t sampled{0};
  std::string samples;

  void sample(const fo4::FormKey& key, std::size_t subrecord, const std::string& what) {
    if (sampled++ >= 8) return;
    samples += "  " + key.to_string() + " subrecord " + std::to_string(subrecord) + ": " + what + "\n";
  }
};

bool is_one_of(Signature signature, std::initializer_list<std::string_view> names) {
  return std::any_of(names.begin(), names.end(), [&](std::string_view name) {
    return signature == Signature::from_string(name);
  });
}

// source:xEdit Core/wbDefinitionsFO4.pas:10604-10634 @93cc0bc5 (PACK Procedure Tree > Branch members)
bool is_branch_member(Signature signature) {
  return is_one_of(signature, {"CITC", "CTDA", "CIS1", "CIS2", "PRCB", "PNAM", "FNAM", "PKC2", "PFO2", "PFOR"});
}

// source:xEdit Core/wbDefinitionsFO4.pas:10779-10793 @93cc0bc5 (QUST Target: QSTA, then conditions)
bool is_target_member(Signature signature) {
  return is_one_of(signature, {"CITC", "CTDA", "CIS1", "CIS2"});
}

std::size_t first_index(const fo4::Record& record, std::string_view name) {
  const auto signature = Signature::from_string(name);
  const auto& subs = record.subrecords;
  for (std::size_t i = 0; i < subs.size(); ++i) {
    if (subs[i].signature == signature) return i;
  }
  return subs.size();
}

std::vector<Span> scope_spans(const fo4::Record& record, bool pack) {
  const auto& subs = record.subrecords;
  const auto start = Signature::from_string(pack ? "ANAM" : "QSTA");
  const auto citc = Signature::from_string("CITC");
  const auto member = pack ? is_branch_member : is_target_member;
  std::vector<Span> spans;
  for (std::size_t i = 0; i < subs.size(); ++i) {
    if (subs[i].signature != start) continue;
    if (pack && (i + 1 >= subs.size() || subs[i + 1].signature != citc)) continue;
    std::size_t end = i + 1;
    while (end < subs.size() && member(subs[end].signature)) ++end;
    spans.push_back(Span{i, end});
  }
  return spans;
}

std::size_t count_ctda(const std::vector<fo4::Subrecord>& subs, Span span) {
  const auto ctda = Signature::from_string("CTDA");
  std::size_t count = 0;
  for (std::size_t i = span.begin; i < span.end; ++i) {
    if (subs[i].signature == ctda) ++count;
  }
  return count;
}

bool same_subrecords(const std::vector<fo4::Subrecord>& a, const std::vector<fo4::Subrecord>& b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (a[i].signature != b[i].signature || a[i].extended_size != b[i].extended_size ||
        a[i].data != b[i].data) {
      return false;
    }
  }
  return true;
}

fo4::Plugin single_record_plugin(const fo4::Record& record) {
  fo4::Plugin plugin;
  plugin.groups.emplace_back();
  plugin.groups.back().children.emplace_back(record);
  return plugin;
}

void check_record(const fo4::Record& record, const ConditionScopeDescriptor& descriptor, bool pack,
                  Tally& tally) {
  const auto& key = record.form_key;
  const auto& subs = record.subrecords;
  const auto spans = scope_spans(record, pack);
  const std::size_t tree_start = spans.empty() ? subs.size() : spans.front().begin;
  const std::size_t objectives_begin = first_index(record, "QOBJ");
  const std::size_t objectives_end = first_index(record, "ANAM");
  if (pack) {
    ++tally.packs;
  } else {
    ++tally.quests;
  }
  const auto snapshot = subs;
  const auto ctda = Signature::from_string("CTDA");

  for (std::size_t c = 0; c < subs.size(); ++c) {
    if (subs[c].signature != ctda) continue;
    const auto owners = std::count_if(spans.begin(), spans.end(), [&](const Span& span) {
      return span.begin <= c && c < span.end;
    });
    const bool exempt = pack ? (owners == 0 && c < tree_start)
                             : (c < objectives_begin || c >= objectives_end);
    if (owners == 1 || exempt) continue;
    ++tally.coverage_offenders;
    tally.sample(key, c, "CTDA lies in no single scope");
  }

  std::size_t index = 0;
  for (const auto& span : spans) {
    if (pack) {
      ++tally.branches;
    } else {
      ++tally.targets;
    }
    const auto ctdas = count_ctda(subs, span);
    if (span.begin + 1 < span.end && subs[span.begin + 1].signature == Signature::from_string("CITC")) {
      const auto& citc = subs[span.begin + 1].data;
      std::uint32_t declared = 0;
      if (citc.size() == sizeof(declared)) std::memcpy(&declared, citc.data(), sizeof(declared));
      if (citc.size() != sizeof(declared) || declared != ctdas) {
        ++tally.citc_mismatches;
        tally.sample(key, span.begin + 1, "CITC differs from CTDA count " + std::to_string(ctdas));
      }
    }
    const auto read = fo4::schema::read_scoped_conditions(record, descriptor, index);
    if (!read.ok || read.conditions.size() != ctdas) {
      ++tally.read_failures;
      tally.sample(key, span.begin, "scope " + std::to_string(index) + " read status " +
                                        std::to_string(static_cast<int>(read.error)) + ", " +
                                        std::to_string(read.conditions.size()) + " of " +
                                        std::to_string(ctdas) + " CTDA");
    }
    tally.conditions += read.conditions.size();
    ++index;
  }

  const auto past = fo4::schema::read_scoped_conditions(record, descriptor, spans.size());
  if (past.ok || past.error != ScopedConditionStatus::container_index_out_of_range) {
    ++tally.read_failures;
    tally.sample(key, 0, "index past the last scope was not rejected");
  }

  auto writable = single_record_plugin(record);
  const auto* live = fo4::find_record(writable, key);
  ASSERT_NE(live, nullptr);
  for (std::size_t k = 0; k < spans.size(); ++k) {
    const auto read = fo4::schema::read_scoped_conditions(*live, descriptor, k);
    if (!read.ok) continue;
    const auto status = fo4::schema::set_record_scoped_conditions(writable, key, descriptor, k, read.conditions);
    if (status != ScopedConditionStatus::updated && status != ScopedConditionStatus::unchanged) {
      ++tally.writeback_changes;
      tally.sample(key, spans[k].begin, "write-back of scope " + std::to_string(k) + " returned status " +
                                            std::to_string(static_cast<int>(status)));
    }
  }

  const auto* after = fo4::find_record(writable, key);
  ASSERT_NE(after, nullptr);
  if (!same_subrecords(snapshot, after->subrecords)) {
    ++tally.writeback_changes;
    tally.sample(key, 0, "subrecords changed after write-back");
  }
}

class ConditionScopesReal : public testing::TestWithParam<const char*> {};

TEST_P(ConditionScopesReal, BranchAndTargetScopesMatchOfficialLayoutAndWriteBackIsIdentity) {
  const auto* directory = std::getenv("FO4_DATA_DIR");
  if (!directory) {
    GTEST_SKIP() << "set FO4_DATA_DIR to run official-master coverage";
  }
  record_game_build(detect_game_build(directory));
  const auto path = std::filesystem::path(directory) / GetParam();
  ASSERT_TRUE(std::filesystem::is_regular_file(path));
  auto plugin = fo4::read_plugin_file(path);
  const auto branch = fo4::schema::scoped_condition_path(Signature::from_string("PACK"), "Branches[0].Conditions");
  const auto target = fo4::schema::scoped_condition_path(Signature::from_string("QUST"), "Targets[0].Conditions");
  ASSERT_TRUE(branch.has_value());
  ASSERT_TRUE(target.has_value());

  std::vector<const fo4::Record*> packs;
  std::vector<const fo4::Record*> quests;
  for (const auto* record : fo4::collect_records(plugin)) {
    if (record->header.signature == Signature::from_string("PACK")) packs.push_back(record);
    if (record->header.signature == Signature::from_string("QUST")) quests.push_back(record);
  }

  Tally tally;
  for (const auto* record : packs) check_record(*record, branch->descriptor, true, tally);
  for (const auto* record : quests) check_record(*record, target->descriptor, false, tally);

  std::cout << GetParam() << " PACK=" << tally.packs << " branches=" << tally.branches
            << " QUST=" << tally.quests << " targets=" << tally.targets
            << " conditions=" << tally.conditions << " read_failures=" << tally.read_failures
            << " citc_mismatches=" << tally.citc_mismatches
            << " writeback_changes=" << tally.writeback_changes
            << " coverage_offenders=" << tally.coverage_offenders << '\n';
  RecordProperty("PackCount", tally.packs);
  RecordProperty("BranchCount", tally.branches);
  RecordProperty("QuestCount", tally.quests);
  RecordProperty("TargetCount", tally.targets);
  RecordProperty("ConditionCount", tally.conditions);
  RecordProperty("ReadFailures", tally.read_failures);
  RecordProperty("CitcMismatches", tally.citc_mismatches);
  RecordProperty("WritebackChanges", tally.writeback_changes);
  RecordProperty("CoverageOffenders", tally.coverage_offenders);

  EXPECT_EQ(tally.read_failures, 0u) << tally.samples;
  EXPECT_EQ(tally.citc_mismatches, 0u) << tally.samples;
  EXPECT_EQ(tally.writeback_changes, 0u) << tally.samples;
  EXPECT_EQ(tally.coverage_offenders, 0u) << tally.samples;
  if (std::string_view(GetParam()) == "Fallout4.esm") {
    EXPECT_GT(tally.branches, 0u);
    EXPECT_GT(tally.targets, 0u);
  }
}

INSTANTIATE_TEST_SUITE_P(OfficialMasters, ConditionScopesReal, testing::Values(
    "Fallout4.esm", "DLCRobot.esm", "DLCworkshop01.esm", "DLCCoast.esm",
    "DLCworkshop02.esm", "DLCworkshop03.esm", "DLCNukaWorld.esm"));

}  // namespace
