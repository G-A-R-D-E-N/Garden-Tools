#include <fo4/abc/abc.hpp>
#include <fo4/abc/flow.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "abc_test_support.hpp"

namespace {

using fo4::abc::AbcException;
using fo4::abc::AbcFlowIssue;
using fo4::abc::AbcFlowStatus;
using fo4::abc::AbcMethodFlow;
using fo4::abc::AbcStatus;

fo4::abc::AbcFile fixture_file() {
  fo4::abc::AbcFile file;
  const std::vector<std::uint8_t> bytes = fo4::test::build_abc();
  EXPECT_EQ(fo4::abc::AbcFile::parse(bytes, file), AbcStatus::Ok);
  return file;
}

struct Probe {
  AbcFlowStatus status{AbcFlowStatus::Ok};
  AbcFlowIssue issue;
  AbcMethodFlow flow;
};

/* Requires the caller to override only fields the fixture body does not pin itself. */
Probe run(const std::vector<std::uint8_t>& code, std::uint32_t max_stack = 4,
          std::uint32_t init_scope_depth = 0, std::uint32_t max_scope_depth = 1,
          const std::vector<AbcException>& exceptions = {}) {
  fo4::abc::AbcFile file = fixture_file();
  file.bodies[0].code = code;
  file.bodies[0].max_stack = max_stack;
  file.bodies[0].init_scope_depth = init_scope_depth;
  file.bodies[0].max_scope_depth = max_scope_depth;
  file.bodies[0].exceptions = exceptions;
  Probe probe;
  probe.status = fo4::abc::verify_method_flow(file, 0, probe.flow, probe.issue);
  return probe;
}

}  // namespace

TEST(AbcVerify, VerifiesABalancedBodyAndReportsDepths) {
  const auto probe = run({0x24, 0x01, 0x24, 0x02, 0xA0, 0x29, 0x47});
  ASSERT_EQ(probe.status, AbcFlowStatus::Ok) << probe.issue.message;
  EXPECT_TRUE(probe.flow.verified);
  EXPECT_EQ(probe.flow.body, 0u);
  EXPECT_EQ(probe.flow.method, 0u);
  ASSERT_EQ(probe.flow.graph.blocks.size(), 1u);
  EXPECT_EQ(probe.flow.observed_max_stack, 2u);
  EXPECT_EQ(probe.flow.observed_max_scope_depth, 0u);
  ASSERT_EQ(probe.flow.entry_stack_depth.size(), 1u);
  EXPECT_EQ(probe.flow.entry_stack_depth[0], 0u);
}

TEST(AbcVerify, RejectsABranchIntoTheMiddleOfAnInstruction) {
  const auto probe = run({0x10, 0xFD, 0xFF, 0xFF, 0x47});
  EXPECT_EQ(probe.status, AbcFlowStatus::TargetInsideInstruction);
  EXPECT_EQ(probe.issue.body, 0u);
  EXPECT_EQ(probe.issue.method, 0u);
  EXPECT_EQ(probe.issue.offset, 0u);
  EXPECT_FALSE(probe.flow.verified);
}

TEST(AbcVerify, RejectsAJumpOutOfTheMethod) {
  const auto probe = run({0x10, 0x01, 0x00, 0x00, 0x47});
  EXPECT_EQ(probe.status, AbcFlowStatus::TargetOutsideCode);
  EXPECT_EQ(probe.issue.offset, 0u);
}

TEST(AbcVerify, RejectsOperandStackUnderflow) {
  const auto probe = run({0x29, 0x47});
  EXPECT_EQ(probe.status, AbcFlowStatus::StackUnderflow);
  EXPECT_EQ(probe.issue.offset, 0u);
  EXPECT_NE(probe.issue.message.find("pop needs 1"), std::string::npos) << probe.issue.message;
}

TEST(AbcVerify, RejectsADepthPastTheDeclaredMaxStack) {
  const auto probe = run({0x24, 0x01, 0x24, 0x02, 0x24, 0x03, 0x29, 0x29, 0x29, 0x47}, 2);
  EXPECT_EQ(probe.status, AbcFlowStatus::StackLimitExceeded);
  EXPECT_EQ(probe.issue.offset, 4u);
}

