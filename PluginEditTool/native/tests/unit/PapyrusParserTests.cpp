#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/papyrus/Ast.hpp>
#include <fo4ide/papyrus/Parser.hpp>

namespace {

using fo4ide::papyrus::AssignStatement;
using fo4ide::papyrus::BinaryExpression;
using fo4ide::papyrus::CallExpression;
using fo4ide::papyrus::CastExpression;
using fo4ide::papyrus::DefineStatement;
using fo4ide::papyrus::ErrorExpression;
using fo4ide::papyrus::Expression;
using fo4ide::papyrus::ExpressionStatement;
using fo4ide::papyrus::IdentifierExpression;
using fo4ide::papyrus::IfStatement;
using fo4ide::papyrus::IndexExpression;
using fo4ide::papyrus::LiteralExpression;
using fo4ide::papyrus::MemberExpression;
using fo4ide::papyrus::NewArrayExpression;
using fo4ide::papyrus::NewStructExpression;
using fo4ide::papyrus::Parser;
using fo4ide::papyrus::PropertyKind;
using fo4ide::papyrus::ReturnStatement;
using fo4ide::papyrus::Script;
using fo4ide::papyrus::Severity;
using fo4ide::papyrus::Statement;
using fo4ide::papyrus::TypeCheckExpression;
using fo4ide::papyrus::UnaryExpression;
using fo4ide::papyrus::WhileStatement;

template <typename T>
const T* as(const fo4ide::papyrus::Node* node) {
    return dynamic_cast<const T*>(node);
}

Script parse(std::string_view source) {
    return Parser::parse(source);
}

Script parseClean(std::string_view source) {
    auto script = Parser::parse(source);
    for (const auto& diagnostic : script.diagnostics) {
        if (diagnostic.severity == Severity::Error) {
            FAIL("the source is valid Papyrus but produced: " + diagnostic.toString());
        }
    }
    return script;
}

bool hasCode(const Script& script, fo4ide::papyrus::DiagnosticCode code) {
    for (const auto& diagnostic : script.diagnostics) {
        if (diagnostic.code == code) return true;
    }
    return false;
}

}  // namespace

TEST_CASE("Parser header captures name parent and flags") {
    const auto script = parseClean("ScriptName HiddenScript Extends ParentScript Hidden\n");
    CHECK(script.name == "HiddenScript");
    CHECK(script.extends == "ParentScript");
    CHECK(script.flags == std::vector<std::string>{"hidden"});
}

TEST_CASE("Parser header accepts a namespaced script name") {
    CHECK(parseClean("ScriptName Traps:Triggers:TripWire\n").name == "Traps:Triggers:TripWire");
}

TEST_CASE("Parser header doc comment is attached to the script") {
    const auto script = parseClean("ScriptName MyCoolScript\n{Docs here\nand here}\n");
    CHECK(script.documentation == "Docs here\nand here");
}

TEST_CASE("Parser missing header is reported") {
    CHECK(hasCode(parse("Function Foo()\nEndFunction\n"),
                  fo4ide::papyrus::DiagnosticCode::ExpectedScriptName));
}

TEST_CASE("Parser imports are collected") {
    const auto script = parseClean("ScriptName S\nimport Utility\nimport MyNamespace:MyScript\n");
    REQUIRE(script.imports.size() == 2);
    CHECK(script.imports[0].name == "Utility");
    CHECK(script.imports[1].name == "MyNamespace:MyScript");
}

TEST_CASE("Parser script variable with initializer and flags") {
    const auto script = parseClean("ScriptName S\nint myValue = 0 Conditional\n");
    REQUIRE(script.variables.size() == 1);
    CHECK(script.variables[0].name == "myValue");
    CHECK(script.variables[0].type.name == "int");
    CHECK(as<LiteralExpression>(script.variables[0].initializer.get()) != nullptr);
    CHECK(script.variables[0].flags == std::vector<std::string>{"conditional"});
}

TEST_CASE("Parser array type is recognised") {
    const auto script = parseClean("ScriptName S\nint[] myArray\n");
    REQUIRE(script.variables.size() == 1);
    CHECK(script.variables[0].type.isArray);
    CHECK(script.variables[0].type.toString() == "int[]");
}

