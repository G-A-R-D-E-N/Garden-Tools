#include <fo4/abc/abc.hpp>
#include <fo4/abc/decompile.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "abc_decompile_fixtures.hpp"

namespace {

using fo4::abc::AbcDecompileStatus;
using fo4::abc::AbcFile;
using fo4::abc::AbcTrait;
using fo4::abc::AbcTraitKind;
using fo4::abc::AbcValueKind;
using fo4::test::CodeBuilder;
using fo4::test::decompile_fixture_file;

constexpr std::uint8_t kDivide = 0xA3;
constexpr std::uint8_t kConstructSuper = 0x49;

const char* const kClassOnly =
    "public class Test : click {\n"
    "  public Test() {\n"
    "  }\n"
    "  public virtual void onClick(void click = 7) {\n"
    "    AddEventListener.AddEventListener(\"click\", handler);\n"
    "  }\n"
    "  public void counter;\n"
    "  public readonly void counter = 7;\n"
    "  /* class Inner -> Test */\n"
    "  public virtual void helper() {\n"
    "  }\n"
    "}\n";

AbcFile class_file() {
  AbcFile file = decompile_fixture_file(CodeBuilder{}.op(fo4::test::kOpReturnVoid).bytes());
  file.instances[0].flags = 0x01;
  file.bodies[2].exceptions.clear();
  return file;
}

std::uint32_t add_name(AbcFile& file, const std::string& ns, const std::string& name) {
  file.pool.strings.push_back(ns);
  const std::uint32_t ns_string = static_cast<std::uint32_t>(file.pool.strings.size() - 1);
  file.pool.strings.push_back(name);
  const std::uint32_t name_string = static_cast<std::uint32_t>(file.pool.strings.size() - 1);
  fo4::abc::AbcNamespace space;
  space.kind = fo4::abc::AbcNamespaceKind::Package;
  space.name = ns_string;
  file.pool.namespaces.push_back(space);
  fo4::abc::AbcMultiname multiname;
  multiname.kind = fo4::abc::AbcMultinameKind::QName;
  multiname.name = name_string;
  multiname.ns = static_cast<std::uint32_t>(file.pool.namespaces.size() - 1);
  file.pool.multinames.push_back(multiname);
  return static_cast<std::uint32_t>(file.pool.multinames.size() - 1);
}

AbcFile constructor_file(const CodeBuilder& code) {
  AbcFile file = decompile_fixture_file(code.bytes());
  file.instances[0].flags = 0x01;
  file.bodies[2].exceptions.clear();
  return file;
}

std::string constructor_text(const CodeBuilder& code) {
  return fo4::abc::write_cs_method(constructor_file(code), 0).text;
}

bool contains(const std::string& text, const std::string& part) {
  return text.find(part) != std::string::npos;
}

}  // namespace

TEST(AbcDecompileCs, WritesAClassWithItsConstructorMembersAndSignatures) {
  const fo4::abc::AbcCsOutput out = fo4::abc::write_cs(class_file());
  EXPECT_EQ(out.text, std::string(kClassOnly) + "\n");
  EXPECT_TRUE(out.refused.empty());
}

TEST(AbcDecompileCs, WritesTheSameClassWhenAScriptTraitNamesIt) {
  AbcFile file = class_file();
  AbcTrait trait;
  trait.name = fo4::test::kMultinameInner;
  trait.kind = AbcTraitKind::Class;
  trait.class_index = 0;
  fo4::abc::AbcScriptInfo script;
  script.init = 0;
  script.traits.push_back(trait);
  file.scripts.push_back(script);
  const fo4::abc::AbcCsOutput out = fo4::abc::write_cs(file);
  EXPECT_EQ(out.text, std::string(kClassOnly) + "\n");
  EXPECT_TRUE(out.refused.empty());
}

TEST(AbcDecompileCs, ChoosesTheClassHeaderFromTheInstanceFlags) {
  AbcFile file = class_file();
  file.instances[0].flags = 0x05;
  std::string text = fo4::abc::write_cs(file).text;
  EXPECT_EQ(text.rfind("public interface Test : click {\n", 0), 0u);
  EXPECT_FALSE(contains(text, "Test()"));

  file.instances[0].flags = 0x00;
  text = fo4::abc::write_cs(file).text;
  EXPECT_EQ(text.rfind("public class Test : click {\n", 0), 0u);

  file.instances[0].flags = 0x03;
  text = fo4::abc::write_cs(file).text;
  EXPECT_EQ(text.rfind("public sealed class Test : click {\n", 0), 0u);
}

