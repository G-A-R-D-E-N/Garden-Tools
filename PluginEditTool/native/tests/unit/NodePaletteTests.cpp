#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/graph/NodePalette.hpp>
#include <fo4ide/papyrus/Parser.hpp>
#include <fo4ide/papyrus/Resolver.hpp>

using namespace fo4ide::graph;
namespace papyrus = fo4ide::papyrus;

namespace {

// The BaseStubs fixture tree lives next to the test sources:
// native/tests/fixtures/BaseStubs.
std::filesystem::path baseStubs() {
    return std::filesystem::path(FO4IDE_TEST_FIXTURES_DIR) / "BaseStubs";
}

struct PaletteFixture {
    papyrus::ScriptIndex index;
    NodePalette palette;

    explicit PaletteFixture() : palette(index) {
        index.addRoot(baseStubs());
    }

    const NodeDefinition& definition(const std::string& id) {
        const auto* found = palette.find(id);
        REQUIRE(found != nullptr);
        return *found;
    }
};

struct SourceRoot {
    std::filesystem::path dir;

    SourceRoot() : dir(std::filesystem::temp_directory_path() /
                      ("fo4re-palette-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                       "-" +
                       std::to_string(reinterpret_cast<std::uintptr_t>(this)))) {
        std::filesystem::create_directories(dir);
    }

    ~SourceRoot() { std::filesystem::remove_all(dir); }
};

}  // namespace

TEST_CASE("An instance function gets a self pin and an argument pin per parameter") {
    PaletteFixture fx;
    const auto& definition = fx.definition("call:ObjectReference.AddItem");

    REQUIRE(definition.kind == GraphNodeKind::Call);
    REQUIRE_FALSE(definition.isGlobal);
    REQUIRE(definition.pin(pinIds::kSelf) != nullptr);

    std::vector<std::string> ids;
    for (const auto* pin : definition.dataInputs()) ids.push_back(pin->id);
    REQUIRE(ids == std::vector<std::string>{
                       std::string(pinIds::kSelf), "arg:akItemToAdd", "arg:aiCount",
                       "arg:abSilent"});

    REQUIRE(definition.execInputs().size() == 1);
    REQUIRE(definition.execOutputs().size() == 1);
}

TEST_CASE("A global function has no self pin") {
    PaletteFixture fx;
    const auto& definition = fx.definition("global:Game.GetPlayer");

    REQUIRE(definition.isGlobal);
    REQUIRE(definition.pin(pinIds::kSelf) == nullptr);
    REQUIRE(definition.dataOutputs().size() == 1);
    REQUIRE(definition.dataOutputs()[0]->type->typeName == "Actor");
}

TEST_CASE("An optional parameter carries its declared default") {
    PaletteFixture fx;
    const auto& definition = fx.definition("call:ObjectReference.AddItem");

    const auto* count = definition.pin("arg:aiCount");
    REQUIRE(count != nullptr);
    REQUIRE(count->isOptional);
    REQUIRE(count->declaredDefault == "1");

    REQUIRE_FALSE(definition.pin("arg:akItemToAdd")->isOptional);
}

TEST_CASE("A function returning nothing has no return pin") {
    PaletteFixture fx;
    REQUIRE(fx.definition("global:Debug.Notification").pin(pinIds::kReturn) == nullptr);
    REQUIRE(fx.definition("call:ObjectReference.GetBaseObject").pin(pinIds::kReturn) != nullptr);
}

TEST_CASE("An array return type is kept as an array") {
    PaletteFixture fx;
    const auto& definition = fx.definition("call:ScriptObject.CallFunction");

    const auto* params = definition.pin("arg:aParams");
    REQUIRE(params != nullptr);
    REQUIRE(params->type->isArray);
    // The C# asserts with BeEquivalentTo (case-insensitive); the source spells
    // the Var[] parameter lowercase.
    REQUIRE(equalsIgnoreCase(params->type->typeName, "Var"));
}

TEST_CASE("An event becomes an entry node whose parameters are outputs") {
    PaletteFixture fx;
    const auto& definition = fx.definition("event:ObjectReference.OnActivate");

    REQUIRE(definition.kind == GraphNodeKind::EventEntry);
    REQUIRE(definition.execInputs().empty());
    REQUIRE(definition.execOutputs().size() == 1);
    REQUIRE(definition.execOutputs()[0]->id == pinIds::kExec);
    REQUIRE(definition.dataOutputs().size() == 1);
    REQUIRE(definition.dataOutputs()[0]->id == "param:akActionRef");
}

TEST_CASE("An event with no parameters has only an exec output") {
    PaletteFixture fx;
    const auto& definition = fx.definition("event:ObjectReference.OnLoad");

    REQUIRE(definition.dataOutputs().empty());
    REQUIRE(definition.execOutputs().size() == 1);
}

