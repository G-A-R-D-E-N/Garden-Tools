#include "papyrus_codegen_fixture.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

using fo4::papyrus::DiagnosticCode;
using fo4::papyrus::codegen_test::CodegenFixture;
using fo4::papyrus::codegen_test::function;
using fo4::papyrus::codegen_test::listing;
using fo4::pex::PexDebugFunction;
using fo4::pex::PexFile;

TEST(PapyrusCodegen, BareScriptNamesScriptObjectAsItsParent) {
  CodegenFixture f;
  f.writeWithScriptObject();
  f.write("Bare", "Scriptname Bare\n");
  EXPECT_EQ(f.compileObject("Bare").parentClassName, "ScriptObject");
}

TEST(PapyrusCodegen, ScriptObjectItselfHasNoParent) {
  CodegenFixture f;
  f.writeWithScriptObject();
  EXPECT_TRUE(f.compileObject("ScriptObject").parentClassName.empty());
}

TEST(PapyrusCodegen, HeaderCarriesDocstringConstFlagAndUserFlags) {
  CodegenFixture f;
  f.writeWithScriptObject();
  f.write("Meta", "Scriptname Meta Const Hidden\n{What it is}\n");
  const auto obj = f.compileObject("Meta");

  EXPECT_EQ(obj.docString, "What it is");
  EXPECT_TRUE(obj.isConst);
  EXPECT_EQ(obj.userFlags, 1u);
}

TEST(PapyrusCodegen, AutoPropertyGetsBackingVariableAndReadWriteAutoVarFlags) {
  CodegenFixture f;
  f.writeWithScriptObject();
  f.write("Props", "Scriptname Props\nint Property Count = 3 Auto Mandatory\n");
  const auto obj = f.compileObject("Props");

  ASSERT_TRUE(obj.properties.size() == 1);
  const auto& property = obj.properties[0];
  EXPECT_EQ(property.flags, 0x07);
  EXPECT_EQ(property.autoVarName, "::Count_var");
  EXPECT_EQ(property.userFlags, 32u);

  ASSERT_TRUE(obj.variables.size() == 1);
  EXPECT_EQ(obj.variables[0].name, "::Count_var");
  EXPECT_EQ(obj.variables[0].type, "Int");
  ASSERT_TRUE(obj.variables[0].defaultValue.has_value());
  EXPECT_EQ(obj.variables[0].defaultValue->int_, 3);
}

TEST(PapyrusCodegen, AutoReadOnlyPropertyBecomesGetterReturningConstant) {
  CodegenFixture f;
  f.writeWithScriptObject();
  f.write("Consts", "Scriptname Consts\nint Property Mask = 0x00200000 AutoReadOnly\n");
  const auto obj = f.compileObject("Consts");

  EXPECT_TRUE(obj.variables.empty());
  ASSERT_TRUE(obj.properties.size() == 1);
  const auto& property = obj.properties[0];
  EXPECT_EQ(property.flags, 0x01);
  EXPECT_TRUE(property.autoVarName.empty());
  ASSERT_TRUE(property.readHandler.has_value());
  EXPECT_EQ(listing(*property.readHandler), (std::vector<std::string>{"return 2097152"}));
}

TEST(PapyrusCodegen, FullPropertyEmitsHandlersAndNoBackingVariable) {
  CodegenFixture f;
  f.writeWithScriptObject();
  f.write("Full",
          "Scriptname Full\nint mCount\nint Property Count\n    int Function Get()\n"
          "        return mCount\n    EndFunction\n    Function Set(int aiValue)\n"
          "        mCount = aiValue\n    EndFunction\nEndProperty\n");
  const auto obj = f.compileObject("Full");

  ASSERT_TRUE(obj.properties.size() == 1);
  const auto& property = obj.properties[0];
  EXPECT_EQ(property.flags, 0x03);
  EXPECT_FALSE(property.isAutoVar());
  ASSERT_TRUE(obj.variables.size() == 1);
  EXPECT_EQ(obj.variables[0].name, "mCount");
  ASSERT_TRUE(property.readHandler.has_value());
  ASSERT_TRUE(property.writeHandler.has_value());
  EXPECT_EQ(listing(*property.readHandler), (std::vector<std::string>{"return mCount"}));
  EXPECT_EQ(listing(*property.writeHandler), (std::vector<std::string>{"assign mCount aiValue"}));
}