TEST(AbcVerify, RejectsAnExceptionRangePastTheCode) {
  // Constraint: the throw at offset 0 is what puts the range in front of the check.
  const std::vector<AbcException> exceptions{AbcException{0, 99, 0, 0, 0}};
  const auto probe = run({0x03, 0x47}, 4, 0, 1, exceptions);
  EXPECT_EQ(probe.status, AbcFlowStatus::ExceptionRangeInvalid);
  EXPECT_EQ(probe.issue.offset, 0u);
}

TEST(AbcVerify, RejectsAHandlerTargetInsideItsProtectedRange) {
  // Constraint: the throw at offset 0 falls inside the range, so the target rule is reached.
  const std::vector<AbcException> exceptions{AbcException{0, 3, 2, 0, 0}};
  const auto probe = run({0x03, 0x47, 0x47, 0x47}, 4, 0, 1, exceptions);
  EXPECT_EQ(probe.status, AbcFlowStatus::ExceptionTargetInvalid);
  EXPECT_EQ(probe.issue.offset, 2u);
}

TEST(AbcVerify, EntersAHandlerAtDepthOne) {
  const std::vector<AbcException> exceptions{AbcException{0, 2, 4, 0, 0}};
  const auto probe = run({0x60, 0x01, 0x29, 0x47, 0x29, 0x47}, 4, 0, 1, exceptions);
  ASSERT_EQ(probe.status, AbcFlowStatus::Ok) << probe.issue.message;
  ASSERT_EQ(probe.flow.graph.blocks.size(), 2u);
  const std::uint32_t handler = probe.flow.graph.block_index_at(4);
  ASSERT_EQ(handler, 1u);
  EXPECT_EQ(probe.flow.entry_stack_depth[handler], 1u);
  EXPECT_EQ(probe.flow.entry_scope_depth[handler], 0u);
}

TEST(AbcVerify, RejectsAScopePopBelowTheInitialDepth) {
  const auto probe = run({0x1D, 0x47});
  EXPECT_EQ(probe.status, AbcFlowStatus::ScopeUnderflow);
  EXPECT_EQ(probe.issue.offset, 0u);
}

TEST(AbcVerify, RejectsAScopeDepthPastTheDeclaredMaximum) {
  const auto probe = run({0x20, 0x30, 0x20, 0x30, 0x1D, 0x1D, 0x47}, 4, 0, 1);
  EXPECT_EQ(probe.status, AbcFlowStatus::ScopeLimitExceeded);
  EXPECT_EQ(probe.issue.offset, 3u);
}

TEST(AbcVerify, RejectsATruncatedBodyAndAHostileSwitchCount) {
  const auto truncated = run({0x24});
  EXPECT_EQ(truncated.status, AbcFlowStatus::DecodeFailed);
  EXPECT_EQ(truncated.issue.decode, AbcStatus::Truncated);

  const auto huge_switch = run({0x1B, 0x00, 0x00, 0x00, 0x7F});
  EXPECT_EQ(huge_switch.status, AbcFlowStatus::DecodeFailed);
  EXPECT_EQ(huge_switch.issue.decode, AbcStatus::CountOutOfRange);
}

TEST(AbcVerify, TerminatesOnASelfJump) {
  const auto probe = run({0x10, 0xFC, 0xFF, 0xFF});
  ASSERT_EQ(probe.status, AbcFlowStatus::Ok) << probe.issue.message;
  ASSERT_EQ(probe.flow.graph.blocks.size(), 1u);
  ASSERT_EQ(probe.flow.graph.blocks[0].successors.size(), 1u);
  EXPECT_EQ(probe.flow.graph.blocks[0].successors[0].block, 0u);
}

TEST(AbcVerify, VerifiesEveryBodyOfTheFixtureFile) {
  const fo4::abc::AbcFile file = fixture_file();
  std::vector<AbcMethodFlow> flows;
  std::vector<AbcFlowIssue> issues;
  EXPECT_EQ(fo4::abc::verify_all_method_flows(file, flows, issues), AbcFlowStatus::Ok);
  EXPECT_TRUE(issues.empty());
  ASSERT_EQ(flows.size(), file.bodies.size());
  for (const AbcMethodFlow& flow : flows) {
    EXPECT_TRUE(flow.verified) << "body " << flow.body;
  }
}