TEST_CASE("A property yields a pure getter and an impure setter") {
    SourceRoot root;
    {
        std::ofstream stream(root.dir / "PropHolder.psc");
        stream << "Scriptname PropHolder\n"
                  "ObjectReference Property Target Auto\n";
    }

    papyrus::ScriptIndex index;
    index.addRoot(root.dir);
    index.addRoot(baseStubs());
    NodePalette palette(index);

    const auto* getter = palette.find("prop.get:PropHolder.Target");
    REQUIRE(getter != nullptr);
    REQUIRE(getter->isPure);
    REQUIRE(getter->execInputs().empty());
    REQUIRE(getter->pin(pinIds::kValue)->type->typeName == "ObjectReference");

    const auto* setter = palette.find("prop.set:PropHolder.Target");
    REQUIRE(setter != nullptr);
    REQUIRE_FALSE(setter->isPure);
    REQUIRE(setter->execInputs().size() == 1);
    REQUIRE(setter->pin(pinIds::kValue) != nullptr);
}

TEST_CASE("No generated call is ever marked pure") {
    PaletteFixture fx;
    for (const auto& script : {"ObjectReference", "Actor", "Game", "Utility", "Math"}) {
        for (const auto& definition : fx.palette.forScript(script)) {
            if (definition.kind == GraphNodeKind::Call) {
                INFO(script << " calls should be sequenced: " << definition.id);
                REQUIRE_FALSE(definition.isPure);
            }
        }
    }
}

TEST_CASE("Structural value nodes are pure") {
    const std::vector<std::string_view> ids = {builtinDefs::kSelf, builtinDefs::kNoneValue,
                                               builtinDefs::kCast, builtinDefs::kTypeCheck,
                                               "literal.int", "op.add", "op.eq", "array.length"};
    for (const auto& id : ids) {
        const auto* definition = builtinDefinitions::find(id);
        REQUIRE(definition != nullptr);
        INFO("no effect to sequence: " << id);
        REQUIRE(definition->isPure);
    }
}

TEST_CASE("Array mutators are sequenced rather than pure") {
    for (const auto& id : {"array.add", "array.remove", "array.clear", "array.insert"}) {
        const auto* definition = builtinDefinitions::find(id);
        REQUIRE(definition != nullptr);
        INFO("changes the array: " << id);
        REQUIRE_FALSE(definition->isPure);
        REQUIRE(definition->execInputs().size() == 1);
    }
}

TEST_CASE("The array builtins match what the resolver actually binds") {
    SourceRoot root;
    papyrus::ScriptIndex index;
    index.addRoot(root.dir);
    index.addRoot(baseStubs());

    for (const auto& member : builtinDefinitions::arrayMemberNames()) {
        const auto source = "Scriptname Probe\n"
                            "Function Go()\n"
                            "\tint[] a = new int[4]\n"
                            "\ta." +
                            member + "\n"
                            "EndFunction\n";
        auto script = papyrus::Parser::parse(source, "Probe.psc");
        const auto resolution = papyrus::Resolver(index).resolve(&script);

        bool hasError = false;
        for (const auto& diagnostic : resolution.diagnostics) {
            if (diagnostic.severity == papyrus::Severity::Error) hasError = true;
        }
        INFO("the resolver should bind a." << member);
        REQUIRE_FALSE(hasError);
    }
}

TEST_CASE("Every builtin has a unique id and at least one pin") {
    std::vector<std::string> ids;
    for (const auto& definition : builtinDefinitions::all()) {
        ids.push_back(definition.id);
        REQUIRE_FALSE(definition.pins.empty());
        REQUIRE_FALSE(definition.title.empty());
    }
    std::sort(ids.begin(), ids.end());
    REQUIRE(std::adjacent_find(ids.begin(), ids.end()) == ids.end());
}

TEST_CASE("Branch and while have the exec shape the lowering expects") {
    const auto* branch = builtinDefinitions::find(builtinDefs::kBranch);
    REQUIRE(branch != nullptr);
    std::vector<std::string> branchOutputs;
    for (const auto* pin : branch->execOutputs()) branchOutputs.push_back(pin->id);
    REQUIRE(branchOutputs == std::vector<std::string>{std::string(pinIds::kThen),
                                                      std::string(pinIds::kElse)});
    REQUIRE(branch->pin(pinIds::kCondition)->type->typeName == "bool");

    const auto* loop = builtinDefinitions::find(builtinDefs::kWhile);
    REQUIRE(loop != nullptr);
    std::vector<std::string> loopOutputs;
    for (const auto* pin : loop->execOutputs()) loopOutputs.push_back(pin->id);
    REQUIRE(loopOutputs == std::vector<std::string>{std::string(pinIds::kBody),
                                                    std::string(pinIds::kCompleted)});
}