TEST(AbcDecompileCs, WrapsAPackageClassInANamespaceAndMapsItsBaseClasses) {
  AbcFile file = class_file();
  file.pool.multinames[fo4::test::kMultinameTest].ns = 1;
  std::string text = fo4::abc::write_cs(file).text;
  EXPECT_EQ(text.rfind("namespace flash.external {\n  public class Test : click {\n    public Test() {\n    }\n", 0),
            0u);
  EXPECT_NE(text.find("  }\n}\n\n", text.size() - 8), std::string::npos);

  file = class_file();
  file.instances[0].super_index = add_name(file, "flash.display", "Sprite");
  const fo4::abc::AbcCsOutput mapped = fo4::abc::write_cs(file);
  EXPECT_EQ(mapped.text.rfind("public class Test : Flash.Display.Sprite, click {\n", 0), 0u);
  EXPECT_TRUE(mapped.refused.empty());
}

TEST(AbcDecompileCs, MarksAnUnmappedBaseClassAndReportsItWithoutAMethod) {
  AbcFile file = class_file();
  file.instances[0].super_index = add_name(file, "flash.display", "Zork");
  const fo4::abc::AbcCsOutput out = fo4::abc::write_cs(file);
  EXPECT_EQ(out.text.rfind("public class Test : click /* unmapped flash.display::Zork */ {\n", 0), 0u);
  ASSERT_EQ(out.refused.size(), 1u);
  EXPECT_EQ(out.refused[0].status, AbcDecompileStatus::UnmappedName);
  EXPECT_EQ(out.refused[0].method, fo4::abc::kNoFlowIndex);
  EXPECT_TRUE(contains(out.refused[0].message, "flash.display::Zork"));
}

TEST(AbcDecompileCs, PairsAGetterAndASetterIntoOnePropertyWithAValueParameter) {
  AbcFile file = class_file();
  file.instances[0].traits[0].kind = AbcTraitKind::Getter;
  AbcTrait setter = file.instances[0].traits[0];
  setter.kind = AbcTraitKind::Setter;
  file.instances[0].traits.push_back(setter);
  file.bodies[1].code = CodeBuilder{}.op(fo4::test::kOpGetLocal1).op(fo4::test::kOpReturnValue).bytes();
  const fo4::abc::AbcCsOutput out = fo4::abc::write_cs(file);
  EXPECT_TRUE(contains(out.text,
                       "  public virtual void onClick {\n"
                       "    get {\n      return click;\n    }\n"
                       "    set {\n      return value;\n    }\n"
                       "  }\n"));
  EXPECT_EQ(out.text.find("set {"), out.text.rfind("set {"));
  EXPECT_EQ(out.functions, 5u);
  EXPECT_TRUE(out.refused.empty());
}

TEST(AbcDecompileCs, WritesModifiersStaticMembersAndAClassInitializer) {
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
  AbcTrait method;
  method.name = fo4::test::kMultinameOnClick;
  method.kind = AbcTraitKind::Method;
  method.method_index = 2;
  file.classes[0].traits.push_back(method);
  file.classes[0].cinit = 1;
  const std::string text = fo4::abc::write_cs(file).text;
  EXPECT_TRUE(contains(text, "  public sealed override void onClick {\n    get {\n"));
  EXPECT_TRUE(contains(text, "  public static readonly void counter = true;\n"));
  EXPECT_TRUE(contains(text, "  public static void onClick() {\n  }\n"));
  EXPECT_TRUE(contains(text, "  static void __class_init(void click = 7) {\n"));
  EXPECT_LT(text.find("static readonly void counter"), text.find("static void __class_init"));
}

