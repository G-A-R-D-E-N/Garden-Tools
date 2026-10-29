#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <fo4/graph/node_palette.hpp>
#include <fo4/papyrus/parser.hpp>
#include <fo4/papyrus/resolver.hpp>

using namespace fo4::graph;
namespace papyrus = fo4::papyrus;

namespace {

std::filesystem::path baseStubs() {
    return std::filesystem::path(FO4_TESTDATA_DIR) / "graph" / "BaseStubs";
}

struct PaletteFixture {
    papyrus::ScriptIndex index;
    NodePalette palette;

    explicit PaletteFixture() : palette(index) {
        index.addRoot(baseStubs());
    }

    const NodeDefinition& definition(const std::string& id) {
        const auto* found = palette.find(id);
        if (found == nullptr) throw std::runtime_error("unknown node definition " + id);
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

}

TEST(NodePalette, AnInstanceFunctionGetsASelfPinAndAnArgumentPinPerParameter) {
    PaletteFixture fx;
    const auto& definition = fx.definition("call:ObjectReference.AddItem");

    ASSERT_TRUE(definition.kind == GraphNodeKind::Call);
    ASSERT_FALSE(definition.isGlobal);
    ASSERT_TRUE(definition.pin(pinIds::kSelf) != nullptr);

    std::vector<std::string> ids;
    for (const auto* pin : definition.dataInputs()) ids.push_back(pin->id);
    ASSERT_EQ(ids, (std::vector<std::string>{std::string(pinIds::kSelf), "arg:akItemToAdd",
                                             "arg:aiCount", "arg:abSilent"}));

    ASSERT_TRUE(definition.execInputs().size() == 1);
    ASSERT_TRUE(definition.execOutputs().size() == 1);
}

TEST(NodePalette, AGlobalFunctionHasNoSelfPin) {
    PaletteFixture fx;
    const auto& definition = fx.definition("global:Game.GetPlayer");

    ASSERT_TRUE(definition.isGlobal);
    ASSERT_TRUE(definition.pin(pinIds::kSelf) == nullptr);
    ASSERT_TRUE(definition.dataOutputs().size() == 1);
    ASSERT_TRUE(definition.dataOutputs()[0]->type->typeName == "Actor");
}

TEST(NodePalette, AnOptionalParameterCarriesItsDeclaredDefault) {
    PaletteFixture fx;
    const auto& definition = fx.definition("call:ObjectReference.AddItem");

    const auto* count = definition.pin("arg:aiCount");
    ASSERT_TRUE(count != nullptr);
    ASSERT_TRUE(count->isOptional);
    ASSERT_TRUE(count->declaredDefault == "1");

    ASSERT_FALSE(definition.pin("arg:akItemToAdd")->isOptional);
}

TEST(NodePalette, AFunctionReturningNothingHasNoReturnPin) {
    PaletteFixture fx;
    ASSERT_TRUE(fx.definition("global:Debug.Notification").pin(pinIds::kReturn) == nullptr);
    ASSERT_TRUE(fx.definition("call:ObjectReference.GetBaseObject").pin(pinIds::kReturn) != nullptr);
}

TEST(NodePalette, AnArrayReturnTypeIsKeptAsAnArray) {
    PaletteFixture fx;
    const auto& definition = fx.definition("call:ScriptObject.CallFunction");

    const auto* params = definition.pin("arg:aParams");
    ASSERT_TRUE(params != nullptr);
    ASSERT_TRUE(params->type->isArray);
    ASSERT_TRUE(equalsIgnoreCase(params->type->typeName, "Var"));
}

TEST(NodePalette, AnEventBecomesAnEntryNodeWhoseParametersAreOutputs) {
    PaletteFixture fx;
    const auto& definition = fx.definition("event:ObjectReference.OnActivate");

    ASSERT_TRUE(definition.kind == GraphNodeKind::EventEntry);
    ASSERT_TRUE(definition.execInputs().empty());
    ASSERT_TRUE(definition.execOutputs().size() == 1);
    ASSERT_TRUE(definition.execOutputs()[0]->id == pinIds::kExec);
    ASSERT_TRUE(definition.dataOutputs().size() == 1);
    ASSERT_TRUE(definition.dataOutputs()[0]->id == "param:akActionRef");
}

TEST(NodePalette, AnEventWithNoParametersHasOnlyAnExecOutput) {
    PaletteFixture fx;
    const auto& definition = fx.definition("event:ObjectReference.OnLoad");

    ASSERT_TRUE(definition.dataOutputs().empty());
    ASSERT_TRUE(definition.execOutputs().size() == 1);
}

TEST(NodePalette, APropertyYieldsAPureGetterAndAnImpureSetter) {
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
    ASSERT_TRUE(getter != nullptr);
    ASSERT_TRUE(getter->isPure);
    ASSERT_TRUE(getter->execInputs().empty());
    ASSERT_TRUE(getter->pin(pinIds::kValue)->type->typeName == "ObjectReference");

    const auto* setter = palette.find("prop.set:PropHolder.Target");
    ASSERT_TRUE(setter != nullptr);
    ASSERT_FALSE(setter->isPure);
    ASSERT_TRUE(setter->execInputs().size() == 1);
    ASSERT_TRUE(setter->pin(pinIds::kValue) != nullptr);
}

TEST(NodePalette, NoGeneratedCallIsEverMarkedPure) {
    PaletteFixture fx;
    for (const auto& script : {"ObjectReference", "Actor", "Game", "Utility", "Math"}) {
        for (const auto& definition : fx.palette.forScript(script)) {
            if (definition.kind == GraphNodeKind::Call) {
                SCOPED_TRACE((::testing::Message() << script << " calls should be sequenced: " << definition.id).GetString());
                ASSERT_FALSE(definition.isPure);
            }
        }
    }
}

TEST(NodePalette, StructuralValueNodesArePure) {
    const std::vector<std::string_view> ids = {builtinDefs::kSelf, builtinDefs::kNoneValue,
                                               builtinDefs::kCast, builtinDefs::kTypeCheck,
                                               "literal.int", "op.add", "op.eq", "array.length"};
    for (const auto& id : ids) {
        const auto* definition = builtinDefinitions::find(id);
        ASSERT_TRUE(definition != nullptr);
        SCOPED_TRACE((::testing::Message() << "no effect to sequence: " << id).GetString());
        ASSERT_TRUE(definition->isPure);
    }
}

TEST(NodePalette, ArrayMutatorsAreSequencedRatherThanPure) {
    for (const auto& id : {"array.add", "array.remove", "array.clear", "array.insert"}) {
        const auto* definition = builtinDefinitions::find(id);
        ASSERT_TRUE(definition != nullptr);
        SCOPED_TRACE((::testing::Message() << "changes the array: " << id).GetString());
        ASSERT_FALSE(definition->isPure);
        ASSERT_TRUE(definition->execInputs().size() == 1);
    }
}

TEST(NodePalette, TheArrayBuiltinsMatchWhatTheResolverActuallyBinds) {
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
        SCOPED_TRACE((::testing::Message() << "the resolver should bind a." << member).GetString());
        ASSERT_FALSE(hasError);
    }
}

TEST(NodePalette, EveryBuiltinHasAUniqueIdAndAtLeastOnePin) {
    std::vector<std::string> ids;
    for (const auto& definition : builtinDefinitions::all()) {
        ids.push_back(definition.id);
        ASSERT_FALSE(definition.pins.empty());
        ASSERT_FALSE(definition.title.empty());
    }
    std::sort(ids.begin(), ids.end());
    ASSERT_TRUE(std::adjacent_find(ids.begin(), ids.end()) == ids.end());
}

TEST(NodePalette, BranchAndWhileHaveTheExecShapeTheLoweringExpects) {
    const auto* branch = builtinDefinitions::find(builtinDefs::kBranch);
    ASSERT_TRUE(branch != nullptr);
    std::vector<std::string> branchOutputs;
    for (const auto* pin : branch->execOutputs()) branchOutputs.push_back(pin->id);
    ASSERT_EQ(branchOutputs, (std::vector<std::string>{std::string(pinIds::kThen),
                                                       std::string(pinIds::kElse)}));
    ASSERT_TRUE(branch->pin(pinIds::kCondition)->type->typeName == "bool");

    const auto* loop = builtinDefinitions::find(builtinDefs::kWhile);
    ASSERT_TRUE(loop != nullptr);
    std::vector<std::string> loopOutputs;
    for (const auto* pin : loop->execOutputs()) loopOutputs.push_back(pin->id);
    ASSERT_EQ(loopOutputs, (std::vector<std::string>{std::string(pinIds::kBody),
                                                     std::string(pinIds::kCompleted)}));
}

TEST(NodePalette, ForEachExposesAnElementAndAnIndex) {
    const auto* forEach = builtinDefinitions::find(builtinDefs::kForEach);
    ASSERT_TRUE(forEach != nullptr);

    ASSERT_TRUE(forEach->pin(pinIds::kElement)->type->form == PinTypeForm::ElementOfGeneric);
    ASSERT_TRUE(forEach->pin(pinIds::kIndex)->type->typeName == "int");
    ASSERT_TRUE(forEach->pin(pinIds::kArray)->type->form == PinTypeForm::ArrayOfGeneric);
}

TEST(NodePalette, OperatorTokensResolveFromTheirDefinitionIds) {
    ASSERT_TRUE(builtinDefinitions::operatorToken("op.add") == "+");
    ASSERT_TRUE(builtinDefinitions::operatorToken("op.eq") == "==");
    ASSERT_TRUE(builtinDefinitions::operatorToken("op.and") == "&&");
    ASSERT_TRUE(builtinDefinitions::operatorToken("op.not") == "!");
    ASSERT_TRUE(builtinDefinitions::operatorToken("branch") == std::nullopt);
    ASSERT_TRUE(builtinDefinitions::operatorToken("op.nonsense") == std::nullopt);
}

TEST(NodePalette, SearchFindsAMemberByNameAndReportsTheTrueTotal) {
    PaletteFixture fx;
    const auto result = fx.palette.search("AddItem", 5);

    ASSERT_FALSE(result.entries.empty());
    ASSERT_TRUE(result.entries[0].title == "AddItem");
    ASSERT_TRUE(result.total >= static_cast<int>(result.entries.size()));
}

TEST(NodePalette, SearchCapsResultsButStillReportsHowManyWereHidden) {
    PaletteFixture fx;
    const auto result = fx.palette.search("Get", 3);

    ASSERT_TRUE(result.entries.size() == 3);
    ASSERT_TRUE(result.total > 3);
    ASSERT_TRUE(result.truncated());
}

TEST(NodePalette, AnExactMatchRanksAboveALongerOneThatMerelyContainsIt) {
    PaletteFixture fx;
    const auto result = fx.palette.search("Add", 10);

    ASSERT_FALSE(result.entries.empty());
    ASSERT_TRUE(result.entries[0].title == "Add");
}

TEST(NodePalette, SearchCanBeFilteredToOneScript) {
    PaletteFixture fx;
    const auto result = fx.palette.search("Get", 100, "Game");

    ASSERT_FALSE(result.entries.empty());
    for (const auto& entry : result.entries) {
        SCOPED_TRACE((::testing::Message() << "only Game entries: " << entry.id).GetString());
        ASSERT_TRUE(entry.category == "Game");
    }
}

TEST(NodePalette, SearchResultsCarryNoPins) {
    PaletteFixture fx;
    const auto result = fx.palette.search("AddItem", 1);

    ASSERT_TRUE(result.entries.size() == 1);
    ASSERT_FALSE(result.entries[0].signature.empty());
    ASSERT_TRUE(result.entries[0].id == "call:ObjectReference.AddItem");
}

TEST(NodePalette, ADefinitionIdNamesItsScriptAndMember) {
    const auto check = [](const std::string& id, const std::string& script,
                          const std::string& member) {
        ASSERT_TRUE(NodePalette::ownerScriptOf(id) == script);
        ASSERT_TRUE(NodePalette::memberNameOf(id) == member);
    };
    check("call:ObjectReference.AddItem", "ObjectReference", "AddItem");
    check("global:Game.GetPlayer", "Game", "GetPlayer");
    check("event:ObjectReference.OnActivate", "ObjectReference", "OnActivate");
    check("prop.get:MyQuest.Target", "MyQuest", "Target");
    check("call:MyMod:Sub.Thing", "MyMod:Sub", "Thing");
}

TEST(NodePalette, ABuiltinIdNamesNoScript) {
    ASSERT_TRUE(NodePalette::ownerScriptOf("branch") == std::nullopt);
    ASSERT_TRUE(NodePalette::ownerScriptOf("op.add") == std::nullopt);
}

TEST(NodePalette, DefinitionIdsAreStableAcrossTwoBuilds) {
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

    ASSERT_TRUE(second == first);
    ASSERT_TRUE(std::adjacent_find(first.begin(), first.end()) == first.end());
}

TEST(NodePalette, AScriptIsBuiltOnceAndCached) {
    PaletteFixture fx;
    const auto& one = fx.palette.forScript("Actor");
    const auto& two = fx.palette.forScript("Actor");
    ASSERT_TRUE(&one == &two);
}

TEST(NodePalette, AnUnknownDefinitionResolvesToNullRatherThanThrowing) {
    PaletteFixture fx;
    ASSERT_TRUE(fx.palette.find("call:NoSuchScript.NoSuchThing") == nullptr);
    ASSERT_TRUE(fx.palette.find("nonsense") == nullptr);
    ASSERT_TRUE(fx.palette.find("") == nullptr);
}

TEST(NodePalette, ALocalNameHintReadsLikeSomethingAPersonWouldWrite) {
    const auto check = [](const std::string& member, const std::string& expected) {
        ASSERT_TRUE(NodePalette::localNameHint(member) == expected);
    };
    check("GetPlayer", "player");
    check("GetActorBase", "actorBase");
    check("IsDead", "dead");
    check("HasKeyword", "keyword");
    check("AddItem", "addItem");
    check("Get", "get");
}

TEST(NodePalette, APaletteWithNoWikiMirrorStillBuilds) {
    PaletteFixture fx;
    ASSERT_FALSE(fx.palette.wikiStats().available());

    const auto* definition = fx.palette.find("call:ObjectReference.AddItem");
    ASSERT_TRUE(definition != nullptr);
    ASSERT_FALSE(definition->summary.has_value());
}

TEST(NodePalette, AWikiRootThatDoesNotExistDegradesToNulls) {
    CkWikiDocProvider provider(std::filesystem::path("/no/such/directory/anywhere"));

    ASSERT_TRUE(provider.function("ObjectReference", "AddItem") == nullptr);
    ASSERT_TRUE(provider.script("ObjectReference") == nullptr);
    ASSERT_FALSE(provider.stats().available());
}

TEST(NodePalette, AWikiMirrorIndexesFunctionPagesAndParsesTheirSections) {
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
    ASSERT_TRUE(provider.stats().available());
    ASSERT_TRUE(provider.stats().pagesIndexed == 1);

    const auto* function = provider.function("ObjectReference", "AddItem");
    ASSERT_TRUE(function != nullptr);
    ASSERT_TRUE(function->summary == "Adds an item to the container.");
    ASSERT_TRUE(function->syntaxLine ==
            "Function AddItem(Form akItemToAdd, int aiCount = 1, bool abSilent = false)");
    ASSERT_TRUE(function->returnValue == "Nothing.");

    const auto found = function->parameters.find("akitemtoadd");
    ASSERT_TRUE(found != function->parameters.end());
    ASSERT_TRUE(found->second.description == "The item to add. Default: None");
    ASSERT_TRUE(found->second.documentedDefault == "None");

    const auto script = provider.script("ObjectReference");
    ASSERT_TRUE(script != nullptr);
    ASSERT_TRUE(script->summary == "The base class for everything in the world.");
}

TEST(NodePalette, APaletteWithAWikiMirrorDecoratesDefinitionsWithDocs) {
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
    ASSERT_TRUE(definition != nullptr);
    ASSERT_TRUE(definition->summary == "Adds an item to the container.");
    const auto* arg = definition->pin("arg:akItemToAdd");
    ASSERT_TRUE(arg != nullptr);
    ASSERT_TRUE(arg->description == "The item to add.");
}
