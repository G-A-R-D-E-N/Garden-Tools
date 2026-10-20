#include <fo4/papyrus_source/lexer.hpp>
#include <fo4/papyrus_source/parser.hpp>
#include <fo4/papyrus_source/summary.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <vector>

using fo4::psc::BinaryOperator;
using fo4::psc::Expression;
using fo4::psc::ExpressionKind;
using fo4::psc::FunctionDeclaration;
using fo4::psc::LexResult;
using fo4::psc::ParseErrorKind;
using fo4::psc::ParseResult;
using fo4::psc::Script;
using fo4::psc::ScriptSummary;
using fo4::psc::Statement;
using fo4::psc::TokenKind;
using fo4::psc::UnaryOperator;

namespace {

std::string describe(const Expression& expression);

std::string describe_left(const Expression& expression) {
  return expression.left != nullptr ? describe(*expression.left) : std::string("?");
}

const char* unary_name(UnaryOperator unary) {
  switch (unary) {
    case UnaryOperator::Negate:
      return "neg";
    case UnaryOperator::Plus:
      return "pos";
    case UnaryOperator::Not:
      return "not";
  }
  return "?";
}

const char* binary_name(BinaryOperator binary) {
  switch (binary) {
    case BinaryOperator::Or:
      return "||";
    case BinaryOperator::And:
      return "&&";
    case BinaryOperator::Equal:
      return "==";
    case BinaryOperator::NotEqual:
      return "!=";
    case BinaryOperator::Less:
      return "<";
    case BinaryOperator::LessEqual:
      return "<=";
    case BinaryOperator::Greater:
      return ">";
    case BinaryOperator::GreaterEqual:
      return ">=";
    case BinaryOperator::Add:
      return "+";
    case BinaryOperator::Subtract:
      return "-";
    case BinaryOperator::Multiply:
      return "*";
    case BinaryOperator::Divide:
      return "/";
    case BinaryOperator::Modulo:
      return "%";
  }
  return "?";
}

std::string describe(const Expression& expression) {
  switch (expression.kind) {
    case ExpressionKind::Identifier:
      return expression.text;
    case ExpressionKind::IntegerLiteral:
      return std::to_string(expression.intValue);
    case ExpressionKind::FloatLiteral:
      return expression.text;
    case ExpressionKind::StringLiteral:
      return "\"" + expression.stringValue + "\"";
    case ExpressionKind::BoolLiteral:
      return expression.boolValue ? "true" : "false";
    case ExpressionKind::Unary:
      return std::string("(") + unary_name(expression.unary) + " " + describe_left(expression) + ")";
    case ExpressionKind::Binary:
      return std::string("(") + binary_name(expression.binary) + " " + describe_left(expression) + " " +
             (expression.right != nullptr ? describe(*expression.right) : std::string("?")) + ")";
    case ExpressionKind::Cast:
      return std::string("(as ") + describe_left(expression) + " " + expression.text + ")";
    case ExpressionKind::Is:
      return std::string("(is ") + describe_left(expression) + " " + expression.text + ")";
    case ExpressionKind::Index:
      return std::string("(index ") + describe_left(expression) + " " +
             (expression.right != nullptr ? describe(*expression.right) : std::string("?")) + ")";
    case ExpressionKind::Member:
      return std::string("(member ") + describe_left(expression) + " " + expression.text + ")";
    case ExpressionKind::Call: {
      std::string out = std::string("(call ") + describe_left(expression);
      for (const fo4::psc::ExpressionPtr& argument : expression.arguments) {
        out += " " + describe(*argument);
      }
      return out + ")";
    }
    case ExpressionKind::New:
      if (expression.arrayNew) return std::string("(new ") + expression.text + " " + describe_left(expression) + ")";
      return std::string("(new ") + expression.text + ")";
    case ExpressionKind::None:
      return "?";
  }
  return "?";
}

const Expression* value_at(const FunctionDeclaration& function, std::size_t index) {
  if (index >= function.body.size()) return nullptr;
  return function.body[index]->value.get();
}

const Statement* statement_at(const FunctionDeclaration& function, std::size_t index) {
  if (index >= function.body.size()) return nullptr;
  return function.body[index].get();
}

std::vector<std::string> identifier_texts(const LexResult& lexed) {
  std::vector<std::string> out;
  for (const fo4::psc::Token& token : lexed.tokens) {
    if (token.kind == TokenKind::Identifier) out.push_back(token.text);
  }
  return out;
}

bool contains(const std::vector<std::string>& values, const std::string& value) {
  for (const std::string& existing : values) {
    if (existing == value) return true;
  }
  return false;
}

}  // namespace