TEST(PapyrusCodegen, StructKeepsMemberOrderInDebugStructOrderTable) {
  CodegenFixture f;
  f.writeWithScriptObject();
  f.write("WithStruct",
          "Scriptname WithStruct\nStruct Point\n    float fX = 0.0\n    float fY = 1.5\n"
          "EndStruct\n");
  const auto result = f.compile("WithStruct");
  EXPECT_TRUE(result.success);

  ASSERT_TRUE(result.pex != nullptr);
  ASSERT_TRUE(result.pex->structOrders.size() == 1);
  EXPECT_EQ(result.pex->structOrders[0].memberNames, (std::vector<std::string>{"fX", "fY"}));
  ASSERT_TRUE(result.pex->objects.size() == 1);
  ASSERT_TRUE(result.pex->objects[0].structs.size() == 1);
  const auto& members = result.pex->objects[0].structs[0].members;
  ASSERT_TRUE(members.size() == 2);
  EXPECT_EQ(members[0].type, "Float");
  EXPECT_EQ(members[1].type, "Float");
  ASSERT_TRUE(members[1].defaultValue.has_value());
  EXPECT_EQ(members[1].defaultValue->float_, 1.5F);
}

TEST(PapyrusCodegen, UngroupedPropertiesListedUnderUnnamedPropertyGroup) {
  CodegenFixture f;
  f.writeWithScriptObject();
  f.write("Grouped", "Scriptname Grouped\nint Property A Auto\nint Property B Auto\n");
  const auto result = f.compile("Grouped");

  ASSERT_TRUE(result.pex != nullptr);
  ASSERT_TRUE(result.pex->propertyGroups.size() == 1);
  EXPECT_TRUE(result.pex->propertyGroups[0].groupName.empty());
  EXPECT_EQ(result.pex->propertyGroups[0].propertyNames, (std::vector<std::string>{"A", "B"}));
}

TEST(PapyrusCodegen, StateOverrideLandsInOwnStateAndAutoStateIsNamed) {
  CodegenFixture f;
  f.writeWithScriptObject();
  f.write("States",
          "Scriptname States\nFunction Poke()\nEndFunction\nAuto State Idle\n    Function Poke()\n"
          "    EndFunction\nEndState\n");
  const auto obj = f.compileObject("States");

  EXPECT_EQ(obj.autoStateName, "Idle");
  ASSERT_TRUE(obj.states.size() == 2);
  EXPECT_TRUE(obj.states[0].name.empty());
  EXPECT_EQ(obj.states[1].name, "Idle");
  ASSERT_TRUE(obj.states[1].functions.size() == 1);
  EXPECT_EQ(obj.states[1].functions[0].name, "Poke");
}

TEST(PapyrusCodegen, LoneIfEmitsTrailingJumpToEnd) {
  CodegenFixture f;
  f.writeWithScriptObject();
  f.write("Flow",
          "Scriptname Flow\nFunction Go(bool abFlag)\n    if abFlag\n        Go(false)\n    endIf\n"
          "EndFunction\n");
  const auto obj = f.compileObject("Flow");

  EXPECT_EQ(listing(function(obj, "Go")),
            (std::vector<std::string>{"jmpf abFlag 3", "callmethod Go self ::nonevar false", "jmp 1"}));
}