TEST_CASE("For each exposes an element and an index") {
    const auto* forEach = builtinDefinitions::find(builtinDefs::kForEach);
    REQUIRE(forEach != nullptr);

    REQUIRE(forEach->pin(pinIds::kElement)->type->form == PinTypeForm::ElementOfGeneric);
    REQUIRE(forEach->pin(pinIds::kIndex)->type->typeName == "int");
    REQUIRE(forEach->pin(pinIds::kArray)->type->form == PinTypeForm::ArrayOfGeneric);
}

TEST_CASE("Operator tokens resolve from their definition ids") {
    REQUIRE(builtinDefinitions::operatorToken("op.add") == "+");
    REQUIRE(builtinDefinitions::operatorToken("op.eq") == "==");
    REQUIRE(builtinDefinitions::operatorToken("op.and") == "&&");
    REQUIRE(builtinDefinitions::operatorToken("op.not") == "!");
    REQUIRE(builtinDefinitions::operatorToken("branch") == std::nullopt);
    REQUIRE(builtinDefinitions::operatorToken("op.nonsense") == std::nullopt);
}

TEST_CASE("Search finds a member by name and reports the true total") {
    PaletteFixture fx;
    const auto result = fx.palette.search("AddItem", /*limit=*/5);

    REQUIRE_FALSE(result.entries.empty());
    REQUIRE(result.entries[0].title == "AddItem");
    REQUIRE(result.total >= static_cast<int>(result.entries.size()));
}

TEST_CASE("Search caps results but still reports how many were hidden") {
    PaletteFixture fx;
    const auto result = fx.palette.search("Get", /*limit=*/3);

    REQUIRE(result.entries.size() == 3);
    REQUIRE(result.total > 3);
    REQUIRE(result.truncated());
}

TEST_CASE("An exact match ranks above a longer one that merely contains it") {
    PaletteFixture fx;
    const auto result = fx.palette.search("Add", /*limit=*/10);

    REQUIRE_FALSE(result.entries.empty());
    REQUIRE(result.entries[0].title == "Add");
}

TEST_CASE("Search can be filtered to one script") {
    PaletteFixture fx;
    const auto result = fx.palette.search("Get", /*limit=*/100, "Game");

    REQUIRE_FALSE(result.entries.empty());
    for (const auto& entry : result.entries) {
        INFO("only Game entries: " << entry.id);
        REQUIRE(entry.category == "Game");
    }
}

TEST_CASE("Search results carry no pins") {
    PaletteFixture fx;
    const auto result = fx.palette.search("AddItem", /*limit=*/1);

    REQUIRE(result.entries.size() == 1);
    REQUIRE_FALSE(result.entries[0].signature.empty());
    REQUIRE(result.entries[0].id == "call:ObjectReference.AddItem");
}

TEST_CASE("A definition id names its script and member") {
    const auto check = [](const std::string& id, const std::string& script,
                          const std::string& member) {
        REQUIRE(NodePalette::ownerScriptOf(id) == script);
        REQUIRE(NodePalette::memberNameOf(id) == member);
    };
    check("call:ObjectReference.AddItem", "ObjectReference", "AddItem");
    check("global:Game.GetPlayer", "Game", "GetPlayer");
    check("event:ObjectReference.OnActivate", "ObjectReference", "OnActivate");
    check("prop.get:MyQuest.Target", "MyQuest", "Target");
    check("call:MyMod:Sub.Thing", "MyMod:Sub", "Thing");
}

TEST_CASE("A builtin id names no script") {
    REQUIRE(NodePalette::ownerScriptOf("branch") == std::nullopt);
    REQUIRE(NodePalette::ownerScriptOf("op.add") == std::nullopt);
}

TEST_CASE("Definition ids are stable across two builds") {
    PaletteFixture fx;
    auto first = [&] {
        std::vector<std::string> ids;
        for (const auto& definition : fx.palette.forScript("ObjectReference")) {
            ids.push_back(definition.id);
        }
        std::sort(ids.begin(), ids.end());
        return ids;
    }();
    auto second = [&] {
        std::vector<std::string> ids;
        for (const auto& definition : fx.palette.forScript("ObjectReference")) {
            ids.push_back(definition.id);
        }
        std::sort(ids.begin(), ids.end());
        return ids;
    }();

    REQUIRE(second == first);
    REQUIRE(std::adjacent_find(first.begin(), first.end()) == first.end());
}

TEST_CASE("A script is built once and cached") {
    PaletteFixture fx;
    const auto& one = fx.palette.forScript("Actor");
    const auto& two = fx.palette.forScript("Actor");
    REQUIRE(&one == &two);
}

