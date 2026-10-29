#include <algorithm>
#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <fo4/papyrus/binding.hpp>
#include <fo4/papyrus/resolver.hpp>
#include <fo4/papyrus/script_index.hpp>

namespace {

using fo4::papyrus::AssignStatement;
using fo4::papyrus::Binding;
using fo4::papyrus::BindingKind;
using fo4::papyrus::DefineStatement;
using fo4::papyrus::DiagnosticCode;
using fo4::papyrus::Expression;
using fo4::papyrus::IdentifierExpression;
using fo4::papyrus::IndexExpression;
using fo4::papyrus::Node;
using fo4::papyrus::NodeKind;
using fo4::papyrus::Resolution;
using fo4::papyrus::Resolver;
using fo4::papyrus::Script;
using fo4::papyrus::ScriptIndex;
using fo4::papyrus::Type;
using fo4::papyrus::TypeKind;

std::string uniqueRoot() {
    const auto base = std::filesystem::temp_directory_path() / "fo4re-resolve";
    std::filesystem::create_directories(base);
    return (base / std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))
        .string();
}

struct Fixture {
    std::string root = uniqueRoot();
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
        if (script == nullptr) throw std::runtime_error("script not indexed: " + std::string(scriptName));
        return Resolver(index).resolve(script);
    }
};