TEST_CASE("Parser auto property with initializer") {
    const auto script = parseClean("ScriptName S\nfloat Property MyProperty = 1.0 Auto\n");
    REQUIRE(script.properties.size() == 1);
    CHECK(script.properties[0].kind == PropertyKind::Auto);
    CHECK(script.properties[0].type.name == "float");
    CHECK(as<LiteralExpression>(script.properties[0].initializer.get()) != nullptr);
}

TEST_CASE("Parser auto read only property") {
    const auto script = parseClean("ScriptName S\nstring Property Hello = \"Hello world!\" AutoReadOnly\n");
    REQUIRE(script.properties.size() == 1);
    CHECK(script.properties[0].kind == PropertyKind::AutoReadOnly);
}

TEST_CASE("Parser full property collects its get and set") {
    const auto script = parseClean(
        "ScriptName S\n"
        "int Property ValueProperty\n"
        "  Function Set(int newValue)\n"
        "    myValue = newValue\n"
        "  EndFunction\n"
        "  int Function Get()\n"
        "    return myValue\n"
        "  EndFunction\n"
        "EndProperty\n");
    REQUIRE(script.properties.size() == 1);
    const auto& property = script.properties[0];
    CHECK(property.kind == PropertyKind::Full);
    REQUIRE(property.setter.has_value());
    REQUIRE(property.getter.has_value());
    CHECK(property.getter->returnType.has_value());
    CHECK(property.getter->returnType->name == "int");
}

TEST_CASE("Parser full property without an accessor is reported") {
    CHECK(hasCode(parse("ScriptName S\nint Property P\nEndProperty\n"),
                  fo4ide::papyrus::DiagnosticCode::PropertyNeedsAccessor));
}

TEST_CASE("Parser group properties are listed on both the group and the script") {
    const auto script = parseClean(
        "ScriptName S\n"
        "Group MyGroup CollapsedOnRef\n"
        "{A group}\n"
        "  int Property FirstProperty auto\n"
        "  float Property SecondProperty auto\n"
        "EndGroup\n");
    REQUIRE(script.groups.size() == 1);
    CHECK(script.groups[0].flags == std::vector<std::string>{"collapsedonref"});
    CHECK(script.groups[0].documentation == "A group");
    CHECK(script.groups[0].properties.size() == 2);
    CHECK(script.properties.size() == 2);
    for (const auto& property : script.properties) {
        CHECK(property.groupName == "MyGroup");
    }
}

TEST_CASE("Parser struct members keep their doc strings") {
    const auto script = parseClean(
        "ScriptName S\n"
        "struct QuestStage\n"
        "  Quest QuestToSet\n"
        "  {The quest whose stage is to be set}\n"
        "  int StageToSet\n"
        "  {The stage to set on the quest}\n"
        "endStruct\n");
    REQUIRE(script.structs.size() == 1);
    REQUIRE(script.structs[0].members.size() == 2);
    CHECK(script.structs[0].members[0].name == "QuestToSet");
    CHECK(script.structs[0].members[1].name == "StageToSet");
    CHECK(script.structs[0].members[0].documentation == "The quest whose stage is to be set");
}

TEST_CASE("Parser empty struct is reported") {
    CHECK(hasCode(parse("ScriptName S\nstruct Empty\nendStruct\n"),
                  fo4ide::papyrus::DiagnosticCode::StructNeedsMember));
}

TEST_CASE("Parser custom event definition") {
    const auto script = parseClean("ScriptName S\nCustomEvent MyCustomEvent\n");
    REQUIRE(script.customEvents.size() == 1);
    CHECK(script.customEvents[0].name == "MyCustomEvent");
}

TEST_CASE("Parser function header with return type parameters and global flag") {
    const auto script = parseClean(
        "ScriptName S\n"
        "int Function AddTwo(int a, int b) global\n"
        "  return a + b\n"
        "endFunction\n");
    REQUIRE(script.functions.size() == 1);
    const auto& fn = script.functions[0];
    CHECK(fn.name == "AddTwo");
    REQUIRE(fn.returnType.has_value());
    CHECK(fn.returnType->name == "int");
    CHECK(fn.isGlobal);
    REQUIRE(fn.parameters.size() == 2);
    CHECK(fn.parameters[0].name == "a");
    CHECK(fn.parameters[1].name == "b");
    REQUIRE(fn.body.size() == 1);
    CHECK(as<ReturnStatement>(fn.body[0].get()) != nullptr);
    CHECK(fn.signature() == "int Function AddTwo(int a, int b) global");
}