TEST(PapyrusCodegen, IfElseJumpsOverElseAndElseFallsThrough) {
  CodegenFixture f;
  f.writeWithScriptObject();
  f.write("Flow2",
          "Scriptname Flow2\nFunction Go(bool abFlag)\n    if abFlag\n        Go(false)\n"
          "    else\n        Go(true)\n    endIf\nEndFunction\n");
  const auto obj = f.compileObject("Flow2");

  EXPECT_EQ(listing(function(obj, "Go")),
            (std::vector<std::string>{"jmpf abFlag 3", "callmethod Go self ::nonevar false", "jmp 2",
                                      "callmethod Go self ::nonevar true"}));
}

TEST(PapyrusCodegen, WhileLoopJumpsBackToCondition) {
  CodegenFixture f;
  f.writeWithScriptObject();
  f.write("Loop",
          "Scriptname Loop\nFunction Go()\n    int i = 0\n    while i < 3\n        i += 1\n"
          "    endWhile\nEndFunction\n");
  const auto obj = f.compileObject("Loop");

  EXPECT_EQ(listing(function(obj, "Go")),
            (std::vector<std::string>{"assign i 0", "cmp_lt T i 3", "jmpf T 3", "iadd i i 1",
                                      "jmp -3"}));
}

TEST(PapyrusCodegen, AndShortCircuitsThroughOneSharedSlot) {
  CodegenFixture f;
  f.writeWithScriptObject();
  f.write("Logic",
          "Scriptname Logic\nFunction Go(bool abLeft, bool abRight)\n    if abLeft && abRight\n"
          "        Go(false, false)\n    endIf\nEndFunction\n");
  const auto obj = f.compileObject("Logic");

  EXPECT_EQ(listing(function(obj, "Go")),
            (std::vector<std::string>{"cast T abLeft", "jmpf T 2", "cast T abRight", "jmpf T 3",
                                      "callmethod Go self ::nonevar false false", "jmp 1"}));
}

TEST(PapyrusCodegen, OrShortCircuitsOnTrue) {
  CodegenFixture f;
  f.writeWithScriptObject();
  f.write("Logic2",
          "Scriptname Logic2\nFunction Go(bool abLeft, bool abRight)\n    if abLeft || abRight\n"
          "        Go(false, false)\n    endIf\nEndFunction\n");
  const auto obj = f.compileObject("Logic2");

  const auto lines = listing(function(obj, "Go"));
  ASSERT_TRUE(lines.size() > 1);
  EXPECT_EQ(lines[1], "jmpt T 2");
}

TEST(PapyrusCodegen, ShortCircuitIntoOperandVariableStagesThroughTemp) {
  CodegenFixture f;
  f.writeWithScriptObject();
  f.write("Alias",
          "Scriptname Alias\nFunction Go(bool abLeft, bool abRight)\n    abLeft = abRight && abLeft\n"
          "EndFunction\nFunction GoOr(bool abLeft, bool abRight)\n    abLeft = abRight || abLeft\n"
          "EndFunction\nFunction Keep(bool abLeft, bool abRight)\n    bool bOut = abLeft && abRight\n"
          "EndFunction\n");
  const auto obj = f.compileObject("Alias");

  EXPECT_EQ(listing(function(obj, "Go")),
            (std::vector<std::string>{"cast T abRight", "jmpf T 2", "cast T abLeft",
                                      "assign abLeft T"}));
  EXPECT_EQ(listing(function(obj, "GoOr")),
            (std::vector<std::string>{"cast T abRight", "jmpt T 2", "cast T abLeft",
                                      "assign abLeft T"}));
  EXPECT_EQ(listing(function(obj, "Keep")),
            (std::vector<std::string>{"cast bOut abLeft", "jmpf bOut 2", "cast bOut abRight"}));
}