TEST(AbcDecompileCs, WritesRestParametersQuotedDefaultsAndExternMethods) {
  AbcFile file = class_file();
  file.methods[1].flags = 0x8C;
  file.bodies[1].local_count = 3;
  file.methods[1].options[0].kind = AbcValueKind::Utf8;
  file.methods[1].options[0].index = fo4::test::kStringFoo;
  const fo4::abc::AbcCsOutput out = fo4::abc::write_cs(file);
  EXPECT_TRUE(out.refused.empty());
  EXPECT_TRUE(contains(out.text,
                       "  public virtual void onClick(void click = \"Foo\", params object[] rest) {\n"
                       "    AddEventListener.AddEventListener(\"click\", handler);\n  }\n"));

  file = class_file();
  file.bodies.erase(file.bodies.begin() + 1);
  EXPECT_TRUE(contains(fo4::abc::write_cs(file).text, "  public virtual void onClick(void click = 7);\n"));
  file.methods[1].flags = 0xA8;
  EXPECT_TRUE(contains(fo4::abc::write_cs(file).text, "  public virtual extern void onClick(void click = 7);\n"));
}

TEST(AbcDecompileCs, KeepsTheSignatureOfARefusedMethodAndReportsTheRefusal) {
  AbcFile file = class_file();
  file.bodies[1].code = CodeBuilder{}.op_u30(0x40, 1).op(fo4::test::kOpPop).op(fo4::test::kOpReturnVoid).bytes();
  const fo4::abc::AbcCsOutput out = fo4::abc::write_cs(file);
  EXPECT_TRUE(contains(out.text,
                       "  public virtual void onClick(void click = 7) {\n    /* refused: UnsupportedOpcode at offset 0: "));
  ASSERT_EQ(out.refused.size(), 1u);
  EXPECT_EQ(out.refused[0].status, AbcDecompileStatus::UnsupportedOpcode);
  EXPECT_EQ(out.refused[0].method, 1u);
  EXPECT_TRUE(contains(out.text, "  public virtual void helper() {\n  }\n"));
}

TEST(AbcDecompileCs, CountsTheSameFunctionsAsTheAs3Writer) {
  AbcFile file = class_file();
  EXPECT_EQ(fo4::abc::write_cs(file).functions, 4u);
  EXPECT_EQ(fo4::abc::write_cs(file).functions, fo4::abc::write_as3(file).functions);
  file.bodies[1].code = CodeBuilder{}.op_u30(0x40, 1).op(fo4::test::kOpPop).op(fo4::test::kOpReturnVoid).bytes();
  EXPECT_EQ(fo4::abc::write_cs(file).functions, 4u);
  EXPECT_EQ(fo4::abc::write_cs(file).refused.size(), fo4::abc::write_as3(file).refused.size());
  file.bodies.erase(file.bodies.begin() + 1);
  EXPECT_EQ(fo4::abc::write_cs(file).functions, 3u);
}

TEST(AbcDecompileCs, ProducesTheSameTextAndHashOnEveryRun) {
  const AbcFile file = class_file();
  const fo4::abc::AbcCsOutput first = fo4::abc::write_cs(file);
  const fo4::abc::AbcCsOutput second = fo4::abc::write_cs(file);
  EXPECT_EQ(first.text, second.text);
  EXPECT_EQ(first.hash, second.hash);
  EXPECT_EQ(first.hash, fo4::abc::as3_text_hash(first.text));
  EXPECT_NE(first.hash, fo4::abc::write_as3(file).hash);
}

TEST(AbcDecompileCs, WritesAKnownFlashTypeAndMarksAnUnknownOneInASignature) {
  AbcFile file = class_file();
  file.methods[2].return_type = fo4::test::kMultinameExternalInterface;
  const fo4::abc::AbcCsOutput mapped = fo4::abc::write_cs(file);
  EXPECT_TRUE(contains(mapped.text, "  public virtual Flash.External.ExternalInterface helper() {\n  }\n"));
  EXPECT_TRUE(mapped.refused.empty());

  file.methods[2].return_type = fo4::test::kMultinameCall;
  const fo4::abc::AbcCsOutput unmapped = fo4::abc::write_cs(file);
  EXPECT_TRUE(contains(unmapped.text,
                       "  public virtual /* unmapped flash.external::call */ dynamic helper() {\n"
                       "    /* refused: UnmappedName: 'flash.external::call' in the signature has no C# mapping */\n"
                       "  }\n"));
  ASSERT_EQ(unmapped.refused.size(), 1u);
  EXPECT_EQ(unmapped.refused[0].status, AbcDecompileStatus::UnmappedName);
  EXPECT_EQ(unmapped.refused[0].method, 2u);
  EXPECT_EQ(unmapped.functions, 4u);
}