TEST_CASE("Parser parameter default makes it optional") {
    const auto script = parseClean("ScriptName S\nFunction IncrementValue(int howMuch = 1)\nendFunction\n");
    REQUIRE(script.functions.size() == 1);
    REQUIRE(script.functions[0].parameters.size() == 1);
    CHECK(as<LiteralExpression>(script.functions[0].parameters[0].defaultValue.get()) != nullptr);
}

TEST_CASE("Parser native function has no body and no terminator") {
    const auto script = parseClean(
        "ScriptName S Native\n"
        "int Function GetThing() native global\n"
        "Function Other()\n"
        "EndFunction\n");
    REQUIRE(script.functions.size() == 2);
    CHECK(script.functions[0].isNative);
    CHECK(script.functions[0].body.empty());
}

TEST_CASE("Parser function doc comment is attached") {
    const auto script = parseClean("ScriptName S\nFunction Foo()\n{What Foo does}\nEndFunction\n");
    REQUIRE(script.functions.size() == 1);
    CHECK(script.functions[0].documentation == "What Foo does");
}

TEST_CASE("Parser plain event") {
    const auto script = parseClean("ScriptName S\nEvent OnActivate(ObjectReference akActivator)\nendEvent\n");
    REQUIRE(script.events.size() == 1);
    CHECK(script.events[0].name == "OnActivate");
    CHECK(!script.events[0].remoteObjectType.has_value());
    REQUIRE(script.events[0].parameters.size() == 1);
    CHECK(script.events[0].parameters[0].type.name == "ObjectReference");
}

TEST_CASE("Parser remote event records the object type") {
    const auto script = parseClean(
        "ScriptName S\n"
        "Event ObjectReference.OnActivate(ObjectReference akSender, ObjectReference akActivator)\n"
        "endEvent\n");
    REQUIRE(script.events.size() == 1);
    CHECK(script.events[0].remoteObjectType == "ObjectReference");
    CHECK(script.events[0].name == "OnActivate");
    CHECK(script.events[0].signature().starts_with("Event ObjectReference.OnActivate("));
}

TEST_CASE("Parser custom event handler takes a var array") {
    const auto script = parseClean(
        "ScriptName S\n"
        "Event MyQuestScript.MyCustomEvent(MyQuestScript akSender, Var[] akArgs)\n"
        "endEvent\n");
    REQUIRE(script.events.size() == 1);
    REQUIRE(script.events[0].parameters.size() == 2);
    CHECK(script.events[0].parameters[1].type.isArray);
    CHECK(script.events[0].parameters[1].type.name == "var");
}

TEST_CASE("Parser special parameter types parse") {
    const auto script = parseClean(
        "ScriptName S\nFunction Send(Form akSender, ScriptEventName asEvent)\nEndFunction\n");
    REQUIRE(script.functions.size() == 1);
    REQUIRE(script.functions[0].parameters.size() == 2);
    CHECK(script.functions[0].parameters[1].type.name == "scripteventname");
}

TEST_CASE("Parser auto state with an override") {
    const auto script = parseClean(
        "ScriptName S\n"
        "Auto State MyState\n"
        "  int Function MyFunction()\n"
        "    Return 2\n"
        "  EndFunction\n"
        "  Event OnActivate(ObjectReference a)\n"
        "  EndEvent\n"
        "EndState\n");
    REQUIRE(script.states.size() == 1);
    CHECK(script.states[0].isAuto);
    CHECK(script.states[0].name == "MyState");
    REQUIRE(script.states[0].functions.size() == 1);
    REQUIRE(script.states[0].events.size() == 1);
    CHECK(script.states[0].functions[0].stateName == "MyState");
    CHECK(script.functions.empty());
}