TEST(PapyrusSourceTest, LexesCommentsContinuationsAndDocComments) {
  const std::string source =
      ";/ a block\ncomment that spans lines /;\n"
      "Scriptname Lex ; a trailing line comment\n"
      "Int Value = 1 + \\\n"
      "    2\n";
  const LexResult lexed = fo4::psc::lex(source);
  ASSERT_TRUE(lexed.ok()) << lexed.error.message;
  const std::vector<std::string> identifiers = identifier_texts(lexed);
  ASSERT_EQ(identifiers.size(), 3u);
  EXPECT_EQ(identifiers[0], "Lex");
  EXPECT_EQ(identifiers[1], "Int");
  EXPECT_EQ(identifiers[2], "Value");
  ASSERT_FALSE(lexed.tokens.empty());
  std::size_t firstCode = 0;
  while (lexed.tokens[firstCode].kind == TokenKind::NewLine) ++firstCode;
  EXPECT_EQ(lexed.tokens[firstCode].position.line, 3u);
  bool sawContinuation = false;
  for (std::size_t at = 0; at + 1 < lexed.tokens.size(); ++at) {
    if (lexed.tokens[at].kind == TokenKind::Integer && lexed.tokens[at].intValue == 2) {
      EXPECT_EQ(lexed.tokens[at].position.line, 5u);
      EXPECT_EQ(lexed.tokens[at].position.column, 5u);
      sawContinuation = true;
    }
  }
  EXPECT_TRUE(sawContinuation);

  const LexResult documented = fo4::psc::lex("{doc text}\nScriptname D\n");
  ASSERT_TRUE(documented.ok()) << documented.error.message;
  ASSERT_GE(documented.tokens.size(), 3u);
  EXPECT_EQ(documented.tokens[0].kind, TokenKind::DocComment);
  EXPECT_EQ(documented.tokens[0].text, "doc text");
  EXPECT_EQ(documented.tokens[0].position.line, 1u);
  EXPECT_EQ(documented.tokens[1].kind, TokenKind::NewLine);
  EXPECT_EQ(documented.tokens[2].kind, TokenKind::KwScriptName);
}

TEST(PapyrusSourceTest, LexesSameLengthIdentifiersAsIdentifiers) {
  const LexResult lexed = fo4::psc::lex("Int Foo Self Ab\nScriptName SCRIPTNAME As EndIf\n");
  ASSERT_TRUE(lexed.ok()) << lexed.error.message;
  std::vector<TokenKind> kinds;
  for (const fo4::psc::Token& token : lexed.tokens) {
    if (token.kind != TokenKind::NewLine && token.kind != TokenKind::EndOfFile) kinds.push_back(token.kind);
  }
  const std::vector<TokenKind> expected = {
      TokenKind::Identifier,   TokenKind::Identifier,   TokenKind::Identifier,   TokenKind::Identifier,
      TokenKind::KwScriptName, TokenKind::KwScriptName, TokenKind::KwAs,         TokenKind::KwEndIf};
  EXPECT_EQ(kinds, expected);
}

TEST(PapyrusSourceTest, LexesHugeFloatExponentWithoutOverflow) {
  const std::string digits(30, '9');
  const LexResult grown = fo4::psc::lex("1.0e" + digits + "\n");
  ASSERT_TRUE(grown.ok()) << grown.error.message;
  ASSERT_FALSE(grown.tokens.empty());
  EXPECT_EQ(grown.tokens[0].kind, TokenKind::Float);
  EXPECT_GT(grown.tokens[0].floatValue, 1e300);
  const LexResult shrunk = fo4::psc::lex("1.0e-" + digits + "\n");
  ASSERT_TRUE(shrunk.ok()) << shrunk.error.message;
  ASSERT_FALSE(shrunk.tokens.empty());
  EXPECT_EQ(shrunk.tokens[0].kind, TokenKind::Float);
  EXPECT_EQ(shrunk.tokens[0].floatValue, 0.0);
}

