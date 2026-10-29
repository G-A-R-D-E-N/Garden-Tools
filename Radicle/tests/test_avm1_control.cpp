#include <fo4/avm1/avm1.hpp>
#include <fo4/avm1/interpreter.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;
using fo4::avm1::Avm1DataKind;
using fo4::avm1::Avm1RunStatus;

Bytes cat(std::initializer_list<Bytes> parts) {
  Bytes out;
  for (const Bytes& part : parts) {
    out.insert(out.end(), part.begin(), part.end());
  }
  return out;
}

void append(Bytes& out, const Bytes& part) { out.insert(out.end(), part.begin(), part.end()); }

Bytes rec(std::uint8_t code, const Bytes& payload) {
  Bytes out{code};
  if (code >= 0x80) {
    out.push_back(static_cast<std::uint8_t>(payload.size() & 0xFFu));
    out.push_back(static_cast<std::uint8_t>(payload.size() >> 8));
  }
  append(out, payload);
  return out;
}

Bytes le16(std::size_t value) {
  return Bytes{static_cast<std::uint8_t>(value & 0xFFu),
               static_cast<std::uint8_t>((value >> 8) & 0xFFu)};
}

Bytes str(std::string_view text) {
  Bytes out(text.begin(), text.end());
  out.push_back(0);
  return out;
}

Bytes text_value(std::string_view text) {
  Bytes out{0x00};
  append(out, str(text));
  return out;
}

Bytes int_value(std::uint32_t value) {
  Bytes out{0x07};
  for (unsigned shift = 0; shift < 32; shift += 8) {
    out.push_back(static_cast<std::uint8_t>(value >> shift));
  }
  return out;
}

Bytes register_value(std::uint8_t index) { return Bytes{0x04, index}; }

Bytes undefined_value() { return Bytes{0x03}; }

Bytes push(std::initializer_list<Bytes> values) {
  Bytes payload;
  for (const Bytes& value : values) {
    append(payload, value);
  }
  return rec(0x96, payload);
}

Bytes define_v1(std::string_view name, const Bytes& body) {
  return cat({rec(0x9B, cat({str(name), le16(0), le16(body.size())})), body});
}

Bytes define_v2(std::string_view name, std::uint8_t registers, std::uint16_t flags,
                const Bytes& body) {
  return cat({rec(0x8E, cat({str(name), le16(0), Bytes{registers}, le16(flags),
                              le16(body.size())})),
              body});
}

Bytes try_record(std::uint8_t flags, const Bytes& tail, std::size_t try_size,
                 std::size_t catch_size, std::size_t finally_size) {
  return rec(0x8F, cat({Bytes{flags}, le16(try_size), le16(catch_size), le16(finally_size), tail}));
}

Bytes with_record(const Bytes& body) { return rec(0x94, le16(body.size())); }

fo4::avm1::Avm1Stream decode(const Bytes& input) {
  std::vector<std::byte> bytes;
  for (const std::uint8_t value : input) {
    bytes.push_back(static_cast<std::byte>(value));
  }
  return fo4::avm1::decode_actions(bytes);
}

fo4::avm1::Avm1Result execute_program(const Bytes& input, std::size_t max_steps = 1000000) {
  const auto stream = decode(input);
  EXPECT_TRUE(stream.ok()) << stream.message;
  return fo4::avm1::execute_actions(stream, max_steps);
}

Bytes instance_prelude() {
  return cat({
      define_v1("A", Bytes{}),
      define_v1("B", Bytes{}),
      push({text_value("B")}), rec(0x1C, {}), push({text_value("A")}), rec(0x1C, {}), rec(0x69, {}),
      push({text_value("holder"), text_value("B"), text_value("B")}), rec(0x1C, {}),
      push({text_value("A"), text_value("A")}), rec(0x1C, {}), push({int_value(2)}), rec(0x43, {}),
      rec(0x1D, {}),
      push({text_value("i"), int_value(0), text_value("holder")}), rec(0x1C, {}),
      push({text_value("B")}), rec(0x53, {}), rec(0x1D, {}),
  });
}