namespace {

// The parse results must outlive the returned pointers, so each parsed
// script is parked in a run-lifetime holder (statements are heap nodes, so
// their addresses stay valid after the Script moves into the holder).
std::vector<const Statement*> bodyOf(std::string_view statements) {
    static std::vector<Script> scripts;
    scripts.push_back(parseClean("ScriptName S\nFunction F()\n" + std::string(statements) +
                                 "\nEndFunction\n"));
    const auto& script = scripts.back();
    REQUIRE(script.functions.size() == 1);
    std::vector<const Statement*> body;
    for (const auto& statement : script.functions[0].body) body.push_back(statement.get());
    return body;
}

const Expression* valueOf(std::string_view expression) {
    const auto body = bodyOf("x = " + std::string(expression));
    REQUIRE(body.size() == 1);
    const auto* assign = as<AssignStatement>(body[0]);
    REQUIRE(assign != nullptr);
    return assign->value.get();
}

}  // namespace

TEST_CASE("Parser define statement with initializer") {
    const auto body = bodyOf("float seconds = CurrentTimeInMinutes() * 60.0");
    REQUIRE(body.size() == 1);
    const auto* define = as<DefineStatement>(body[0]);
    REQUIRE(define != nullptr);
    CHECK(define->name == "seconds");
    CHECK(as<BinaryExpression>(define->initializer.get()) != nullptr);
}

TEST_CASE("Parser local may carry a const flag") {
    const auto body = bodyOf("string filename = \"S7System\" const");
    REQUIRE(body.size() == 1);
    const auto* define = as<DefineStatement>(body[0]);
    REQUIRE(define != nullptr);
    CHECK(define->flags == std::vector<std::string>{"const"});
}

TEST_CASE("Parser assignment is not mistaken for a definition") {
    const auto body = bodyOf("x = 5");
    REQUIRE(body.size() == 1);
    CHECK(as<AssignStatement>(body[0]) != nullptr);
}

TEST_CASE("Parser indexed assignment is not mistaken for an array definition") {
    const auto body = bodyOf("myArray[0] = 1");
    REQUIRE(body.size() == 1);
    const auto* assign = as<AssignStatement>(body[0]);
    REQUIRE(assign != nullptr);
    CHECK(as<IndexExpression>(assign->target.get()) != nullptr);
}

TEST_CASE("Parser array definition with empty brackets is a definition") {
    const auto body = bodyOf("int[] myArray = new int[10]");
    REQUIRE(body.size() == 1);
    const auto* define = as<DefineStatement>(body[0]);
    REQUIRE(define != nullptr);
    CHECK(define->type.isArray);
    CHECK(as<NewArrayExpression>(define->initializer.get()) != nullptr);
}

TEST_CASE("Parser compound assignment operators") {
    const auto body = bodyOf("MyObject.MyProperty += CoolFunction() * 10");
    REQUIRE(body.size() == 1);
    const auto* assign = as<AssignStatement>(body[0]);
    REQUIRE(assign != nullptr);
    CHECK(assign->op == fo4ide::papyrus::TokenKind::PlusAssign);
    CHECK(as<MemberExpression>(assign->target.get()) != nullptr);
}

TEST_CASE("Parser bare call is an expression statement") {
    const auto body = bodyOf("PlayAnimation(\"CoolStuff\")");
    REQUIRE(body.size() == 1);
    const auto* statement = as<ExpressionStatement>(body[0]);
    REQUIRE(statement != nullptr);
    CHECK(as<CallExpression>(statement->expression.get()) != nullptr);
}

TEST_CASE("Parser return with and without a value") {
    const auto withValue = bodyOf("Return 5");
    REQUIRE(withValue.size() == 1);
    const auto* ret = as<ReturnStatement>(withValue[0]);
    REQUIRE(ret != nullptr);
    CHECK(ret->value != nullptr);

    const auto without = bodyOf("Return");
    REQUIRE(without.size() == 1);
    const auto* bare = as<ReturnStatement>(without[0]);
    REQUIRE(bare != nullptr);
    CHECK(bare->value == nullptr);
}

