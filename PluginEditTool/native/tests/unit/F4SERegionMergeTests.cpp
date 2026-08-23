#include <string>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/f4se/BindingModel.hpp>
#include <fo4ide/f4se/Emitter.hpp>
#include <fo4ide/f4se/RegionMerge.hpp>
#include <fo4ide/f4se/TypeMap.hpp>

namespace {

using fo4ide::f4se::F4SEEmitOptions;
using fo4ide::f4se::F4SEEmitter;
using fo4ide::f4se::F4SETypeMap;
using fo4ide::f4se::ModuleBinding;
using fo4ide::f4se::NativeBinding;
using fo4ide::f4se::PapyrusTypeText;
using fo4ide::f4se::PluginBinding;
using fo4ide::f4se::RegionMerge;

std::string generated(const std::string& name, const std::string& stub = "\t\tSTUB;") {
    std::string text;
    text += "void " + name + "(StaticFunctionTag * base)\n";
    text += "{\n";
    text += "\t\t" + RegionMerge::begin(name) + "\n";
    text += stub + "\n";
    text += "\t\t" + RegionMerge::end(name) + "\n";
    text += "}\n";
    return text;
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

std::string trim(std::string value) {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
        value.erase(value.begin());
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
        value.pop_back();
    }
    return value;
}

}  // namespace

TEST_CASE("F4SE region merge: new body markers are preprocessor directives") {
    CHECK(RegionMerge::begin("Alpha") == "#pragma region FO4IDE_BODY Alpha");
    CHECK(RegionMerge::end("Alpha") == "#pragma endregion FO4IDE_BODY Alpha");
}

TEST_CASE("F4SE region merge: a legacy comment marked body survives regeneration") {
    const std::string previous =
        "void Alpha(StaticFunctionTag * base)\n{\n\t\t// >>> body: Alpha\n\t\tkept();\n\t\t"
        "// <<< body: Alpha\n}\n";
    const auto merged = RegionMerge::merge(generated("Alpha"), previous);
    CHECK(contains(merged, "kept();"));
}

TEST_CASE("F4SE region merge: a first emit with no previous file is unchanged") {
    const auto text = generated("Alpha");
    const auto normalized = [](std::string s) {
        std::string out;
        for (std::size_t i = 0; i < s.size(); i++) {
            if (s[i] == '\r' && i + 1 < s.size() && s[i + 1] == '\n') continue;
            out.push_back(s[i]);
        }
        return out;
    };
    CHECK(RegionMerge::merge(text, "") == normalized(text));
}

TEST_CASE("F4SE region merge: a hand written body survives regeneration") {
    const auto previous = generated("Alpha", "\t\treturn DoTheRealThing();");
    const auto merged = RegionMerge::merge(generated("Alpha"), previous);
    CHECK(contains(merged, "return DoTheRealThing();"));
    CHECK_FALSE(contains(merged, "STUB;"));
}

TEST_CASE("F4SE region merge: everything outside a region is machine owned and overwritten") {
    const std::string previous =
        "void Alpha(int oldSignature)\n{\n\t\t" + RegionMerge::begin("Alpha") +
        "\n\t\tkept();\n\t\t" + RegionMerge::end("Alpha") + "\n}\n// a stale trailing comment\n";
    const auto merged = RegionMerge::merge(generated("Alpha"), previous);
    CHECK(contains(merged, "kept();"));
    CHECK_FALSE(contains(merged, "a stale trailing comment"));
    CHECK_FALSE(contains(merged, "oldSignature"));
}

TEST_CASE("F4SE region merge: a changed signature keeps the body and gains a banner") {
    const std::string previous =
        "void Alpha(StaticFunctionTag * base, int extra)\n{\n\t\t" +
        RegionMerge::begin("Alpha") + "\n\t\tkept();\n\t\t" + RegionMerge::end("Alpha") + "\n}\n";
    const auto merged = RegionMerge::merge(generated("Alpha"), previous);
    CHECK(contains(merged, "kept();"));
    CHECK(contains(merged, "preserved body signature changed"));
}

TEST_CASE("F4SE region merge: an unchanged signature gets no banner") {
    const auto previous = generated("Alpha", "\t\tkept();");
    const auto merged = RegionMerge::merge(generated("Alpha"), previous);
    CHECK(contains(merged, "kept();"));
    CHECK_FALSE(contains(merged, "SIGNATURE CHANGED"));
}

TEST_CASE("F4SE region merge: several regions are matched by name not by order") {
    const auto previous = generated("Alpha", "\t\talphaBody();") + "\n" +
                          generated("Beta", "\t\tbetaBody();");
    const auto regenerated = generated("Beta") + "\n" + generated("Alpha");
    const auto merged = RegionMerge::merge(regenerated, previous);
    CHECK(contains(merged, "alphaBody();"));
    CHECK(contains(merged, "betaBody();"));
    CHECK(merged.find("betaBody();") < merged.find("alphaBody();"));
}

