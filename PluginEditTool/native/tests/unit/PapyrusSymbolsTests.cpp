#include <catch2/catch_test_macros.hpp>
#include "../helpers/TempRoot.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include <fo4ide/papyrus/Parser.hpp>
#include <fo4ide/papyrus/ScriptIndex.hpp>
#include <fo4ide/papyrus/Symbols.hpp>

using namespace fo4ide::papyrus;

namespace {

struct SourceRoot {
    std::string path = [] {
        const auto dir = std::filesystem::temp_directory_path() /
                         ("fo4ide-symbols-" +
                          fo4ide_test::uniqueTempName());
        std::filesystem::create_directories(dir);
        return dir.string();
    }();

    ~SourceRoot() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }

    std::string write(const std::string& name, const std::string& content) {
        const auto file = (std::filesystem::path(path) / name).string();
        std::filesystem::create_directories(std::filesystem::path(file).parent_path());
        std::ofstream out(file, std::ios::binary);
        out << content;
        return file;
    }
};

// ("a|b") -> ("ab", 1): strips the caret and returns its 0-based offset.
std::pair<std::string, int> caret(const std::string& marked) {
    const auto pos = marked.find('|');
    REQUIRE(pos != std::string::npos);
    auto source = marked;
    source.erase(pos, 1);
    return {source, static_cast<int>(pos)};
}

int countKind(const std::vector<Symbol>& symbols, SymbolKind kind) {
    return static_cast<int>(
        std::count_if(symbols.begin(), symbols.end(),
                      [kind](const Symbol& s) { return s.kind == kind; }));
}

int countNamed(const std::vector<Symbol>& symbols, const std::string& name) {
    return static_cast<int>(
        std::count_if(symbols.begin(), symbols.end(),
                      [&name](const Symbol& s) { return s.name == name; }));
}

const Symbol& symbolNamed(const std::vector<Symbol>& symbols, const std::string& name) {
    const auto found = std::find_if(symbols.begin(), symbols.end(),
                                    [&name](const Symbol& s) { return s.name == name; });
    REQUIRE(found != symbols.end());
    return *found;
}

}  // namespace

TEST_CASE("Document symbols cover every declaration kind") {
    auto script = Parser::parse(
        "ScriptName S extends Parent\n"
        "import Utility\n"
        "struct Point\n"
        "  float X\n"
        "endStruct\n"
        "CustomEvent Ping\n"
        "Group G\n"
        "  int Property InGroup auto\n"
        "EndGroup\n"
        "int Property Loose auto\n"
        "int myVar\n"
        "Function Fn()\n"
        "EndFunction\n"
        "Event OnActivate(ObjectReference a)\n"
        "EndEvent\n"
        "State Idle\n"
        "  Function Fn()\n"
        "  EndFunction\n"
        "EndState\n");

    const auto symbols = Symbols::documentSymbols(script);
    CHECK(countKind(symbols, SymbolKind::Script) == 1);
    CHECK(countKind(symbols, SymbolKind::Import) == 1);
    CHECK(countKind(symbols, SymbolKind::Struct) == 1);
    CHECK(countKind(symbols, SymbolKind::StructMember) == 1);
    CHECK(countKind(symbols, SymbolKind::CustomEvent) == 1);
    CHECK(countKind(symbols, SymbolKind::Group) == 1);
    CHECK(countKind(symbols, SymbolKind::Property) == 2);
    CHECK(countKind(symbols, SymbolKind::Variable) == 1);
    CHECK(countKind(symbols, SymbolKind::Function) == 2);
    CHECK(countKind(symbols, SymbolKind::Event) == 1);
    CHECK(countKind(symbols, SymbolKind::State) == 1);

    CHECK(symbolNamed(symbols, "InGroup").container == "G");
    // The state override is its own symbol.
    CHECK(countNamed(symbols, "Fn") == 2);
}

TEST_CASE("Definition of a local points at the local") {
    const auto [source, offset] =
        caret("ScriptName S\nFunction F()\n  int counter = 0\n  counter = |counter + 1\nEndFunction\n");
    auto script = Parser::parse(source);
    ScriptIndex index;
    const auto symbol = Symbols::findDefinition(index, script, offset);
    REQUIRE(symbol.has_value());
    CHECK(symbol->kind == SymbolKind::Local);
    CHECK(symbol->name == "counter");
}

TEST_CASE("A local declared after the caret does not capture the reference") {
    const auto [source, offset] =
        caret("ScriptName S\nint shared\nFunction F()\n  shared = |shared + 1\n  int shared = 2\nEndFunction\n");
    auto script = Parser::parse(source);
    ScriptIndex index;
    const auto symbol = Symbols::findDefinition(index, script, offset);
    REQUIRE(symbol.has_value());
    CHECK(symbol->kind == SymbolKind::Variable);
}

TEST_CASE("Definition of a parameter") {
    const auto [source, offset] =
        caret("ScriptName S\nFunction F(int howMuch)\n  x = |howMuch\nEndFunction\n");
    auto script = Parser::parse(source);
    ScriptIndex index;
    const auto symbol = Symbols::findDefinition(index, script, offset);
    REQUIRE(symbol.has_value());
    CHECK(symbol->kind == SymbolKind::Parameter);
}

TEST_CASE("Definition of a property on this script") {
    const auto [source, offset] =
        caret("ScriptName S\nint Property Health auto\nFunction F()\n  |Health = 10\nEndFunction\n");
    auto script = Parser::parse(source);
    ScriptIndex index;
    const auto symbol = Symbols::findDefinition(index, script, offset);
    REQUIRE(symbol.has_value());
    CHECK(symbol->kind == SymbolKind::Property);
    CHECK(symbol->signature == "int Property Health Auto");
}

