#include "papyrus_codegen_fixture.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using fo4::papyrus::codegen_test::CodegenFixture;
using fo4::papyrus::codegen_test::function;
using fo4::papyrus::codegen_test::listing;

// Invariant: an else-if chain sends each failed test to the next and each taken branch to the end.
TEST(PapyrusCodegen, ElseIfChainJumpsFromEachBranchToTheEndOfTheChain) {
  CodegenFixture f;
  f.writeWithScriptObject();
  f.write("Chain",
          "Scriptname Chain\nFunction Go(bool abA, bool abB)\n    if abA\n        Log(1)\n"
          "    elseif abB\n        Log(2)\n    else\n        Log(3)\n    endIf\nEndFunction\n"
          "Function Log(int aiWhich)\nEndFunction\n");
  const auto obj = f.compileObject("Chain");

  EXPECT_EQ(listing(function(obj, "Go")),
            (std::vector<std::string>{"jmpf abA 3", "callmethod Log self ::nonevar 1", "jmp 5",
                                      "jmpf abB 3", "callmethod Log self ::nonevar 2", "jmp 2",
                                      "callmethod Log self ::nonevar 3"}));
}

// Invariant: a return inside a loop body keeps the loop exit and back-jump offsets on the right instructions.
TEST(PapyrusCodegen, ReturnInsideALoopKeepsTheExitAndBackJumpOffsets) {
  CodegenFixture f;
  f.writeWithScriptObject();
  f.write("Search",
          "Scriptname Search\nint Function Find(int aiLimit)\n    int i = 0\n    while i < aiLimit\n"
          "        if i == 2\n            return i\n        endIf\n        i += 1\n    endWhile\n"
          "    return i\nEndFunction\n");
  const auto obj = f.compileObject("Search");

  EXPECT_EQ(listing(function(obj, "Find")),
            (std::vector<std::string>{"assign i 0", "cmp_lt T i aiLimit", "jmpf T 7", "cmp_eq T i 2",
                                      "jmpf T 3", "return i", "jmp 1", "iadd i i 1", "jmp -7",
                                      "return i"}));
}

// Invariant: temps freed by one statement are reused by the next, so locals do not grow per statement.
TEST(PapyrusCodegen, TemporariesFreedByOneStatementAreReusedByTheNext) {
  CodegenFixture f;
  f.writeWithScriptObject();
  f.write("Temps",
          "Scriptname Temps\nFunction Go(int aiX)\n    float a = aiX * 1.5\n"
          "    float b = aiX * 2.5\nEndFunction\n");
  const auto obj = f.compileObject("Temps");
  const auto& fn = function(obj, "Go");

  EXPECT_EQ(listing(fn), (std::vector<std::string>{"cast T aiX", "fmul a T 1.5", "cast T aiX",
                                                   "fmul b T 2.5"}));
  int temps = 0;
  for (const auto& local : fn.locals) {
    if (local.name.rfind("::temp", 0) == 0) ++temps;
  }
  EXPECT_EQ(temps, 1);
}

// Invariant: an Auto backing variable holds its literal converted to the property type, else zero.
TEST(PapyrusCodegen, AutoBackingVariableDefaultsConvertTheLiteralOrFallBackToZero) {
  CodegenFixture f;
  f.writeWithScriptObject();
  f.write("Backing", "Scriptname Backing\nfloat Property Scale = 2 Auto\nstring Property Label Auto\n");
  const auto obj = f.compileObject("Backing");

  ASSERT_TRUE(obj.variables.size() == 2);
  EXPECT_EQ(obj.variables[0].name, "::Scale_var");
  ASSERT_TRUE(obj.variables[0].defaultValue.has_value());
  EXPECT_EQ(obj.variables[0].defaultValue->type, fo4::pex::PexValueType::Float);
  EXPECT_EQ(obj.variables[0].defaultValue->float_, 2.0F);
  EXPECT_EQ(obj.variables[1].name, "::Label_var");
  ASSERT_TRUE(obj.variables[1].defaultValue.has_value());
  EXPECT_EQ(obj.variables[1].defaultValue->str, "");
}

