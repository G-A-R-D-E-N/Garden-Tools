#include <fo4/abc/abc.hpp>
#include <fo4/abc/decompile.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "abc_decompile_fixtures.hpp"

namespace {

using fo4::abc::AbcDecompileStatus;
using fo4::abc::AbcFile;
using fo4::abc::AbcTrait;
using fo4::abc::AbcTraitKind;
using fo4::abc::AbcValueKind;
using fo4::test::CodeBuilder;
using fo4::test::decompile_fixture_file;

const char* const kFullClass =
    "class Test extends Object implements click {\n"
    "  function Test() {\n"
    "  }\n"
    "  function onClick(click:void = 7):void {\n"
    "    addEventListener.addEventListener(\"click\", handler);\n"
    "  }\n"
    "  var counter:void;\n"
    "  const counter:void = 7;\n"
    "  /* class Inner -> Test */\n"
    "  function helper():void {\n"
    "  }\n"
    "}\n"
    "\n";

AbcFile class_file() {
  AbcFile file = decompile_fixture_file(CodeBuilder{}.op(fo4::test::kOpReturnVoid).bytes());
  file.instances[0].flags = 0x01;
  file.bodies[2].exceptions.clear();
  return file;
}

bool contains(const std::string& text, const std::string& part) {
  return text.find(part) != std::string::npos;
}

}  // namespace

TEST(AbcDecompileAs3, WritesAClassWithItsConstructorMembersAndSignatures) {
  const AbcFile file = class_file();
  const fo4::abc::AbcAs3Output out = fo4::abc::write_as3(file);
  EXPECT_EQ(out.text, kFullClass);
  EXPECT_TRUE(out.refused.empty());
}

TEST(AbcDecompileAs3, WritesTheSameClassWhenAScriptTraitNamesIt) {
  AbcFile file = class_file();
  AbcTrait trait;
  trait.name = fo4::test::kMultinameInner;
  trait.kind = AbcTraitKind::Class;
  trait.class_index = 0;
  fo4::abc::AbcScriptInfo script;
  script.init = 0;
  script.traits.push_back(trait);
  file.scripts.push_back(script);
  const fo4::abc::AbcAs3Output out = fo4::abc::write_as3(file);
  EXPECT_EQ(out.text, kFullClass);
  EXPECT_TRUE(out.refused.empty());
}

TEST(AbcDecompileAs3, ChoosesTheClassHeaderFromTheInstanceFlags) {
  AbcFile file = class_file();
  file.instances[0].flags = 0x05;
  std::string text = fo4::abc::write_as3(file).text;
  EXPECT_EQ(text.rfind("interface Test extends click {\n", 0), 0u);
  EXPECT_FALSE(contains(text, "function Test()"));

  file.instances[0].flags = 0x00;
  text = fo4::abc::write_as3(file).text;
  EXPECT_EQ(text.rfind("dynamic class Test extends Object implements click {\n", 0), 0u);

  file.instances[0].flags = 0x03;
  text = fo4::abc::write_as3(file).text;
  EXPECT_EQ(text.rfind("final class Test extends Object implements click {\n", 0), 0u);
}

TEST(AbcDecompileAs3, WritesModifiersAccessorsStaticTraitsAndAClassInitializer) {
  AbcFile file = class_file();
  file.instances[0].traits[0].kind = AbcTraitKind::Getter;
  file.instances[0].traits[0].attrs = 0x03;
  AbcTrait constant;
  constant.name = fo4::test::kMultinameCounter;
  constant.kind = AbcTraitKind::Const;
  constant.type_name = fo4::test::kMultinameVoid;
  constant.value_index = 0x0B;
  constant.value_kind = AbcValueKind::True;
  file.classes[0].traits.push_back(constant);
  file.classes[0].cinit = 1;
  const std::string text = fo4::abc::write_as3(file).text;
  EXPECT_TRUE(contains(text, "  final override function get onClick(click:void = 7):void {\n"));
  EXPECT_TRUE(contains(text, "  static const counter:void = true;\n"));
  EXPECT_TRUE(contains(text, "  static function $cinit(click:void = 7):void {\n"));
  EXPECT_LT(text.find("static const counter"), text.find("static function $cinit"));
}

TEST(AbcDecompileAs3, WritesRestParametersAndQuotedDefaults) {
  AbcFile file = class_file();
  file.methods[1].flags = 0x8C;
  file.bodies[1].local_count = 3;
  file.methods[1].options[0].kind = AbcValueKind::Utf8;
  file.methods[1].options[0].index = fo4::test::kStringFoo;
  const fo4::abc::AbcAs3Output out = fo4::abc::write_as3(file);
  EXPECT_TRUE(out.refused.empty());
  EXPECT_TRUE(contains(out.text,
                       "  function onClick(click:void = \"Foo\", ...rest):void {\n"
                       "    addEventListener.addEventListener(\"click\", handler);\n  }\n"));
}

TEST(AbcDecompileAs3, WritesAMethodWithoutABodyWithASemicolon) {
  AbcFile file = class_file();
  file.bodies.erase(file.bodies.begin() + 1);
  const fo4::abc::AbcAs3Output out = fo4::abc::write_as3(file);
  EXPECT_TRUE(contains(out.text, "  function onClick(click:void = 7):void;\n"));
  EXPECT_TRUE(out.refused.empty());
}