Bytes nested(bool is_with, std::size_t levels) {
  Bytes body = cat({push({int_value(7)}), rec(0x26, {})});
  for (std::size_t level = 0; level < levels; ++level) {
    body = is_with ? cat({push({text_value("obj")}), rec(0x1C, {}), with_record(body), body})
                   : cat({try_record(2, str(""), 0, 0, body.size()), body});
  }
  return cat({push({text_value("obj"), int_value(0)}), rec(0x43, {}), rec(0x1D, {}), body, Bytes{0x00}});
}

// Invariant: a Throw inside a Try reaches the catch name, the catch body runs once, and the run continues after the Try; a Try that ignored the Throw would trace nothing from the catch.
TEST(Avm1Control, ThrowIsCaughtByTheCatchNameAndTheRunContinues) {
  const Bytes try_body = cat({push({int_value(5)}), rec(0x2A, {})});
  const Bytes catch_body = cat({push({text_value("e")}), rec(0x1C, {}), rec(0x26, {})});
  const Bytes input = cat({
      try_record(1, str("e"), try_body.size(), catch_body.size(), 0), try_body, catch_body,
      push({text_value("after")}), rec(0x26, {}), Bytes{0x00},
  });
  const auto result = execute_program(input);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_EQ(result.trace, (std::vector<std::string>{"5", "after"}));
  EXPECT_EQ(result.steps, 9u);
  ASSERT_EQ(result.variables.count("e"), 1u);
  EXPECT_EQ(result.variables.at("e").kind, Avm1DataKind::Number);
  EXPECT_EQ(result.variables.at("e").number, 5.0);
}

// Invariant: a finally body runs after a normal Try and after a caught Throw, in source order; a finally that skipped either path would drop its trace.
TEST(Avm1Control, FinallyRunsOnNormalExitAndAfterACaughtThrow) {
  const Bytes normal = cat({
      try_record(2, str(""), 7, 0, 7), push({text_value("t")}), rec(0x26, {}),
      push({text_value("f")}), rec(0x26, {}), push({text_value("a")}), rec(0x26, {}), Bytes{0x00},
  });
  const Bytes caught = cat({
      try_record(3, str("e"), 9, 7, 7), push({int_value(1)}), rec(0x2A, {}),
      push({text_value("c")}), rec(0x26, {}), push({text_value("f")}), rec(0x26, {}),
      push({text_value("a")}), rec(0x26, {}), Bytes{0x00},
  });
  const auto normal_result = execute_program(normal);
  ASSERT_TRUE(normal_result.ok()) << normal_result.message;
  EXPECT_EQ(normal_result.trace, (std::vector<std::string>{"t", "f", "a"}));
  const auto caught_result = execute_program(caught);
  ASSERT_TRUE(caught_result.ok()) << caught_result.message;
  EXPECT_EQ(caught_result.trace, (std::vector<std::string>{"c", "f", "a"}));
}

// Invariant: an uncaught Throw ends the run at the Throw's own offset and traces nothing after it; a Throw that fell through would trace the 'x' that follows.
TEST(Avm1Control, UncaughtThrowEndsTheRunAtTheThrowAndTracesNothingAfterIt) {
  const Bytes input = cat({
      push({int_value(7)}), rec(0x2A, {}), push({text_value("x")}), rec(0x26, {}), Bytes{0x00},
  });
  const auto result = execute_program(input);
  EXPECT_EQ(result.status, Avm1RunStatus::Thrown);
  EXPECT_EQ(result.opcode, 0x2A);
  EXPECT_EQ(result.offset, 8u);
  EXPECT_EQ(result.steps, 2u);
  EXPECT_NE(result.message.find("not caught"), std::string::npos) << result.message;
  EXPECT_TRUE(result.trace.empty());
}

