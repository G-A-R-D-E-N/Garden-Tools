#include <fo4/papyrus/ast.hpp>
#include <fo4/papyrus/parser.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

using fo4::papyrus::AssignStatement;
using fo4::papyrus::BinaryExpression;
using fo4::papyrus::CallExpression;
using fo4::papyrus::CastExpression;
using fo4::papyrus::DefineStatement;
using fo4::papyrus::DiagnosticCode;
using fo4::papyrus::Expression;
using fo4::papyrus::ExpressionStatement;
using fo4::papyrus::IdentifierExpression;
using fo4::papyrus::IfStatement;
using fo4::papyrus::IndexExpression;
using fo4::papyrus::LiteralExpression;
using fo4::papyrus::MemberExpression;
using fo4::papyrus::NewArrayExpression;
using fo4::papyrus::NewStructExpression;
using fo4::papyrus::Node;
using fo4::papyrus::Parser;
using fo4::papyrus::PropertyKind;
using fo4::papyrus::ReturnStatement;
using fo4::papyrus::Script;
using fo4::papyrus::Severity;
using fo4::papyrus::Statement;
using fo4::papyrus::TokenKind;
using fo4::papyrus::TypeCheckExpression;
using fo4::papyrus::UnaryExpression;
using fo4::papyrus::WhileStatement;

template <typename T>
const T* as(const Node* node) {
  return dynamic_cast<const T*>(node);
}

Script parse(std::string_view source) {
  return Parser::parse(source);
}

Script parse_clean(std::string_view source) {
  auto script = Parser::parse(source);
  for (const auto& diagnostic : script.diagnostics) {
    if (diagnostic.severity == Severity::Error) {
      ADD_FAILURE() << "the source is valid Papyrus but produced: " << diagnostic.toString();
    }
  }
  return script;
}

bool has_code(const Script& script, DiagnosticCode code) {
  for (const auto& diagnostic : script.diagnostics) {
    if (diagnostic.code == code) return true;
  }
  return false;
}

}  // namespace

// Port of PET's native PapyrusParserTests: every name below must keep stating the behaviour it pins.

TEST(PapyrusParser, HeaderCapturesNameParentAndFlags) {
  const auto script = parse_clean("ScriptName HiddenScript Extends ParentScript Hidden\n");
  EXPECT_EQ(script.name, "HiddenScript");
  ASSERT_TRUE(script.extends.has_value());
  EXPECT_EQ(*script.extends, "ParentScript");
  EXPECT_EQ(script.flags, std::vector<std::string>{"hidden"});
}

TEST(PapyrusParser, HeaderAcceptsANamespacedScriptName) {
  EXPECT_EQ(parse_clean("ScriptName Traps:Triggers:TripWire\n").name, "Traps:Triggers:TripWire");
}

TEST(PapyrusParser, HeaderDocCommentIsAttachedToTheScript) {
  const auto script = parse_clean("ScriptName MyCoolScript\n{Docs here\nand here}\n");
  ASSERT_TRUE(script.documentation.has_value());
  EXPECT_EQ(*script.documentation, "Docs here\nand here");
}

TEST(PapyrusParser, MissingHeaderIsReported) {
  EXPECT_TRUE(has_code(parse("Function Foo()\nEndFunction\n"), DiagnosticCode::ExpectedScriptName));
}

TEST(PapyrusParser, ImportsAreCollected) {
  const auto script = parse_clean("ScriptName S\nimport Utility\nimport MyNamespace:MyScript\n");
  ASSERT_EQ(script.imports.size(), 2u);
  EXPECT_EQ(script.imports[0].name, "Utility");
  EXPECT_EQ(script.imports[1].name, "MyNamespace:MyScript");
}

TEST(PapyrusParser, ScriptVariableWithInitializerAndFlags) {
  const auto script = parse_clean("ScriptName S\nint myValue = 0 Conditional\n");
  ASSERT_EQ(script.variables.size(), 1u);
  EXPECT_EQ(script.variables[0].name, "myValue");
  EXPECT_EQ(script.variables[0].type.name, "int");
  EXPECT_NE(as<LiteralExpression>(script.variables[0].initializer.get()), nullptr);
  EXPECT_EQ(script.variables[0].flags, std::vector<std::string>{"conditional"});
}