TEST(PapyrusSourceTest, ParsesLiteralsOfEveryKind) {
  const std::string source =
      "Scriptname L\n"
      "Function F()\n"
      "  Int a = 0xC\n"
      "  Float b = 1.5\n"
      "  String c = \"hi\\n\"\n"
      "  Bool d = true\n"
      "  Form e = None\n"
      "  Int f = -2\n"
      "EndFunction\n";
  const ParseResult result = fo4::psc::parse(source);
  ASSERT_TRUE(result.ok()) << result.error.message;
  ASSERT_EQ(result.script.functions.size(), 1u);
  const FunctionDeclaration& function = result.script.functions[0];
  ASSERT_EQ(function.body.size(), 6u);
  EXPECT_EQ(describe(*value_at(function, 0)), "12");
  EXPECT_EQ(describe(*value_at(function, 1)), "1.5");
  EXPECT_EQ(describe(*value_at(function, 2)), "\"hi\n\"");
  EXPECT_EQ(describe(*value_at(function, 3)), "true");
  EXPECT_EQ(describe(*value_at(function, 4)), "None");
  EXPECT_EQ(describe(*value_at(function, 5)), "(neg 2)");
}

TEST(PapyrusSourceTest, ParsesScriptHeaderImportsAndFlags) {
  const std::string source =
      "Scriptname Hydra:Game extends Form Native Hidden Conditional\n"
      "Import Hydra:Arrays\n"
      "Import Utility\n";
  const ParseResult result = fo4::psc::parse(source);
  ASSERT_TRUE(result.ok()) << result.error.message;
  EXPECT_EQ(result.script.name, "Hydra:Game");
  EXPECT_EQ(result.script.extends, "Form");
  EXPECT_TRUE(result.script.isNative);
  EXPECT_TRUE(result.script.isHidden);
  EXPECT_TRUE(result.script.isConditional);
  EXPECT_FALSE(result.script.isBetaOnly);
  ASSERT_EQ(result.script.imports.size(), 2u);
  EXPECT_EQ(result.script.imports[0], "Hydra:Arrays");
  EXPECT_EQ(result.script.imports[1], "Utility");
}

TEST(PapyrusSourceTest, ParsesAutoAndFullPropertiesWithDefaults) {
  const std::string source =
      "Scriptname P\n"
      "Int Property Count = 22 AutoReadOnly\n"
      "GlobalVariable Property Target Auto Const Mandatory\n"
      "Float Property X\n"
      "  Float Function Get()\n"
      "    Return 1.0\n"
      "  EndFunction\n"
      "  Function Set(Float value)\n"
      "    _x = value\n"
      "  EndFunction\n"
      "EndProperty\n"
      "Int Property Plain auto hidden\n";
  const ParseResult result = fo4::psc::parse(source);
  ASSERT_TRUE(result.ok()) << result.error.message;
  const Script& script = result.script;
  ASSERT_EQ(script.properties.size(), 4u);
  EXPECT_EQ(script.properties[0].name, "Count");
  EXPECT_EQ(script.properties[0].type, "Int");
  EXPECT_TRUE(script.properties[0].isAutoReadOnly);
  EXPECT_FALSE(script.properties[0].isAuto);
  ASSERT_NE(script.properties[0].defaultValue, nullptr);
  EXPECT_EQ(describe(*script.properties[0].defaultValue), "22");
  EXPECT_EQ(script.properties[1].name, "Target");
  EXPECT_TRUE(script.properties[1].isAuto);
  EXPECT_TRUE(script.properties[1].isConst);
  EXPECT_TRUE(script.properties[1].isMandatory);
  ASSERT_EQ(script.properties[2].accessors.size(), 2u);
  EXPECT_EQ(script.properties[2].accessors[0].name, "Get");
  EXPECT_EQ(script.properties[2].accessors[0].returnType, "Float");
  ASSERT_EQ(script.properties[2].accessors[0].body.size(), 1u);
  EXPECT_EQ(script.properties[2].accessors[1].name, "Set");
  ASSERT_EQ(script.properties[2].accessors[1].parameters.size(), 1u);
  EXPECT_EQ(script.properties[2].accessors[1].parameters[0].name, "value");
  EXPECT_EQ(script.properties[2].accessors[1].parameters[0].type, "Float");
  EXPECT_TRUE(script.properties[3].isAuto);
  EXPECT_TRUE(script.properties[3].isHidden);
  EXPECT_TRUE(script.functions.empty());
}