// Invariant: a Throw in a called function unwinds the call into the caller's Try, so the callee's Trace never runs and the catch binds the value; a call that swallowed the Throw would trace nothing.
TEST(Avm1Control, ThrowInACalleeIsCaughtByTheCallersTry) {
  const Bytes callee = cat({push({int_value(9)}), rec(0x2A, {})});
  const Bytes try_body =
      cat({push({int_value(0), text_value("f")}), rec(0x3D, {}), rec(0x26, {})});
  const Bytes catch_body = cat({push({text_value("e")}), rec(0x1C, {}), rec(0x26, {})});
  const Bytes input = cat({
      define_v1("f", callee), try_record(1, str("e"), try_body.size(), catch_body.size(), 0),
      try_body, catch_body, Bytes{0x00},
  });
  const auto result = execute_program(input);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_EQ(result.trace, (std::vector<std::string>{"9"}));
  EXPECT_EQ(result.steps, 10u);
}

// Invariant: a finally runs before an uncaught exception leaves its Try, and the exception keeps going; a finally that consumed the Throw would let the Trace after the Try run.
TEST(Avm1Control, FinallyRunsBeforeAnUncaughtThrowKeepsGoing) {
  const Bytes callee = cat({push({int_value(9)}), rec(0x2A, {})});
  const Bytes try_body = cat({push({int_value(0), text_value("f")}), rec(0x3D, {})});
  const Bytes finally_body = cat({push({text_value("f")}), rec(0x26, {})});
  const Bytes input = cat({
      define_v1("f", callee), try_record(2, str(""), try_body.size(), 0, finally_body.size()),
      try_body, finally_body, push({text_value("after")}), rec(0x26, {}), Bytes{0x00},
  });
  const auto result = execute_program(input);
  EXPECT_EQ(result.status, Avm1RunStatus::Thrown);
  EXPECT_EQ(result.trace, (std::vector<std::string>{"f"}));
  EXPECT_EQ(result.opcode, 0x8F);
}

// Invariant: a Return inside a finally that follows a pending Throw ends the call with the returned value and drops the throw; a finally that resumed the throw would report Thrown and trace nothing from the call. The record after the Try keeps the Try off the block end, which is the known limit in Radicle.md.
TEST(Avm1Control, ReturnInsideAFinallyDropsAPendingThrow) {
  const Bytes try_body = cat({push({int_value(1)}), rec(0x2A, {})});
  const Bytes finally_body = cat({push({int_value(5)}), rec(0x3E, {})});
  const Bytes callee = cat({
      try_record(2, str(""), try_body.size(), 0, finally_body.size()), try_body, finally_body,
      push({int_value(99)}), rec(0x26, {}),
  });
  const Bytes input = cat({
      define_v1("f", callee), push({int_value(0), text_value("f")}), rec(0x3D, {}), rec(0x26, {}),
      Bytes{0x00},
  });
  const auto result = execute_program(input);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_EQ(result.trace, (std::vector<std::string>{"5"}));
}

// Invariant: a catch with CatchInRegister stores the thrown value in the function's register, not a variable; a catch that wrote a global would leave the register undefined and trace it.
TEST(Avm1Control, CatchRegisterStoresTheThrownValueInAFunctionRegister) {
  const Bytes try_body = cat({push({int_value(4)}), rec(0x2A, {})});
  const Bytes catch_body = cat({push({register_value(1)}), rec(0x26, {})});
  const Bytes callee =
      cat({try_record(5, Bytes{1}, try_body.size(), catch_body.size(), 0), try_body, catch_body});
  const Bytes input = cat({
      define_v2("g", 2, 0, callee), push({int_value(0), text_value("g")}), rec(0x3D, {}),
      Bytes{0x00},
  });
  const auto result = execute_program(input);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_EQ(result.trace, (std::vector<std::string>{"4"}));
  EXPECT_EQ(result.variables.count("e"), 0u);
}