TEST(PapyrusParser, ArrayTypeIsRecognised) {
  const auto script = parse_clean("ScriptName S\nint[] myArray\n");
  ASSERT_EQ(script.variables.size(), 1u);
  EXPECT_TRUE(script.variables[0].type.isArray);
  EXPECT_EQ(script.variables[0].type.toString(), "int[]");
}

TEST(PapyrusParser, AutoPropertyWithInitializer) {
  const auto script = parse_clean("ScriptName S\nfloat Property MyProperty = 1.0 Auto\n");
  ASSERT_EQ(script.properties.size(), 1u);
  EXPECT_EQ(script.properties[0].kind, PropertyKind::Auto);
  EXPECT_EQ(script.properties[0].type.name, "float");
  EXPECT_NE(as<LiteralExpression>(script.properties[0].initializer.get()), nullptr);
}

TEST(PapyrusParser, AutoReadOnlyProperty) {
  const auto script =
      parse_clean("ScriptName S\nstring Property Hello = \"Hello world!\" AutoReadOnly\n");
  ASSERT_EQ(script.properties.size(), 1u);
  EXPECT_EQ(script.properties[0].kind, PropertyKind::AutoReadOnly);
}

TEST(PapyrusParser, FullPropertyCollectsItsGetAndSet) {
  const auto script = parse_clean(
      "ScriptName S\n"
      "int Property ValueProperty\n"
      "  Function Set(int newValue)\n"
      "    myValue = newValue\n"
      "  EndFunction\n"
      "  int Function Get()\n"
      "    return myValue\n"
      "  EndFunction\n"
      "EndProperty\n");
  ASSERT_EQ(script.properties.size(), 1u);
  const auto& property = script.properties[0];
  EXPECT_EQ(property.kind, PropertyKind::Full);
  ASSERT_TRUE(property.setter.has_value());
  ASSERT_TRUE(property.getter.has_value());
  ASSERT_TRUE(property.getter->returnType.has_value());
  EXPECT_EQ(property.getter->returnType->name, "int");
}

TEST(PapyrusParser, FullPropertyWithoutAnAccessorIsReported) {
  EXPECT_TRUE(has_code(parse("ScriptName S\nint Property P\nEndProperty\n"),
                       DiagnosticCode::PropertyNeedsAccessor));
}

TEST(PapyrusParser, GroupPropertiesAreListedOnBothTheGroupAndTheScript) {
  const auto script = parse_clean(
      "ScriptName S\n"
      "Group MyGroup CollapsedOnRef\n"
      "{A group}\n"
      "  int Property FirstProperty auto\n"
      "  float Property SecondProperty auto\n"
      "EndGroup\n");
  ASSERT_EQ(script.groups.size(), 1u);
  EXPECT_EQ(script.groups[0].flags, std::vector<std::string>{"collapsedonref"});
  ASSERT_TRUE(script.groups[0].documentation.has_value());
  EXPECT_EQ(*script.groups[0].documentation, "A group");
  ASSERT_EQ(script.groups[0].properties.size(), 2u);
  ASSERT_EQ(script.properties.size(), 2u);
  for (const auto& property : script.properties) {
    ASSERT_TRUE(property.groupName.has_value());
    EXPECT_EQ(*property.groupName, "MyGroup");
  }
}

TEST(PapyrusParser, StructMembersKeepTheirDocStrings) {
  const auto script = parse_clean(
      "ScriptName S\n"
      "struct QuestStage\n"
      "  Quest QuestToSet\n"
      "  {The quest whose stage is to be set}\n"
      "  int StageToSet\n"
      "  {The stage to set on the quest}\n"
      "endStruct\n");
  ASSERT_EQ(script.structs.size(), 1u);
  ASSERT_EQ(script.structs[0].members.size(), 2u);
  EXPECT_EQ(script.structs[0].members[0].name, "QuestToSet");
  EXPECT_EQ(script.structs[0].members[1].name, "StageToSet");
  ASSERT_TRUE(script.structs[0].members[0].documentation.has_value());
  EXPECT_EQ(*script.structs[0].members[0].documentation, "The quest whose stage is to be set");
}