TEST(PapyrusSourceTest, ParsesGroupsStructsAndCustomEvents) {
  const std::string source =
      "Scriptname G\n"
      "Group Base Collapsed\n"
      "  Actor Property PlayerRef Auto Const Mandatory\n"
      "EndGroup\n"
      "Struct WornItem\n"
      "  Form item\n"
      "  String modelName = \"\"\n"
      "  Int[] slots\n"
      "EndStruct\n"
      "CustomEvent MyEvent(Int aiIndex, Bool abFlag = true) Native\n";
  const ParseResult result = fo4::psc::parse(source);
  ASSERT_TRUE(result.ok()) << result.error.message;
  const Script& script = result.script;
  ASSERT_EQ(script.groups.size(), 1u);
  EXPECT_EQ(script.groups[0].name, "Base");
  ASSERT_EQ(script.groups[0].flags.size(), 1u);
  EXPECT_EQ(script.groups[0].flags[0], "Collapsed");
  ASSERT_EQ(script.groups[0].propertyNames.size(), 1u);
  EXPECT_EQ(script.groups[0].propertyNames[0], "PlayerRef");
  ASSERT_EQ(script.properties.size(), 1u);
  EXPECT_EQ(script.properties[0].name, "PlayerRef");
  ASSERT_EQ(script.structs.size(), 1u);
  EXPECT_EQ(script.structs[0].name, "WornItem");
  ASSERT_EQ(script.structs[0].members.size(), 3u);
  EXPECT_EQ(script.structs[0].members[0].type, "Form");
  EXPECT_EQ(script.structs[0].members[0].name, "item");
  EXPECT_FALSE(script.structs[0].members[0].isArray);
  EXPECT_EQ(script.structs[0].members[1].name, "modelName");
  ASSERT_NE(script.structs[0].members[1].defaultValue, nullptr);
  EXPECT_EQ(describe(*script.structs[0].members[1].defaultValue), "\"\"");
  EXPECT_EQ(script.structs[0].members[2].name, "slots");
  EXPECT_TRUE(script.structs[0].members[2].isArray);
  ASSERT_EQ(script.customEvents.size(), 1u);
  EXPECT_EQ(script.customEvents[0].name, "MyEvent");
  EXPECT_TRUE(script.customEvents[0].isNative);
  ASSERT_EQ(script.customEvents[0].parameters.size(), 2u);
  EXPECT_EQ(script.customEvents[0].parameters[0].name, "aiIndex");
  EXPECT_EQ(script.customEvents[0].parameters[1].type, "Bool");
  ASSERT_NE(script.customEvents[0].parameters[1].defaultValue, nullptr);
  EXPECT_EQ(describe(*script.customEvents[0].parameters[1].defaultValue), "true");
}

TEST(PapyrusSourceTest, ParsesStatesWithFunctionsAndNestedStates) {
  const std::string source =
      "Scriptname S\n"
      "State Busy\n"
      "  Function Run()\n"
      "    GotoState(\"Idle\")\n"
      "  EndFunction\n"
      "  State Inner\n"
      "  EndState\n"
      "EndState\n"
      "Auto State Idle\n"
      "EndState\n"
      "Function Run()\n"
      "EndFunction\n";
  const ParseResult result = fo4::psc::parse(source);
  ASSERT_TRUE(result.ok()) << result.error.message;
  const Script& script = result.script;
  ASSERT_EQ(script.states.size(), 2u);
  EXPECT_EQ(script.states[0].name, "Busy");
  EXPECT_FALSE(script.states[0].isAuto);
  ASSERT_EQ(script.states[0].functions.size(), 1u);
  EXPECT_EQ(script.states[0].functions[0].name, "Run");
  ASSERT_EQ(script.states[0].states.size(), 1u);
  EXPECT_EQ(script.states[0].states[0].name, "Inner");
  EXPECT_EQ(script.states[1].name, "Idle");
  EXPECT_TRUE(script.states[1].isAuto);
  EXPECT_EQ(script.autoState, "Idle");
  ASSERT_EQ(script.functions.size(), 1u);
  EXPECT_EQ(script.functions[0].name, "Run");
}