// Invariant: a catch resumes with the operand stack cut back to its depth at the Try, so the Trace after the catch pops the 1 pushed before the Try; an uncut stack would trace the 3 the throw left.
TEST(Avm1Control, CatchResumesWithTheStackCutBackToTheTryDepth) {
  const Bytes try_body = cat({push({int_value(2), int_value(3), int_value(4)}), rec(0x2A, {})});
  const Bytes catch_body = rec(0x26, {});
  const Bytes input = cat({
      push({int_value(1)}), try_record(1, str("e"), try_body.size(), catch_body.size(), 0),
      try_body, catch_body, Bytes{0x00},
  });
  const auto result = execute_program(input);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_EQ(result.trace, (std::vector<std::string>{"1"}));
  ASSERT_EQ(result.variables.count("e"), 1u);
  EXPECT_EQ(result.variables.at("e").number, 4.0);
}

// Invariant: a Jump that loops inside a Try body stops at the step limit with the Jump's opcode, since the body runs the same counted records; an uncounted body would never return.
TEST(Avm1Control, LoopInsideATryBodyStopsAtTheStepLimit) {
  const Bytes loop = rec(0x99, Bytes{0xFB, 0xFF});
  const Bytes input = cat({try_record(0, str(""), loop.size(), 0, 0), loop, Bytes{0x00}});
  const auto result = execute_program(input, 10);
  EXPECT_EQ(result.status, Avm1RunStatus::StepLimit);
  EXPECT_EQ(result.steps, 10u);
  EXPECT_EQ(result.opcode, 0x99);
  EXPECT_TRUE(result.trace.empty());
}

// Invariant: inside a With, a name the object holds reads and writes that property, and a name it lacks falls through to the global; a With that ignored its object would trace the global 10 for x.
TEST(Avm1Control, WithResolvesNamesThroughItsObjectAndWritesThere) {
  const Bytes read_body = cat({push({text_value("x")}), rec(0x1C, {}), rec(0x26, {})});
  const Bytes write_body = cat({push({text_value("x"), int_value(2)}), rec(0x1D, {})});
  const Bytes input = cat({
      push({text_value("x"), int_value(10)}), rec(0x1D, {}),
      push({text_value("obj"), text_value("x"), int_value(1), int_value(1)}), rec(0x43, {}),
      rec(0x1D, {}),
      push({text_value("obj")}), rec(0x1C, {}), with_record(read_body), read_body,
      push({text_value("obj")}), rec(0x1C, {}), with_record(write_body), write_body,
      push({text_value("obj")}), rec(0x1C, {}), push({text_value("x")}), rec(0x4E, {}),
      rec(0x26, {}),
      push({text_value("x")}), rec(0x1C, {}), rec(0x26, {}), Bytes{0x00},
  });
  const auto result = execute_program(input);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_EQ(result.trace, (std::vector<std::string>{"1", "2", "10"}));
  ASSERT_EQ(result.variables.count("x"), 1u);
  EXPECT_EQ(result.variables.at("x").number, 10.0);
}

// Invariant: a With on a value that is not an object skips its body instead of pushing a scope; a body that ran would trace 5 before 'after'.
TEST(Avm1Control, WithSkipsItsBodyForAValueThatIsNotAnObject) {
  const Bytes body = cat({push({int_value(5)}), rec(0x26, {})});
  const Bytes input = cat({
      push({undefined_value()}), with_record(body), body, push({text_value("after")}),
      rec(0x26, {}), Bytes{0x00},
  });
  const auto result = execute_program(input);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_EQ(result.trace, (std::vector<std::string>{"after"}));
}

// Invariant: a Throw that leaves a With pops the With's scope, so a later read of x resolves to the global 10 and not to the object's x of 1; a scope left on the stack would trace 1.
TEST(Avm1Control, ThrowThatLeavesAWithPopsItsScope) {
  const Bytes with_body = cat({push({int_value(7)}), rec(0x2A, {})});
  const Bytes try_body =
      cat({push({text_value("o")}), rec(0x1C, {}), with_record(with_body), with_body});
  const Bytes input = cat({
      push({text_value("x"), int_value(10)}), rec(0x1D, {}),
      push({text_value("o"), text_value("x"), int_value(1), int_value(1)}), rec(0x43, {}),
      rec(0x1D, {}),
      try_record(1, str("e"), try_body.size(), 0, 0), try_body,
      push({text_value("x")}), rec(0x1C, {}), rec(0x26, {}), Bytes{0x00},
  });
  const auto result = execute_program(input);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_EQ(result.trace, (std::vector<std::string>{"10"}));
}