TEST(PapyrusParser, EmptyStructIsReported) {
  EXPECT_TRUE(has_code(parse("ScriptName S\nstruct Empty\nendStruct\n"),
                       DiagnosticCode::StructNeedsMember));
}

TEST(PapyrusParser, CustomEventDefinition) {
  const auto script = parse_clean("ScriptName S\nCustomEvent MyCustomEvent\n");
  ASSERT_EQ(script.customEvents.size(), 1u);
  EXPECT_EQ(script.customEvents[0].name, "MyCustomEvent");
}

TEST(PapyrusParser, FunctionHeaderWithReturnTypeParametersAndGlobalFlag) {
  const auto script = parse_clean(
      "ScriptName S\n"
      "int Function AddTwo(int a, int b) global\n"
      "  return a + b\n"
      "endFunction\n");
  ASSERT_EQ(script.functions.size(), 1u);
  const auto& fn = script.functions[0];
  EXPECT_EQ(fn.name, "AddTwo");
  ASSERT_TRUE(fn.returnType.has_value());
  EXPECT_EQ(fn.returnType->name, "int");
  EXPECT_TRUE(fn.isGlobal);
  ASSERT_EQ(fn.parameters.size(), 2u);
  EXPECT_EQ(fn.parameters[0].name, "a");
  EXPECT_EQ(fn.parameters[1].name, "b");
  ASSERT_EQ(fn.body.size(), 1u);
  EXPECT_NE(as<ReturnStatement>(fn.body[0].get()), nullptr);
  EXPECT_EQ(fn.signature(), "int Function AddTwo(int a, int b) global");
}

TEST(PapyrusParser, ParameterDefaultMakesItOptional) {
  const auto script =
      parse_clean("ScriptName S\nFunction IncrementValue(int howMuch = 1)\nendFunction\n");
  ASSERT_EQ(script.functions.size(), 1u);
  ASSERT_EQ(script.functions[0].parameters.size(), 1u);
  EXPECT_NE(as<LiteralExpression>(script.functions[0].parameters[0].defaultValue.get()), nullptr);
}

TEST(PapyrusParser, NativeFunctionHasNoBodyAndNoTerminator) {
  const auto script = parse_clean(
      "ScriptName S Native\n"
      "int Function GetThing() native global\n"
      "Function Other()\n"
      "EndFunction\n");
  ASSERT_EQ(script.functions.size(), 2u);
  EXPECT_TRUE(script.functions[0].isNative);
  EXPECT_TRUE(script.functions[0].body.empty());
}

TEST(PapyrusParser, FunctionDocCommentIsAttached) {
  const auto script = parse_clean("ScriptName S\nFunction Foo()\n{What Foo does}\nEndFunction\n");
  ASSERT_EQ(script.functions.size(), 1u);
  ASSERT_TRUE(script.functions[0].documentation.has_value());
  EXPECT_EQ(*script.functions[0].documentation, "What Foo does");
}

TEST(PapyrusParser, PlainEvent) {
  const auto script =
      parse_clean("ScriptName S\nEvent OnActivate(ObjectReference akActivator)\nendEvent\n");
  ASSERT_EQ(script.events.size(), 1u);
  EXPECT_EQ(script.events[0].name, "OnActivate");
  EXPECT_FALSE(script.events[0].remoteObjectType.has_value());
  ASSERT_EQ(script.events[0].parameters.size(), 1u);
  EXPECT_EQ(script.events[0].parameters[0].type.name, "ObjectReference");
}

