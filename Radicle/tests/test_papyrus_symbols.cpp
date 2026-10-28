#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include <fo4/papyrus/parser.hpp>
#include <fo4/papyrus/script_index.hpp>
#include <fo4/papyrus/symbols.hpp>

using namespace fo4::papyrus;

namespace {

struct SourceRoot {
    std::string path = [] {
        const auto dir = std::filesystem::temp_directory_path() /
                         ("fo4re-symbols-" +
                          std::to_string(
                              std::chrono::steady_clock::now().time_since_epoch().count()));
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

std::pair<std::string, int> caret(const std::string& marked) {
    const auto pos = marked.find('|');
    if (pos == std::string::npos) throw std::runtime_error("caret marker missing");
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
    if (found == symbols.end()) throw std::runtime_error("no symbol named " + name);
    return *found;
}

}

TEST(PapyrusSymbols, DocumentSymbolsCoverEveryDeclarationKind) {
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
    EXPECT_TRUE(countKind(symbols, SymbolKind::Script) == 1);
    EXPECT_TRUE(countKind(symbols, SymbolKind::Import) == 1);
    EXPECT_TRUE(countKind(symbols, SymbolKind::Struct) == 1);
    EXPECT_TRUE(countKind(symbols, SymbolKind::StructMember) == 1);
    EXPECT_TRUE(countKind(symbols, SymbolKind::CustomEvent) == 1);
    EXPECT_TRUE(countKind(symbols, SymbolKind::Group) == 1);
    EXPECT_TRUE(countKind(symbols, SymbolKind::Property) == 2);
    EXPECT_TRUE(countKind(symbols, SymbolKind::Variable) == 1);
    EXPECT_TRUE(countKind(symbols, SymbolKind::Function) == 2);
    EXPECT_TRUE(countKind(symbols, SymbolKind::Event) == 1);
    EXPECT_TRUE(countKind(symbols, SymbolKind::State) == 1);

    EXPECT_TRUE(symbolNamed(symbols, "InGroup").container == "G");
    EXPECT_TRUE(countNamed(symbols, "Fn") == 2);
}

TEST(PapyrusSymbols, DefinitionOfALocalPointsAtTheLocal) {
    const auto [source, offset] =
        caret("ScriptName S\nFunction F()\n  int counter = 0\n  counter = |counter + 1\nEndFunction\n");
    auto script = Parser::parse(source);
    ScriptIndex index;
    const auto symbol = Symbols::findDefinition(index, script, offset);
    ASSERT_TRUE(symbol.has_value());
    EXPECT_TRUE(symbol->kind == SymbolKind::Local);
    EXPECT_TRUE(symbol->name == "counter");
}

TEST(PapyrusSymbols, ALocalDeclaredAfterTheCaretDoesNotCaptureTheReference) {
    const auto [source, offset] =
        caret("ScriptName S\nint shared\nFunction F()\n  shared = |shared + 1\n  int shared = 2\nEndFunction\n");
    auto script = Parser::parse(source);
    ScriptIndex index;
    const auto symbol = Symbols::findDefinition(index, script, offset);
    ASSERT_TRUE(symbol.has_value());
    EXPECT_TRUE(symbol->kind == SymbolKind::Variable);
}

TEST(PapyrusSymbols, DefinitionOfAParameter) {
    const auto [source, offset] =
        caret("ScriptName S\nFunction F(int howMuch)\n  x = |howMuch\nEndFunction\n");
    auto script = Parser::parse(source);
    ScriptIndex index;
    const auto symbol = Symbols::findDefinition(index, script, offset);
    ASSERT_TRUE(symbol.has_value());
    EXPECT_TRUE(symbol->kind == SymbolKind::Parameter);
}

TEST(PapyrusSymbols, DefinitionOfAPropertyOnThisScript) {
    const auto [source, offset] =
        caret("ScriptName S\nint Property Health auto\nFunction F()\n  |Health = 10\nEndFunction\n");
    auto script = Parser::parse(source);
    ScriptIndex index;
    const auto symbol = Symbols::findDefinition(index, script, offset);
    ASSERT_TRUE(symbol.has_value());
    EXPECT_TRUE(symbol->kind == SymbolKind::Property);
    EXPECT_TRUE(symbol->signature == "int Property Health Auto");
}

TEST(PapyrusSymbols, DefinitionFollowsExtendsIntoTheParentScript) {
    SourceRoot root;
    root.write("Parent.psc",
               "ScriptName Parent\nFunction DoStuff()\n{Parent version}\nEndFunction\n");
    const auto [source, offset] =
        caret("ScriptName Child extends Parent\nFunction F()\n  |DoStuff()\nEndFunction\n");
    const auto childFile = root.write("Child.psc", source);

    ScriptIndex index;
    index.addRoot(root.path);
    const Script* script = index.parseCached(childFile);
    ASSERT_TRUE(script != nullptr);

    const auto symbol = Symbols::findDefinition(index, *script, offset);
    ASSERT_TRUE(symbol.has_value());
    EXPECT_TRUE(symbol->name == "DoStuff");
    EXPECT_TRUE(symbol->container == "Parent");
    EXPECT_TRUE(symbol->file.has_value());
    EXPECT_TRUE(symbol->file->ends_with("Parent.psc"));
}

TEST(PapyrusSymbols, DefinitionOfAGlobalCallThroughItsScriptName) {
    SourceRoot root;
    root.write("Utility.psc",
               "ScriptName Utility Native\nfloat Function GetCurrentRealTime() global native\n");
    const auto [source, offset] =
        caret("ScriptName S\nFunction F()\n  float t = Utility.Get|CurrentRealTime()\nEndFunction\n");
    const auto file = root.write("S.psc", source);

    ScriptIndex index;
    index.addRoot(root.path);

    const auto symbol = Symbols::findDefinition(index, *index.parseCached(file), offset);
    ASSERT_TRUE(symbol.has_value());
    EXPECT_TRUE(symbol->name == "GetCurrentRealTime");
    EXPECT_TRUE(symbol->container == "Utility");
}

TEST(PapyrusSymbols, DefinitionOfAnImportedGlobal) {
    SourceRoot root;
    root.write("Helpers.psc",
               "ScriptName Helpers\nint Function MyGlobal() global\nEndFunction\n");
    const auto [source, offset] =
        caret("ScriptName S\nimport Helpers\nFunction F()\n  x = |MyGlobal()\nEndFunction\n");
    const auto file = root.write("S.psc", source);

    ScriptIndex index;
    index.addRoot(root.path);

    const auto symbol = Symbols::findDefinition(index, *index.parseCached(file), offset);
    ASSERT_TRUE(symbol.has_value());
    EXPECT_TRUE(symbol->container == "Helpers");
}

TEST(PapyrusSymbols, DefinitionOfATypeNameLandsOnTheScript) {
    SourceRoot root;
    root.write("ObjectReference.psc", "ScriptName ObjectReference extends Form Native\n");
    const auto [source, offset] = caret("ScriptName S\nObject|Reference Property Ref auto\n");
    const auto file = root.write("S.psc", source);

    ScriptIndex index;
    index.addRoot(root.path);

    const auto symbol = Symbols::findDefinition(index, *index.parseCached(file), offset);
    ASSERT_TRUE(symbol.has_value());
    EXPECT_TRUE(symbol->kind == SymbolKind::Script);
    EXPECT_TRUE(symbol->name == "ObjectReference");
}

TEST(PapyrusSymbols, DefinitionOfTheExtendsClause) {
    SourceRoot root;
    root.write("Parent.psc", "ScriptName Parent\n");
    const auto [source, offset] = caret("ScriptName Child extends Par|ent\n");
    const auto file = root.write("Child.psc", source);

    ScriptIndex index;
    index.addRoot(root.path);

    const auto symbol = Symbols::findDefinition(index, *index.parseCached(file), offset);
    ASSERT_TRUE(symbol.has_value());
    EXPECT_TRUE(symbol->name == "Parent");
}

TEST(PapyrusSymbols, BuiltInTypesHaveNoDefinitionToJumpTo) {
    const auto [source, offset] = caret("ScriptName S\ni|nt Property X auto\n");
    auto script = Parser::parse(source);
    ScriptIndex index;
    EXPECT_TRUE(!Symbols::findDefinition(index, script, offset).has_value());
}

TEST(PapyrusSymbols, AnUnresolvableReferenceReturnsNullRatherThanAGuess) {
    const auto [source, offset] =
        caret("ScriptName S\nFunction F()\n  GetSomething().Totally|Unknown()\nEndFunction\n");
    auto script = Parser::parse(source);
    ScriptIndex index;
    EXPECT_TRUE(!Symbols::findDefinition(index, script, offset).has_value());
}

TEST(PapyrusSymbols, StandingOnADeclarationNameResolvesToThatDeclaration) {
    const auto [source, offset] = caret("ScriptName S\nint Function My|Func()\nEndFunction\n");
    auto script = Parser::parse(source);
    ScriptIndex index;
    const auto symbol = Symbols::findDefinition(index, script, offset);
    ASSERT_TRUE(symbol.has_value());
    EXPECT_TRUE(symbol->name == "MyFunc");
    EXPECT_TRUE(symbol->kind == SymbolKind::Function);
}

TEST(PapyrusSymbols, HoverShowsTheSignatureAndTheDocComment) {
    const auto [source, offset] = caret("ScriptName S\n"
                                        "int Property MyProperty auto\n"
                                        "{This property is fun}\n"
                                        "Function F()\n"
                                        "  x = |MyProperty\n"
                                        "EndFunction\n");
    auto script = Parser::parse(source);
    ScriptIndex index;
    const auto hover = Symbols::hover(index, script, offset);
    ASSERT_TRUE(hover.has_value());
    EXPECT_TRUE(*hover == "int Property MyProperty Auto\n\nThis property is fun");
}

TEST(PapyrusSymbols, HoverOnNothingReturnsNull) {
    auto script = Parser::parse("ScriptName S\n");
    ScriptIndex index;
    EXPECT_TRUE(!Symbols::hover(index, script, 100000).has_value());
}
