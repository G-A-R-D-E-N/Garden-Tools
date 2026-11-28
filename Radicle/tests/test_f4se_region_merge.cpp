#include <string>

#include <gtest/gtest.h>

#include <fo4/f4se/binding_model.hpp>
#include <fo4/f4se/emitter.hpp>
#include <fo4/f4se/region_merge.hpp>
#include <fo4/f4se/type_map.hpp>

namespace {

using fo4::f4se::F4SEEmitOptions;
using fo4::f4se::F4SEEmitter;
using fo4::f4se::F4SETypeMap;
using fo4::f4se::ModuleBinding;
using fo4::f4se::NativeBinding;
using fo4::f4se::PapyrusTypeText;
using fo4::f4se::PluginBinding;
using fo4::f4se::RegionMerge;

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

}

TEST(F4seRegionMerge, NewBodyMarkersArePreprocessorDirectives) {
    EXPECT_TRUE(RegionMerge::begin("Alpha") == "#pragma region FO4IDE_BODY Alpha");
    EXPECT_TRUE(RegionMerge::end("Alpha") == "#pragma endregion FO4IDE_BODY Alpha");
}

TEST(F4seRegionMerge, ALegacyCommentMarkedBodySurvivesRegeneration) {
    const std::string previous =
        "void Alpha(StaticFunctionTag * base)\n{\n\t\t// >>> body: Alpha\n\t\tkept();\n\t\t"
        "// <<< body: Alpha\n}\n";
    const auto merged = RegionMerge::merge(generated("Alpha"), previous);
    EXPECT_TRUE(contains(merged, "kept();"));
}

TEST(F4seRegionMerge, AFirstEmitWithNoPreviousFileIsUnchanged) {
    const auto text = generated("Alpha");
    const auto normalized = [](std::string s) {
        std::string out;
        for (std::size_t i = 0; i < s.size(); i++) {
            if (s[i] == '\r' && i + 1 < s.size() && s[i + 1] == '\n') continue;
            out.push_back(s[i]);
        }
        return out;
    };
    EXPECT_TRUE(RegionMerge::merge(text, "") == normalized(text));
}

TEST(F4seRegionMerge, AHandWrittenBodySurvivesRegeneration) {
    const auto previous = generated("Alpha", "\t\treturn DoTheRealThing();");
    const auto merged = RegionMerge::merge(generated("Alpha"), previous);
    EXPECT_TRUE(contains(merged, "return DoTheRealThing();"));
    EXPECT_FALSE(contains(merged, "STUB;"));
}

TEST(F4seRegionMerge, EverythingOutsideARegionIsMachineOwnedAndOverwritten) {
    const std::string previous =
        "void Alpha(int oldSignature)\n{\n\t\t" + RegionMerge::begin("Alpha") +
        "\n\t\tkept();\n\t\t" + RegionMerge::end("Alpha") + "\n}\n// a stale trailing comment\n";
    const auto merged = RegionMerge::merge(generated("Alpha"), previous);
    EXPECT_TRUE(contains(merged, "kept();"));
    EXPECT_FALSE(contains(merged, "a stale trailing comment"));
    EXPECT_FALSE(contains(merged, "oldSignature"));
}

TEST(F4seRegionMerge, AChangedSignatureKeepsTheBodyAndGainsABanner) {
    const std::string previous =
        "void Alpha(StaticFunctionTag * base, int extra)\n{\n\t\t" +
        RegionMerge::begin("Alpha") + "\n\t\tkept();\n\t\t" + RegionMerge::end("Alpha") + "\n}\n";
    const auto merged = RegionMerge::merge(generated("Alpha"), previous);
    EXPECT_TRUE(contains(merged, "kept();"));
    EXPECT_TRUE(contains(merged, "preserved body signature changed"));
}

TEST(F4seRegionMerge, AnUnchangedSignatureGetsNoBanner) {
    const auto previous = generated("Alpha", "\t\tkept();");
    const auto merged = RegionMerge::merge(generated("Alpha"), previous);
    EXPECT_TRUE(contains(merged, "kept();"));
    EXPECT_FALSE(contains(merged, "SIGNATURE CHANGED"));
}