TEST(PapyrusSourceTest, ParsesFunctionSignaturesWithDefaultsAndModifiers) {
  const std::string source =
      "Scriptname F\n"
      "Var[] Function VarToVarArray(Var v) Native Global\n"
      "Int Function GetWornItemMods(Int slotIndex, Bool firstPerson = false) Global\n"
      "  Return 0\n"
      "EndFunction\n"
      "Event Actor.OnPlayerLoadGame(Actor akSender)\n"
      "EndEvent\n"
      "Function QueueUpdate(Bool bDoEquipment = false, Int flags = 0xC) Native\n";
  const ParseResult result = fo4::psc::parse(source);
  ASSERT_TRUE(result.ok()) << result.error.message;
  const Script& script = result.script;
  ASSERT_EQ(script.functions.size(), 4u);
  EXPECT_EQ(script.functions[0].name, "VarToVarArray");
  EXPECT_EQ(script.functions[0].returnType, "Var");
  EXPECT_TRUE(script.functions[0].returnsArray);
  EXPECT_TRUE(script.functions[0].isNative);
  EXPECT_TRUE(script.functions[0].isGlobal);
  ASSERT_EQ(script.functions[0].parameters.size(), 1u);
  EXPECT_EQ(script.functions[0].parameters[0].type, "Var");
  EXPECT_EQ(script.functions[0].parameters[0].name, "v");
  EXPECT_EQ(script.functions[1].name, "GetWornItemMods");
  EXPECT_EQ(script.functions[1].returnType, "Int");
  EXPECT_FALSE(script.functions[1].isNative);
  ASSERT_EQ(script.functions[1].parameters.size(), 2u);
  EXPECT_EQ(script.functions[1].parameters[0].defaultValue, nullptr);
  ASSERT_NE(script.functions[1].parameters[1].defaultValue, nullptr);
  EXPECT_EQ(describe(*script.functions[1].parameters[1].defaultValue), "false");
  EXPECT_EQ(script.functions[2].kind, fo4::psc::FunctionKind::Event);
  EXPECT_EQ(script.functions[2].name, "Actor.OnPlayerLoadGame");
  EXPECT_EQ(script.functions[2].returnType, "None");
  EXPECT_TRUE(script.functions[3].isNative);
  ASSERT_EQ(script.functions[3].parameters.size(), 2u);
  ASSERT_NE(script.functions[3].parameters[1].defaultValue, nullptr);
  EXPECT_EQ(describe(*script.functions[3].parameters[1].defaultValue), "12");
}

TEST(PapyrusSourceTest, ParsesIfChainsLoopsAndCompoundAssignment) {
  const std::string source =
      "Scriptname C\n"
      "Function Step(Int aiIndex)\n"
      "  Int found = 0\n"
      "  Int i = 0\n"
      "  While i < 10\n"
      "    If i == 3\n"
      "      found += 1\n"
      "    ElseIf i > 5\n"
      "      Return\n"
      "    Else\n"
      "      found = found - 1\n"
      "    EndIf\n"
      "    i += 1\n"
      "  EndWhile\n"
      "EndFunction\n";
  const ParseResult result = fo4::psc::parse(source);
  ASSERT_TRUE(result.ok()) << result.error.message;
  const FunctionDeclaration& function = result.script.functions[0];
  ASSERT_EQ(function.body.size(), 3u);
  EXPECT_EQ(function.body[0]->kind, fo4::psc::StatementKind::VariableDeclaration);
  const Statement* loop = statement_at(function, 2);
  ASSERT_NE(loop, nullptr);
  EXPECT_EQ(loop->kind, fo4::psc::StatementKind::While);
  EXPECT_EQ(describe(*loop->value), "(< i 10)");
  ASSERT_EQ(loop->body.size(), 2u);
  const Statement* branch = loop->body[0].get();
  EXPECT_EQ(branch->kind, fo4::psc::StatementKind::If);
  ASSERT_EQ(branch->branches.size(), 3u);
  EXPECT_EQ(describe(*branch->branches[0].condition), "(== i 3)");
  ASSERT_EQ(branch->branches[0].body.size(), 1u);
  EXPECT_EQ(branch->branches[0].body[0]->kind, fo4::psc::StatementKind::Assignment);
  EXPECT_TRUE(branch->branches[0].body[0]->compound);
  EXPECT_EQ(branch->branches[0].body[0]->binary, BinaryOperator::Add);
  EXPECT_EQ(describe(*branch->branches[1].condition), "(> i 5)");
  ASSERT_EQ(branch->branches[1].body.size(), 1u);
  EXPECT_EQ(branch->branches[1].body[0]->kind, fo4::psc::StatementKind::Return);
  EXPECT_EQ(branch->branches[1].body[0]->value, nullptr);
  EXPECT_EQ(branch->branches[2].condition, nullptr);
  ASSERT_EQ(branch->branches[2].body.size(), 1u);
  EXPECT_FALSE(branch->branches[2].body[0]->compound);
  EXPECT_EQ(describe(*branch->branches[2].body[0]->target), "found");
  EXPECT_EQ(describe(*branch->branches[2].body[0]->value), "(- found 1)");
  EXPECT_TRUE(loop->body[1]->compound);
}