// Invariant: an AutoReadOnly getter returns its literal in the declared type (int literal to float).
TEST(PapyrusCodegen, AutoReadOnlyGettersReturnTheLiteralInTheDeclaredType) {
  CodegenFixture f;
  f.writeWithScriptObject();
  f.write("Consts2",
          "Scriptname Consts2\nfloat Property Scale = 2 AutoReadOnly\n"
          "string Property Label = \"hi\" AutoReadOnly\n");
  const auto obj = f.compileObject("Consts2");

  ASSERT_TRUE(obj.properties.size() == 2);
  ASSERT_TRUE(obj.properties[0].readHandler.has_value());
  ASSERT_TRUE(obj.properties[1].readHandler.has_value());
  EXPECT_EQ(listing(*obj.properties[0].readHandler), (std::vector<std::string>{"return 2.0"}));
  EXPECT_EQ(listing(*obj.properties[1].readHandler), (std::vector<std::string>{"return \"hi\""}));
}

// Invariant: an explicit "as" cast between int, float and string emits exactly one cast into its destination.
TEST(PapyrusCodegen, ExplicitIntToStringAndFloatToIntCastsEmitOneCastEach) {
  CodegenFixture f;
  f.writeWithScriptObject();
  f.write("Casts",
          "Scriptname Casts\nFunction Go(int aiX, float afY)\n    string s = aiX as string\n"
          "    int i = afY as int\nEndFunction\n");
  const auto obj = f.compileObject("Casts");

  EXPECT_EQ(listing(function(obj, "Go")), (std::vector<std::string>{"cast s aiX", "cast i afY"}));
}

// Invariant: an int literal beside a float operand folds to a float constant instead of an emitted cast.
TEST(PapyrusCodegen, IntLiteralsBesideFloatOperandsFoldToFloatConstants) {
  CodegenFixture f;
  f.writeWithScriptObject();
  f.write("Fold2",
          "Scriptname Fold2\nFunction Go(float afValue)\n    float f = afValue * 2\n"
          "    bool b = afValue == 2\n    float g = -2\nEndFunction\n");
  const auto obj = f.compileObject("Fold2");

  EXPECT_EQ(listing(function(obj, "Go")),
            (std::vector<std::string>{"fmul f afValue 2.0", "cmp_eq b afValue 2.0",
                                      "assign g -2.0"}));
}

// Invariant: a chained concatenation casts each non-string operand once and chains through a temp.
TEST(PapyrusCodegen, ChainedConcatenationCastsTheIntOperandAndChainsThroughATemp) {
  CodegenFixture f;
  f.writeWithScriptObject();
  f.write("Chain2",
          "Scriptname Chain2\nFunction Go(int aiCount)\n    string s = \"n=\" + aiCount + \"!\"\n"
          "EndFunction\n");
  const auto obj = f.compileObject("Chain2");

  EXPECT_EQ(listing(function(obj, "Go")),
            (std::vector<std::string>{"cast T aiCount", "strcat T \"n=\" T", "strcat s T \"!\""}));
}

// Invariant: states keep source order after the empty state, and only the Auto State is the auto state.
TEST(PapyrusCodegen, StatesKeepSourceOrderAfterTheEmptyStateAndOnlyOneIsAuto) {
  CodegenFixture f;
  f.writeWithScriptObject();
  f.write("Ordered",
          "Scriptname Ordered\nState Busy\n    Function Poke()\n    EndFunction\nEndState\n"
          "Auto State Idle\nEndState\n");
  const auto obj = f.compileObject("Ordered");

  EXPECT_EQ(obj.autoStateName, "Idle");
  ASSERT_TRUE(obj.states.size() == 3);
  EXPECT_TRUE(obj.states[0].name.empty());
  EXPECT_EQ(obj.states[1].name, "Busy");
  EXPECT_EQ(obj.states[2].name, "Idle");
}