std::vector<const Node*> walkExpressions(const Script& script) {
    std::vector<const Node*> out;
    const auto visitExpression = [&](auto&& self, const Node* node) -> void {
        out.push_back(node);
        switch (node->nodeKind()) {
            case NodeKind::MemberExpression: {
                const auto* member = static_cast<const fo4::papyrus::MemberExpression*>(node);
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
                const auto* call = static_cast<const fo4::papyrus::CallExpression*>(node);
                self(self, call->callee.get());
                for (const auto& arg : call->arguments) self(self, arg.value.get());
                break;
            }
            case NodeKind::UnaryExpression: {
                const auto* unary = static_cast<const fo4::papyrus::UnaryExpression*>(node);
                self(self, unary->operand.get());
                break;
            }
            case NodeKind::BinaryExpression: {
                const auto* bin = static_cast<const fo4::papyrus::BinaryExpression*>(node);
                self(self, bin->left.get());
                self(self, bin->right.get());
                break;
            }
            case NodeKind::CastExpression: {
                const auto* cast = static_cast<const fo4::papyrus::CastExpression*>(node);
                self(self, cast->operand.get());
                break;
            }
            case NodeKind::TypeCheckExpression: {
                const auto* check = static_cast<const fo4::papyrus::TypeCheckExpression*>(node);
                self(self, check->operand.get());
                break;
            }
            case NodeKind::NewArrayExpression: {
                const auto* newArray = static_cast<const fo4::papyrus::NewArrayExpression*>(node);
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
                const auto* expr = static_cast<const fo4::papyrus::ExpressionStatement*>(node);
                visitExpression(visitExpression, expr->expression.get());
                break;
            }
            case NodeKind::ReturnStatement: {
                const auto* ret = static_cast<const fo4::papyrus::ReturnStatement*>(node);
                if (ret->value) visitExpression(visitExpression, ret->value.get());
                break;
            }
            case NodeKind::IfStatement: {
                const auto* iff = static_cast<const fo4::papyrus::IfStatement*>(node);
                for (const auto& branch : iff->branches) {
                    visitExpression(visitExpression, branch.condition.get());
                    for (const auto& stmt : branch.body) self(self, stmt.get());
                }
                for (const auto& stmt : iff->elseBody) self(self, stmt.get());
                break;
            }
            case NodeKind::WhileStatement: {
                const auto* wh = static_cast<const fo4::papyrus::WhileStatement*>(node);
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
    if (all.size() != 1) throw std::runtime_error("expected one binding named " + std::string(name));
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
    if (found == nullptr) throw std::runtime_error("no assignment to " + std::string(target));
    return r.typeOf(found->value.get());
}

const Node* singleNodeOfKind(const Resolution& r, NodeKind kind) {
    const auto nodes = walkExpressions(*r.script);
    const Node* found = nullptr;
    for (const Node* node : nodes) {
        if (node->nodeKind() == kind) {
            if (found != nullptr) throw std::runtime_error("more than one node of the kind");
            found = node;
        }
    }
    if (found == nullptr) throw std::runtime_error("no node of the kind");
    return found;
}

}

TEST(PapyrusResolver, ALocalBindsToItsDefinitionWithItsDeclaredType) {
    Fixture f;
    f.write("A", "\nScriptName A\nFunction Go()\n    int count = 1\n    count = 2\nEndFunction");
    const auto r = f.resolve("A");

    const auto& binding = only(r, "count");
    EXPECT_TRUE(binding.kind == BindingKind::Local);
    EXPECT_TRUE(binding.type == Type::int_());
}

TEST(PapyrusResolver, AParameterBindsWithItsDeclaredType) {
    Fixture f;
    f.write("A", "\nScriptName A\nFunction Go(string label)\n    label = \"x\"\nEndFunction");
    const auto r = f.resolve("A");

    EXPECT_TRUE(only(r, "label").kind == BindingKind::Parameter);
    EXPECT_TRUE(only(r, "label").type == Type::string_());
}

TEST(PapyrusResolver, AVariableDefinedInABlockIsNotVisibleAfterIt) {
    Fixture f;
    f.write("A",
            "\nScriptName A\nFunction Go(bool flag)\n    If flag\n        int inner = 1\n    EndIf\n"
            "    inner = 2\nEndFunction");
    const auto r = f.resolve("A");

    ASSERT_TRUE(r.diagnostics.size() == 1);
    EXPECT_TRUE(r.diagnostics[0].code == DiagnosticCode::UnresolvedName);
    EXPECT_TRUE(r.diagnostics[0].message.find("inner") != std::string::npos);
}

TEST(PapyrusResolver, AnInitializerIsResolvedBeforeTheVariableItInitializesExists) {
    Fixture f;
    f.write("A", "\nScriptName A\nint outer\nFunction Go()\n    int outer2 = outer\nEndFunction");
    const auto r = f.resolve("A");

    EXPECT_TRUE(only(r, "outer").kind == BindingKind::ScriptVariable);
    EXPECT_TRUE(r.diagnostics.empty());
}

TEST(PapyrusResolver, AMemberInheritedFromAParentBindsToTheParentSDeclaration) {
    Fixture f;
    f.write("Base", "\nScriptName Base\nint Counter\nFunction Tick()\nEndFunction");
    f.write("Child",
            "\nScriptName Child extends Base\nFunction Go()\n    Counter = 1\n    Tick()\n"
            "EndFunction");
    const auto r = f.resolve("Child");

    const auto& counter = only(r, "Counter");
    EXPECT_TRUE(counter.kind == BindingKind::ScriptVariable);
    ASSERT_TRUE(counter.owner != nullptr);
    EXPECT_TRUE(counter.owner->name == "Base");
    EXPECT_TRUE(counter.type == Type::int_());

    EXPECT_TRUE(only(r, "Tick").kind == BindingKind::Function);
    EXPECT_TRUE(r.diagnostics.empty());
}

TEST(PapyrusResolver, APropertyResolvesWithItsType) {
    Fixture f;
    f.write("A",
            "\nScriptName A\nint Property Health Auto\nFunction Go()\n    Health = 5\nEndFunction");
    const auto r = f.resolve("A");

    const auto& health = only(r, "Health");
    EXPECT_TRUE(health.kind == BindingKind::Property);
    EXPECT_TRUE(health.type == Type::int_());
}

TEST(PapyrusResolver, SelfIsTheTypeOfTheScriptBeingResolved) {
    Fixture f;
    f.write("A", "\nScriptName A\nFunction Go()\n    A other = Self\nEndFunction");
    const auto r = f.resolve("A");

    const auto& self = only(r, "Self");
    EXPECT_TRUE(self.kind == BindingKind::SelfKeyword);
    EXPECT_TRUE(self.type == Type::object("A"));
}

TEST(PapyrusResolver, ParentIsTheTypeOfTheParentScript) {
    Fixture f;
    f.write("Base", "\nScriptName Base\nFunction Go()\nEndFunction");
    f.write("Child",
            "\nScriptName Child extends Base\nFunction Go()\n    Parent.Go()\nEndFunction");
    const auto r = f.resolve("Child");

    EXPECT_TRUE(only(r, "Parent").type == Type::object("Base"));
    EXPECT_TRUE(r.diagnostics.empty());
}

TEST(PapyrusResolver, AGlobalFunctionCannotSeeScriptMembers) {
    Fixture f;
    f.write("A", "\nScriptName A\nint Counter\nint Function Go() global\n    return Counter\nEndFunction");
    const auto r = f.resolve("A");

    ASSERT_TRUE(r.diagnostics.size() == 1);
    EXPECT_TRUE(r.diagnostics[0].message.find("Counter") != std::string::npos);
}

TEST(PapyrusResolver, AMemberOnAnObjectTypedExpressionResolvesOnThatScriptSChain) {
    Fixture f;
    f.write("Target", "\nScriptName Target\nfloat Property Weight Auto");
    f.write("A",
            "\nScriptName A\nTarget Property Thing Auto\nFunction Go()\n    float w = Thing.Weight\n"
            "EndFunction");
    const auto r = f.resolve("A");

    const Binding* member = nullptr;
    for (const auto& [key, value] : r.bindings) {
        if (value.name == "Weight") {
            ASSERT_TRUE(member == nullptr);
            member = &value;
        }
    }
    ASSERT_TRUE(member != nullptr);
    EXPECT_TRUE(member->kind == BindingKind::Property);
    ASSERT_TRUE(member->owner != nullptr);
    EXPECT_TRUE(member->owner->name == "Target");
    EXPECT_TRUE(member->type == Type::float_());
    EXPECT_TRUE(r.diagnostics.empty());
}

TEST(PapyrusResolver, AMemberThatDoesNotExistOnAKnownTypeIsReported) {
    Fixture f;
    f.write("Target", "\nScriptName Target\nfloat Property Weight Auto");
    f.write("A",
            "\nScriptName A\nTarget Property Thing Auto\nFunction Go()\n    float w = Thing.Nope\n"
            "EndFunction");
    const auto r = f.resolve("A");

    ASSERT_TRUE(r.diagnostics.size() == 1);
    EXPECT_TRUE(r.diagnostics[0].code == DiagnosticCode::UnknownMember);
    EXPECT_TRUE(r.diagnostics[0].message.find("Nope") != std::string::npos);
    EXPECT_TRUE(r.diagnostics[0].message.find("Target") != std::string::npos);
}

TEST(PapyrusResolver, AScriptNameReceiverResolvesAGlobalFunction) {
    Fixture f;
    f.write("Game", "\nScriptName Game\nActor Function GetPlayer() global native");
    f.write("Actor", "ScriptName Actor");
    f.write("A", "\nScriptName A\nFunction Go()\n    Actor p = Game.GetPlayer()\nEndFunction");
    const auto r = f.resolve("A");

    EXPECT_TRUE(only(r, "Game").kind == BindingKind::Script);

    bool found = false;
    for (const auto& [key, value] : r.bindings) {
        if (value.name == "GetPlayer") {
            EXPECT_TRUE(value.kind == BindingKind::Function);
            EXPECT_TRUE(value.type == Type::object("Actor"));
            found = true;
        }
    }
    EXPECT_TRUE(found);
    EXPECT_TRUE(r.diagnostics.empty());
}

TEST(PapyrusResolver, AnImportedScriptSGlobalFunctionIsCallableUnqualified) {
    Fixture f;
    f.write("Utility", "\nScriptName Utility\nfloat Function GetCurrentRealTime() global native");
    f.write("A",
            "\nScriptName A\nImport Utility\nFunction Go()\n    float t = GetCurrentRealTime()\n"
            "EndFunction");
    const auto r = f.resolve("A");

    const auto& fn = only(r, "GetCurrentRealTime");
    EXPECT_TRUE(fn.kind == BindingKind::Function);
    ASSERT_TRUE(fn.owner != nullptr);
    EXPECT_TRUE(fn.owner->name == "Utility");
    EXPECT_TRUE(r.diagnostics.empty());
}

TEST(PapyrusResolver, AnImportDoesNotBringInInstanceMembers) {
    Fixture f;
    f.write("Utility", "\nScriptName Utility\nint NotGlobal");
    f.write("A", "\nScriptName A\nImport Utility\nFunction Go()\n    int x = NotGlobal\nEndFunction");
    const auto r = f.resolve("A");

    ASSERT_TRUE(r.diagnostics.size() == 1);
    EXPECT_TRUE(r.diagnostics[0].message.find("NotGlobal") != std::string::npos);
}

TEST(PapyrusResolver, AnArrayElementHasTheElementType) {
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
    ASSERT_TRUE(initializer != nullptr);
    EXPECT_TRUE(*r.typeOf(initializer) == Type::arrayOf(Type::string_()));

    const auto* index = dynamic_cast<const IndexExpression*>(singleNodeOfKind(r, NodeKind::IndexExpression));
    ASSERT_TRUE(index != nullptr);
    EXPECT_TRUE(*r.typeOf(index) == Type::string_());
    EXPECT_TRUE(r.diagnostics.empty());
}

TEST(PapyrusResolver, ArrayBuiltinsResolve) {
    Fixture f;
    f.write("A",
            "\nScriptName A\nFunction Go()\n    int[] items = new int[4]\n    int x = items.Length\n"
            "    int y = items.Find(1)\n    int z = items.RFind(1)\n    items.Add(1)\n    items.Clear()\n"
            "    items.RemoveLast()\nEndFunction");
    const auto r = f.resolve("A");

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
                EXPECT_TRUE(value.type.kind == c.kind);
                found = true;
            }
        }
        EXPECT_TRUE(found);
    }
    EXPECT_TRUE(r.diagnostics.empty());
}

