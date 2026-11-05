#include <algorithm>
#include "../helpers/TempRoot.hpp"
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/papyrus/Binding.hpp>
#include <fo4ide/papyrus/Resolver.hpp>
#include <fo4ide/papyrus/ScriptIndex.hpp>

namespace {

using fo4ide::papyrus::AssignStatement;
using fo4ide::papyrus::Binding;
using fo4ide::papyrus::BindingKind;
using fo4ide::papyrus::DefineStatement;
using fo4ide::papyrus::DiagnosticCode;
using fo4ide::papyrus::Expression;
using fo4ide::papyrus::IdentifierExpression;
using fo4ide::papyrus::IndexExpression;
using fo4ide::papyrus::Node;
using fo4ide::papyrus::NodeKind;
using fo4ide::papyrus::Resolution;
using fo4ide::papyrus::Resolver;
using fo4ide::papyrus::Script;
using fo4ide::papyrus::ScriptIndex;
using fo4ide::papyrus::Type;
using fo4ide::papyrus::TypeKind;

std::string uniqueRoot() {
    const auto base = std::filesystem::temp_directory_path() / "fo4re-resolve";
    std::filesystem::create_directories(base);
    return (base / fo4ide_test::uniqueTempName())
        .string();
}

struct Fixture {
    std::string root = uniqueRoot();
    // The index owns every parsed Script; the returned Resolution points into
    // them, so the index must outlive the resolution (same lifetime contract
    // as the C# GC'd index keeping its scripts reachable). addRoot runs lazily
    // in resolve() so it sees all Write() output, like the C# tests.
    ScriptIndex index;

    ~Fixture() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    void write(std::string_view scriptName, std::string_view source) {
        std::string relative(scriptName);
        std::replace(relative.begin(), relative.end(), ':', '/');
        const auto path = std::filesystem::path(root) / (relative + ".psc");
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path);
        out << source;
    }

    Resolution resolve(std::string_view scriptName) {
        index.addRoot(root);
        const Script* script = index.resolve(scriptName);
        REQUIRE(script != nullptr);
        return Resolver(index).resolve(script);
    }
};