TEST(F4seRegionMerge, SeveralRegionsAreMatchedByNameNotByOrder) {
    const auto previous = generated("Alpha", "\t\talphaBody();") + "\n" +
                          generated("Beta", "\t\tbetaBody();");
    const auto regenerated = generated("Beta") + "\n" + generated("Alpha");
    const auto merged = RegionMerge::merge(regenerated, previous);
    EXPECT_TRUE(contains(merged, "alphaBody();"));
    EXPECT_TRUE(contains(merged, "betaBody();"));
    EXPECT_TRUE(merged.find("betaBody();") < merged.find("alphaBody();"));
}

TEST(F4seRegionMerge, ABodyWhoseFunctionIsGoneIsReportedRatherThanSilentlyDropped) {
    const auto previous = generated("Alpha", "\t\tkept();") + "\n" +
                          generated("Removed", "\t\torphan();");
    const auto orphans = RegionMerge::orphaned(generated("Alpha"), previous);
    ASSERT_TRUE(orphans.size() == 1);
    EXPECT_TRUE(orphans[0] == "Removed");
}

TEST(F4seRegionMerge, ANewFunctionAddedSinceTheLastEmitKeepsItsStub) {
    const auto previous = generated("Alpha", "\t\tkept();");
    const auto merged = RegionMerge::merge(generated("Alpha") + "\n" + generated("Fresh"),
                                           previous);
    EXPECT_TRUE(contains(merged, "kept();"));
    EXPECT_TRUE(contains(merged, "STUB;"));
}

TEST(F4seRegionMerge, ReadingAFileRecoversEachRegionWithItsSignatureLine) {
    const auto regions = RegionMerge::read(generated("Alpha", "\t\tbody();"));
    ASSERT_TRUE(regions.find("Alpha") != regions.end());
    EXPECT_TRUE(trim(regions.at("Alpha").body) == "body();");
    EXPECT_TRUE(regions.at("Alpha").signatureLine == "void Alpha(StaticFunctionTag * base)");
}

TEST(F4seRegionMerge, AnEndMarkerWithNoBeginningIsIgnoredRatherThanPairedWrongly) {
    const std::string damaged = "\t\t" + RegionMerge::end("Alpha") + "\n\t\tloose();\n";
    EXPECT_TRUE(RegionMerge::read(damaged).empty());
}

TEST(F4seRegionMerge, WindowsLineEndingsInThePreviousFileDoNotCorruptTheMerge) {
    auto previous = generated("Alpha", "\t\tkept();");
    std::string crlf;
    for (std::size_t i = 0; i < previous.size(); i++) {
        crlf.push_back(previous[i]);
        if (previous[i] == '\n') crlf.insert(crlf.size() - 1, 1, '\r');
    }
    const auto merged = RegionMerge::merge(generated("Alpha"), crlf);
    EXPECT_TRUE(contains(merged, "kept();"));
    EXPECT_FALSE(contains(merged, "\r"));
}

TEST(F4seRegionMerge, TheEmitterPreservesABodyWhenHandedThePreviousFile) {
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
    ASSERT_TRUE(first.file(path) != nullptr);
    auto edited = first.file(path)->text;
    const auto marker = edited.find("return SInt32();");
    ASSERT_TRUE(marker != std::string::npos);
    edited.replace(marker, 15, "return 42;");

    F4SEEmitOptions options;
    options.existing[path] = edited;
    const auto second = emitter.emit(plugin, &options);

    ASSERT_TRUE(second.file(path) != nullptr);
    EXPECT_TRUE(contains(second.file(path)->text, "return 42;"));
    EXPECT_TRUE(second.orphanedBodies.empty());
}

TEST(F4seRegionMerge, TheEmitterReportsAnOrphanedBodyWhenANativeIsRemoved) {
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
    ASSERT_TRUE(previous.file(path) != nullptr);

    F4SEEmitOptions options;
    options.existing[path] = previous.file(path)->text;
    const auto result = emitter.emit(withOne, &options);

    ASSERT_TRUE(result.orphanedBodies.size() == 1);
    EXPECT_TRUE(contains(result.orphanedBodies[0], "Dropped"));
}