TEST(PapyrusSourceTest, AppliesExpressionPrecedenceToCastsCallsAndArrays) {
  const std::string source =
      "Scriptname E\n"
      "Function Probe(ObjectReference akRef, Int count)\n"
      "  Bool isPlayer = (akRef == Game.GetPlayer() as ObjectReference)\n"
      "  Int total = 1 + 2 * 3 - 4 % 3\n"
      "  Bool both = !akRef.IsDisabled() && akRef.IsEnabled() || false\n"
      "  Int pick = candidates[Utility.RandomInt(0, count - 1)]\n"
      "  Int size = GetList().Length\n"
      "  Int found = GetList().Find(7)\n"
      "  Int[] made = New Int[count]\n"
      "  RayCastParams params = New RayCastParams\n"
      "  Debug.Notification(\"x\", abPrefix = true)\n"
      "EndFunction\n";
  const ParseResult result = fo4::psc::parse(source);
  ASSERT_TRUE(result.ok()) << result.error.message;
  const FunctionDeclaration& function = result.script.functions[0];
  ASSERT_EQ(function.body.size(), 9u);
  EXPECT_EQ(describe(*value_at(function, 0)), "(== akRef (as (call (member Game GetPlayer)) ObjectReference))");
  EXPECT_EQ(describe(*value_at(function, 1)), "(- (+ 1 (* 2 3)) (% 4 3))");
  EXPECT_EQ(describe(*value_at(function, 2)),
            "(|| (&& (not (call (member akRef IsDisabled))) (call (member akRef IsEnabled))) false)");
  EXPECT_EQ(describe(*value_at(function, 3)), "(index candidates (call (member Utility RandomInt) 0 (- count 1)))");
  EXPECT_EQ(describe(*value_at(function, 4)), "(member (call GetList) Length)");
  EXPECT_EQ(describe(*value_at(function, 5)), "(call (member (call GetList) Find) 7)");
  EXPECT_EQ(describe(*value_at(function, 6)), "(new Int count)");
  EXPECT_EQ(describe(*value_at(function, 7)), "(new RayCastParams)");
  const Expression* notification = value_at(function, 8);
  ASSERT_NE(notification, nullptr);
  EXPECT_EQ(describe(*notification), "(call (member Debug Notification) \"x\" true)");
  ASSERT_EQ(notification->argumentNames.size(), 2u);
  EXPECT_EQ(notification->argumentNames[0], "");
  EXPECT_EQ(notification->argumentNames[1], "abPrefix");
}