// Visits every statement and expression node in the script, matching the C#
// tests' Walk(...).OfType<...>() traversal.
std::vector<const Node*> walkExpressions(const Script& script) {
    std::vector<const Node*> out;
    const auto visitExpression = [&](auto&& self, const Node* node) -> void {
        out.push_back(node);
        switch (node->nodeKind()) {
            case NodeKind::MemberExpression: {
                const auto* member = static_cast<const fo4ide::papyrus::MemberExpression*>(node);
                self(self, member->target.get());
                break;
            }
            case NodeKind::IndexExpression: {
                const auto* index = static_cast<const IndexExpression*>(node);
                self(self, index->target.get());
                self(self, index->index.get());
                break;
            }
            case NodeKind::CallExpression: {
                const auto* call = static_cast<const fo4ide::papyrus::CallExpression*>(node);
                self(self, call->callee.get());
                for (const auto& arg : call->arguments) self(self, arg.value.get());
                break;
            }
            case NodeKind::UnaryExpression: {
                const auto* unary = static_cast<const fo4ide::papyrus::UnaryExpression*>(node);
                self(self, unary->operand.get());
                break;
            }
            case NodeKind::BinaryExpression: {
                const auto* bin = static_cast<const fo4ide::papyrus::BinaryExpression*>(node);
                self(self, bin->left.get());
                self(self, bin->right.get());
                break;
            }
            case NodeKind::CastExpression: {
                const auto* cast = static_cast<const fo4ide::papyrus::CastExpression*>(node);
                self(self, cast->operand.get());
                break;
            }
            case NodeKind::TypeCheckExpression: {
                const auto* check = static_cast<const fo4ide::papyrus::TypeCheckExpression*>(node);
                self(self, check->operand.get());
                break;
            }
            case NodeKind::NewArrayExpression: {
                const auto* newArray = static_cast<const fo4ide::papyrus::NewArrayExpression*>(node);
                self(self, newArray->size.get());
                break;
            }
            default:
                break;
        }
    };

    const auto visitStatement = [&](auto&& self, const Node* node) -> void {
        out.push_back(node);
        switch (node->nodeKind()) {
            case NodeKind::DefineStatement: {
                const auto* def = static_cast<const DefineStatement*>(node);
                if (def->initializer) visitExpression(visitExpression, def->initializer.get());
                break;
            }
            case NodeKind::AssignStatement: {
                const auto* assign = static_cast<const AssignStatement*>(node);
                visitExpression(visitExpression, assign->target.get());
                visitExpression(visitExpression, assign->value.get());
                break;
            }
            case NodeKind::ExpressionStatement: {
                const auto* expr = static_cast<const fo4ide::papyrus::ExpressionStatement*>(node);
                visitExpression(visitExpression, expr->expression.get());
                break;
            }
            case NodeKind::ReturnStatement: {
                const auto* ret = static_cast<const fo4ide::papyrus::ReturnStatement*>(node);
                if (ret->value) visitExpression(visitExpression, ret->value.get());
                break;
            }
            case NodeKind::IfStatement: {
                const auto* iff = static_cast<const fo4ide::papyrus::IfStatement*>(node);
                for (const auto& branch : iff->branches) {
                    visitExpression(visitExpression, branch.condition.get());
                    for (const auto& stmt : branch.body) self(self, stmt.get());
                }
                for (const auto& stmt : iff->elseBody) self(self, stmt.get());
                break;
            }
            case NodeKind::WhileStatement: {
                const auto* wh = static_cast<const fo4ide::papyrus::WhileStatement*>(node);
                visitExpression(visitExpression, wh->condition.get());
                for (const auto& stmt : wh->body) self(self, stmt.get());
                break;
            }
            default:
                break;
        }
    };

    for (const auto& fn : script.functions) {
        for (const auto& stmt : fn.body) visitStatement(visitStatement, stmt.get());
    }
    for (const auto& ev : script.events) {
        for (const auto& stmt : ev.body) visitStatement(visitStatement, stmt.get());
    }
    for (const auto& state : script.states) {
        for (const auto& fn : state.functions) {
            for (const auto& stmt : fn.body) visitStatement(visitStatement, stmt.get());
        }
        for (const auto& ev : state.events) {
            for (const auto& stmt : ev.body) visitStatement(visitStatement, stmt.get());
        }
    }
    return out;
}

std::vector<const Binding*> bindingsNamed(const Resolution& r, std::string_view name) {
    std::vector<const Binding*> out;
    for (const auto& [key, value] : r.bindings) {
        const auto* id = dynamic_cast<const IdentifierExpression*>(key);
        if (id && id->name == name) out.push_back(&value);
    }
    return out;
}

const Binding& only(const Resolution& r, std::string_view name) {
    const auto all = bindingsNamed(r, name);
    REQUIRE(all.size() == 1);
    return *all[0];
}

const Type* assignedTypeIn(const Resolution& r, std::string_view target) {
    const auto nodes = walkExpressions(*r.script);
    const AssignStatement* found = nullptr;
    for (const Node* node : nodes) {
        const auto* assign = dynamic_cast<const AssignStatement*>(node);
        if (!assign) continue;
        const auto* id = dynamic_cast<const IdentifierExpression*>(assign->target.get());
        if (id && id->name == target) {
            found = assign;
            break;
        }
    }
    if (found == nullptr) FAIL("no assignment to " << target);
    return r.typeOf(found->value.get());
}

const Node* singleNodeOfKind(const Resolution& r, NodeKind kind) {
    const auto nodes = walkExpressions(*r.script);
    const Node* found = nullptr;
    for (const Node* node : nodes) {
        if (node->nodeKind() == kind) {
            REQUIRE(found == nullptr);
            found = node;
        }
    }
    REQUIRE(found != nullptr);
    return found;
}

}  // namespace