TEST(PapyrusCodegen, ShortCircuitIntoAutoPropertyStagesThroughBackingVariable) {
  CodegenFixture f;
  f.writeWithScriptObject();
  f.write("AutoAlias",
          "Scriptname AutoAlias\nBool Property bFlag Auto\nFunction Go(bool abOther)\n"
          "    bFlag = abOther && bFlag\nEndFunction\nFunction GoSelf(bool abOther)\n"
          "    bFlag = abOther && self.bFlag\nEndFunction\n");
  const auto obj = f.compileObject("AutoAlias");

  EXPECT_EQ(listing(function(obj, "Go")),
            (std::vector<std::string>{"cast T abOther", "jmpf T 2", "cast T ::bFlag_var",
                                      "assign ::bFlag_var T"}));
  EXPECT_EQ(listing(function(obj, "GoSelf")),
            (std::vector<std::string>{"cast T abOther", "jmpf T 3", "assign T ::bFlag_var",
                                      "cast T T", "assign ::bFlag_var T"}));
}

TEST(PapyrusCodegen, FindStructConvertsSearchValueToMemberType) {
  CodegenFixture f;
  f.writeWithScriptObject();
  f.write("Finder",
          "Scriptname Finder\nStruct Point\n    float fX = 0.0\nEndStruct\nFunction Go()\n"
          "    Point[] pts = new Point[2]\n    int i = pts.FindStruct(\"fX\", 2)\n"
          "    int j = pts.RFindStruct(\"fX\", 2)\nEndFunction\n");
  const auto obj = f.compileObject("Finder");

  EXPECT_EQ(listing(function(obj, "Go")),
            (std::vector<std::string>{"array_create pts 2", "array_findstruct pts i \"fX\" 2.0 0",
                                      "array_rfindstruct pts j \"fX\" 2.0 -1"}));
}

TEST(PapyrusCodegen, InequalityIsEqualityFollowedByNot) {
  CodegenFixture f;
  f.writeWithScriptObject();
  f.write("Cmp", "Scriptname Cmp\nFunction Go(int aiLeft)\n    bool b = aiLeft != 2\nEndFunction\n");
  const auto obj = f.compileObject("Cmp");

  EXPECT_EQ(listing(function(obj, "Go")),
            (std::vector<std::string>{"cmp_eq b aiLeft 2", "not b b"}));
}

TEST(PapyrusCodegen, MixedComparisonPromotesIntSideToFloat) {
  CodegenFixture f;
  f.writeWithScriptObject();
  f.write("Promote",
          "Scriptname Promote\nFunction Go(float afValue, int aiOther)\n    bool b = afValue > "
          "aiOther\nEndFunction\n");
  const auto obj = f.compileObject("Promote");

  EXPECT_EQ(listing(function(obj, "Go")),
            (std::vector<std::string>{"cast T aiOther", "cmp_gt b afValue T"}));
}

TEST(PapyrusCodegen, IntLiteralInFloatSlotIsFolded) {
  CodegenFixture f;
  f.writeWithScriptObject();
  f.write("Fold", "Scriptname Fold\nFunction Go()\n    float f = 5\nEndFunction\n");
  const auto obj = f.compileObject("Fold");

  EXPECT_EQ(listing(function(obj, "Go")), (std::vector<std::string>{"assign f 5.0"}));
}

TEST(PapyrusCodegen, StringConcatenationUsesStrcatAndCastsOtherSide) {
  CodegenFixture f;
  f.writeWithScriptObject();
  f.write("Cat",
          "Scriptname Cat\nFunction Go(int aiCount)\n    string s = \"n=\" + aiCount\n"
          "EndFunction\n");
  const auto obj = f.compileObject("Cat");

  EXPECT_EQ(listing(function(obj, "Go")),
            (std::vector<std::string>{"cast T aiCount", "strcat s \"n=\" T"}));
}

TEST(PapyrusCodegen, LocalWithNoInitialiserIsStillZeroed) {
  CodegenFixture f;
  f.writeWithScriptObject();
  f.write("Zero", "Scriptname Zero\nFunction Go()\n    int i\n    string s\nEndFunction\n");
  const auto obj = f.compileObject("Zero");

  EXPECT_EQ(listing(function(obj, "Go")),
            (std::vector<std::string>{"assign i 0", "assign s \"\""}));
}