TEST_CASE("F4SE region merge: a body whose function is gone is reported rather than silently dropped") {
    const auto previous = generated("Alpha", "\t\tkept();") + "\n" +
                          generated("Removed", "\t\torphan();");
    const auto orphans = RegionMerge::orphaned(generated("Alpha"), previous);
    REQUIRE(orphans.size() == 1);
    CHECK(orphans[0] == "Removed");
}

TEST_CASE("F4SE region merge: a new function added since the last emit keeps its stub") {
    const auto previous = generated("Alpha", "\t\tkept();");
    const auto merged = RegionMerge::merge(generated("Alpha") + "\n" + generated("Fresh"),
                                           previous);
    CHECK(contains(merged, "kept();"));
    CHECK(contains(merged, "STUB;"));
}

TEST_CASE("F4SE region merge: reading a file recovers each region with its signature line") {
    const auto regions = RegionMerge::read(generated("Alpha", "\t\tbody();"));
    REQUIRE(regions.find("Alpha") != regions.end());
    CHECK(trim(regions.at("Alpha").body) == "body();");
    CHECK(regions.at("Alpha").signatureLine == "void Alpha(StaticFunctionTag * base)");
}

TEST_CASE("F4SE region merge: an end marker with no beginning is ignored rather than paired wrongly") {
    const std::string damaged = "\t\t" + RegionMerge::end("Alpha") + "\n\t\tloose();\n";
    CHECK(RegionMerge::read(damaged).empty());
}

TEST_CASE("F4SE region merge: windows line endings in the previous file do not corrupt the merge") {
    auto previous = generated("Alpha", "\t\tkept();");
    std::string crlf;
    for (std::size_t i = 0; i < previous.size(); i++) {
        crlf.push_back(previous[i]);
        if (previous[i] == '\n') crlf.insert(crlf.size() - 1, 1, '\r');
    }
    const auto merged = RegionMerge::merge(generated("Alpha"), crlf);
    CHECK(contains(merged, "kept();"));
    CHECK_FALSE(contains(merged, "\r"));
}

TEST_CASE("F4SE region merge: the emitter preserves a body when handed the previous file") {
    PluginBinding plugin;
    plugin.name = "Preserve";
    ModuleBinding module;
    module.name = "Util";
    module.scriptName = "PreserveUtil";
    NativeBinding answer;
    answer.functionName = "Answer";
    answer.papyrusClass = "PreserveUtil";
    answer.cppBaseType = F4SETypeMap::kStaticFunctionTag;
    answer.returnType = PapyrusTypeText{"int", false};
    answer.cppFunctionName = "Answer";
    module.natives.push_back(std::move(answer));
    plugin.modules.push_back(std::move(module));

    const F4SEEmitter emitter;
    const std::string path = "src/PapyrusPreserveUtil.cpp";

    const auto first = emitter.emit(plugin);
    REQUIRE(first.file(path) != nullptr);
    auto edited = first.file(path)->text;
    const auto marker = edited.find("return SInt32();");
    REQUIRE(marker != std::string::npos);
    edited.replace(marker, 15, "return 42;");

    F4SEEmitOptions options;
    options.existing[path] = edited;
    const auto second = emitter.emit(plugin, &options);

    REQUIRE(second.file(path) != nullptr);
    CHECK(contains(second.file(path)->text, "return 42;"));
    CHECK(second.orphanedBodies.empty());
}

TEST_CASE("F4SE region merge: the emitter reports an orphaned body when a native is removed") {
    const auto makeNative = [](const std::string& name) {
        NativeBinding out;
        out.functionName = name;
        out.papyrusClass = "OrphanUtil";
        out.cppBaseType = F4SETypeMap::kStaticFunctionTag;
        out.cppFunctionName = name;
        return out;
    };

    PluginBinding withBoth;
    withBoth.name = "Orphan";
    ModuleBinding module;
    module.name = "Util";
    module.scriptName = "OrphanUtil";
    module.natives.push_back(makeNative("Kept"));
    module.natives.push_back(makeNative("Dropped"));
    withBoth.modules.push_back(std::move(module));

    PluginBinding withOne;
    withOne.name = "Orphan";
    ModuleBinding oneModule;
    oneModule.name = "Util";
    oneModule.scriptName = "OrphanUtil";
    oneModule.natives.push_back(makeNative("Kept"));
    withOne.modules.push_back(std::move(oneModule));

    const F4SEEmitter emitter;
    const std::string path = "src/PapyrusOrphanUtil.cpp";
    const auto previous = emitter.emit(withBoth);
    REQUIRE(previous.file(path) != nullptr);

    F4SEEmitOptions options;
    options.existing[path] = previous.file(path)->text;
    const auto result = emitter.emit(withOne, &options);

    REQUIRE(result.orphanedBodies.size() == 1);
    CHECK(contains(result.orphanedBodies[0], "Dropped"));
}