TEST_CASE("Resolver a local binds to its definition with its declared type") {
    Fixture f;
    f.write("A", "\nScriptName A\nFunction Go()\n    int count = 1\n    count = 2\nEndFunction");
    const auto r = f.resolve("A");

    const auto& binding = only(r, "count");
    CHECK(binding.kind == BindingKind::Local);
    CHECK(binding.type == Type::int_());
}

TEST_CASE("Resolver a parameter binds with its declared type") {
    Fixture f;
    f.write("A", "\nScriptName A\nFunction Go(string label)\n    label = \"x\"\nEndFunction");
    const auto r = f.resolve("A");

    CHECK(only(r, "label").kind == BindingKind::Parameter);
    CHECK(only(r, "label").type == Type::string_());
}

TEST_CASE("Resolver a variable defined in a block is not visible after it") {
    Fixture f;
    f.write("A",
            "\nScriptName A\nFunction Go(bool flag)\n    If flag\n        int inner = 1\n    EndIf\n"
            "    inner = 2\nEndFunction");
    const auto r = f.resolve("A");

    REQUIRE(r.diagnostics.size() == 1);
    CHECK(r.diagnostics[0].code == DiagnosticCode::UnresolvedName);
    CHECK(r.diagnostics[0].message.find("inner") != std::string::npos);
}

TEST_CASE("Resolver an initializer is resolved before the variable it initializes exists") {
    Fixture f;
    f.write("A", "\nScriptName A\nint outer\nFunction Go()\n    int outer2 = outer\nEndFunction");
    const auto r = f.resolve("A");

    CHECK(only(r, "outer").kind == BindingKind::ScriptVariable);
    CHECK(r.diagnostics.empty());
}

TEST_CASE("Resolver a member inherited from a parent binds to the parent's declaration") {
    Fixture f;
    f.write("Base", "\nScriptName Base\nint Counter\nFunction Tick()\nEndFunction");
    f.write("Child",
            "\nScriptName Child extends Base\nFunction Go()\n    Counter = 1\n    Tick()\n"
            "EndFunction");
    const auto r = f.resolve("Child");

    const auto& counter = only(r, "Counter");
    CHECK(counter.kind == BindingKind::ScriptVariable);
    REQUIRE(counter.owner != nullptr);
    CHECK(counter.owner->name == "Base");
    CHECK(counter.type == Type::int_());

    CHECK(only(r, "Tick").kind == BindingKind::Function);
    CHECK(r.diagnostics.empty());
}

TEST_CASE("Resolver a property resolves with its type") {
    Fixture f;
    f.write("A",
            "\nScriptName A\nint Property Health Auto\nFunction Go()\n    Health = 5\nEndFunction");
    const auto r = f.resolve("A");

    const auto& health = only(r, "Health");
    CHECK(health.kind == BindingKind::Property);
    CHECK(health.type == Type::int_());
}

TEST_CASE("Resolver self is the type of the script being resolved") {
    Fixture f;
    f.write("A", "\nScriptName A\nFunction Go()\n    A other = Self\nEndFunction");
    const auto r = f.resolve("A");

    const auto& self = only(r, "Self");
    CHECK(self.kind == BindingKind::SelfKeyword);
    CHECK(self.type == Type::object("A"));
}

TEST_CASE("Resolver parent is the type of the parent script") {
    Fixture f;
    f.write("Base", "\nScriptName Base\nFunction Go()\nEndFunction");
    f.write("Child",
            "\nScriptName Child extends Base\nFunction Go()\n    Parent.Go()\nEndFunction");
    const auto r = f.resolve("Child");

    CHECK(only(r, "Parent").type == Type::object("Base"));
    CHECK(r.diagnostics.empty());
}