TEST(PapyrusSourceTest, SummarizesCallsPropertiesWritesAndStateTransitions) {
  const std::string source =
      "Scriptname Sum\n"
      "Int Property Count Auto\n"
      "Function Run(Int seed)\n"
      "  Int local = seed + Count\n"
      "  Count = local\n"
      "  self.Helper(local)\n"
      "  Game.GetPlayer().AddItem(akItem)\n"
      "  GotoState(\"Busy\")\n"
      "  Self.GoToState(\"Idle\")\n"
      "EndFunction\n"
      "Function Helper(Int value) Native Global\n";
  const ParseResult result = fo4::psc::parse(source);
  ASSERT_TRUE(result.ok()) << result.error.message;
  const ScriptSummary summary = fo4::psc::summarize(result.script);
  EXPECT_EQ(summary.name, "Sum");
  ASSERT_EQ(summary.declaredProperties.size(), 1u);
  EXPECT_EQ(summary.declaredProperties[0], "Count");
  ASSERT_EQ(summary.autoProperties.size(), 1u);
  EXPECT_EQ(summary.autoProperties[0], "Count");
  ASSERT_EQ(summary.functions.size(), 2u);
  const fo4::psc::FunctionSummary* run = fo4::psc::find_function(summary, "Run");
  ASSERT_NE(run, nullptr);
  EXPECT_TRUE(contains(run->calledFunctions, "self.Helper"));
  EXPECT_TRUE(contains(run->calledFunctions, "Game.GetPlayer().AddItem"));
  EXPECT_FALSE(contains(run->calledFunctions, "GotoState"));
  EXPECT_FALSE(contains(run->calledFunctions, "Self.GoToState"));
  EXPECT_TRUE(contains(run->propertiesRead, "seed"));
  EXPECT_TRUE(contains(run->propertiesRead, "Count"));
  EXPECT_TRUE(contains(run->propertiesWritten, "Count"));
  EXPECT_FALSE(contains(run->propertiesWritten, "seed"));
  ASSERT_EQ(run->stateTransitions.size(), 2u);
  EXPECT_EQ(run->stateTransitions[0], "Busy");
  EXPECT_EQ(run->stateTransitions[1], "Idle");
  EXPECT_TRUE(contains(summary.calledFunctions, "Game.GetPlayer().AddItem"));
  EXPECT_TRUE(contains(summary.propertiesWritten, "Count"));
  const fo4::psc::FunctionSummary* helper = fo4::psc::find_function(summary, "Helper");
  ASSERT_NE(helper, nullptr);
  EXPECT_TRUE(helper->isNative);
  EXPECT_TRUE(helper->calledFunctions.empty());
  EXPECT_EQ(fo4::psc::find_function(summary, "Missing"), nullptr);
}

TEST(PapyrusSourceTest, SummarizesPropertyAccessorBodies) {
  const std::string source =
      "Scriptname PA\n"
      "Int Property Count Auto\n"
      "Float Property Scaled\n"
      "  Float Function Get()\n"
      "    Return Helper(Count)\n"
      "  EndFunction\n"
      "  Function Set(Float value)\n"
      "    Refresh(value)\n"
      "    Count = 1\n"
      "  EndFunction\n"
      "EndProperty\n"
      "Int Function Helper(Int value) Native Global\n";
  const ParseResult result = fo4::psc::parse(source);
  ASSERT_TRUE(result.ok()) << result.error.message;
  const ScriptSummary summary = fo4::psc::summarize(result.script);
  ASSERT_EQ(summary.functions.size(), 3u);
  const fo4::psc::FunctionSummary* getter = fo4::psc::find_function(summary, "Scaled.Get");
  ASSERT_NE(getter, nullptr);
  EXPECT_TRUE(contains(getter->calledFunctions, "Helper"));
  EXPECT_TRUE(contains(getter->propertiesRead, "Count"));
  const fo4::psc::FunctionSummary* setter = fo4::psc::find_function(summary, "Scaled.Set");
  ASSERT_NE(setter, nullptr);
  EXPECT_TRUE(contains(setter->calledFunctions, "Refresh"));
  EXPECT_TRUE(contains(setter->propertiesWritten, "Count"));
  EXPECT_TRUE(contains(summary.calledFunctions, "Helper"));
  EXPECT_TRUE(contains(summary.calledFunctions, "Refresh"));
  EXPECT_TRUE(contains(summary.propertiesWritten, "Count"));
}

TEST(PapyrusSourceTest, RejectsUnterminatedStringAndBlockComment) {
  const ParseResult text = fo4::psc::parse("Scriptname B\nString s = \"oops\n");
  ASSERT_FALSE(text.ok());
  EXPECT_EQ(text.error.kind, ParseErrorKind::LexFailed);
  EXPECT_EQ(text.error.message, "unterminated string literal");
  EXPECT_EQ(text.error.position.line, 2u);

  const ParseResult block = fo4::psc::parse("Scriptname B\n;/ never closed\nInt a = 1\n");
  ASSERT_FALSE(block.ok());
  EXPECT_EQ(block.error.kind, ParseErrorKind::LexFailed);
  EXPECT_EQ(block.error.message, "unterminated block comment");
  EXPECT_EQ(block.error.position.line, 2u);
}

TEST(PapyrusSourceTest, RejectsUnknownTokenWithPosition) {
  const ParseResult result = fo4::psc::parse("Scriptname B\nInt a = 1 # 2\n");
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.error.kind, ParseErrorKind::LexFailed);
  EXPECT_EQ(result.error.message, "unexpected character '#'");
  EXPECT_EQ(result.error.position.line, 2u);
  EXPECT_EQ(result.error.position.column, 11u);
}