TEST(PapyrusResolver, AStructMemberResolvesWithItsDeclaredType) {
    Fixture f;
    f.write("A",
            "\nScriptName A\nStruct Point\n    int X\n    float Y\nEndStruct\n"
            "Function Go()\n    Point p = new Point\n    float y = p.Y\nEndFunction");
    const auto r = f.resolve("A");

    EXPECT_TRUE(only(r, "p").type.kind == TypeKind::Struct);
    const Binding* member = nullptr;
    for (const auto& [key, value] : r.bindings) {
        if (value.kind == BindingKind::StructMember) {
            ASSERT_TRUE(member == nullptr);
            member = &value;
        }
    }
    ASSERT_TRUE(member != nullptr);
    EXPECT_TRUE(member->name == "Y");
    EXPECT_TRUE(member->type == Type::float_());
    EXPECT_TRUE(r.diagnostics.empty());
}

TEST(PapyrusResolver, TwoScriptsWithSameNamedStructsAreDistinctTypes) {
    Fixture f;
    f.write("A", "\nScriptName A\nStruct Point\n    int X\nEndStruct");
    f.write("B", "\nScriptName B\nStruct Point\n    int X\nEndStruct");

    const auto rA = f.resolve("A");
    const auto rB = f.resolve("B");
    ASSERT_TRUE(rA.script != nullptr);
    ASSERT_TRUE(rB.script != nullptr);

    const auto& aPoint = rA.script->structs[0];
    const auto& bPoint = rB.script->structs[0];
    const auto aType = Type::structOf("A", aPoint.name);
    const auto bType = Type::structOf("B", bPoint.name);
    EXPECT_TRUE(aType != bType);
}

