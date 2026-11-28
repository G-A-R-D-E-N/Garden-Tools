#pragma once

#include <fo4/avm2/display.hpp>
#include <fo4/avm2/events.hpp>
#include <fo4/avm2/hit_test.hpp>
#include <fo4/avm2/interpreter.hpp>
#include <fo4/avm2/name.hpp>
#include <fo4/avm2/object.hpp>
#include <fo4/avm2/runtime.hpp>
#include <fo4/avm2/shape_geometry.hpp>
#include <fo4/avm2/snapshot.hpp>
#include <fo4/avm2/value.hpp>

#include "avm2_test_support.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace fo4::test::avm2_display {

namespace abc = fo4::abc;

using fo4::avm2::ExecutionResult;

using fo4::avm2::Interpreter;

using fo4::avm2::Runtime;

struct Harness {
  Runtime runtime;
  Interpreter interpreter{runtime};

  ExecutionResult run(avm2_test::ScriptBuilder& script, avm2_test::CodeBuilder& code,
                      std::uint32_t local_count) {
    const std::uint32_t method = script.method("init", code.bytes(), local_count);
    abc::AbcFile file = script.finish(method);
    runtime.add_file(file);
    if (!runtime.link().ok()) {
      ExecutionResult failure;
      failure.message = "runtime did not link";
      return failure;
    }
    return interpreter.run_script(0);
  }
};

inline fo4::avm2::Value property(const fo4::avm2::Value& object, const char* name) {
  if (!object.is_object()) return fo4::avm2::Value::undefined();
  fo4::avm2::Multiname multiname;
  multiname.name = name;
  multiname.namespaces.push_back(fo4::avm2::Namespace{});
  fo4::avm2::Value out;
  if (!object.as_object()->read_value(multiname, out)) return fo4::avm2::Value::undefined();
  return out;
}

inline void call_on(avm2_test::ScriptBuilder& script, avm2_test::CodeBuilder& code, std::uint32_t local,
                    const char* name, const std::vector<std::int32_t>& args, bool keep = false) {
  code.getlocal(local);
  for (const std::int32_t arg : args) {
    code.pushbyte(arg);
  }
  code.callproperty(script.multiname(name), static_cast<std::uint32_t>(args.size()));
  if (!keep) {
    code.pop();
  }
}

inline fo4::avm2::ShapeGeometry draw_open_path(double x0) {
  Harness harness;
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  const std::uint32_t shape = script.multiname("Shape", "flash.display");
  const std::uint32_t graphics = script.multiname("graphics");
  const std::uint32_t begin_fill = script.multiname("beginFill");
  const std::uint32_t move_to = script.multiname("moveTo");
  const std::uint32_t line_to = script.multiname("lineTo");
  const std::uint32_t start = script.add_double(x0);
  const std::uint32_t zero = script.add_double(0.0);
  const std::uint32_t ten = script.add_double(10.0);
  code.getlex(shape);
  code.construct(0);
  code.setlocal(1);
  code.getlocal(1);
  code.getproperty(graphics);
  code.setlocal(2);
  code.getlocal(2);
  code.pushbyte(255);
  code.pushbyte(1);
  code.callproperty(begin_fill, 2);
  code.pop();
  code.getlocal(2);
  code.pushdouble(start);
  code.pushdouble(zero);
  code.callproperty(move_to, 2);
  code.pop();
  code.getlocal(2);
  code.pushdouble(ten);
  code.pushdouble(zero);
  code.callproperty(line_to, 2);
  code.pop();
  code.getlocal(2);
  code.pushdouble(ten);
  code.pushdouble(ten);
  code.callproperty(line_to, 2);
  code.pop();
  code.getlocal(1);
  code.returnvalue();
  const ExecutionResult result = harness.run(script, code, 3);
  EXPECT_TRUE(result.ok()) << result.message;
  EXPECT_TRUE(result.value.is_object());
  return *fo4::avm2::drawn_geometry_of(
      static_cast<const fo4::avm2::DisplayObject&>(*result.value.as_object()));
}

}  // namespace fo4::test::avm2_display