TEST(AbcVerify, SweepsHostileBodyMutationsWithoutCrashing) {
  // Constraint: a mutation may only turn a verified body into a refusal, never into a crash or a hang.
  const fo4::abc::AbcFile pristine = fixture_file();
  const std::vector<std::vector<std::uint8_t>> seeds{
      {0x60, 0x01, 0x29, 0x47},
      {0x24, 0x01, 0x24, 0x02, 0xA0, 0x29, 0x47},
      {0x10, 0x00, 0x00, 0x00, 0x47},
      {0x1B, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x47},
  };
  std::uint64_t state = 0x9E3779B97F4A7C15ull;
  const auto next = [&state]() {
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
  };
  for (const auto& seed : seeds) {
    for (int mutation = 0; mutation < 64; ++mutation) {
      fo4::abc::AbcFile file = pristine;
      file.bodies[0].code = seed;
      file.bodies[0].exceptions.clear();
      const std::size_t touches = 1 + static_cast<std::size_t>(mutation % 3);
      for (std::size_t touch = 0; touch < touches; ++touch) {
        const std::size_t at = static_cast<std::size_t>(next() % seed.size());
        file.bodies[0].code[at] =
            static_cast<std::uint8_t>(file.bodies[0].code[at] + (next() & 0x1Fu) + 1u);
      }
      fo4::abc::AbcMethodFlow flow;
      fo4::abc::AbcFlowIssue issue;
      const fo4::abc::AbcFlowStatus status = fo4::abc::verify_method_flow(file, 0, flow, issue);
      EXPECT_EQ(flow.body, 0u);
      EXPECT_EQ(flow.method, file.bodies[0].method);
      EXPECT_TRUE(issue.offset == fo4::abc::kNoFlowIndex ||
                  issue.offset <= file.bodies[0].code.size());
      if (status == fo4::abc::AbcFlowStatus::Ok) {
        EXPECT_TRUE(flow.verified);
        EXPECT_TRUE(issue.ok());
      } else {
        EXPECT_FALSE(flow.verified);
        EXPECT_FALSE(issue.message.empty());
      }
    }
  }

  for (const std::uint32_t from : {0u, 1u, 7u}) {
    for (const std::uint32_t to : {0u, 2u, 99u}) {
      for (const std::uint32_t target : {0u, 3u, 99u}) {
        fo4::abc::AbcFile file = pristine;
        file.bodies[2].exceptions[0] = AbcException{from, to, target, 0, 0};
        fo4::abc::AbcMethodFlow flow;
        fo4::abc::AbcFlowIssue issue;
        const fo4::abc::AbcFlowStatus status = fo4::abc::verify_method_flow(file, 2, flow, issue);
        EXPECT_EQ(flow.body, 2u);
        if (status == fo4::abc::AbcFlowStatus::Ok) {
          EXPECT_TRUE(flow.verified);
          EXPECT_TRUE(issue.ok());
        } else {
          EXPECT_FALSE(flow.verified);
          EXPECT_FALSE(issue.message.empty());
        }
      }
    }
  }
}
TEST(AbcVerify, ReportsTheFailingBodyIndexAndKeepsFlowsParallel) {

  fo4::abc::AbcFile file = fixture_file();
  file.bodies[1].code = {0x29, 0x47};
  std::vector<AbcMethodFlow> flows;
  std::vector<AbcFlowIssue> issues;
  EXPECT_EQ(fo4::abc::verify_all_method_flows(file, flows, issues), AbcFlowStatus::StackUnderflow);
  ASSERT_EQ(issues.size(), 1u);
  EXPECT_EQ(issues[0].body, 1u);
  EXPECT_EQ(issues[0].method, file.bodies[1].method);
  ASSERT_EQ(flows.size(), file.bodies.size());
  EXPECT_TRUE(flows[0].verified);
  EXPECT_FALSE(flows[1].verified);
  EXPECT_TRUE(flows[2].verified);
}
