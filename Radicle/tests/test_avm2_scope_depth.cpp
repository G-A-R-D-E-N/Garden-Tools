#include <fo4/avm2/interpreter.hpp>
#include <fo4/avm2/runtime.hpp>

#include "avm2_test_support.hpp"

#include <gtest/gtest.h>

#include <cstdint>

namespace {

namespace abc = fo4::abc;

using fo4::avm2::ExecutionResult;
using fo4::avm2::Interpreter;
using fo4::avm2::InterpreterStatus;
using fo4::avm2::Runtime;

ExecutionResult run_two_scope_pushes(std::uint32_t max_scope_depth) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  code.getglobalscope();
  code.pushscope();
  code.getglobalscope();
  code.pushscope();
  code.returnvoid();
  const std::uint32_t method = script.method("init", code.bytes(), 1);
  abc::AbcFile file = script.finish(method);
  file.bodies[0].max_scope_depth = max_scope_depth;
  Runtime runtime;
  EXPECT_TRUE(runtime.add_file(file).ok());
  EXPECT_TRUE(runtime.link().ok());
  Interpreter interpreter(runtime);
  return interpreter.run_script(0);
}

}  // namespace

TEST(Avm2ScopeDepth, PushscopeRunsWhileTheDepthStaysWithinMaxScopeDepth) {
  const ExecutionResult result = run_two_scope_pushes(2);
  ASSERT_TRUE(result.ok()) << fo4::avm2::interpreter_status_name(result.status) << ": "
                           << result.message;
}

TEST(Avm2ScopeDepth, PushscopePastMaxScopeDepthRaisesAnErrorInsteadOfGrowing) {
  const ExecutionResult result = run_two_scope_pushes(1);
  EXPECT_EQ(result.status, InterpreterStatus::ScopeDepthExceeded);
}