TEST(AbcDecompileAs3, KeepsTheSignatureOfARefusedMethodAndReportsTheRefusal) {
  AbcFile file = class_file();
  file.bodies[1].code = CodeBuilder{}
                            .op_u30(0x40, 1)
                            .op(fo4::test::kOpPop)
                            .op(fo4::test::kOpReturnVoid)
                            .bytes();
  const fo4::abc::AbcAs3Output out = fo4::abc::write_as3(file);
  EXPECT_TRUE(contains(out.text,
                       "  function onClick(click:void = 7):void {\n    /* refused: UnsupportedOpcode at offset 0: "));
  ASSERT_EQ(out.refused.size(), 1u);
  EXPECT_EQ(out.refused[0].status, AbcDecompileStatus::UnsupportedOpcode);
  EXPECT_EQ(out.refused[0].method, 1u);
  EXPECT_TRUE(contains(out.text, "  function helper():void {\n  }\n"));
}

TEST(AbcDecompileAs3, CountsTheFunctionsItAttempts) {
  AbcFile file = class_file();
  EXPECT_EQ(fo4::abc::write_as3(file).functions, 4u);
  file.bodies[1].code = CodeBuilder{}
                            .op_u30(0x40, 1)
                            .op(fo4::test::kOpPop)
                            .op(fo4::test::kOpReturnVoid)
                            .bytes();
  const fo4::abc::AbcAs3Output refused = fo4::abc::write_as3(file);
  EXPECT_EQ(refused.functions, 4u);
  EXPECT_EQ(refused.refused.size(), 1u);
  file.bodies.erase(file.bodies.begin() + 1);
  EXPECT_EQ(fo4::abc::write_as3(file).functions, 3u);
}

TEST(AbcDecompileAs3, ProducesTheSameTextAndHashOnEveryRun) {
  const AbcFile file = class_file();
  const fo4::abc::AbcAs3Output first = fo4::abc::write_as3(file);
  const fo4::abc::AbcAs3Output second = fo4::abc::write_as3(file);
  EXPECT_EQ(first.text, second.text);
  EXPECT_EQ(first.hash, second.hash);
  EXPECT_EQ(first.hash, fo4::abc::as3_text_hash(first.text));
  EXPECT_NE(first.hash, fo4::abc::as3_text_hash(first.text + " "));
}

TEST(AbcDecompileAs3, HashesWithFnv1a64) {
  EXPECT_EQ(fo4::abc::as3_text_hash(""), 0xcbf29ce484222325ull);
  EXPECT_EQ(fo4::abc::as3_text_hash("a"), 0xaf63dc4c8601ec8cull);
  EXPECT_EQ(fo4::abc::as3_hash_hex(0xaf63dc4c8601ec8cull), "af63dc4c8601ec8c");
  EXPECT_EQ(fo4::abc::as3_hash_hex(1ull), "0000000000000001");
}

TEST(AbcDecompileAs3, WritesOneClassAloneAsItStandsInTheWholeText) {
  const AbcFile file = class_file();
  const std::string whole = kFullClass;
  const fo4::abc::AbcAs3Output out = fo4::abc::write_as3_class(file, 0);
  EXPECT_EQ(out.text, whole.substr(0, whole.size() - 1));
  EXPECT_EQ(out.functions, 4u);
  EXPECT_EQ(out.hash, fo4::abc::as3_text_hash(out.text));
  EXPECT_TRUE(out.refused.empty());
  const fo4::abc::AbcAs3Output past = fo4::abc::write_as3_class(file, 1);
  EXPECT_TRUE(past.text.empty());
  EXPECT_EQ(past.hash, fo4::abc::as3_text_hash(""));
  EXPECT_TRUE(past.refused.empty());
}

TEST(AbcDecompileAs3, WritesOneMethodAloneFoundThroughItsOwner) {
  AbcFile file = class_file();
  EXPECT_EQ(fo4::abc::write_as3_method(file, 0).text, "function Test() {\n}\n");
  const fo4::abc::AbcAs3Output click = fo4::abc::write_as3_method(file, 1);
  EXPECT_EQ(click.text,
            "function onClick(click:void = 7):void {\n"
            "  addEventListener.addEventListener(\"click\", handler);\n}\n");
  EXPECT_EQ(click.functions, 1u);
  EXPECT_EQ(fo4::abc::write_as3_method(file, 2).text, "function helper():void {\n}\n");

  file.methods.push_back(file.methods[2]);
  EXPECT_EQ(fo4::abc::write_as3_method(file, 3).text, "function $method3():void;\n");
  EXPECT_EQ(fo4::abc::write_as3_method(file, 3).functions, 0u);

  fo4::abc::AbcMethodBody body = file.bodies[2];
  body.method = 3;
  file.bodies.push_back(body);
  file.classes[0].cinit = 3;
  EXPECT_EQ(fo4::abc::write_as3_method(file, 3).text, "static function $cinit():void {\n}\n");

  file.classes[0].cinit = 0;
  fo4::abc::AbcScriptInfo script;
  script.init = 3;
  file.scripts.push_back(script);
  EXPECT_EQ(fo4::abc::write_as3_method(file, 3).text, "function $init():void {\n}\n");

  const fo4::abc::AbcAs3Output past = fo4::abc::write_as3_method(file, 99);
  EXPECT_TRUE(past.text.empty());
  EXPECT_TRUE(past.refused.empty());
}

TEST(AbcDecompileAs3, ReportsARefusedMethodAtMethodScope) {
  AbcFile file = class_file();
  file.bodies[1].code = CodeBuilder{}
                            .op_u30(0x40, 1)
                            .op(fo4::test::kOpPop)
                            .op(fo4::test::kOpReturnVoid)
                            .bytes();
  const fo4::abc::AbcAs3Output out = fo4::abc::write_as3_method(file, 1);
  EXPECT_TRUE(contains(out.text, "function onClick(click:void = 7):void {\n  /* refused: UnsupportedOpcode at offset 0: "));
  ASSERT_EQ(out.refused.size(), 1u);
  EXPECT_EQ(out.refused[0].method, 1u);
}