// Invariant: Try and With share one nesting budget, so 64 static levels run, and a recursion through Try is refused with ResourceLimit once the budget is spent; the 25th call hits 64 regions while the call depth is 25, under its cap of 64. An unbounded recursion would overflow the native stack before the step limit.
TEST(Avm1Control, NestedTryAndWithStopAtSixtyFourLevels) {
  for (const bool is_with : {true, false}) {
    SCOPED_TRACE(is_with ? "With" : "Try");
    const auto within = execute_program(nested(is_with, 64));
    ASSERT_TRUE(within.ok()) << within.message;
    EXPECT_EQ(within.trace, (std::vector<std::string>{"7"}));
  }
  const Bytes call = cat({push({int_value(0), text_value("f")}), rec(0x3D, {})});
  const Bytes callee = cat({try_record(1, str("e"), call.size(), 0, 0), call});
  Bytes body = call;
  for (std::size_t level = 0; level < 40; ++level) {
    body = cat({try_record(2, str(""), 0, 0, body.size()), body});
  }
  const auto recursed = execute_program(cat({define_v1("f", callee), body, Bytes{0x00}}));
  EXPECT_EQ(recursed.status, Avm1RunStatus::ResourceLimit) << recursed.message;
  EXPECT_NE(recursed.message.find("Try and With nesting"), std::string::npos) << recursed.message;
  EXPECT_TRUE(recursed.trace.empty());
}

// Invariant: Enumerate and Enumerate2 push the names so the most recently added name pops first, over a null floor, so popping traces b, a and then null; a missing variable or undefined yields only null, and an insertion order would trace a first.
TEST(Avm1Control, EnumerateYieldsTheMostRecentNameFirstOverANullFloor) {
  const Bytes object = cat({
      push({text_value("a"), int_value(1), text_value("b"), int_value(2)}),
      push({int_value(2)}), rec(0x43, {}),
  });
  const Bytes trace_three = cat({rec(0x26, {}), rec(0x26, {}), rec(0x26, {})});
  const Bytes set_o = cat({push({text_value("o")}), object, rec(0x1D, {})});
  const std::vector<std::pair<Bytes, std::vector<std::string>>> rows = {
      {cat({object, rec(0x55, {}), trace_three, Bytes{0x00}}), {"b", "a", "null"}},
      {cat({push({undefined_value()}), rec(0x55, {}), rec(0x26, {}), Bytes{0x00}}), {"null"}},
      {cat({set_o, push({text_value("o")}), rec(0x46, {}), trace_three, Bytes{0x00}}),
       {"b", "a", "null"}},
      {cat({push({text_value("missing")}), rec(0x46, {}), rec(0x26, {}), Bytes{0x00}}),
       {"null"}},
  };
  for (const auto& [input, trace] : rows) {
    const auto result = execute_program(input);
    ASSERT_TRUE(result.ok()) << result.message;
    EXPECT_EQ(result.trace, trace);
  }
}

// Invariant: InstanceOf follows the constructor chain, so an instance of B is also an instance of its superclass A, while a plain object is an instance of neither; a missing Extends would answer false for A.
TEST(Avm1Control, InstanceOfFollowsTheExtendsChain) {
  const Bytes input = cat({
      instance_prelude(),
      push({text_value("i")}), rec(0x1C, {}), push({text_value("B")}), rec(0x1C, {}), rec(0x54, {}),
      rec(0x26, {}),
      push({text_value("i")}), rec(0x1C, {}), push({text_value("A")}), rec(0x1C, {}), rec(0x54, {}),
      rec(0x26, {}),
      push({int_value(0)}), rec(0x43, {}), push({text_value("B")}), rec(0x1C, {}), rec(0x54, {}),
      rec(0x26, {}), Bytes{0x00},
  });
  const auto result = execute_program(input);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_EQ(result.trace, (std::vector<std::string>{"true", "true", "false"}));
}