TEST(PapyrusCodegen, CompoundAssignmentReadsModifiesAndWritesInPlace) {
  CodegenFixture f;
  f.writeWithScriptObject();
  f.write("Compound",
          "Scriptname Compound\nFunction Go()\n    float f = 1.0\n    f *= 2.0\nEndFunction\n");
  const auto obj = f.compileObject("Compound");

  EXPECT_EQ(listing(function(obj, "Go")),
            (std::vector<std::string>{"assign f 1.0", "fmul f f 2.0"}));
}

TEST(PapyrusCodegen, NewArrayAndElementAccessUseArrayOpcodes) {
  CodegenFixture f;
  f.writeWithScriptObject();
  f.write("Arrays",
          "Scriptname Arrays\nFunction Go()\n    int[] xs = new int[4]\n    xs[0] = 7\n"
          "    int n = xs.Length\n    xs.Add(9)\n    int at = xs.Find(9)\nEndFunction\n");
  const auto obj = f.compileObject("Arrays");

  EXPECT_EQ(listing(function(obj, "Go")),
            (std::vector<std::string>{"array_create xs 4", "array_setelement xs 0 7",
                                      "array_length n xs", "array_add xs 9 1",
                                      "array_findelement xs at 9 0"}));
}

TEST(PapyrusCodegen, StructMembersReadAndWrittenThroughStructOpcodes) {
  CodegenFixture f;
  f.writeWithScriptObject();
  f.write("Structs",
          "Scriptname Structs\nStruct Point\n    float fX = 0.0\nEndStruct\nFunction Go()\n"
          "    Point p = new Point\n    p.fX = 2.0\n    float got = p.fX\nEndFunction\n");
  const auto obj = f.compileObject("Structs");

  EXPECT_EQ(listing(function(obj, "Go")),
            (std::vector<std::string>{"struct_create p", "assign T 2.0", "struct_set p fX T",
                                      "struct_get got p fX"}));
}

TEST(PapyrusCodegen, IsOperatorNamesItsTypeAsIdentifier) {
  CodegenFixture f;
  f.writeWithScriptObject();
  f.write("Other", "Scriptname Other\n");
  f.write("Check",
          "Scriptname Check\nFunction Go(ScriptObject akThing)\n    bool b = akThing is Other\n"
          "EndFunction\n");
  const auto obj = f.compileObject("Check");

  EXPECT_EQ(listing(function(obj, "Go")), (std::vector<std::string>{"is b akThing Other"}));
}

TEST(PapyrusCodegen, OptionalArgumentsFilledInFromDeclaration) {
  CodegenFixture f;
  f.writeWithScriptObject();
  f.write("Defaults",
          "Scriptname Defaults\nFunction Target(int aiFirst, bool abSecond = true, string "
          "asThird = \"x\")\nEndFunction\nFunction Go()\n    Target(1)\nEndFunction\n");
  const auto obj = f.compileObject("Defaults");

  EXPECT_EQ(listing(function(obj, "Go")),
            (std::vector<std::string>{"callmethod Target self ::nonevar 1 true \"x\""}));
}

TEST(PapyrusCodegen, NamedArgumentPlacedByName) {
  CodegenFixture f;
  f.writeWithScriptObject();
  f.write("Named",
          "Scriptname Named\nFunction Target(int aiFirst = 1, int aiSecond = 2)\nEndFunction\n"
          "Function Go()\n    Target(aiSecond = 9)\nEndFunction\n");
  const auto obj = f.compileObject("Named");

  EXPECT_EQ(listing(function(obj, "Go")),
            (std::vector<std::string>{"callmethod Target self ::nonevar 1 9"}));
}

TEST(PapyrusCodegen, BareCallToOwnGlobalIsStaticCallOnThisScript) {
  CodegenFixture f;
  f.writeWithScriptObject();
  f.write("Globals",
          "Scriptname Globals\nint Function Helper() Global\n    return 1\nEndFunction\n"
          "int Function Go() Global\n    return Helper()\nEndFunction\n");
  const auto obj = f.compileObject("Globals");

  EXPECT_EQ(listing(function(obj, "Go")),
            (std::vector<std::string>{"callstatic Globals Helper T", "return T"}));
}