TEST(PapyrusResolver, OperatorsProduceTheExpectedType) {
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
        ASSERT_TRUE(type != nullptr);
        EXPECT_TRUE(type->kind == c.expected);
    }
}

TEST(PapyrusResolver, ACastHasTheTypeItCastsToAndATypeCheckIsABool) {
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
    ASSERT_TRUE(assigns.size() == 2);
    const auto* assign0 = static_cast<const AssignStatement*>(assigns[0]);
    const auto* assign1 = static_cast<const AssignStatement*>(assigns[1]);
    ASSERT_TRUE(r.typeOf(assign0->value.get()) != nullptr);
    ASSERT_TRUE(r.typeOf(assign1->value.get()) != nullptr);
    EXPECT_TRUE(*r.typeOf(assign0->value.get()) == Type::int_());
    EXPECT_TRUE(*r.typeOf(assign1->value.get()) == Type::bool_());
}

TEST(PapyrusResolver, AScriptWhoseParentIsMissingReportsNothingAndSaysSo) {
    Fixture f;
    f.write("Child",
            "\nScriptName Child extends SomethingNotOnDisk\nFunction Go()\n    InheritedThing = 1\n"
            "    AlsoInherited()\nEndFunction");
    const auto r = f.resolve("Child");

    EXPECT_FALSE(r.baseChainComplete);
    EXPECT_TRUE(r.diagnostics.empty());
}