// Invariant: CastOp keeps the object when it is an instance and pushes null otherwise, so a kept instance types as object and a plain object types as null.
TEST(Avm1Control, CastOpKeepsAnInstanceAndNullsOtherObjects) {
  const Bytes input = cat({
      instance_prelude(),
      push({text_value("A")}), rec(0x1C, {}), push({text_value("i")}), rec(0x1C, {}), rec(0x2B, {}),
      rec(0x44, {}), rec(0x26, {}),
      push({text_value("A")}), rec(0x1C, {}), push({int_value(0)}), rec(0x43, {}), rec(0x2B, {}),
      rec(0x44, {}), rec(0x26, {}), Bytes{0x00},
  });
  const auto result = execute_program(input);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_EQ(result.trace, (std::vector<std::string>{"object", "null"}));
}

// Invariant: Extends refuses a cycle and a non-function operand by name, so a class never inherits from itself and a number never becomes a superclass; a silent link would make InstanceOf answer for a class that cannot exist.
TEST(Avm1Control, ExtendsRefusesACycleAndANonFunction) {
  const Bytes defs = cat({define_v1("A", Bytes{}), define_v1("B", Bytes{})});
  const std::vector<std::pair<Bytes, std::string_view>> rows = {
      {cat({defs, push({text_value("A")}), rec(0x1C, {}), push({text_value("A")}), rec(0x1C, {}),
            rec(0x69, {}), rec(0x26, {}), Bytes{0x00}}),
       "extend itself"},
      {cat({defs, push({text_value("B")}), rec(0x1C, {}), push({text_value("A")}), rec(0x1C, {}),
            rec(0x69, {}), push({text_value("A")}), rec(0x1C, {}), push({text_value("B")}),
            rec(0x1C, {}), rec(0x69, {}), rec(0x26, {}), Bytes{0x00}}),
       "extend itself"},
      {cat({push({int_value(1), int_value(2)}), rec(0x69, {}), rec(0x26, {}), Bytes{0x00}}),
       "not a function"},
  };
  for (const auto& [input, message] : rows) {
    SCOPED_TRACE(std::string(message));
    const auto result = execute_program(input);
    EXPECT_EQ(result.opcode, 0x69);
    EXPECT_NE(result.message.find(std::string(message)), std::string::npos) << result.message;
    EXPECT_TRUE(result.trace.empty());
  }
  EXPECT_EQ(execute_program(rows[0].first).status, Avm1RunStatus::InvalidStream);
  EXPECT_EQ(execute_program(rows[1].first).status, Avm1RunStatus::InvalidStream);
  EXPECT_EQ(execute_program(rows[2].first).status, Avm1RunStatus::Unsupported);
}

Bytes variable(std::string_view name) { return cat({push({text_value(name)}), rec(0x1C, {})}); }

Bytes trace_instance_of(std::string_view constructor) {
  return cat({variable("i"), variable(constructor), rec(0x54, {}), rec(0x26, {})});
}

Bytes implements_op(std::string_view constructor, std::uint32_t count,
                    const std::vector<std::string_view>& interfaces) {
  Bytes out;
  for (const std::string_view name : interfaces) {
    append(out, variable(name));
  }
  append(out, push({int_value(count)}));
  append(out, variable(constructor));
  append(out, rec(0x2C, {}));
  return out;
}

// Invariant: ImplementsOp records its interfaces on the constructor, so an instance answers InstanceOf for an interface its superclass implements; before the op and for a plain object the same test answers false, so a stub that never records the list traces true nowhere and fails the second row.
TEST(Avm1Control, ImplementsOpLetsAnInstanceReachTheInterfaceThroughItsSuperclass) {
  const Bytes input = cat({
      define_v1("I", Bytes{}),
      instance_prelude(),
      trace_instance_of("I"),
      implements_op("A", 1, {"I"}),
      trace_instance_of("I"),
      push({int_value(0)}), rec(0x43, {}), variable("I"), rec(0x54, {}), rec(0x26, {}),
      Bytes{0x00},
  });
  const auto result = execute_program(input);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_EQ(result.trace, (std::vector<std::string>{"false", "true", "false"}));
}