TEST(PapyrusCodegen, QualifiedGlobalCallNamesReceivingScript) {
  CodegenFixture f;
  f.writeWithScriptObject();
  f.write("Util", "Scriptname Util\nint Function Read() Global\n    return 2\nEndFunction\n");
  f.write("Caller", "Scriptname Caller\nFunction Go()\n    int n = Util.Read()\nEndFunction\n");
  const auto obj = f.compileObject("Caller");

  EXPECT_EQ(listing(function(obj, "Go")),
            (std::vector<std::string>{"callstatic Util Read n"}));
}

TEST(PapyrusCodegen, ParentCallUsesCallparent) {
  CodegenFixture f;
  f.writeWithScriptObject();
  f.write("Base", "Scriptname Base\nFunction Poke()\nEndFunction\n");
  f.write("Derived",
          "Scriptname Derived extends Base\nFunction Poke()\n    Parent.Poke()\nEndFunction\n");
  const auto obj = f.compileObject("Derived");

  EXPECT_EQ(listing(function(obj, "Poke")),
            (std::vector<std::string>{"callparent Poke ::nonevar"}));
}

TEST(PapyrusCodegen, MethodCallReceiverEvaluatedBeforeArguments) {
  CodegenFixture f;
  f.writeWithScriptObject();
  f.write("Order",
          "Scriptname Order\nOrder Function First() Global\n    return None\nEndFunction\n"
          "int Function Second() Global\n    return 1\nEndFunction\nFunction Take(int aiValue)\n"
          "EndFunction\nFunction Go() Global\n    First().Take(Second())\nEndFunction\n");
  const auto obj = f.compileObject("Order");

  EXPECT_EQ(listing(function(obj, "Go")),
            (std::vector<std::string>{"callstatic Order First T", "callstatic Order Second T",
                                      "callmethod Take T ::nonevar T"}));
}

TEST(PapyrusCodegen, OwnAutoPropertyReadAndWrittenThroughBackingVariable) {
  CodegenFixture f;
  f.writeWithScriptObject();
  f.write("Self",
          "Scriptname Self\nint Property Count Auto\nFunction Go()\n    Count = Count + 1\n"
          "EndFunction\n");
  const auto obj = f.compileObject("Self");

  EXPECT_EQ(listing(function(obj, "Go")),
            (std::vector<std::string>{"iadd ::Count_var ::Count_var 1"}));
}

TEST(PapyrusCodegen, OtherObjectPropertyGoesThroughPropgetAndPropset) {
  CodegenFixture f;
  f.writeWithScriptObject();
  f.write("Holder", "Scriptname Holder\nint Property Count Auto\n");
  f.write("User",
          "Scriptname User\nFunction Go(Holder akHolder)\n    akHolder.Count = 5\n"
          "    int n = akHolder.Count\nEndFunction\n");
  const auto obj = f.compileObject("User");

  EXPECT_EQ(listing(function(obj, "Go")),
            (std::vector<std::string>{"assign T 5", "propset Count akHolder T",
                                      "propget Count akHolder n"}));
}

TEST(PapyrusCodegen, RemoteEventHandlerIsNamedAfterItsTypeAndEvent) {
  CodegenFixture f;
  f.writeWithScriptObject();
  f.write("Sender", "Scriptname Sender\n");
  f.write("Listener",
          "Scriptname Listener\nEvent Sender.OnSomething(Sender akSender)\nEndEvent\n");
  const auto obj = f.compileObject("Listener");

  ASSERT_TRUE(obj.states.size() == 1);
  ASSERT_TRUE(obj.states[0].functions.size() == 1);
  EXPECT_EQ(obj.states[0].functions[0].name, "::remote_Sender_OnSomething");
}