TEST(AbcDecompileCs, MarksAnUnmappedFieldTypeWithoutDroppingTheField) {
  AbcFile file = class_file();
  file.instances[0].traits[1].type_name = fo4::test::kMultinameCall;
  const fo4::abc::AbcCsOutput out = fo4::abc::write_cs(file);
  EXPECT_TRUE(contains(out.text, "  public /* unmapped flash.external::call */ dynamic counter;\n"));
  ASSERT_EQ(out.refused.size(), 1u);
  EXPECT_EQ(out.refused[0].method, fo4::abc::kNoFlowIndex);
}

TEST(AbcDecompileCs, ListsTheFlashNamesTheTableLacksSortedAndUnique) {
  AbcFile file = class_file();
  EXPECT_EQ(fo4::abc::cs_unmapped_names(file), std::vector<std::string>{"flash.external::call"});
  add_name(file, "flash.display", "Zork");
  add_name(file, "flash.display", "Sprite");
  const std::vector<std::string> expected{"flash.display::Zork", "flash.external::call"};
  EXPECT_EQ(fo4::abc::cs_unmapped_names(file), expected);
}

TEST(AbcDecompileCs, WritesAMappedFlashNameAndRefusesAnUnmappedOneInABody) {
  AbcFile file = class_file();
  const std::uint32_t sprite = add_name(file, "flash.display", "Sprite");
  file.bodies[0].code =
      CodeBuilder{}.op_u30(fo4::test::kOpGetLex, sprite).op(fo4::test::kOpPop).op(fo4::test::kOpReturnVoid).bytes();
  EXPECT_EQ(fo4::abc::write_cs_method(file, 0).text, "public Test() {\n  _ = Flash.Display.Sprite;\n}\n");

  const std::uint32_t zork = add_name(file, "flash.display", "Zork");
  file.bodies[0].code =
      CodeBuilder{}.op_u30(fo4::test::kOpGetLex, zork).op(fo4::test::kOpPop).op(fo4::test::kOpReturnVoid).bytes();
  const fo4::abc::AbcCsOutput out = fo4::abc::write_cs_method(file, 0);
  EXPECT_EQ(out.text,
            "public Test() {\n  /* refused: UnmappedName: 'flash.display::Zork' has no C# mapping */\n}\n");
  ASSERT_EQ(out.refused.size(), 1u);
  EXPECT_EQ(out.refused[0].status, AbcDecompileStatus::UnmappedName);
  EXPECT_EQ(out.refused[0].method, 0u);
}

TEST(AbcDecompileCs, WritesANonBooleanConditionThroughTheTruthinessHelper) {
  CodeBuilder plain;
  plain.op(fo4::test::kOpGetLocal1)
      .branch(fo4::test::kOpIfFalse, "END")
      .stmt(1)
      .label("END")
      .op(fo4::test::kOpReturnVoid);
  EXPECT_EQ(constructor_text(plain),
            "public Test() {\n  dynamic local1 = null;\n  if (Flash.Ops.ToBool(local1)) {\n    _ = 1;\n  }\n}\n");

  CodeBuilder both;
  both.op(fo4::test::kOpGetLocal1)
      .branch(fo4::test::kOpIfFalse, "END")
      .op(fo4::test::kOpGetLocal2)
      .branch(fo4::test::kOpIfFalse, "END")
      .stmt(1)
      .label("END")
      .op(fo4::test::kOpReturnVoid);
  EXPECT_EQ(constructor_text(both),
            "public Test() {\n  dynamic local1 = null;\n  dynamic local2 = null;\n"
            "  if (Flash.Ops.ToBool(local1) && Flash.Ops.ToBool(local2)) {\n    _ = 1;\n  }\n}\n");

  CodeBuilder negated;
  negated.op(fo4::test::kOpGetLocal1)
      .pushbyte(3)
      .branch(0x15, "END")
      .stmt(1)
      .label("END")
      .op(fo4::test::kOpReturnVoid);
  EXPECT_EQ(constructor_text(negated),
            "public Test() {\n  dynamic local1 = null;\n  if (!(local1 < 3)) {\n    _ = 1;\n  }\n}\n");
}