TEST(PapyrusParser, RemoteEventRecordsTheObjectType) {
  const auto script = parse_clean(
      "ScriptName S\n"
      "Event ObjectReference.OnActivate(ObjectReference akSender, ObjectReference akActivator)\n"
      "endEvent\n");
  ASSERT_EQ(script.events.size(), 1u);
  ASSERT_TRUE(script.events[0].remoteObjectType.has_value());
  EXPECT_EQ(*script.events[0].remoteObjectType, "ObjectReference");
  EXPECT_EQ(script.events[0].name, "OnActivate");
  EXPECT_EQ(script.events[0].signature().rfind("Event ObjectReference.OnActivate(", 0), 0u);
}

TEST(PapyrusParser, CustomEventHandlerTakesAVarArray) {
  const auto script = parse_clean(
      "ScriptName S\n"
      "Event MyQuestScript.MyCustomEvent(MyQuestScript akSender, Var[] akArgs)\n"
      "endEvent\n");
  ASSERT_EQ(script.events.size(), 1u);
  ASSERT_EQ(script.events[0].parameters.size(), 2u);
  EXPECT_TRUE(script.events[0].parameters[1].type.isArray);
  EXPECT_EQ(script.events[0].parameters[1].type.name, "var");
}

TEST(PapyrusParser, SpecialParameterTypesParse) {
  const auto script = parse_clean(
      "ScriptName S\nFunction Send(Form akSender, ScriptEventName asEvent)\nEndFunction\n");
  ASSERT_EQ(script.functions.size(), 1u);
  ASSERT_EQ(script.functions[0].parameters.size(), 2u);
  EXPECT_EQ(script.functions[0].parameters[1].type.name, "scripteventname");
}

TEST(PapyrusParser, AutoStateWithAnOverride) {
  const auto script = parse_clean(
      "ScriptName S\n"
      "Auto State MyState\n"
      "  int Function MyFunction()\n"
      "    Return 2\n"
      "  EndFunction\n"
      "  Event OnActivate(ObjectReference a)\n"
      "  EndEvent\n"
      "EndState\n");
  ASSERT_EQ(script.states.size(), 1u);
  EXPECT_TRUE(script.states[0].isAuto);
  EXPECT_EQ(script.states[0].name, "MyState");
  ASSERT_EQ(script.states[0].functions.size(), 1u);
  ASSERT_EQ(script.states[0].events.size(), 1u);
  ASSERT_TRUE(script.states[0].functions[0].stateName.has_value());
  EXPECT_EQ(*script.states[0].functions[0].stateName, "MyState");
  EXPECT_TRUE(script.functions.empty());
}

namespace {

// Each script is parked here so the returned statement pointers must stay valid for the whole run.
std::vector<const Statement*> body_of(std::string_view statements) {
  static std::vector<Script> scripts;
  scripts.push_back(parse_clean("ScriptName S\nFunction F()\n" + std::string(statements) +
                                "\nEndFunction\n"));
  const auto& script = scripts.back();
  std::vector<const Statement*> body;
  if (script.functions.empty()) {
    ADD_FAILURE() << "the wrapper function did not parse";
    return body;
  }
  for (const auto& statement : script.functions[0].body) body.push_back(statement.get());
  return body;
}

const Expression* value_of(std::string_view expression) {
  const auto body = body_of("x = " + std::string(expression));
  if (body.empty()) return nullptr;
  const auto* assign = as<AssignStatement>(body[0]);
  return assign != nullptr ? assign->value.get() : nullptr;
}

}  // namespace

TEST(PapyrusParser, DefineStatementWithInitializer) {
  const auto body = body_of("float seconds = CurrentTimeInMinutes() * 60.0");
  ASSERT_EQ(body.size(), 1u);
  const auto* define = as<DefineStatement>(body[0]);
  ASSERT_NE(define, nullptr);
  EXPECT_EQ(define->name, "seconds");
  EXPECT_NE(as<BinaryExpression>(define->initializer.get()), nullptr);
}

TEST(PapyrusParser, LocalMayCarryAConstFlag) {
  const auto body = body_of("string filename = \"S7System\" const");
  ASSERT_EQ(body.size(), 1u);
  const auto* define = as<DefineStatement>(body[0]);
  ASSERT_NE(define, nullptr);
  EXPECT_EQ(define->flags, std::vector<std::string>{"const"});
}