TEST(PapyrusCodegen, VoidCallWrittenAsStatementDiscardsIntoNoneLocal) {
  CodegenFixture f;
  f.writeWithScriptObject();
  f.write("Void",
          "Scriptname Void\nint Function Answer()\n    return 1\nEndFunction\n"
          "Function Nothing()\nEndFunction\nFunction Go()\n    Nothing()\n    Answer()\n"
          "EndFunction\n");
  const auto obj = f.compileObject("Void");
  const auto& fn = function(obj, "Go");

  EXPECT_EQ(listing(fn), (std::vector<std::string>{"callmethod Nothing self ::nonevar",
                                                   "callmethod Answer self T"}));
  bool hasNoneVar = false;
  for (const auto& l : fn.locals) {
    if (l.name == "::nonevar" && l.type == "None") hasNoneVar = true;
  }
  EXPECT_TRUE(hasNoneVar);
}

TEST(PapyrusCodegen, CallIntoScriptNotOnRootsIsRefusedRatherThanGuessed) {
  CodegenFixture f;
  f.writeWithScriptObject();
  f.write("Blind",
          "Scriptname Blind extends MissingBase\nFunction Go()\n    SomeInheritedThing()\n"
          "EndFunction\n");
  const auto result = f.compile("Blind");

  EXPECT_FALSE(result.success);
  EXPECT_FALSE(result.sourcesComplete);
  EXPECT_TRUE(result.pex == nullptr);
}

TEST(PapyrusCodegen, ArrayBuiltinWithWrongArgumentCountIsRefusedNotThrown) {
  const std::string_view calls[] = {"xs.Add()", "xs.Insert(1)", "xs.Remove()", "xs.Clear(1)",
                                    "xs.RemoveLast(1)"};
  for (const auto call : calls) {
    CodegenFixture f;
    f.writeWithScriptObject();
    f.write("BadArray",
            "Scriptname BadArray\nFunction Go()\n    int[] xs = new int[2]\n    " +
                std::string(call) + "\nEndFunction\n");
    const auto result = f.compile("BadArray");

    EXPECT_FALSE(result.success);
    bool found = false;
    for (const auto& d : result.diagnostics) {
      if (d.code == DiagnosticCode::ArgumentCount) found = true;
    }
    EXPECT_TRUE(found);
  }
}

TEST(PapyrusCodegen, ArrayBuiltinWithLegalArgumentCountStillCompiles) {
  const std::string_view calls[] = {"xs.Find(1)",  "xs.Find(1, 2)",  "xs.Add(1)",
                                    "xs.Add(1, 3)", "xs.Insert(1, 0)", "xs.Remove(0)",
                                    "xs.RemoveLast()", "xs.Clear()"};
  for (const auto call : calls) {
    CodegenFixture f;
    f.writeWithScriptObject();
    f.write("GoodArray",
            "Scriptname GoodArray\nFunction Go()\n    int[] xs = new int[2]\n    " +
                std::string(call) + "\nEndFunction\n");
    const auto result = f.compile("GoodArray");
    EXPECT_TRUE(result.success);
  }
}

TEST(PapyrusCodegen, ArrayBuiltinWithNoOpcodeIsRefusedByName) {
  CodegenFixture f;
  f.writeWithScriptObject();
  f.write("NoOpcode",
          "Scriptname NoOpcode\nStruct Pair\n    int iKey = 0\nEndStruct\nFunction Go()\n"
          "    Pair[] xs = new Pair[2]\n    Pair[] hits = xs.GetMatchingStructs(\"iKey\", 1)\n"
          "EndFunction\n");
  const auto result = f.compile("NoOpcode");

  EXPECT_FALSE(result.success);
  bool found = false;
  for (const auto& d : result.diagnostics) {
    if (d.message.find("GetMatchingStructs") != std::string::npos) found = true;
  }
  EXPECT_TRUE(found);
}

