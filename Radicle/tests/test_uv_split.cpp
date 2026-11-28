#include "fo4/nif/uv.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <set>
#include <vector>

namespace {

namespace uv = fo4::nif::uv;
using Ids = std::vector<std::uint32_t>;
using Pairs = std::vector<uv::IndexPair>;

const Ids k_strip_tris{0, 1, 2, 0, 2, 3, 1, 4, 5, 1, 5, 2};
const Ids k_fan_tris{0, 1, 2, 0, 2, 3, 0, 3, 4, 0, 4, 1};

Ids all(std::size_t n) {
  Ids out(n);
  for (std::size_t i = 0; i < n; ++i) out[i] = static_cast<std::uint32_t>(i);
  return out;
}

Pairs sorted(Pairs pairs) {
  std::sort(pairs.begin(), pairs.end(), [](const uv::IndexPair& a, const uv::IndexPair& b) {
    return a.first != b.first ? a.first < b.first : a.second < b.second;
  });
  return pairs;
}

}  // namespace

TEST(UvSplitSeams, MarksEveryMeshEdgeWhoseTwoVerticesAreBothSelected) {
  EXPECT_EQ(uv::seams_in_selection(k_strip_tris, Ids{1, 2}, Ids{}), (Pairs{{1, 2}}));
  EXPECT_EQ(sorted(uv::seams_in_selection(k_strip_tris, Ids{0, 1, 2, 3}, Ids{})), (Pairs{{0, 1}, {0, 2}, {0, 3}, {1, 2}, {2, 3}}));
  EXPECT_TRUE(uv::seams_in_selection(k_strip_tris, Ids{}, Ids{}).empty());
}

TEST(UvSplitSeams, SkipsHiddenFacesWhenMarking) {
  EXPECT_TRUE(uv::seams_in_selection(k_strip_tris, Ids{1, 2}, Ids{0, 3}).empty());
}

TEST(UvSplitPlan, LeavesABowtieVertexAloneWhenNoEdgeIsMarkedAsASeam) {
  const Ids bowtie{0, 1, 2, 0, 3, 4};
  const auto plan = uv::split_plan(5, bowtie, Pairs{});
  EXPECT_TRUE(plan.sources.empty());
  EXPECT_EQ(plan.tris, bowtie);
}

TEST(UvSplitPlan, SplitsOnlyTheVerticesOnTheMarkedSeamAndLeavesASeparateBowtieVertexAlone) {
  const Ids mixed{0, 1, 2, 0, 3, 4, 5, 6, 7, 6, 5, 8};
  const auto plan = uv::split_plan(9, mixed, Pairs{{5, 6}});
  EXPECT_EQ(plan.sources, (Ids{5, 6}));
  EXPECT_EQ(plan.tris, (Ids{0, 1, 2, 0, 3, 4, 5, 6, 7, 10, 9, 8}));
}

TEST(UvSplitPlan, LeavesTheTopologyExactlyAsItWasWhenNoEdgeIsASeam) {
  const auto plan = uv::split_plan(6, k_strip_tris, Pairs{});
  EXPECT_TRUE(plan.sources.empty());
  EXPECT_EQ(plan.tris, k_strip_tris);
}

TEST(UvSplitPlan, LeavesAlreadySeparatedIslandsAloneBecauseTheirVerticesAreSingleUse) {
  const Ids four{0, 1, 2, 3, 4, 5, 6, 7, 8};
  const auto plan = uv::split_plan(9, four, Pairs{});
  EXPECT_TRUE(plan.sources.empty());
  EXPECT_EQ(plan.tris, four);
}

TEST(UvSplitPlan, DuplicatesOnlyTheSeamVerticesAndKeepsTheNonSeamSideOnTheOriginalIndex) {
  const auto plan = uv::split_plan(6, k_strip_tris, Pairs{{1, 2}});
  EXPECT_EQ(plan.sources, (Ids{1, 2}));
  EXPECT_EQ(plan.tris, (Ids{0, 1, 2, 0, 2, 3, 6, 4, 5, 6, 5, 7}));
}

TEST(UvSplitPlan, GivesEveryTriangleItsOwnVertexCopyWhenAllEdgesAreSeams) {
  const auto seams = uv::seams_in_selection(k_fan_tris, all(5), Ids{});
  const auto plan = uv::split_plan(5, k_fan_tris, seams);
  EXPECT_EQ(plan.sources.size(), 7u);
  EXPECT_EQ(plan.tris.size(), 12u);
  EXPECT_EQ(std::set<std::uint32_t>(plan.tris.begin(), plan.tris.end()).size(), 12u);
}

TEST(UvSplitPlan, AppendsOneCreatedVertexPerSourceEntryInVertexOrder) {
  const auto plan = uv::split_plan(6, k_strip_tris, Pairs{{1, 2}});
  EXPECT_EQ(plan.sources, (Ids{1, 2}));
  EXPECT_EQ(std::count_if(plan.tris.begin(), plan.tris.end(), [](std::uint32_t i) { return i >= 6; }), 3);
}

TEST(UvSplitPlan, NeverWritesToItsInputsAndIsDeterministic) {
  const Ids tris = k_strip_tris;
  const Pairs seams{{1, 2}};
  const auto first = uv::split_plan(6, tris, seams);
  const auto second = uv::split_plan(6, tris, seams);
  EXPECT_EQ(first.sources, second.sources);
  EXPECT_EQ(first.tris, second.tris);
  EXPECT_EQ(tris, k_strip_tris);
  EXPECT_EQ(seams, (Pairs{{1, 2}}));
}

TEST(UvSplitPlan, AcceptsASeamGivenInEitherEndOrderAndAVertexIndexPastSixtyFiveThousand) {
  const Ids tris{0, 1, 2, 0, 2, 70000};
  const auto plan = uv::split_plan(70001, tris, Pairs{{2, 0}});
  EXPECT_EQ(plan.sources, (Ids{0, 2}));
  EXPECT_EQ(plan.tris, (Ids{0, 1, 2, 70001, 70002, 70000}));
}