TEST(PapyrusParser, AssignmentIsNotMistakenForADefinition) {
  const auto body = body_of("x = 5");
  ASSERT_EQ(body.size(), 1u);
  EXPECT_NE(as<AssignStatement>(body[0]), nullptr);
}

TEST(PapyrusParser, IndexedAssignmentIsNotMistakenForAnArrayDefinition) {
  const auto body = body_of("myArray[0] = 1");
  ASSERT_EQ(body.size(), 1u);
  const auto* assign = as<AssignStatement>(body[0]);
  ASSERT_NE(assign, nullptr);
  EXPECT_NE(as<IndexExpression>(assign->target.get()), nullptr);
}

TEST(PapyrusParser, ArrayDefinitionWithEmptyBracketsIsADefinition) {
  const auto body = body_of("int[] myArray = new int[10]");
  ASSERT_EQ(body.size(), 1u);
  const auto* define = as<DefineStatement>(body[0]);
  ASSERT_NE(define, nullptr);
  EXPECT_TRUE(define->type.isArray);
  EXPECT_NE(as<NewArrayExpression>(define->initializer.get()), nullptr);
}

TEST(PapyrusParser, CompoundAssignmentOperators) {
  const auto body = body_of("MyObject.MyProperty += CoolFunction() * 10");
  ASSERT_EQ(body.size(), 1u);
  const auto* assign = as<AssignStatement>(body[0]);
  ASSERT_NE(assign, nullptr);
  EXPECT_EQ(assign->op, TokenKind::PlusAssign);
  EXPECT_NE(as<MemberExpression>(assign->target.get()), nullptr);
}

TEST(PapyrusParser, BareCallIsAnExpressionStatement) {
  const auto body = body_of("PlayAnimation(\"CoolStuff\")");
  ASSERT_EQ(body.size(), 1u);
  const auto* statement = as<ExpressionStatement>(body[0]);
  ASSERT_NE(statement, nullptr);
  EXPECT_NE(as<CallExpression>(statement->expression.get()), nullptr);
}

TEST(PapyrusParser, ReturnWithAndWithoutAValue) {
  const auto withValue = body_of("Return 5");
  ASSERT_EQ(withValue.size(), 1u);
  const auto* ret = as<ReturnStatement>(withValue[0]);
  ASSERT_NE(ret, nullptr);
  EXPECT_NE(ret->value, nullptr);

  const auto without = body_of("Return");
  ASSERT_EQ(without.size(), 1u);
  const auto* bare = as<ReturnStatement>(without[0]);
  ASSERT_NE(bare, nullptr);
  EXPECT_EQ(bare->value, nullptr);
}

TEST(PapyrusParser, IfElseIfElse) {
  const auto body = body_of(
      "if value > 10\n"
      "  x = 1\n"
      "elseif value < 10\n"
      "  x = -1\n"
      "else\n"
      "  x = 0\n"
      "endIf");
  ASSERT_EQ(body.size(), 1u);
  const auto* statement = as<IfStatement>(body[0]);
  ASSERT_NE(statement, nullptr);
  ASSERT_EQ(statement->branches.size(), 2u);
  EXPECT_EQ(statement->branches[0].body.size(), 1u);
  EXPECT_EQ(statement->elseBody.size(), 1u);
}

TEST(PapyrusParser, WhileLoop) {
  const auto body = body_of("while x < 10\n  DoCoolStuff()\n  x += 1\nendWhile");
  ASSERT_EQ(body.size(), 1u);
  const auto* loop = as<WhileStatement>(body[0]);
  ASSERT_NE(loop, nullptr);
  EXPECT_EQ(loop->body.size(), 2u);
  EXPECT_NE(as<BinaryExpression>(loop->condition.get()), nullptr);
}

TEST(PapyrusParser, MultiplicationBindsTighterThanAddition) {
  const auto* root = as<BinaryExpression>(value_of("2 + 4 * 10"));
  ASSERT_NE(root, nullptr);
  EXPECT_EQ(root->op, TokenKind::Plus);
  const auto* right = as<BinaryExpression>(root->right.get());
  ASSERT_NE(right, nullptr);
  EXPECT_EQ(right->op, TokenKind::Star);
}