TEST(PapyrusCodegen, NonLiteralVariableInitialiserIsRefused) {
  CodegenFixture f;
  f.writeWithScriptObject();
  f.write("BadInit", "Scriptname BadInit\nint Property Count = 1 + 1 Auto\n");
  const auto result = f.compile("BadInit");

  EXPECT_FALSE(result.success);
  bool found = false;
  for (const auto& d : result.diagnostics) {
    if (d.code == DiagnosticCode::NonConstantInitializer) found = true;
  }
  EXPECT_TRUE(found);
}

TEST(PapyrusCodegen, GeneratedFileWritesAndReadsBackIdentically) {
  CodegenFixture f;
  f.writeWithScriptObject();
  f.write("RoundTrip",
          "Scriptname RoundTrip\n{A script with most of the shapes in it}\nStruct Pair\n"
          "    int iKey = 0\n    string sValue = \"\"\nEndStruct\n"
          "int Property Count = 1 Auto Hidden\nint Property Mask = 8 AutoReadOnly\n"
          "string mName\nEvent OnInit()\n    Count += 1\n    Pair p = new Pair\n"
          "    p.iKey = Count\n    if Count > 2 && mName != \"\"\n        mName = \"big\"\n"
          "    else\n        mName = \"small\"\n    endIf\n    int i = 0\n"
          "    while i < Count\n        i += 1\n    endWhile\nEndEvent\n");
  const auto result = f.compile("RoundTrip");
  EXPECT_TRUE(result.success);

  ASSERT_TRUE(result.pex != nullptr);
  const auto bytes = result.pex->toBytes();
  const auto reread = PexFile::fromBytes(bytes);
  EXPECT_TRUE(reread.toBytes() == bytes);

  ASSERT_TRUE(reread.objects.size() == 1);
  EXPECT_EQ(reread.objects[0].name, "RoundTrip");
  EXPECT_TRUE(reread.hasDebugInfo);
  bool hasOnInit = false;
  for (const auto& df : reread.debugFunctions) {
    if (df.functionName == "OnInit") hasOnInit = true;
  }
  EXPECT_TRUE(hasOnInit);
}

TEST(PapyrusCodegen, DebugLineNumbersRecordedOnePerInstruction) {
  CodegenFixture f;
  f.writeWithScriptObject();
  f.write("Lines", "Scriptname Lines\nFunction Go()\n    int a = 1\n    int b = 2\nEndFunction\n");
  const auto result = f.compile("Lines");

  ASSERT_TRUE(result.pex != nullptr);
  const PexDebugFunction* debug = nullptr;
  for (const auto& df : result.pex->debugFunctions) {
    if (df.functionName == "Go") debug = &df;
  }
  ASSERT_TRUE(debug != nullptr);
  EXPECT_EQ(debug->lineNumbers, (std::vector<std::uint16_t>{3, 4}));
}

TEST(PapyrusCodegen, DebugOnlyCallIsDroppedOnlyWhenAskedFor) {
  CodegenFixture f;
  f.writeWithScriptObject();
  f.write("Logger",
          "Scriptname Logger Native DebugOnly Hidden\nFunction Say(string asText) Global\n"
          "EndFunction\n");
  f.write("Talker", "Scriptname Talker\nFunction Go()\n    Logger.Say(\"hello\")\nEndFunction\n");

  const auto kept = f.compile("Talker", /*emitDebugOnly=*/true);
  ASSERT_TRUE(kept.success);
  ASSERT_TRUE(kept.pex != nullptr);
  EXPECT_EQ(listing(function(kept.pex->objects[0], "Go")),
            (std::vector<std::string>{"callstatic Logger Say ::nonevar \"hello\""}));

  const auto stripped = f.compile("Talker", /*emitDebugOnly=*/false);
  ASSERT_TRUE(stripped.success);
  ASSERT_TRUE(stripped.pex != nullptr);
  EXPECT_TRUE(listing(function(stripped.pex->objects[0], "Go")).empty());
}
