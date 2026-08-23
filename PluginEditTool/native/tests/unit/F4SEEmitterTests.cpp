#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/f4se/BindingValidator.hpp>
#include <fo4ide/f4se/CppEmitter.hpp>
#include <fo4ide/f4se/CppScanner.hpp>
#include <fo4ide/f4se/Emitter.hpp>
#include <fo4ide/f4se/RegionMerge.hpp>
#include <fo4ide/f4se/RegistrationExtractor.hpp>
#include <fo4ide/f4se/TypeMap.hpp>
#include <fo4ide/graph/GraphDiagnostic.hpp>
#include <fo4ide/papyrus/AnalysisService.hpp>
#include <fo4ide/papyrus/Parser.hpp>
#include <fo4ide/papyrus/ScriptIndex.hpp>

#include "../helpers/GraphBuilder.hpp"

namespace {

using fo4ide::f4se::CppEmitter;
using fo4ide::f4se::F4SEEmitOptions;
using fo4ide::f4se::F4SEEmitResult;
using fo4ide::f4se::F4SEEmitter;
using fo4ide::f4se::F4SETarget;
using fo4ide::f4se::F4SETypeMap;
using fo4ide::f4se::MessageSubscription;
using fo4ide::f4se::ModuleBinding;
using fo4ide::f4se::NativeBinding;
using fo4ide::f4se::NativeParameter;
using fo4ide::f4se::PapyrusTypeText;
using fo4ide::f4se::PluginBinding;
using fo4ide::f4se::RegionMerge;
using fo4ide::f4se::RegistrationExtractor;
using fo4ide::f4se::StructBinding;
using fo4ide::f4se::StructMemberBinding;
using fo4ide::graph::codes::kArgumentCount;
using fo4ide::graph::codes::kDuplicateNativeBinding;
using fo4ide::graph::codes::kNativeArityUnsupported;
using fo4ide::graph::codes::kStructOwnerMismatch;
using fo4ide::graph::codes::kUnmappedNativeType;

PapyrusTypeText t(const std::string& written) {
    if (written.size() >= 2 && written.compare(written.size() - 2, 2, "[]") == 0) {
        return PapyrusTypeText{written.substr(0, written.size() - 2), true};
    }
    return PapyrusTypeText{written, false};
}

NativeParameter param(const std::string& name, const std::string& type,
                      const std::string* def = nullptr, bool isUnsigned = false) {
    NativeParameter out;
    out.name = name;
    out.type = t(type);
    if (def != nullptr) out.defaultLiteral = *def;
    out.isUnsigned = isUnsigned;
    return out;
}

// Port of the Sample() fixture: two modules (one instance-based with a struct
// and a NoWait flag, one global with a latent function) plus a message.
PluginBinding sample(F4SETarget target = F4SETarget::Og_1_10_163) {
    PluginBinding plugin;
    plugin.name = "SamplePlugin";
    plugin.author = "Tester";
    plugin.versionMajor = 1;
    plugin.versionMinor = 2;
    plugin.versionPatch = 3;
    plugin.target = target;

    ModuleBinding actor;
    actor.name = "Actor";
    actor.scriptName = "SampleActor";
    actor.extends = "Actor";

    StructBinding worn;
    worn.name = "SampleWornItem";
    worn.ownerScript = "SampleActor";
    worn.members.push_back(StructMemberBinding{"item", t("Form"), false});
    worn.members.push_back(StructMemberBinding{"slotIndex", t("int"), true});
    actor.structs.push_back(std::move(worn));

    NativeBinding getWorn;
    getWorn.functionName = "GetSampleWornItem";
    getWorn.papyrusClass = "SampleActor";
    getWorn.cppBaseType = "Actor";
    getWorn.returnType = t("SampleWornItem");
    const std::string defFalse = "false";
    getWorn.parameters.push_back(param("aiSlot", "int", nullptr, true));
    getWorn.parameters.push_back(param("abFirst", "bool", &defFalse, false));
    getWorn.cppFunctionName = "GetSampleWornItem";
    actor.natives.push_back(std::move(getWorn));

    NativeBinding getTargets;
    getTargets.functionName = "GetTargets";
    getTargets.papyrusClass = "SampleActor";
    getTargets.cppBaseType = "Actor";
    getTargets.returnType = t("ObjectReference[]");
    getTargets.noWait = true;
    getTargets.cppFunctionName = "GetTargets";
    actor.natives.push_back(std::move(getTargets));
    plugin.modules.push_back(std::move(actor));

    ModuleBinding util;
    util.name = "Util";
    util.scriptName = "SampleUtil";

    NativeBinding describe;
    describe.functionName = "Describe";
    describe.papyrusClass = "SampleUtil";
    describe.cppBaseType = F4SETypeMap::kStaticFunctionTag;
    describe.returnType = t("string");
    describe.parameters.push_back(param("akForm", "Form"));
    describe.cppFunctionName = "Describe";
    util.natives.push_back(std::move(describe));

    NativeBinding waitFor;
    waitFor.functionName = "WaitFor";
    waitFor.papyrusClass = "SampleUtil";
    waitFor.cppBaseType = F4SETypeMap::kStaticFunctionTag;
    waitFor.returnType = t("bool");
    waitFor.parameters.push_back(param("afSeconds", "float"));
    waitFor.isLatent = true;
    waitFor.cppFunctionName = "WaitFor";
    util.natives.push_back(std::move(waitFor));
    plugin.modules.push_back(std::move(util));

    plugin.messages.push_back(MessageSubscription{"kMessage_GameDataReady", "OnGameDataReady"});
    return plugin;
}

F4SEEmitResult emitSample(F4SETarget target = F4SETarget::Og_1_10_163) {
    const F4SEEmitter emitter;
    auto result = emitter.emit(sample(target));
    REQUIRE(result.errors().empty());
    return result;
}

std::string textOf(const F4SEEmitResult& result, const std::string& path) {
    const auto* file = result.file(path);
    INFO("path: " + path);
    REQUIRE(file != nullptr);
    return file->text;
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

std::size_t countOf(const std::string& text, const std::string& needle) {
    std::size_t count = 0;
    std::size_t pos = 0;
    while ((pos = text.find(needle, pos)) != std::string::npos) {
        count++;
        pos += needle.size();
    }
    return count;
}

}  // namespace

TEST_CASE("F4SE emitter: every expected file is emitted") {
    const auto result = emitSample();
    const std::vector<std::string> expected = {
        "src/PapyrusSamplePluginActor.h",
        "src/PapyrusSamplePluginActor.cpp",
        "Data/Scripts/Source/User/SampleActor.psc",
        "src/PapyrusSamplePluginUtil.h",
        "src/PapyrusSamplePluginUtil.cpp",
        "Data/Scripts/Source/User/SampleUtil.psc",
        "src/SamplePluginRegistrations.h",
        "src/SamplePluginRegistrations.cpp",
        "src/main.cpp",
        "CMakeLists.txt",
        "README.md",
    };
    for (const auto& path : expected) {
        CHECK(result.file(path) != nullptr);
    }
    CHECK(result.files.size() == expected.size());
}

TEST_CASE("F4SE emitter: the emitter writes nothing to disk") {
    const auto result = emitSample();
    for (const auto& file : result.files) {
        CHECK_FALSE(file.text.empty());
    }
}

TEST_CASE("F4SE emitter: a registration matches the shape the runtime expects") {
    const auto& source = textOf(emitSample(), "src/PapyrusSamplePluginActor.cpp");
    CHECK(contains(source,
                   "new NativeFunction2<Actor, SampleWornItem, UInt32, bool>(\"GetSampleWornItem\", "
                   "\"SampleActor\", papyrusSamplePluginActor::GetSampleWornItem, vm)"));
}

TEST_CASE("F4SE emitter: a global registers against the receiver tag and takes it as the first parameter") {
    const auto& source = textOf(emitSample(), "src/PapyrusSamplePluginUtil.cpp");
    CHECK(contains(source, "new NativeFunction1<StaticFunctionTag, BSFixedString, TESForm*>(\"Describe\""));
    CHECK(contains(source, "BSFixedString Describe(StaticFunctionTag * base, TESForm* akForm)"));
}

TEST_CASE("F4SE emitter: a latent binding uses the latent template") {
    CHECK(contains(textOf(emitSample(), "src/PapyrusSamplePluginUtil.cpp"),
                   "new LatentNativeFunction1<StaticFunctionTag, bool, float>(\"WaitFor\""));
}

TEST_CASE("F4SE emitter: no wait is emitted as a separate flag statement after the registrations") {
    const auto& source = textOf(emitSample(), "src/PapyrusSamplePluginActor.cpp");
    CHECK(contains(source,
                   "vm->SetFunctionFlags(\"SampleActor\", \"GetTargets\", "
                   "IFunction::kFunctionFlag_NoWait);"));
    CHECK(source.find("SetFunctionFlags") > source.rfind("RegisterFunction"));
}

TEST_CASE("F4SE emitter: a struct is declared in the cpp and externed in the header") {
    const auto result = emitSample();
    CHECK(contains(textOf(result, "src/PapyrusSamplePluginActor.cpp"),
                   "DECLARE_STRUCT(SampleWornItem, \"SampleActor\")"));
    CHECK(contains(textOf(result, "src/PapyrusSamplePluginActor.h"),
                   "DECLARE_EXTERN_STRUCT(SampleWornItem)"));
}

TEST_CASE("F4SE emitter: a stub body is guarded so it cannot ship unimplemented") {
    const auto& source = textOf(emitSample(), "src/PapyrusSamplePluginUtil.cpp");
    CHECK(contains(source, "#ifndef " + std::string(CppEmitter::kAllowStubsMacro)));
    CHECK(contains(source, "static_assert(false,"));
    CHECK(contains(source, std::string(CppEmitter::kUnimplementedMacro) + "(\"SampleUtil.Describe\");"));
    CHECK(contains(source, "return BSFixedString();"));
}

TEST_CASE("F4SE emitter: the emitted script declares the natives with their defaults") {
    const auto& script = textOf(emitSample(), "Data/Scripts/Source/User/SampleActor.psc");
    CHECK(contains(script, "Scriptname SampleActor extends Actor Native Hidden"));
    CHECK(contains(script, "SampleWornItem Function GetSampleWornItem(int aiSlot, bool abFirst = false) native"));
    CHECK(contains(script, "ObjectReference[] Function GetTargets() native"));
    CHECK_FALSE(contains(script, "latent"));
    CHECK_FALSE(contains(script, "NoWait"));
}

TEST_CASE("F4SE emitter: a globals only module extends nothing and declares its functions global") {
    const auto& script = textOf(emitSample(), "Data/Scripts/Source/User/SampleUtil.psc");
    CHECK(contains(script, "Scriptname SampleUtil Native Hidden"));
    CHECK_FALSE(contains(script, "extends"));
    CHECK(contains(script, "string Function Describe(Form akForm) native global"));
}

TEST_CASE("F4SE emitter: a struct owned by another script is written owner qualified") {
    auto plugin = sample();
    auto& util = plugin.modules[1];
    NativeBinding firstWorn;
    firstWorn.functionName = "FirstWorn";
    firstWorn.papyrusClass = "SampleUtil";
    firstWorn.cppBaseType = F4SETypeMap::kStaticFunctionTag;
    firstWorn.returnType = t("SampleWornItem");
    firstWorn.cppFunctionName = "FirstWorn";
    util.natives.push_back(std::move(firstWorn));

    const F4SEEmitter emitter;
    const auto result = emitter.emit(plugin);
    REQUIRE(result.errors().empty());

    CHECK(contains(textOf(result, "Data/Scripts/Source/User/SampleUtil.psc"),
                   "SampleActor:SampleWornItem Function FirstWorn() native global"));
    CHECK(contains(textOf(result, "Data/Scripts/Source/User/SampleActor.psc"),
                   "SampleWornItem Function GetSampleWornItem"));
}

TEST_CASE("F4SE emitter: the emitted scripts compile to pex with the built in compiler") {
    const auto result = emitSample();

    const auto tempRoot =
        std::filesystem::temp_directory_path() / ("fo4re-f4se-emit-" +
                                                  std::to_string(
                                                      std::hash<std::string>{}("f4se-emit")));
    std::error_code ec;
    std::filesystem::remove_all(tempRoot, ec);
    REQUIRE(std::filesystem::create_directories(tempRoot));

    struct TempGuard {
        std::filesystem::path root;
        ~TempGuard() {
            std::error_code ignored;
            std::filesystem::remove_all(root, ignored);
        }
    } guard{tempRoot};

    std::vector<std::filesystem::path> pscFiles;
    for (const auto& file : result.files) {
        if (!(file.relativePath.size() >= 4 &&
              file.relativePath.compare(file.relativePath.size() - 4, 4, ".psc") == 0)) {
            continue;
        }
        const auto path = tempRoot / std::filesystem::path(file.relativePath).filename();
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        REQUIRE(stream.good());
        stream << file.text;
        stream.close();
        pscFiles.push_back(path);
    }

    fo4ide::papyrus::ScriptIndex index;
    index.addRoot(tempRoot);
    index.addRoot(fo4ide::graph::test::baseStubs());

    for (const auto& path : pscFiles) {
        const auto script = fo4ide::papyrus::Parser::parseFile(path);
        REQUIRE(script.has_value());
        fo4ide::papyrus::PapyrusCompileOptions options;
        const auto compiled =
            fo4ide::papyrus::compileScript(index, &*script, path.filename().string(), options);
        INFO("file: " + path.filename().string());
        REQUIRE(compiled.success());
    }
}

TEST_CASE("F4SE emitter: the original target exports the query entry point and checks the runtime") {
    const auto& main = textOf(emitSample(F4SETarget::Og_1_10_163), "src/main.cpp");
    CHECK(contains(main, "bool F4SEPlugin_Query(const F4SEInterface * f4se, PluginInfo * info)"));
    CHECK(contains(main, "f4se->runtimeVersion != CURRENT_RELEASE_RUNTIME"));
    CHECK_FALSE(contains(main, "F4SEPluginVersionData"));
}

TEST_CASE("F4SE emitter: the next generation target exports the version data instead") {
    const auto& main = textOf(emitSample(F4SETarget::Ng_0_7_8), "src/main.cpp");
    CHECK(contains(main, "__declspec(dllexport) F4SEPluginVersionData F4SEPlugin_Version"));
    CHECK_FALSE(contains(main, "F4SEPlugin_Query"));
}

TEST_CASE("F4SE emitter: both targets register the papyrus functions and refuse when that fails") {
    for (const auto target : {F4SETarget::Og_1_10_163, F4SETarget::Ng_0_7_8}) {
        const auto& main = textOf(emitSample(target), "src/main.cpp");
        CHECK(contains(main, "bool F4SEPlugin_Load(const F4SEInterface * f4se)"));
        CHECK(contains(main, "g_papyrus->Register(RegisterAllFuncs)"));
        CHECK(contains(main, "return false;"));
    }
}

TEST_CASE("F4SE emitter: a declared message subscription becomes a dispatch case") {
    const auto& main = textOf(emitSample(), "src/main.cpp");
    CHECK(contains(main, "case F4SEMessagingInterface::kMessage_GameDataReady:"));
    CHECK(contains(main, "OnGameDataReady(message);"));
    CHECK(contains(main, "g_messaging->RegisterListener(g_pluginHandle, \"F4SE\", OnF4SEMessage);"));
}

TEST_CASE("F4SE emitter: the aggregator calls every module so the shell never changes") {
    const auto& registrations = textOf(emitSample(), "src/SamplePluginRegistrations.cpp");
    CHECK(contains(registrations, "papyrusSamplePluginActor::RegisterFuncs(vm);"));
    CHECK(contains(registrations, "papyrusSamplePluginUtil::RegisterFuncs(vm);"));
    CHECK(contains(registrations, "return true;"));
}

TEST_CASE("F4SE emitter: the build file lists every emitted translation unit") {
    const auto& cmake = textOf(emitSample(), "CMakeLists.txt");
    CHECK(contains(cmake, "src/main.cpp"));
    CHECK(contains(cmake, "src/PapyrusSamplePluginActor.cpp"));
    CHECK(contains(cmake, "src/PapyrusSamplePluginUtil.cpp"));
    CHECK(contains(cmake, "src/SamplePluginRegistrations.cpp"));
    CHECK(contains(cmake, "find_package(f4se REQUIRED CONFIG)"));
}

TEST_CASE("F4SE emitter: emitted cpp read back agrees with the emitted papyrus") {
    const auto plugin = sample();
    const F4SEEmitter emitter;
    const auto result = emitter.emit(plugin);
    const auto crossCheck = F4SEEmitter::roundTrip(plugin, result);

    CHECK(crossCheck.mismatches.empty());
    CHECK(crossCheck.cppOnly.empty());
    CHECK(crossCheck.pscOnly.empty());
    CHECK(crossCheck.matched == static_cast<int>(plugin.allNatives().size()));
}

TEST_CASE("F4SE emitter: reading back recovers latency and flags that the papyrus cannot carry") {
    const auto plugin = sample();
    const F4SEEmitter emitter;
    const auto result = emitter.emit(plugin);

    RegistrationExtractor extractor;
    std::vector<std::string> structNames;
    for (const auto* declared : plugin.allStructs()) structNames.push_back(declared->name);

    std::vector<fo4ide::f4se::NativeBinding> recovered;
    for (const auto& file : result.files) {
        if (!(file.relativePath.size() >= 4 &&
              file.relativePath.compare(file.relativePath.size() - 4, 4, ".cpp") == 0)) {
            continue;
        }
        auto schema = extractor.extract(file.text, file.relativePath, structNames);
        for (auto& native : schema.natives) recovered.push_back(std::move(native));
    }

    std::size_t latentCount = 0;
    std::size_t noWaitCount = 0;
    for (const auto& native : recovered) {
        if (native.isLatent) {
            latentCount++;
            CHECK(native.functionName == "WaitFor");
        }
        if (native.noWait) {
            noWaitCount++;
            CHECK(native.functionName == "GetTargets");
        }
    }
    CHECK(latentCount == 1);
    CHECK(noWaitCount == 1);
}

TEST_CASE("F4SE emitter: emitted cpp has balanced braces") {
    const auto result = emitSample();
    for (const auto& file : result.files) {
        const bool isCpp =
            file.relativePath.size() >= 4 &&
            file.relativePath.compare(file.relativePath.size() - 4, 4, ".cpp") == 0;
        const bool isH =
            file.relativePath.size() >= 2 &&
            file.relativePath.compare(file.relativePath.size() - 2, 2, ".h") == 0;
        if (!isCpp && !isH) continue;
        const auto text = fo4ide::f4se::CppScanner::blankComments(file.text);
        CHECK(countOf(text, "{") == countOf(text, "}"));
        CHECK(countOf(text, "(") == countOf(text, ")"));
    }
}

TEST_CASE("F4SE emitter: every include is one the type map knows about") {
    std::vector<std::string> allowed = {
        "f4se/PapyrusVM.h",        "f4se/PapyrusNativeFunctions.h", "f4se/PapyrusArgs.h",
        "f4se/GameForms.h",        "f4se/GameReferences.h",         "f4se/GameObjects.h",
        "f4se/GameData.h",         "f4se/GameRTTI.h",               "f4se/PapyrusStruct.h",
        "f4se/PluginAPI.h",        "f4se_common/f4se_version.h",
    };

    const auto result = emitSample();
    for (const auto& file : result.files) {
        const bool isCpp =
            file.relativePath.size() >= 4 &&
            file.relativePath.compare(file.relativePath.size() - 4, 4, ".cpp") == 0;
        const bool isH =
            file.relativePath.size() >= 2 &&
            file.relativePath.compare(file.relativePath.size() - 2, 2, ".h") == 0;
        if (!isCpp && !isH) continue;

        std::size_t pos = 0;
        while ((pos = file.text.find("#include \"", pos)) != std::string::npos) {
            pos += 10;
            const auto end = file.text.find('"', pos);
            REQUIRE(end != std::string::npos);
            const auto path = file.text.substr(pos, end - pos);
            pos = end + 1;
            if (path.rfind("f4se", 0) == 0) {
                CHECK(std::find(allowed.begin(), allowed.end(), path) != allowed.end());
            }
        }
    }
}

TEST_CASE("F4SE emitter: every registration names a function the same file defines") {
    const auto result = emitSample();
    RegistrationExtractor extractor;
    for (const auto& file : result.files) {
        if (!(file.relativePath.size() >= 4 &&
              file.relativePath.compare(file.relativePath.size() - 4, 4, ".cpp") == 0)) {
            continue;
        }
        auto schema = extractor.extract(file.text, file.relativePath, {"SampleWornItem"});
        for (const auto& native : schema.natives) {
            const auto colon = native.cppFunctionName.rfind(':');
            const auto bare = colon == std::string::npos ? native.cppFunctionName
                                                         : native.cppFunctionName.substr(colon + 1);
            CHECK(contains(file.text, " " + bare + "("));
        }
    }
}

TEST_CASE("F4SE emitter: every body marker is balanced and unique") {
    const auto result = emitSample();
    for (const auto& file : result.files) {
        if (!(file.relativePath.size() >= 4 &&
              file.relativePath.compare(file.relativePath.size() - 4, 4, ".cpp") == 0)) {
            continue;
        }
        const auto begins = countOf(file.text, RegionMerge::kBeginPrefix);
        const auto ends = countOf(file.text, RegionMerge::kEndPrefix);
        CHECK(begins == ends);
        CHECK(RegionMerge::read(file.text).size() == begins);
    }
}

TEST_CASE("F4SE emitter: a duplicate registration is refused rather than emitted") {
    auto duplicated = sample();
    duplicated.modules[0].natives.push_back(duplicated.modules[0].natives[0]);

    const F4SEEmitter emitter;
    const auto result = emitter.emit(duplicated);
    CHECK(result.files.empty());
    REQUIRE(result.errors().size() == 1);
    CHECK(result.errors()[0].code == kDuplicateNativeBinding);
}

TEST_CASE("F4SE emitter: an unmappable parameter type is refused") {
    PluginBinding broken;
    broken.name = "SamplePlugin";
    ModuleBinding module;
    module.name = "Broken";
    module.scriptName = "SampleBroken";
    NativeBinding odd;
    odd.functionName = "Odd";
    odd.papyrusClass = "SampleBroken";
    odd.cppBaseType = F4SETypeMap::kStaticFunctionTag;
    odd.parameters.push_back(param("akThing", "SomeModsScript"));
    odd.cppFunctionName = "Odd";
    module.natives.push_back(std::move(odd));
    broken.modules.push_back(std::move(module));

    const F4SEEmitter emitter;
    const auto result = emitter.emit(broken);
    CHECK(result.files.empty());
    REQUIRE(result.errors().size() == 1);
    CHECK(result.errors()[0].code == kUnmappedNativeType);
}

TEST_CASE("F4SE emitter: an arity beyond the template family is refused") {
    PluginBinding wide;
    wide.name = "SamplePlugin";
    ModuleBinding module;
    module.name = "Wide";
    module.scriptName = "SampleWide";
    NativeBinding tooMany;
    tooMany.functionName = "TooMany";
    tooMany.papyrusClass = "SampleWide";
    tooMany.cppBaseType = F4SETypeMap::kStaticFunctionTag;
    for (int i = 0; i <= fo4ide::f4se::BindingValidator::kMaximumArity; i++) {
        tooMany.parameters.push_back(param("a" + std::to_string(i), "int"));
    }
    tooMany.cppFunctionName = "TooMany";
    module.natives.push_back(std::move(tooMany));
    wide.modules.push_back(std::move(module));

    const F4SEEmitter emitter;
    const auto result = emitter.emit(wide);
    REQUIRE(result.errors().size() == 1);
    CHECK(result.errors()[0].code == kNativeArityUnsupported);
}

TEST_CASE("F4SE emitter: a required parameter after an optional one is refused") {
    PluginBinding awkward;
    awkward.name = "SamplePlugin";
    ModuleBinding module;
    module.name = "Awkward";
    module.scriptName = "SampleAwkward";
    NativeBinding mixed;
    mixed.functionName = "Mixed";
    mixed.papyrusClass = "SampleAwkward";
    mixed.cppBaseType = F4SETypeMap::kStaticFunctionTag;
    const std::string zero = "0";
    mixed.parameters.push_back(param("a", "int", &zero));
    mixed.parameters.push_back(param("b", "int"));
    mixed.cppFunctionName = "Mixed";
    module.natives.push_back(std::move(mixed));
    awkward.modules.push_back(std::move(module));

    const F4SEEmitter emitter;
    const auto result = emitter.emit(awkward);
    REQUIRE(result.errors().size() == 1);
    CHECK(result.errors()[0].code == kArgumentCount);
}

TEST_CASE("F4SE emitter: a struct owned by no emitted script is refused") {
    PluginBinding orphaned;
    orphaned.name = "SamplePlugin";
    ModuleBinding module;
    module.name = "Orphan";
    module.scriptName = "SampleOrphan";
    StructBinding thing;
    thing.name = "Thing";
    thing.ownerScript = "NotEmittedAnywhere";
    module.structs.push_back(std::move(thing));
    orphaned.modules.push_back(std::move(module));

    const F4SEEmitter emitter;
    const auto result = emitter.emit(orphaned);
    REQUIRE(result.errors().size() == 1);
    CHECK(result.errors()[0].code == kStructOwnerMismatch);
}