TEST(PapyrusParser, ParenthesesOverridePrecedenceWithoutAddingANode) {
  const auto* root = as<BinaryExpression>(value_of("(2 + 4) * 10"));
  ASSERT_NE(root, nullptr);
  EXPECT_EQ(root->op, TokenKind::Star);
  const auto* left = as<BinaryExpression>(root->left.get());
  ASSERT_NE(left, nullptr);
  EXPECT_EQ(left->op, TokenKind::Plus);
}

TEST(PapyrusParser, ComparisonBindsLooserThanArithmeticAndTighterThanAnd) {
  const auto* root = as<BinaryExpression>(value_of("a + 1 > b && c"));
  ASSERT_NE(root, nullptr);
  EXPECT_EQ(root->op, TokenKind::And);
  const auto* left = as<BinaryExpression>(root->left.get());
  ASSERT_NE(left, nullptr);
  EXPECT_EQ(left->op, TokenKind::Greater);
}

TEST(PapyrusParser, AndBindsTighterThanOr) {
  const auto* root = as<BinaryExpression>(value_of("a || b && c"));
  ASSERT_NE(root, nullptr);
  EXPECT_EQ(root->op, TokenKind::Or);
  const auto* right = as<BinaryExpression>(root->right.get());
  ASSERT_NE(right, nullptr);
  EXPECT_EQ(right->op, TokenKind::And);
}

TEST(PapyrusParser, UnaryMinusAndNot) {
  const auto* minus = as<UnaryExpression>(value_of("-a"));
  ASSERT_NE(minus, nullptr);
  EXPECT_EQ(minus->op, TokenKind::Minus);
  const auto* notExpr = as<UnaryExpression>(value_of("!a"));
  ASSERT_NE(notExpr, nullptr);
  EXPECT_EQ(notExpr->op, TokenKind::Not);
}

TEST(PapyrusParser, CastAndTypeCheck) {
  const auto* cast = as<CastExpression>(value_of("a as float"));
  ASSERT_NE(cast, nullptr);
  EXPECT_EQ(cast->type.name, "float");
  const auto* check = as<TypeCheckExpression>(value_of("myObject is ObjectReference"));
  ASSERT_NE(check, nullptr);
  EXPECT_EQ(check->type.name, "ObjectReference");
}

TEST(PapyrusParser, DotChainWithCallAndIndex) {
  const auto* index = as<IndexExpression>(value_of("(MyVariable as MyObject).MyFunction()[0]"));
  ASSERT_NE(index, nullptr);
  const auto* call = as<CallExpression>(index->target.get());
  ASSERT_NE(call, nullptr);
  EXPECT_EQ(call->functionName(), "MyFunction");
  const auto* member = as<MemberExpression>(call->callee.get());
  ASSERT_NE(member, nullptr);
  EXPECT_NE(as<CastExpression>(member->target.get()), nullptr);
}

TEST(PapyrusParser, ArrayLengthIsAMemberAccess) {
  const auto* member = as<MemberExpression>(value_of("myArray.Length"));
  ASSERT_NE(member, nullptr);
  EXPECT_EQ(member->name, "Length");
}

TEST(PapyrusParser, NamedArgumentIsDistinguishedFromAComparison) {
  const auto* call = as<CallExpression>(value_of("MyFunction(howMuch = 1, other == 2)"));
  ASSERT_NE(call, nullptr);
  ASSERT_EQ(call->arguments.size(), 2u);
  ASSERT_TRUE(call->arguments[0].name.has_value());
  EXPECT_EQ(*call->arguments[0].name, "howMuch");
  EXPECT_FALSE(call->arguments[1].name.has_value());
  EXPECT_NE(as<BinaryExpression>(call->arguments[1].value.get()), nullptr);
}