TEST_CASE("Parser if elseif else") {
    const auto body = bodyOf(
        "if value > 10\n"
        "  x = 1\n"
        "elseif value < 10\n"
        "  x = -1\n"
        "else\n"
        "  x = 0\n"
        "endIf");
    REQUIRE(body.size() == 1);
    const auto* statement = as<IfStatement>(body[0]);
    REQUIRE(statement != nullptr);
    CHECK(statement->branches.size() == 2);
    CHECK(statement->branches[0].body.size() == 1);
    CHECK(statement->elseBody.size() == 1);
}

TEST_CASE("Parser while loop") {
    const auto body = bodyOf("while x < 10\n  DoCoolStuff()\n  x += 1\nendWhile");
    REQUIRE(body.size() == 1);
    const auto* loop = as<WhileStatement>(body[0]);
    REQUIRE(loop != nullptr);
    CHECK(loop->body.size() == 2);
    CHECK(as<BinaryExpression>(loop->condition.get()) != nullptr);
}

TEST_CASE("Parser multiplication binds tighter than addition") {
    const auto* root = as<BinaryExpression>(valueOf("2 + 4 * 10"));
    REQUIRE(root != nullptr);
    CHECK(root->op == fo4ide::papyrus::TokenKind::Plus);
    const auto* right = as<BinaryExpression>(root->right.get());
    REQUIRE(right != nullptr);
    CHECK(right->op == fo4ide::papyrus::TokenKind::Star);
}

TEST_CASE("Parser parentheses override precedence without adding a node") {
    const auto* root = as<BinaryExpression>(valueOf("(2 + 4) * 10"));
    REQUIRE(root != nullptr);
    CHECK(root->op == fo4ide::papyrus::TokenKind::Star);
    const auto* left = as<BinaryExpression>(root->left.get());
    REQUIRE(left != nullptr);
    CHECK(left->op == fo4ide::papyrus::TokenKind::Plus);
}

TEST_CASE("Parser comparison binds looser than arithmetic and tighter than and") {
    const auto* root = as<BinaryExpression>(valueOf("a + 1 > b && c"));
    REQUIRE(root != nullptr);
    CHECK(root->op == fo4ide::papyrus::TokenKind::And);
    const auto* left = as<BinaryExpression>(root->left.get());
    REQUIRE(left != nullptr);
    CHECK(left->op == fo4ide::papyrus::TokenKind::Greater);
}

TEST_CASE("Parser and binds tighter than or") {
    const auto* root = as<BinaryExpression>(valueOf("a || b && c"));
    REQUIRE(root != nullptr);
    CHECK(root->op == fo4ide::papyrus::TokenKind::Or);
    const auto* right = as<BinaryExpression>(root->right.get());
    REQUIRE(right != nullptr);
    CHECK(right->op == fo4ide::papyrus::TokenKind::And);
}

TEST_CASE("Parser unary minus and not") {
    const auto* minus = as<UnaryExpression>(valueOf("-a"));
    REQUIRE(minus != nullptr);
    CHECK(minus->op == fo4ide::papyrus::TokenKind::Minus);
    const auto* notExpr = as<UnaryExpression>(valueOf("!a"));
    REQUIRE(notExpr != nullptr);
    CHECK(notExpr->op == fo4ide::papyrus::TokenKind::Not);
}

TEST_CASE("Parser cast and type check") {
    const auto* cast = as<CastExpression>(valueOf("a as float"));
    REQUIRE(cast != nullptr);
    CHECK(cast->type.name == "float");
    const auto* check = as<TypeCheckExpression>(valueOf("myObject is ObjectReference"));
    REQUIRE(check != nullptr);
    CHECK(check->type.name == "ObjectReference");
}

TEST_CASE("Parser dot chain with call and index") {
    const auto* index = as<IndexExpression>(valueOf("(MyVariable as MyObject).MyFunction()[0]"));
    REQUIRE(index != nullptr);
    const auto* call = as<CallExpression>(index->target.get());
    REQUIRE(call != nullptr);
    CHECK(call->functionName() == "MyFunction");
    const auto* member = as<MemberExpression>(call->callee.get());
    REQUIRE(member != nullptr);
    CHECK(as<CastExpression>(member->target.get()) != nullptr);
}

TEST_CASE("Parser array length is a member access") {
    const auto* member = as<MemberExpression>(valueOf("myArray.Length"));
    REQUIRE(member != nullptr);
    CHECK(member->name == "Length");
}