// Invariant: a named Group lists only its own properties and the ungrouped ones form the unnamed group.
TEST(PapyrusCodegen, NamedGroupListsItsOwnPropertiesApartFromUngroupedOnes) {
  CodegenFixture f;
  f.writeWithScriptObject();
  f.write("Groups",
          "Scriptname Groups\nGroup Visuals\nint Property A Auto\nint Property B Auto\nEndGroup\n"
          "int Property C Auto\n");
  const auto result = f.compile("Groups");

  ASSERT_TRUE(result.pex != nullptr);
  ASSERT_TRUE(result.pex->propertyGroups.size() == 2);
  EXPECT_TRUE(result.pex->propertyGroups[0].groupName.empty());
  EXPECT_EQ(result.pex->propertyGroups[0].propertyNames, (std::vector<std::string>{"C"}));
  EXPECT_EQ(result.pex->propertyGroups[1].groupName, "Visuals");
  EXPECT_EQ(result.pex->propertyGroups[1].propertyNames, (std::vector<std::string>{"A", "B"}));
}

// Invariant: a CustomEventName literal is qualified by the script that declares the event.
TEST(PapyrusCodegen, CustomEventNameArgumentIsQualifiedByTheDeclaringScript) {
  CodegenFixture f;
  f.writeWithScriptObject();
  f.write("Sender", "Scriptname Sender\nCustomEvent Ready\n");
  f.write("Util",
          "Scriptname Util\nFunction Hook(Sender akSender, CustomEventName asEvent) Global\n"
          "EndFunction\n");
  f.write("Caller",
          "Scriptname Caller\nFunction Go(Sender akSender)\n    Util.Hook(akSender, \"Ready\")\n"
          "EndFunction\n");
  const auto obj = f.compileObject("Caller");

  EXPECT_EQ(listing(function(obj, "Go")),
            (std::vector<std::string>{"callstatic Util Hook ::nonevar akSender \"sender_Ready\""}));
}

// Invariant: a struct member read or written through an array element goes via that element's temp.
TEST(PapyrusCodegen, StructMemberOfAnArrayElementIsReadAndWrittenThroughATemp) {
  CodegenFixture f;
  f.writeWithScriptObject();
  f.write("Elements",
          "Scriptname Elements\nStruct Point\n    float fX = 0.0\nEndStruct\nFunction Go()\n"
          "    Point[] pts = new Point[2]\n    pts[0].fX = 2.0\n    float got = pts[1].fX\n"
          "EndFunction\n");
  const auto obj = f.compileObject("Elements");

  EXPECT_EQ(listing(function(obj, "Go")),
            (std::vector<std::string>{"array_create pts 2", "assign T 2.0",
                                      "array_getelement T pts 0", "struct_set T fX T",
                                      "array_getelement T pts 1", "struct_get got T fX"}));
}

// Invariant: FindStruct and Insert emit their own opcodes, FindStruct with the default start of 0.
TEST(PapyrusCodegen, FindStructAndInsertUseTheirOpcodesAndDefaultStart) {
  CodegenFixture f;
  f.writeWithScriptObject();
  f.write("Search2",
          "Scriptname Search2\nStruct Point\n    float fX = 0.0\nEndStruct\nFunction Go(Point akPoint)\n"
          "    Point[] pts = new Point[2]\n    int at = pts.FindStruct(\"fX\", 2.0)\n"
          "    pts.Insert(akPoint, 0)\nEndFunction\n");
  const auto obj = f.compileObject("Search2");

  EXPECT_EQ(listing(function(obj, "Go")),
            (std::vector<std::string>{"array_create pts 2", "array_findstruct pts at \"fX\" 2.0 0",
                                      "array_insert pts akPoint 0"}));
}
