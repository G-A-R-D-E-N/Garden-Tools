#include <fo4/avm1/avm1.hpp>
#include <fo4/avm1/interpreter.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
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

Bytes le16(std::uint16_t value) {
  return Bytes{static_cast<std::uint8_t>(value & 0xFFu), static_cast<std::uint8_t>(value >> 8)};
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

Bytes push(std::initializer_list<Bytes> values) {
  Bytes payload;
  for (const Bytes& value : values) {
    append(payload, value);
  }
  return rec(0x96, payload);
}

Bytes define_v1(std::string_view name, std::initializer_list<std::string_view> params,
                const Bytes& body) {
  Bytes header = str(name);
  append(header, le16(static_cast<std::uint16_t>(params.size())));
  for (const std::string_view param : params) {
    append(header, str(param));
  }
  append(header, le16(static_cast<std::uint16_t>(body.size())));
  return cat({rec(0x9B, header), body});
}

Bytes parameter2(std::uint8_t register_index, std::string_view name) {
  return cat({Bytes{register_index}, str(name)});
}

Bytes define_v2(std::string_view name, std::uint16_t count, std::uint8_t registers,
                std::uint16_t flags, const Bytes& params, const Bytes& body) {
  return cat({rec(0x8E, cat({str(name), le16(count), Bytes{registers}, le16(flags), params,
                              le16(static_cast<std::uint16_t>(body.size()))})),
              body});
}

fo4::avm1::Avm1Stream decode(const Bytes& input) {
  std::vector<std::byte> bytes;
  for (const std::uint8_t value : input) {
    bytes.push_back(static_cast<std::byte>(value));
  }
  return fo4::avm1::decode_actions(bytes);
}

Bytes factorial_body() {
  const Bytes recurse = cat({
      push({text_value("n")}), rec(0x1C, {}), push({text_value("n")}), rec(0x1C, {}),
      push({int_value(1)}), rec(0x0B, {}), push({int_value(1), text_value("fact")}),
      rec(0x3D, {}), rec(0x0C, {}), rec(0x3E, {}),
  });
  const Bytes base = cat({push({int_value(1)}), rec(0x3E, {})});
  return cat({
      push({text_value("n")}), rec(0x1C, {}), push({int_value(2)}), rec(0x48, {}),
      rec(0x9D, le16(static_cast<std::uint16_t>(recurse.size()))), recurse, base,
  });
}

Bytes factorial_program() {
  return cat({
      define_v1("fact", {"n"}, factorial_body()),
      push({int_value(5), int_value(1), text_value("fact")}), rec(0x3D, {}), rec(0x26, {}),
      Bytes{0x00},
  });
}

struct Refusal {
  const char* name;
  Bytes input;
  Avm1RunStatus status;
  std::string_view message;
  std::vector<std::string> trace;
};

TEST(Avm1Interpreter, DefineFunctionCallsWithArgsAndReturnsItsValue) {
  const Bytes body = cat({
      push({text_value("a")}), rec(0x1C, {}), push({text_value("b")}), rec(0x1C, {}),
      rec(0x0B, {}), rec(0x3E, {}),
  });
  const Bytes input = cat({
      define_v1("sub", {"a", "b"}, body),
      push({int_value(2), int_value(5), int_value(2), text_value("sub")}), rec(0x3D, {}),
      rec(0x26, {}), Bytes{0x00},
  });
  const auto stream = decode(input);
  ASSERT_TRUE(stream.ok()) << stream.message;
  const auto result = fo4::avm1::execute_actions(stream);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_EQ(result.trace, (std::vector<std::string>{"3"}));
  EXPECT_EQ(result.steps, 11u);
  ASSERT_EQ(result.variables.count("sub"), 1u);
  EXPECT_EQ(result.variables.at("sub").kind, Avm1DataKind::Function);
}

TEST(Avm1Interpreter, RecursiveFactorialReturnsOneHundredTwentyInSeventyTwoSteps) {
  const auto stream = decode(factorial_program());
  ASSERT_TRUE(stream.ok()) << stream.message;
  const auto result = fo4::avm1::execute_actions(stream);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_EQ(result.trace, (std::vector<std::string>{"120"}));
  EXPECT_EQ(result.steps, 72u);
  const auto limited = fo4::avm1::execute_actions(stream, 70);
  EXPECT_EQ(limited.status, Avm1RunStatus::StepLimit);
  EXPECT_EQ(limited.opcode, 0x26);
  EXPECT_EQ(limited.steps, 70u);
  EXPECT_TRUE(limited.trace.empty());
}

TEST(Avm1Interpreter, StepLimitRefusesTheReturnThatRunsPastTheLimitInsideNestedCalls) {
  const auto stream = decode(factorial_program());
  ASSERT_TRUE(stream.ok()) << stream.message;
  const auto result = fo4::avm1::execute_actions(stream, 69);
  EXPECT_EQ(result.status, Avm1RunStatus::StepLimit);
  EXPECT_EQ(result.opcode, 0x3E);
  EXPECT_EQ(result.steps, 69u);
  EXPECT_TRUE(result.trace.empty());
}

TEST(Avm1Interpreter, DefineFunction2PreloadsThisAndMapsParametersToRegisters) {
  const Bytes body = cat({
      push({register_value(1)}), push({text_value("k")}), rec(0x4E, {}),
      push({register_value(2)}), rec(0x47, {}), rec(0x3E, {}),
  });
  const Bytes input = cat({
      push({int_value(7)}), push({int_value(1)}), push({int_value(0)}), rec(0x43, {}),
      rec(0x4C, {}), push({text_value("k"), int_value(5)}), rec(0x4F, {}), rec(0x4C, {}),
      push({text_value("f")}), define_v2("", 1, 3, 0x0001, parameter2(2, "a"), body),
      rec(0x4F, {}), push({text_value("f")}), rec(0x52, {}), rec(0x26, {}), Bytes{0x00},
  });
  const auto stream = decode(input);
  ASSERT_TRUE(stream.ok()) << stream.message;
  const auto result = fo4::avm1::execute_actions(stream);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_EQ(result.trace, (std::vector<std::string>{"12"}));
  EXPECT_EQ(result.steps, 21u);
}

TEST(Avm1Interpreter, DefineLocalShadowsTheGlobalForOneCall) {
  const Bytes body = cat({
      push({text_value("x"), int_value(2)}), rec(0x3C, {}), push({text_value("x")}),
      rec(0x1C, {}), rec(0x26, {}), push({text_value("y")}), rec(0x41, {}),
      push({text_value("y")}), rec(0x1C, {}), rec(0x26, {}),
  });
  const Bytes input = cat({
      push({text_value("x"), int_value(1)}), rec(0x1D, {}), define_v1("f", {}, body),
      push({int_value(0), text_value("f")}), rec(0x3D, {}), rec(0x17, {}),
      push({text_value("x")}), rec(0x1C, {}), rec(0x26, {}), push({text_value("y")}),
      rec(0x1C, {}), rec(0x26, {}), Bytes{0x00},
  });
  const auto stream = decode(input);
  ASSERT_TRUE(stream.ok()) << stream.message;
  const auto result = fo4::avm1::execute_actions(stream);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_EQ(result.trace, (std::vector<std::string>{"2", "undefined", "1", "undefined"}));
  ASSERT_EQ(result.variables.count("x"), 1u);
  EXPECT_EQ(result.variables.at("x").number, 1.0);
  EXPECT_EQ(result.variables.count("y"), 0u);
}

TEST(Avm1Interpreter, UnboundedRecursionStopsAtTheCallDepthLimit) {
  const Bytes body = cat({push({int_value(0), text_value("f")}), rec(0x3D, {})});
  const Bytes input = cat({
      define_v1("f", {}, body), push({int_value(0), text_value("f")}), rec(0x3D, {}),
      rec(0x26, {}), Bytes{0x00},
  });
  const auto stream = decode(input);
  ASSERT_TRUE(stream.ok()) << stream.message;
  const auto result = fo4::avm1::execute_actions(stream);
  EXPECT_EQ(result.status, Avm1RunStatus::ResourceLimit);
  EXPECT_EQ(result.opcode, 0x3D);
  EXPECT_EQ(result.steps, 131u);
  EXPECT_NE(result.message.find("call depth of 64"), std::string::npos) << result.message;
  EXPECT_TRUE(result.trace.empty());
}

TEST(Avm1Interpreter, DefineFunctionRunTwiceSkipsItsBodyAndReusesIt) {
  const Bytes inner = define_v1("inner", {}, cat({push({int_value(9)}), rec(0x3E, {})}));
  const Bytes outer_body = cat({
      inner,
      push({int_value(0), text_value("inner")}), rec(0x3D, {}), push({int_value(1)}),
      rec(0x0A, {}), rec(0x3E, {}),
  });
  const Bytes call_outer =
      cat({push({int_value(0), text_value("outer")}), rec(0x3D, {}), rec(0x26, {})});
  const Bytes input =
      cat({define_v1("outer", {}, outer_body), call_outer, call_outer, Bytes{0x00}});
  const auto stream = decode(input);
  ASSERT_TRUE(stream.ok()) << stream.message;
  const auto result = fo4::avm1::execute_actions(stream);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_EQ(result.trace, (std::vector<std::string>{"10", "10"}));
}

TEST(Avm1Interpreter, FunctionRefusalsAreNamedAndTraceNothing) {
  const std::vector<Refusal> rows = {
      {"Return outside a function ends the action list",
       cat({push({int_value(7)}), rec(0x3E, {}), rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Ok,
       "",
       {}},
      {"CallFunction of a name that is not a function",
       cat({push({int_value(0), text_value("missing")}), rec(0x3D, {}), rec(0x26, {}),
            Bytes{0x00}}),
       Avm1RunStatus::Unsupported,
       "CallFunction of 'missing'",
       {}},
      {"CallMethod of a method the object does not have",
       cat({push({int_value(0)}), push({int_value(0)}), rec(0x43, {}), push({text_value("f")}),
            rec(0x52, {}), rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Unsupported,
       "method 'f' is not a script function",
       {}},
      {"StoreRegister outside a function",
       cat({push({int_value(1)}), rec(0x87, Bytes{0x01}), rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Unsupported,
       "StoreRegister outside a function",
       {}},
      {"a register push outside a function",
       cat({push({register_value(1)}), rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Unsupported,
       "a register outside a function",
       {}},
      {"a preloaded register past the declared count",
       cat({define_v2("g", 0, 1, 0x0001, {}, {}), push({int_value(0), text_value("g")}),
            rec(0x3D, {}), rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::InvalidStream,
       "register 1 is past the 1 registers",
       {}},
      {"a preload of _root is refused by name",
       cat({define_v2("g", 0, 0, 0x0040, {}, {}), push({int_value(0), text_value("g")}),
            rec(0x3D, {}), rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Unsupported,
       "preloading super, _root, _parent or _global",
       {}},
  };
  for (const Refusal& row : rows) {
    SCOPED_TRACE(row.name);
    const auto stream = decode(row.input);
    ASSERT_TRUE(stream.ok()) << stream.message;
    const auto result = fo4::avm1::execute_actions(stream);
    EXPECT_EQ(result.status, row.status) << result.message;
    EXPECT_NE(result.message.find(std::string(row.message)), std::string::npos) << result.message;
    EXPECT_EQ(result.trace, row.trace);
  }
}

}  // namespace