TEST(PapyrusResolver, AMissingImportAlsoSuppressesReporting) {
    Fixture f;
    f.write("A",
            "\nScriptName A\nImport NotOnDisk\nFunction Go()\n    MaybeFromTheImport()\nEndFunction");
    const auto r = f.resolve("A");

    EXPECT_FALSE(r.baseChainComplete);
    EXPECT_TRUE(r.diagnostics.empty());
}

TEST(PapyrusResolver, ACallQualifiedByAScriptThatIsNotOnTheRootsIsNotATypo) {
    Fixture f;
    f.write("A",
            "\nScriptName A\nFunction Go()\n    SomeFrameworkNotOnDisk.RegisterFor(\"A\")\n"
            "EndFunction");
    const auto r = f.resolve("A");

    EXPECT_FALSE(r.baseChainComplete);
    EXPECT_TRUE(r.diagnostics.empty());
}

TEST(PapyrusResolver, ABareUnknownNameUsedAsAValueIsStillReported) {
    Fixture f;
    f.write("A", "\nScriptName A\nFunction Go()\n    int x = SomethingUndefined\nEndFunction");
    const auto r = f.resolve("A");

    ASSERT_TRUE(r.diagnostics.size() == 1);
    EXPECT_TRUE(r.diagnostics[0].code == DiagnosticCode::UnresolvedName);
}

TEST(PapyrusResolver, ACompleteScriptWithAGenuineTypoIsReported) {
    Fixture f;
    f.write("A", "\nScriptName A\nint Counter\nFunction Go()\n    Countr = 1\nEndFunction");
    const auto r = f.resolve("A");

    EXPECT_TRUE(r.baseChainComplete);
    ASSERT_TRUE(r.diagnostics.size() == 1);
    EXPECT_TRUE(r.diagnostics[0].code == DiagnosticCode::UnresolvedName);
}

// Invariant: a two-script extends cycle terminates the base chain and leaves the resolution marked incomplete.
TEST(PapyrusResolver, ACyclicExtendsPairStopsAndMarksTheChainIncomplete) {
    Fixture f;
    f.write("A", "\nScriptName A extends B\nFunction Go()\nEndFunction");
    f.write("B", "\nScriptName B extends A\nFunction Go()\nEndFunction");
    const auto r = f.resolve("A");

    EXPECT_FALSE(r.baseChainComplete);
}

// Invariant: a script extending itself terminates the base chain at itself and is marked incomplete.
TEST(PapyrusResolver, AScriptThatExtendsItselfStopsAndMarksTheChainIncomplete) {
    Fixture f;
    f.write("A", "\nScriptName A extends A\nFunction Go()\nEndFunction");
    const auto r = f.resolve("A");

    EXPECT_FALSE(r.baseChainComplete);
}

// Invariant: a missing parent two levels up marks the chain incomplete, not only a missing direct parent.
TEST(PapyrusResolver, AMissingParentTwoLevelsUpMarksTheChainIncomplete) {
    Fixture f;
    f.write("B", "\nScriptName B extends SomethingNotOnDisk\nFunction Poke()\nEndFunction");
    f.write("A", "\nScriptName A extends B\nFunction Go()\n    Poke()\nEndFunction");
    const auto r = f.resolve("A");

    EXPECT_FALSE(r.baseChainComplete);
}