TEST(AbcDecompileCs, CastsTheDividendSoDivisionKeepsActionScriptSemantics) {
  CodeBuilder divide;
  divide.op(fo4::test::kOpGetLocal1).op(fo4::test::kOpGetLocal2).op(kDivide).op(fo4::test::kOpReturnValue);
  EXPECT_EQ(constructor_text(divide),
            "public Test() {\n  dynamic local1 = null;\n  dynamic local2 = null;\n  return (double)local1 / local2;\n}\n");
}

TEST(AbcDecompileCs, MovesTheSuperCallIntoTheConstructorInitializerAndRefusesOtherSuperCalls) {
  CodeBuilder with_argument;
  with_argument.op(fo4::test::kOpGetLocal0).pushbyte(5).op_u30(kConstructSuper, 1).op(fo4::test::kOpReturnVoid);
  EXPECT_EQ(constructor_text(with_argument), "public Test() : base(5) {\n}\n");

  CodeBuilder implicit;
  implicit.op(fo4::test::kOpGetLocal0).op_u30(kConstructSuper, 0).op(fo4::test::kOpReturnVoid);
  EXPECT_EQ(constructor_text(implicit), "public Test() {\n}\n");

  CodeBuilder late;
  late.stmt(1).op(fo4::test::kOpGetLocal0).op_u30(kConstructSuper, 0).op(fo4::test::kOpReturnVoid);
  const fo4::abc::AbcCsOutput out = fo4::abc::write_cs_method(constructor_file(late), 0);
  EXPECT_TRUE(contains(out.text, "/* refused: UnsupportedExpression: "));
  ASSERT_EQ(out.refused.size(), 1u);
}

TEST(AbcDecompileCs, WritesOneClassAloneAsItStandsInTheWholeText) {
  const AbcFile file = class_file();
  const fo4::abc::AbcCsOutput out = fo4::abc::write_cs_class(file, 0);
  EXPECT_EQ(out.text, kClassOnly);
  EXPECT_EQ(out.functions, 4u);
  EXPECT_EQ(out.hash, fo4::abc::as3_text_hash(kClassOnly));
  const fo4::abc::AbcCsOutput past = fo4::abc::write_cs_class(file, 1);
  EXPECT_TRUE(past.text.empty());
  EXPECT_EQ(past.hash, fo4::abc::as3_text_hash(""));
  EXPECT_TRUE(past.refused.empty());
}

TEST(AbcDecompileCs, WritesOneMethodAloneWithTheHeadItHasInItsClass) {
  AbcFile file = class_file();
  EXPECT_EQ(fo4::abc::write_cs_method(file, 0).text, "public Test() {\n}\n");
  EXPECT_EQ(fo4::abc::write_cs_method(file, 1).text,
            "public virtual void onClick(void click = 7) {\n"
            "  AddEventListener.AddEventListener(\"click\", handler);\n}\n");
  EXPECT_EQ(fo4::abc::write_cs_method(file, 2).text, "public virtual void helper() {\n}\n");
  EXPECT_EQ(fo4::abc::write_cs_method(file, 1).functions, 1u);

  file.methods.push_back(file.methods[2]);
  EXPECT_EQ(fo4::abc::write_cs_method(file, 3).text, "public static void __method3();\n");
  EXPECT_EQ(fo4::abc::write_cs_method(file, 3).functions, 0u);

  fo4::abc::AbcMethodBody body = file.bodies[2];
  body.method = 3;
  file.bodies.push_back(body);
  file.classes[0].cinit = 3;
  EXPECT_EQ(fo4::abc::write_cs_method(file, 3).text, "static void __class_init() {\n}\n");

  file.classes[0].cinit = 0;
  fo4::abc::AbcScriptInfo script;
  script.init = 3;
  file.scripts.push_back(script);
  EXPECT_EQ(fo4::abc::write_cs_method(file, 3).text, "public static void __script_init() {\n}\n");

  const fo4::abc::AbcCsOutput past = fo4::abc::write_cs_method(file, 99);
  EXPECT_TRUE(past.text.empty());
  EXPECT_TRUE(past.refused.empty());
}