// Invariant: ImplementsOp does nothing before SWF 7, keeps the first list a class was given, and a count of 0 records nothing; a version-blind op or a last-wins list would trace true on the rows that expect false.
TEST(Avm1Control, ImplementsOpIsSwfSevenOnlyAndKeepsTheFirstList) {
  struct Row {
    const char* name;
    std::uint8_t version;
    Bytes input;
    std::vector<std::string> trace;
  };
  const Bytes defs = cat({define_v1("I", Bytes{}), define_v1("J", Bytes{})});
  const std::vector<Row> rows = {
      {"SWF 6 ignores ImplementsOp", 6,
       cat({defs, instance_prelude(), implements_op("A", 1, {"I"}), trace_instance_of("I"),
            Bytes{0x00}}),
       {"false"}},
      {"SWF 7 records the list", 7,
       cat({defs, instance_prelude(), implements_op("A", 1, {"I"}), trace_instance_of("I"),
            Bytes{0x00}}),
       {"true"}},
      {"the first list is kept", 7,
       cat({defs, instance_prelude(), implements_op("A", 1, {"I"}), implements_op("A", 1, {"J"}),
            trace_instance_of("J"), trace_instance_of("I"), Bytes{0x00}}),
       {"false", "true"}},
      {"a count of 0 records nothing", 7,
       cat({defs, instance_prelude(), implements_op("A", 0, {}), trace_instance_of("I"),
            Bytes{0x00}}),
       {"false"}},
  };
  for (const Row& row : rows) {
    SCOPED_TRACE(row.name);
    const auto stream = decode(row.input);
    ASSERT_TRUE(stream.ok()) << stream.message;
    const auto result = fo4::avm1::execute_actions(stream, 1000000, row.version);
    ASSERT_TRUE(result.ok()) << result.message;
    EXPECT_EQ(result.trace, row.trace);
  }
}

// Invariant: an interface counts through its own list, so A implementing K and K implementing I makes an instance of B an I, and an interface that loops back through K still ends the run with the later InstanceOf answering false; a one-level check traces false on the first row and an unbounded walk never returns.
TEST(Avm1Control, ImplementsOpFollowsInterfaceListsAndStopsOnACycle) {
  const Bytes input = cat({
      define_v1("I", Bytes{}),
      define_v1("J", Bytes{}),
      define_v1("K", Bytes{}),
      instance_prelude(),
      implements_op("K", 1, {"I"}),
      implements_op("A", 1, {"K"}),
      trace_instance_of("I"),
      implements_op("I", 1, {"K"}),
      trace_instance_of("J"),
      Bytes{0x00},
  });
  const auto result = execute_program(input);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_EQ(result.trace, (std::vector<std::string>{"true", "false"}));
}

// Invariant: ImplementsOp clamps its count to the operands left, so a count of 5 over one interface records that one, and a constructor with no count left refuses with StackUnderflow at its own offset; an unclamped count would underflow the first row, and a missing refusal would read past an empty stack.
TEST(Avm1Control, ImplementsOpClampsItsCountAndRefusesAShortStack) {
  const Bytes defs = define_v1("I", Bytes{});
  const auto clamped = execute_program(
      cat({defs, instance_prelude(), implements_op("A", 5, {"I"}), trace_instance_of("I"),
           Bytes{0x00}}));
  ASSERT_TRUE(clamped.ok()) << clamped.message;
  EXPECT_EQ(clamped.trace, (std::vector<std::string>{"true"}));
  const auto short_stack =
      execute_program(cat({defs, instance_prelude(), variable("A"), rec(0x2C, {}), Bytes{0x00}}));
  EXPECT_EQ(short_stack.status, Avm1RunStatus::StackUnderflow) << short_stack.message;
  EXPECT_EQ(short_stack.opcode, 0x2C);
  EXPECT_TRUE(short_stack.trace.empty());
}

}  // namespace