TEST(PapyrusParser, NewStructVersusNewArray) {
  EXPECT_NE(as<NewStructExpression>(value_of("new Point")), nullptr);
  const auto* array = as<NewArrayExpression>(value_of("new MyScript[5 * count]"));
  ASSERT_NE(array, nullptr);
  EXPECT_NE(as<BinaryExpression>(array->size.get()), nullptr);
}

TEST(PapyrusParser, NamespacedScriptIsOneAtomNotAMemberChain) {
  const auto* call = as<CallExpression>(value_of("MyNamespace:MyScript.MyGlobal()"));
  ASSERT_NE(call, nullptr);
  const auto* member = as<MemberExpression>(call->callee.get());
  ASSERT_NE(member, nullptr);
  const auto* target = as<IdentifierExpression>(member->target.get());
  ASSERT_NE(target, nullptr);
  EXPECT_EQ(target->name, "MyNamespace:MyScript");
}

TEST(PapyrusParser, NoneTrueAndFalseAreLiterals) {
  const auto* none = as<LiteralExpression>(value_of("none"));
  ASSERT_NE(none, nullptr);
  EXPECT_EQ(none->kind, fo4::papyrus::LiteralKind::None);
  const auto* truth = as<LiteralExpression>(value_of("true"));
  ASSERT_NE(truth, nullptr);
  EXPECT_EQ(truth->kind, fo4::papyrus::LiteralKind::Bool);
}

TEST(PapyrusParser, ABrokenLineDoesNotCostTheDeclarationsAfterIt) {
  const auto script =
      parse("ScriptName S\nint Property Good1 auto\nint Property % auto\nint Property Good2 auto\n");
  EXPECT_TRUE(script.hasErrors());
  std::vector<std::string> names;
  for (const auto& property : script.properties) names.push_back(property.name);
  EXPECT_NE(std::find(names.begin(), names.end(), "Good1"), names.end());
  EXPECT_NE(std::find(names.begin(), names.end(), "Good2"), names.end());
  EXPECT_EQ(script.properties.size(), 3u);
}

TEST(PapyrusParser, ABrokenStatementDoesNotCostTheRestOfTheFunction) {
  const auto script = parse(
      "ScriptName S\n"
      "Function F()\n"
      "  x = 1\n"
      "  y = *\n"
      "  z = 3\n"
      "EndFunction\n"
      "Function G()\n"
      "EndFunction\n");
  EXPECT_TRUE(script.hasErrors());
  ASSERT_EQ(script.functions.size(), 2u);
  EXPECT_EQ(script.functions[0].name, "F");
  EXPECT_EQ(script.functions[1].name, "G");
  EXPECT_GE(script.functions[0].body.size(), 2u);
}

TEST(PapyrusParser, UnterminatedFunctionIsReportedAndDoesNotHang) {
  const auto script = parse("ScriptName S\nFunction F()\n  x = 1\n");
  EXPECT_TRUE(has_code(script, DiagnosticCode::UnterminatedBlock));
}

TEST(PapyrusParser, DiagnosticsAreCappedRatherThanUnbounded) {
  std::string junk = "ScriptName S\n";
  for (int i = 0; i < 500; ++i) junk += "%\n";
  const auto script = parse(junk);
  EXPECT_LE(script.diagnostics.size(), 201u);
}

TEST(PapyrusParser, EmptyInputProducesAScriptAndASingleComplaint) {
  const auto script = parse("");
  EXPECT_TRUE(has_code(script, DiagnosticCode::ExpectedScriptName));
  EXPECT_EQ(script.diagnostics.size(), 1u);
}

TEST(PapyrusParser, DiagnosticsCarryTheFilePathWhenOneIsGiven) {
  const auto script = Parser::parse("nonsense\n", std::string("/tmp/Foo.psc"));
  ASSERT_TRUE(script.filePath.has_value());
  EXPECT_EQ(*script.filePath, "/tmp/Foo.psc");
  ASSERT_FALSE(script.diagnostics.empty());
  for (const auto& diagnostic : script.diagnostics) {
    ASSERT_TRUE(diagnostic.file.has_value());
    EXPECT_EQ(*diagnostic.file, "/tmp/Foo.psc");
  }
}