TEST_CASE("Definition follows extends into the parent script") {
    SourceRoot root;
    root.write("Parent.psc",
               "ScriptName Parent\nFunction DoStuff()\n{Parent version}\nEndFunction\n");
    const auto [source, offset] =
        caret("ScriptName Child extends Parent\nFunction F()\n  |DoStuff()\nEndFunction\n");
    const auto childFile = root.write("Child.psc", source);

    ScriptIndex index;
    index.addRoot(root.path);
    const Script* script = index.parseCached(childFile);
    REQUIRE(script != nullptr);

    const auto symbol = Symbols::findDefinition(index, *script, offset);
    REQUIRE(symbol.has_value());
    CHECK(symbol->name == "DoStuff");
    CHECK(symbol->container == "Parent");
    CHECK(symbol->file.has_value());
    CHECK(symbol->file->ends_with("Parent.psc"));
}

TEST_CASE("Definition of a global call through its script name") {
    SourceRoot root;
    root.write("Utility.psc",
               "ScriptName Utility Native\nfloat Function GetCurrentRealTime() global native\n");
    const auto [source, offset] =
        caret("ScriptName S\nFunction F()\n  float t = Utility.Get|CurrentRealTime()\nEndFunction\n");
    const auto file = root.write("S.psc", source);

    ScriptIndex index;
    index.addRoot(root.path);

    const auto symbol = Symbols::findDefinition(index, *index.parseCached(file), offset);
    REQUIRE(symbol.has_value());
    CHECK(symbol->name == "GetCurrentRealTime");
    CHECK(symbol->container == "Utility");
}

TEST_CASE("Definition of an imported global") {
    SourceRoot root;
    root.write("Helpers.psc",
               "ScriptName Helpers\nint Function MyGlobal() global\nEndFunction\n");
    const auto [source, offset] =
        caret("ScriptName S\nimport Helpers\nFunction F()\n  x = |MyGlobal()\nEndFunction\n");
    const auto file = root.write("S.psc", source);

    ScriptIndex index;
    index.addRoot(root.path);

    const auto symbol = Symbols::findDefinition(index, *index.parseCached(file), offset);
    REQUIRE(symbol.has_value());
    CHECK(symbol->container == "Helpers");
}

TEST_CASE("Definition of a type name lands on the script") {
    SourceRoot root;
    root.write("ObjectReference.psc", "ScriptName ObjectReference extends Form Native\n");
    const auto [source, offset] = caret("ScriptName S\nObject|Reference Property Ref auto\n");
    const auto file = root.write("S.psc", source);

    ScriptIndex index;
    index.addRoot(root.path);

    const auto symbol = Symbols::findDefinition(index, *index.parseCached(file), offset);
    REQUIRE(symbol.has_value());
    CHECK(symbol->kind == SymbolKind::Script);
    CHECK(symbol->name == "ObjectReference");
}

TEST_CASE("Definition of the extends clause") {
    SourceRoot root;
    root.write("Parent.psc", "ScriptName Parent\n");
    const auto [source, offset] = caret("ScriptName Child extends Par|ent\n");
    const auto file = root.write("Child.psc", source);

    ScriptIndex index;
    index.addRoot(root.path);

    const auto symbol = Symbols::findDefinition(index, *index.parseCached(file), offset);
    REQUIRE(symbol.has_value());
    CHECK(symbol->name == "Parent");
}

TEST_CASE("Built in types have no definition to jump to") {
    const auto [source, offset] = caret("ScriptName S\ni|nt Property X auto\n");
    auto script = Parser::parse(source);
    ScriptIndex index;
    CHECK(!Symbols::findDefinition(index, script, offset).has_value());
}

TEST_CASE("An unresolvable reference returns null rather than a guess") {
    const auto [source, offset] =
        caret("ScriptName S\nFunction F()\n  GetSomething().Totally|Unknown()\nEndFunction\n");
    auto script = Parser::parse(source);
    ScriptIndex index;
    CHECK(!Symbols::findDefinition(index, script, offset).has_value());
}

TEST_CASE("Standing on a declaration name resolves to that declaration") {
    const auto [source, offset] = caret("ScriptName S\nint Function My|Func()\nEndFunction\n");
    auto script = Parser::parse(source);
    ScriptIndex index;
    const auto symbol = Symbols::findDefinition(index, script, offset);
    REQUIRE(symbol.has_value());
    CHECK(symbol->name == "MyFunc");
    CHECK(symbol->kind == SymbolKind::Function);
}

TEST_CASE("Hover shows the signature and the doc comment") {
    const auto [source, offset] = caret("ScriptName S\n"
                                        "int Property MyProperty auto\n"
                                        "{This property is fun}\n"
                                        "Function F()\n"
                                        "  x = |MyProperty\n"
                                        "EndFunction\n");
    auto script = Parser::parse(source);
    ScriptIndex index;
    const auto hover = Symbols::hover(index, script, offset);
    REQUIRE(hover.has_value());
    CHECK(*hover == "int Property MyProperty Auto\n\nThis property is fun");
}

TEST_CASE("Hover on nothing returns null") {
    auto script = Parser::parse("ScriptName S\n");
    ScriptIndex index;
    CHECK(!Symbols::hover(index, script, 100000).has_value());
}