TEST(AbcDecompileCs, ReportsAnUnmappedSignatureTypeOfAMethodWithoutABody) {
  AbcFile file = class_file();
  file.instances[0].flags = 0x05;
  file.bodies.erase(file.bodies.begin() + 1);
  file.methods[1].param_types[0] = add_name(file, "flash.display", "Zork");
  const fo4::abc::AbcCsOutput out = fo4::abc::write_cs(file);
  EXPECT_TRUE(contains(out.text, "  void onClick(/* unmapped flash.display::Zork */ dynamic click = 7);\n"));
  ASSERT_EQ(out.refused.size(), 1u);
  EXPECT_EQ(out.refused[0].status, AbcDecompileStatus::UnmappedName);
  EXPECT_EQ(out.refused[0].method, fo4::abc::kNoFlowIndex);
  EXPECT_TRUE(contains(out.refused[0].message, "method signature"));
}

TEST(AbcDecompileCs, MapsAPackageQualifiedCallNameThroughTheMemberTableLikeItsDeclaration) {
  AbcFile file = class_file();
  const std::uint32_t start = add_name(file, "com.foo", "start");
  file.bodies[0].code =
      CodeBuilder{}.op_u30(fo4::test::kOpGetLex, start).op(fo4::test::kOpPop).op(fo4::test::kOpReturnVoid).bytes();
  EXPECT_EQ(fo4::abc::write_cs_method(file, 0).text, "public Test() {\n  _ = com.foo.Start;\n}\n");
}

TEST(AbcDecompileCs, RefusesAsToAValueTypeBecauseCSharpAsNeedsAReferenceType) {
  AbcFile file = class_file();
  const std::uint32_t number = add_name(file, "", "int");
  file.bodies[0].code = CodeBuilder{}
                            .op(fo4::test::kOpGetLocal1)
                            .op_u30(0x86, number)
                            .op(fo4::test::kOpPop)
                            .op(fo4::test::kOpReturnVoid)
                            .bytes();
  const fo4::abc::AbcCsOutput out = fo4::abc::write_cs_method(file, 0);
  EXPECT_TRUE(contains(out.text, "/* refused: UnsupportedExpression: as to the value type int has no C# form */"));
  ASSERT_EQ(out.refused.size(), 1u);
  EXPECT_EQ(out.refused[0].method, 0u);
}

std::string read_text_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  std::string out = text.str();
  out.erase(std::remove(out.begin(), out.end(), '\r'), out.end());
  return out;
}

TEST(AbcDecompileCs, EmittedNamesMatchTheCheckedInFlashShimList) {
  const std::string listed = read_text_file(FO4_FLASH_SHIM_NAMES);
  ASSERT_FALSE(listed.empty()) << FO4_FLASH_SHIM_NAMES;
  std::string expected;
  for (const std::string& line : fo4::abc::cs_emitted_names()) {
    expected += line + "\n";
  }
  EXPECT_EQ(listed, expected);
}

TEST(AbcDecompileCs, EveryFlashTokenInTheWriterSourcesIsInTheShimList) {
  std::set<std::string> known;
  for (const std::string& line : fo4::abc::cs_emitted_names()) {
    known.insert(line.substr(line.find(' ') + 1));
  }
  const std::regex token("(Flash|Scaleform)\\.[A-Za-z_][A-Za-z0-9_.]*");
  int scanned = 0;
  for (const auto& entry : std::filesystem::directory_iterator(FO4_ABC_SRC_DIR)) {
    const std::string name = entry.path().filename().string();
    if (name.rfind("decompile_cs", 0) != 0 || entry.path().extension() != ".cpp") {
      continue;
    }
    ++scanned;
    const std::string text = read_text_file(entry.path().string());
    for (std::sregex_iterator it(text.begin(), text.end(), token), end; it != end; ++it) {
      std::string found = it->str();
      while (!found.empty() && found.back() == '.') {
        found.pop_back();
      }
      EXPECT_EQ(known.count(found), 1u) << name << " prints " << found;
    }
  }
  EXPECT_GE(scanned, 3);
}