TEST_CASE("Resolver a global function cannot see script members") {
    Fixture f;
    f.write("A", "\nScriptName A\nint Counter\nint Function Go() global\n    return Counter\nEndFunction");
    const auto r = f.resolve("A");

    REQUIRE(r.diagnostics.size() == 1);
    CHECK(r.diagnostics[0].message.find("Counter") != std::string::npos);
}

TEST_CASE("Resolver a member on an object typed expression resolves on that script's chain") {
    Fixture f;
    f.write("Target", "\nScriptName Target\nfloat Property Weight Auto");
    f.write("A",
            "\nScriptName A\nTarget Property Thing Auto\nFunction Go()\n    float w = Thing.Weight\n"
            "EndFunction");
    const auto r = f.resolve("A");

    const Binding* member = nullptr;
    for (const auto& [key, value] : r.bindings) {
        if (value.name == "Weight") {
            REQUIRE(member == nullptr);
            member = &value;
        }
    }
    REQUIRE(member != nullptr);
    CHECK(member->kind == BindingKind::Property);
    REQUIRE(member->owner != nullptr);
    CHECK(member->owner->name == "Target");
    CHECK(member->type == Type::float_());
    CHECK(r.diagnostics.empty());
}

TEST_CASE("Resolver a member that does not exist on a known type is reported") {
    Fixture f;
    f.write("Target", "\nScriptName Target\nfloat Property Weight Auto");
    f.write("A",
            "\nScriptName A\nTarget Property Thing Auto\nFunction Go()\n    float w = Thing.Nope\n"
            "EndFunction");
    const auto r = f.resolve("A");

    REQUIRE(r.diagnostics.size() == 1);
    CHECK(r.diagnostics[0].code == DiagnosticCode::UnknownMember);
    CHECK(r.diagnostics[0].message.find("Nope") != std::string::npos);
    CHECK(r.diagnostics[0].message.find("Target") != std::string::npos);
}

TEST_CASE("Resolver a script name receiver resolves a global function") {
    Fixture f;
    f.write("Game", "\nScriptName Game\nActor Function GetPlayer() global native");
    f.write("Actor", "ScriptName Actor");
    f.write("A", "\nScriptName A\nFunction Go()\n    Actor p = Game.GetPlayer()\nEndFunction");
    const auto r = f.resolve("A");

    CHECK(only(r, "Game").kind == BindingKind::Script);

    bool found = false;
    for (const auto& [key, value] : r.bindings) {
        if (value.name == "GetPlayer") {
            CHECK(value.kind == BindingKind::Function);
            CHECK(value.type == Type::object("Actor"));
            found = true;
        }
    }
    CHECK(found);
    CHECK(r.diagnostics.empty());
}

TEST_CASE("Resolver an imported script's global function is callable unqualified") {
    Fixture f;
    f.write("Utility", "\nScriptName Utility\nfloat Function GetCurrentRealTime() global native");
    f.write("A",
            "\nScriptName A\nImport Utility\nFunction Go()\n    float t = GetCurrentRealTime()\n"
            "EndFunction");
    const auto r = f.resolve("A");

    const auto& fn = only(r, "GetCurrentRealTime");
    CHECK(fn.kind == BindingKind::Function);
    REQUIRE(fn.owner != nullptr);
    CHECK(fn.owner->name == "Utility");
    CHECK(r.diagnostics.empty());
}

TEST_CASE("Resolver an import does not bring in instance members") {
    Fixture f;
    f.write("Utility", "\nScriptName Utility\nint NotGlobal");
    f.write("A", "\nScriptName A\nImport Utility\nFunction Go()\n    int x = NotGlobal\nEndFunction");
    const auto r = f.resolve("A");

    REQUIRE(r.diagnostics.size() == 1);
    CHECK(r.diagnostics[0].message.find("NotGlobal") != std::string::npos);
}