TEST_CASE("Parser named argument is distinguished from a comparison") {
    const auto* call = as<CallExpression>(valueOf("MyFunction(howMuch = 1, other == 2)"));
    REQUIRE(call != nullptr);
    REQUIRE(call->arguments.size() == 2);
    CHECK(call->arguments[0].name == "howMuch");
    CHECK(!call->arguments[1].name.has_value());
    CHECK(as<BinaryExpression>(call->arguments[1].value.get()) != nullptr);
}

TEST_CASE("Parser new struct versus new array") {
    CHECK(as<NewStructExpression>(valueOf("new Point")) != nullptr);
    const auto* array = as<NewArrayExpression>(valueOf("new MyScript[5 * count]"));
    REQUIRE(array != nullptr);
    CHECK(as<BinaryExpression>(array->size.get()) != nullptr);
}

TEST_CASE("Parser namespaced script is one atom not a member chain") {
    const auto* call = as<CallExpression>(valueOf("MyNamespace:MyScript.MyGlobal()"));
    REQUIRE(call != nullptr);
    const auto* member = as<MemberExpression>(call->callee.get());
    REQUIRE(member != nullptr);
    const auto* target = as<IdentifierExpression>(member->target.get());
    REQUIRE(target != nullptr);
    CHECK(target->name == "MyNamespace:MyScript");
}

TEST_CASE("Parser none true and false are literals") {
    const auto* none = as<LiteralExpression>(valueOf("none"));
    REQUIRE(none != nullptr);
    CHECK(none->kind == fo4ide::papyrus::LiteralKind::None);
    const auto* truth = as<LiteralExpression>(valueOf("true"));
    REQUIRE(truth != nullptr);
    CHECK(truth->kind == fo4ide::papyrus::LiteralKind::Bool);
}

TEST_CASE("Parser a broken line does not cost the declarations after it") {
    const auto script = parse("ScriptName S\nint Property Good1 auto\nint Property % auto\nint Property Good2 auto\n");
    CHECK(script.hasErrors());
    // The oracle asserts Contain({Good1, Good2}): the broken line still yields a
    // placeholder property with an empty name, so exact equality would fail.
    std::vector<std::string> names;
    for (const auto& property : script.properties) names.push_back(property.name);
    CHECK(std::find(names.begin(), names.end(), "Good1") != names.end());
    CHECK(std::find(names.begin(), names.end(), "Good2") != names.end());
}

TEST_CASE("Parser a broken statement does not cost the rest of the function") {
    const auto script = parse(
        "ScriptName S\n"
        "Function F()\n"
        "  x = 1\n"
        "  y = *\n"
        "  z = 3\n"
        "EndFunction\n"
        "Function G()\n"
        "EndFunction\n");
    CHECK(script.hasErrors());
    REQUIRE(script.functions.size() == 2);
    CHECK(script.functions[0].name == "F");
    CHECK(script.functions[1].name == "G");
    CHECK(script.functions[0].body.size() >= 2);
}

TEST_CASE("Parser unterminated function is reported and does not hang") {
    const auto script = parse("ScriptName S\nFunction F()\n  x = 1\n");
    CHECK(hasCode(script, fo4ide::papyrus::DiagnosticCode::UnterminatedBlock));
}

TEST_CASE("Parser diagnostics are capped rather than unbounded") {
    std::string junk = "ScriptName S\n";
    for (int i = 0; i < 500; ++i) junk += "%\n";
    const auto script = parse(junk);
    CHECK(script.diagnostics.size() <= 201);
}

TEST_CASE("Parser empty input produces a script and a single complaint") {
    const auto script = parse("");
    CHECK(hasCode(script, fo4ide::papyrus::DiagnosticCode::ExpectedScriptName));
    CHECK(script.diagnostics.size() == 1);
}

TEST_CASE("Parser diagnostics carry the file path when one is given") {
    const auto script = Parser::parse("nonsense\n", std::string("/tmp/Foo.psc"));
    CHECK(script.filePath == "/tmp/Foo.psc");
    REQUIRE(!script.diagnostics.empty());
    for (const auto& diagnostic : script.diagnostics) {
        CHECK(diagnostic.file == "/tmp/Foo.psc");
    }
}
