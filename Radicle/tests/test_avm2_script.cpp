#include <fo4/avm2/fonts.hpp>
#include <fo4/avm2/interpreter.hpp>
#include <fo4/avm2/name.hpp>
#include <fo4/avm2/object.hpp>
#include <fo4/avm2/runtime.hpp>
#include <fo4/avm2/swf_runtime.hpp>
#include <fo4/avm2/value.hpp>
#include <fo4/abc/abc.hpp>
#include <fo4/swf/swf.hpp>

#include "abc_test_support.hpp"
#include "avm2_test_support.hpp"
#include "swf_movie_builder.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace {

namespace abc = fo4::abc;

using fo4::avm2::ExecutionResult;
using fo4::avm2::Interpreter;
using fo4::avm2::Multiname;
using fo4::avm2::Namespace;
using fo4::avm2::Runtime;
using fo4::avm2::SwfRuntime;
using fo4::avm2::Value;

std::vector<std::uint8_t> serialize_script(avm2_test::ScriptBuilder& script,
                                           avm2_test::CodeBuilder& code) {
  const std::uint32_t method = script.method("init", code.bytes(), 0);
  abc::AbcFile file = script.finish(method);
  std::vector<std::uint8_t> bytes;
  EXPECT_EQ(file.serialize(bytes), abc::AbcStatus::Ok);
  return bytes;
}

std::string global_string(Runtime& runtime, const std::string& name) {
  Multiname multiname;
  multiname.name = name;
  multiname.namespaces.push_back(Namespace{});
  Value value;
  if (!runtime.global_object()->read_value(multiname, value) || !value.is_string()) {
    return {};
  }
  return value.as_string();
}

std::size_t global_count(Runtime& runtime, const std::string& name) {
  Multiname multiname;
  multiname.name = name;
  multiname.namespaces.push_back(Namespace{});
  Value value;
  if (!runtime.global_object()->read_value(multiname, value) || !value.is_number_like()) {
    return 0;
  }
  return static_cast<std::size_t>(value.as_number());
}

TEST(Avm2Script, LinksAddedFilesIncrementallyAndRunsTheirScripts) {
  Runtime runtime;
  Interpreter interpreter{runtime};

  avm2_test::ScriptBuilder first_script;
  avm2_test::CodeBuilder first_code;
  first_code.findpropstrict(first_script.multiname("phase"));
  first_code.pushstring(first_script.string_of("one"));
  first_code.initproperty(first_script.multiname("phase"));
  first_code.returnvoid();
  const std::vector<std::uint8_t> first_method = serialize_script(first_script, first_code);
  abc::AbcFile first_file;
  ASSERT_EQ(abc::AbcFile::parse(first_method, first_file), abc::AbcStatus::Ok);
  ASSERT_TRUE(runtime.add_file(first_file).ok());
  ASSERT_TRUE(runtime.link().ok());
  ASSERT_EQ(runtime.scripts().size(), 1u);
  ASSERT_TRUE(interpreter.run_script(0).ok());
  EXPECT_EQ(global_string(runtime, "phase"), "one");

  avm2_test::ScriptBuilder second_script;
  avm2_test::CodeBuilder second_code;
  second_code.findpropstrict(second_script.multiname("phase"));
  second_code.pushstring(second_script.string_of("two"));
  second_code.initproperty(second_script.multiname("phase"));
  second_code.findpropstrict(second_script.multiname("boots"));
  second_code.pushbyte(1);
  second_code.initproperty(second_script.multiname("boots"));
  second_code.returnvoid();
  const std::vector<std::uint8_t> second_method = serialize_script(second_script, second_code);
  abc::AbcFile second_file;
  ASSERT_EQ(abc::AbcFile::parse(second_method, second_file), abc::AbcStatus::Ok);
  ASSERT_TRUE(runtime.add_file(second_file).ok());
  ASSERT_EQ(runtime.file_count(), 2u);
  ASSERT_TRUE(runtime.link_added_file(1).ok());
  ASSERT_EQ(runtime.scripts().size(), 2u);
  ASSERT_TRUE(interpreter.run_script(1).ok());
  EXPECT_EQ(global_string(runtime, "phase"), "two");
  EXPECT_EQ(global_count(runtime, "boots"), 1u);
  EXPECT_EQ(runtime.link_added_file(1).status, fo4::avm2::RuntimeStatus::AlreadyLinked);
}

TEST(Avm2Script, SwfRuntimeLinksAndRunsTheMoviesAbcBlocks) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  code.findpropstrict(script.multiname("booted"));
  code.pushstring(script.string_of("yes"));
  code.initproperty(script.multiname("booted"));
  code.returnvoid();
  const std::vector<std::uint8_t> abc_bytes = serialize_script(script, code);

  swf_test::SwfBuilder builder;
  builder.rect(0, 200, 0, 100, 9);
  builder.u16(24u * 256u);
  builder.u16(1);
  builder.tag(fo4::swf::tag::kShowFrame);
  swf_test::SwfBuilder abc_payload;
  abc_payload.u32(0);
  abc_payload.text("boot");
  abc_payload.bytes(std::span<const std::byte>(
      reinterpret_cast<const std::byte*>(abc_bytes.data()), abc_bytes.size()));
  builder.tag(fo4::swf::tag::kDoAbc2, abc_payload.body, true);
  builder.tag(fo4::swf::tag::kEnd);
  const std::vector<std::byte> bytes = swf_test::wrap_body(builder.body, 10, false, false);
  const fo4::swf::SwfReadResult read = fo4::swf::read_swf(bytes);
  ASSERT_TRUE(read.ok()) << fo4::swf::status_name(read.status);

  Runtime runtime;
  Interpreter interpreter{runtime};
  const std::shared_ptr<SwfRuntime> movie = SwfRuntime::create(runtime, interpreter, read.file);
  ASSERT_NE(movie, nullptr) << fo4::avm2::SwfRuntime::last_error();
  EXPECT_EQ(runtime.file_count(), 1u);
  EXPECT_EQ(runtime.scripts().size(), 1u);
  EXPECT_EQ(global_string(runtime, "booted"), "yes");
}

}  // namespace

TEST(Avm2Script, AliasesDuplicateClassesFromASecondMovieFile) {
  const std::vector<std::uint8_t> bytes = fo4::test::build_abc();
  abc::AbcFile first;
  ASSERT_EQ(abc::AbcFile::parse(bytes, first), abc::AbcStatus::Ok);
  Runtime runtime;
  Interpreter interpreter{runtime};
  ASSERT_TRUE(runtime.add_file(first).ok());
  ASSERT_TRUE(runtime.link().ok());
  const std::size_t classes_after_first = runtime.class_count();

  abc::AbcFile second;
  ASSERT_EQ(abc::AbcFile::parse(bytes, second), abc::AbcStatus::Ok);
  ASSERT_TRUE(runtime.add_file(second).ok());
  ASSERT_TRUE(runtime.link_added_file(1).ok());
  EXPECT_EQ(runtime.class_count(), classes_after_first);
  EXPECT_EQ(runtime.class_at(first, 0), runtime.class_at(second, 0));
}