TEST_CASE("Resolver an array element has the element type") {
    Fixture f;
    f.write("A",
            "\nScriptName A\nFunction Go()\n    string[] names = new string[4]\n"
            "    string first = names[0]\nEndFunction");
    const auto r = f.resolve("A");

    const auto nodes = walkExpressions(*r.script);
    const Node* initializer = nullptr;
    for (const Node* node : nodes) {
        const auto* def = dynamic_cast<const DefineStatement*>(node);
        if (def && def->name == "names") initializer = def->initializer.get();
    }
    REQUIRE(initializer != nullptr);
    CHECK(*r.typeOf(initializer) == Type::arrayOf(Type::string_()));

    const auto* index = dynamic_cast<const IndexExpression*>(singleNodeOfKind(r, NodeKind::IndexExpression));
    REQUIRE(index != nullptr);
    CHECK(*r.typeOf(index) == Type::string_());
    CHECK(r.diagnostics.empty());
}

TEST_CASE("Resolver array builtins resolve") {
    Fixture f;
    f.write("A",
            "\nScriptName A\nFunction Go()\n    int[] items = new int[4]\n    int x = items.Length\n"
            "    int y = items.Find(1)\n    int z = items.RFind(1)\n    items.Add(1)\n    items.Clear()\n"
            "    items.RemoveLast()\nEndFunction");
    const auto r = f.resolve("A");

    // The oracle tests each builtin in its own case; the C# binds both the
    // member node and the call node to the same ArrayMember binding, so we
    // assert presence + type per member instead of an exact count.
    struct Case {
        std::string_view name;
        TypeKind kind;
    };
    const Case cases[] = {
        {"Length", TypeKind::Int},         {"Find", TypeKind::Int},
        {"RFind", TypeKind::Int},         {"Add", TypeKind::None},
        {"Clear", TypeKind::None},        {"RemoveLast", TypeKind::None},
    };
    for (const auto& c : cases) {
        bool found = false;
        for (const auto& [key, value] : r.bindings) {
            if (value.kind == BindingKind::ArrayMember && value.name == c.name) {
                CHECK(value.type.kind == c.kind);
                found = true;
            }
        }
        CHECK(found);
    }
    CHECK(r.diagnostics.empty());
}

TEST_CASE("Resolver a struct member resolves with its declared type") {
    Fixture f;
    f.write("A",
            "\nScriptName A\nStruct Point\n    int X\n    float Y\nEndStruct\n"
            "Function Go()\n    Point p = new Point\n    float y = p.Y\nEndFunction");
    const auto r = f.resolve("A");

    CHECK(only(r, "p").type.kind == TypeKind::Struct);
    const Binding* member = nullptr;
    for (const auto& [key, value] : r.bindings) {
        if (value.kind == BindingKind::StructMember) {
            REQUIRE(member == nullptr);
            member = &value;
        }
    }
    REQUIRE(member != nullptr);
    CHECK(member->name == "Y");
    CHECK(member->type == Type::float_());
    CHECK(r.diagnostics.empty());
}

TEST_CASE("Resolver two scripts with same named structs are distinct types") {
    Fixture f;
    f.write("A", "\nScriptName A\nStruct Point\n    int X\nEndStruct");
    f.write("B", "\nScriptName B\nStruct Point\n    int X\nEndStruct");

    const auto rA = f.resolve("A");
    const auto rB = f.resolve("B");
    REQUIRE(rA.script != nullptr);
    REQUIRE(rB.script != nullptr);

    const auto& aPoint = rA.script->structs[0];
    const auto& bPoint = rB.script->structs[0];
    const auto aType = Type::structOf("A", aPoint.name);
    const auto bType = Type::structOf("B", bPoint.name);
    CHECK(aType != bType);
}