TEST(PapyrusSourceTest, RejectsUnbalancedBlockKeywords) {
  const ParseResult mismatched =
      fo4::psc::parse("Scriptname B\nFunction F()\n  If 1\n    Int a = 1\n  EndWhile\nEndFunction\n");
  ASSERT_FALSE(mismatched.ok());
  EXPECT_EQ(mismatched.error.kind, ParseErrorKind::UnbalancedIf);

  const ParseResult truncated = fo4::psc::parse("Scriptname B\nFunction F()\n  If 1\n    Int a = 1\n");
  ASSERT_FALSE(truncated.ok());
  EXPECT_EQ(truncated.error.kind, ParseErrorKind::UnexpectedEndOfFile);

  const ParseResult loop = fo4::psc::parse("Scriptname B\nFunction F()\n  While 1\n    Int a = 1\n  EndIf\nEndFunction\n");
  ASSERT_FALSE(loop.ok());
  EXPECT_EQ(loop.error.kind, ParseErrorKind::UnbalancedWhile);
}

TEST(PapyrusSourceTest, RejectsAssignmentToALiteral) {
  const ParseResult result = fo4::psc::parse("Scriptname B\nFunction F()\n  1 = 2\nEndFunction\n");
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.error.kind, ParseErrorKind::InvalidAssignmentTarget);
  EXPECT_EQ(result.error.position.line, 3u);
}

TEST(PapyrusSourceTest, RejectsNestingPastTheDepthLimit) {
  std::string source = "Scriptname B\nFunction F()\n  Int a = ";
  for (int step = 0; step < 200; ++step) source += "(";
  source += "1";
  for (int step = 0; step < 200; ++step) source += ")";
  source += "\nEndFunction\n";
  const ParseResult result = fo4::psc::parse(source);
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.error.kind, ParseErrorKind::DepthLimit);
}

TEST(PapyrusSourceTest, RejectsLongChainsPastTheDepthLimitWithoutRecursing) {
  std::string members = "Scriptname B\nFunction F()\n  Int a = x";
  for (int step = 0; step < 5000; ++step) members += ".b";
  members += "\nEndFunction\n";
  const ParseResult member = fo4::psc::parse(members);
  ASSERT_FALSE(member.ok());
  EXPECT_EQ(member.error.kind, ParseErrorKind::DepthLimit);

  std::string sums = "Scriptname B\nFunction F()\n  Int a = 1";
  for (int step = 0; step < 5000; ++step) sums += " + 1";
  sums += "\nEndFunction\n";
  const ParseResult sum = fo4::psc::parse(sums);
  ASSERT_FALSE(sum.ok());
  EXPECT_EQ(sum.error.kind, ParseErrorKind::DepthLimit);

  std::string realistic = "Scriptname B\nFunction F()\n  Int a = x";
  for (int step = 0; step < 200; ++step) realistic += ".b";
  realistic += "\nEndFunction\n";
  EXPECT_TRUE(fo4::psc::parse(realistic).ok());

  std::string boundary = "Scriptname B\nFunction F()\n  Int a = x";
  for (int step = 0; step < 512; ++step) boundary += ".b";
  boundary += "\nEndFunction\n";
  EXPECT_TRUE(fo4::psc::parse(boundary).ok());

  std::string over = "Scriptname B\nFunction F()\n  Int a = x";
  for (int step = 0; step < 513; ++step) over += ".b";
  over += "\nEndFunction\n";
  const ParseResult overResult = fo4::psc::parse(over);
  ASSERT_FALSE(overResult.ok());
  EXPECT_EQ(overResult.error.kind, ParseErrorKind::DepthLimit);

  std::string nested = "1";
  for (int group = 0; group < 32; ++group) {
    std::string chain = "1";
    for (int step = 0; step < 20; ++step) chain += " + 1";
    nested = "(" + nested + " + " + chain + ")";
  }
  const ParseResult nestedResult =
      fo4::psc::parse("Scriptname B\nFunction F()\n  Int a = " + nested + "\nEndFunction\n");
  ASSERT_FALSE(nestedResult.ok());
  EXPECT_EQ(nestedResult.error.kind, ParseErrorKind::DepthLimit);
}