TEST_CASE("An unknown definition resolves to null rather than throwing") {
    PaletteFixture fx;
    REQUIRE(fx.palette.find("call:NoSuchScript.NoSuchThing") == nullptr);
    REQUIRE(fx.palette.find("nonsense") == nullptr);
    REQUIRE(fx.palette.find("") == nullptr);
}

TEST_CASE("A local name hint reads like something a person would write") {
    const auto check = [](const std::string& member, const std::string& expected) {
        REQUIRE(NodePalette::localNameHint(member) == expected);
    };
    check("GetPlayer", "player");
    check("GetActorBase", "actorBase");
    check("IsDead", "dead");
    check("HasKeyword", "keyword");
    check("AddItem", "addItem");
    check("Get", "get");
}

TEST_CASE("A palette with no wiki mirror still builds") {
    PaletteFixture fx;
    REQUIRE_FALSE(fx.palette.wikiStats().available());

    const auto* definition = fx.palette.find("call:ObjectReference.AddItem");
    REQUIRE(definition != nullptr);
    REQUIRE_FALSE(definition->summary.has_value());
}

TEST_CASE("A wiki root that does not exist degrades to nulls") {
    CkWikiDocProvider provider(std::filesystem::path("/no/such/directory/anywhere"));

    REQUIRE(provider.function("ObjectReference", "AddItem") == nullptr);
    REQUIRE(provider.script("ObjectReference") == nullptr);
    REQUIRE_FALSE(provider.stats().available());
}

TEST_CASE("A wiki mirror indexes function pages and parses their sections") {
    SourceRoot root;
    {
        std::ofstream page(root.dir / "AddItem_-_ObjectReference.html");
        page << "<html><body>"
                "<p>Adds an item to the container.</p>"
                "<h1>AddItem</h1>"
                "<h2><span class=\"mw-headline\" id=\"Syntax\">Syntax</span></h2>"
                "Function AddItem(Form akItemToAdd, int aiCount = 1, bool abSilent = false)"
                "<h2><span class=\"mw-headline\" id=\"Parameters\">Parameters</span></h2>"
                "<ul><li>akItemToAdd: The item to add. Default: None</li>"
                "<li>aiCount: How many.</li></ul>"
                "<h2><span class=\"mw-headline\" id=\"Return_Value\">Return Value</span></h2>"
                "Nothing."
                "<div id=\"catlinks\">x</div>"
                "</body></html>";
    }
    {
        std::ofstream page(root.dir / "ObjectReference_Script.html");
        page << "<html><body>"
                "<p>The base class for everything in the world.</p>"
                "</body></html>";
    }

    CkWikiDocProvider provider(root.dir);
    REQUIRE(provider.stats().available());
    // PagesIndexed counts function pages only, mirroring the C# Stats
    // (WikiDocStats(_byFunction.Count, ...)).
    REQUIRE(provider.stats().pagesIndexed == 1);

    const auto* function = provider.function("ObjectReference", "AddItem");
    REQUIRE(function != nullptr);
    REQUIRE(function->summary == "Adds an item to the container.");
    REQUIRE(function->syntaxLine ==
            "Function AddItem(Form akItemToAdd, int aiCount = 1, bool abSilent = false)");
    REQUIRE(function->returnValue == "Nothing.");

    const auto found = function->parameters.find("akitemtoadd");
    REQUIRE(found != function->parameters.end());
    // The C# keeps the full described text (the Default: clause stays in the
    // description) and records the default separately.
    REQUIRE(found->second.description == "The item to add. Default: None");
    REQUIRE(found->second.documentedDefault == "None");

    const auto script = provider.script("ObjectReference");
    REQUIRE(script != nullptr);
    REQUIRE(script->summary == "The base class for everything in the world.");
}

TEST_CASE("A palette with a wiki mirror decorates definitions with docs") {
    SourceRoot root;
    {
        std::ofstream page(root.dir / "AddItem_-_ObjectReference.html");
        page << "<html><body><p>Adds an item to the container.</p>"
                "<h2><span class=\"mw-headline\" id=\"Syntax\">Syntax</span></h2>"
                "Function AddItem(Form akItemToAdd, int aiCount = 1)"
                "<h2><span class=\"mw-headline\" id=\"Parameters\">Parameters</span></h2>"
                "<ul><li>akItemToAdd: The item to add.</li></ul>"
                "<div id=\"catlinks\">x</div>"
                "</body></html>";
    }

    papyrus::ScriptIndex index;
    index.addRoot(baseStubs());
    CkWikiDocProvider provider(root.dir);
    NodePalette palette(index, &provider);

    const auto* definition = palette.find("call:ObjectReference.AddItem");
    REQUIRE(definition != nullptr);
    REQUIRE(definition->summary == "Adds an item to the container.");
    const auto* arg = definition->pin("arg:akItemToAdd");
    REQUIRE(arg != nullptr);
    REQUIRE(arg->description == "The item to add.");
}