TEST_CASE("Resolver operators produce the expected type") {
    struct Case {
        std::string_view expression;
        TypeKind expected;
    };
    const Case cases[] = {
        {"1 + 2", TypeKind::Int},     {"1 + 2.0", TypeKind::Float},
        {"1.0 * 2", TypeKind::Float}, {"\"a\" + 1", TypeKind::String},
        {"1 < 2", TypeKind::Bool},    {"1 == 2", TypeKind::Bool},
        {"true && false", TypeKind::Bool}, {"!true", TypeKind::Bool},
        {"-3", TypeKind::Int},
    };
    for (const auto& c : cases) {
        Fixture f;
        f.write("A",
                "\nScriptName A\nvar slot\nFunction Go()\n    slot = " + std::string(c.expression) +
                    "\nEndFunction");
        const auto r = f.resolve("A");
        const Type* type = assignedTypeIn(r, "slot");
        REQUIRE(type != nullptr);
        CHECK(type->kind == c.expected);
    }
}

TEST_CASE("Resolver a cast has the type it casts to and a type check is a bool") {
    Fixture f;
    f.write("A",
            "\nScriptName A\nvar slot\nFunction Go(float f)\n    slot = f as int\n"
            "    slot = f is int\nEndFunction");
    const auto r = f.resolve("A");

    const auto nodes = walkExpressions(*r.script);
    std::vector<const Node*> assigns;
    for (const Node* node : nodes) {
        if (node->nodeKind() == NodeKind::AssignStatement) assigns.push_back(node);
    }
    REQUIRE(assigns.size() == 2);
    const auto* assign0 = static_cast<const AssignStatement*>(assigns[0]);
    const auto* assign1 = static_cast<const AssignStatement*>(assigns[1]);
    REQUIRE(r.typeOf(assign0->value.get()) != nullptr);
    REQUIRE(r.typeOf(assign1->value.get()) != nullptr);
    CHECK(*r.typeOf(assign0->value.get()) == Type::int_());
    CHECK(*r.typeOf(assign1->value.get()) == Type::bool_());
}

TEST_CASE("Resolver a script whose parent is missing reports nothing and says so") {
    Fixture f;
    f.write("Child",
            "\nScriptName Child extends SomethingNotOnDisk\nFunction Go()\n    InheritedThing = 1\n"
            "    AlsoInherited()\nEndFunction");
    const auto r = f.resolve("Child");

    CHECK_FALSE(r.baseChainComplete);
    CHECK(r.diagnostics.empty());
}

TEST_CASE("Resolver a missing import also suppresses reporting") {
    Fixture f;
    f.write("A",
            "\nScriptName A\nImport NotOnDisk\nFunction Go()\n    MaybeFromTheImport()\nEndFunction");
    const auto r = f.resolve("A");

    CHECK_FALSE(r.baseChainComplete);
    CHECK(r.diagnostics.empty());
}

TEST_CASE("Resolver a call qualified by a script that is not on the roots is not a typo") {
    Fixture f;
    f.write("A",
            "\nScriptName A\nFunction Go()\n    SomeFrameworkNotOnDisk.RegisterFor(\"A\")\n"
            "EndFunction");
    const auto r = f.resolve("A");

    CHECK_FALSE(r.baseChainComplete);
    CHECK(r.diagnostics.empty());
}

TEST_CASE("Resolver a bare unknown name used as a value is still reported") {
    Fixture f;
    f.write("A", "\nScriptName A\nFunction Go()\n    int x = SomethingUndefined\nEndFunction");
    const auto r = f.resolve("A");

    REQUIRE(r.diagnostics.size() == 1);
    CHECK(r.diagnostics[0].code == DiagnosticCode::UnresolvedName);
}

TEST_CASE("Resolver a complete script with a genuine typo is reported") {
    Fixture f;
    f.write("A", "\nScriptName A\nint Counter\nFunction Go()\n    Countr = 1\nEndFunction");
    const auto r = f.resolve("A");

    CHECK(r.baseChainComplete);
    REQUIRE(r.diagnostics.size() == 1);
    CHECK(r.diagnostics[0].code == DiagnosticCode::UnresolvedName);
}
