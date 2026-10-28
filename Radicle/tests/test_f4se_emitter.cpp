#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <fo4/f4se/binding_validator.hpp>
#include <fo4/f4se/cpp_emitter.hpp>
#include <fo4/f4se/cpp_scanner.hpp>
#include <fo4/f4se/emitter.hpp>
#include <fo4/f4se/region_merge.hpp>
#include <fo4/f4se/registration_extractor.hpp>
#include <fo4/f4se/type_map.hpp>
#include <fo4/f4se/diagnostic.hpp>

namespace {

using fo4::f4se::CppEmitter;
using fo4::f4se::F4SEEmitOptions;
using fo4::f4se::F4SEEmitResult;
using fo4::f4se::F4SEEmitter;
using fo4::f4se::F4SETarget;
using fo4::f4se::F4SETypeMap;
using fo4::f4se::MessageSubscription;
using fo4::f4se::ModuleBinding;
using fo4::f4se::NativeBinding;
using fo4::f4se::NativeParameter;
using fo4::f4se::PapyrusTypeText;
using fo4::f4se::PluginBinding;
using fo4::f4se::RegionMerge;
using fo4::f4se::RegistrationExtractor;
using fo4::f4se::StructBinding;
using fo4::f4se::StructMemberBinding;
using fo4::f4se::codes::kArgumentCount;
using fo4::f4se::codes::kDuplicateNativeBinding;
using fo4::f4se::codes::kNativeArityUnsupported;
using fo4::f4se::codes::kStructOwnerMismatch;
using fo4::f4se::codes::kUnmappedNativeType;

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
    EXPECT_TRUE(result.errors().empty());
    return result;
}

std::string textOf(const F4SEEmitResult& result, const std::string& path) {
    const auto* file = result.file(path);
    SCOPED_TRACE("path: " + path);
    EXPECT_TRUE(file != nullptr);
    return file == nullptr ? std::string() : file->text;
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

}

TEST(F4seEmitter, EveryExpectedFileIsEmitted) {
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
        EXPECT_TRUE(result.file(path) != nullptr);
    }
    EXPECT_TRUE(result.files.size() == expected.size());
}

TEST(F4seEmitter, TheEmitterWritesNothingToDisk) {
    const auto result = emitSample();
    for (const auto& file : result.files) {
        EXPECT_FALSE(file.text.empty());
    }
}

TEST(F4seEmitter, ARegistrationMatchesTheShapeTheRuntimeExpects) {
    const auto& source = textOf(emitSample(), "src/PapyrusSamplePluginActor.cpp");
    EXPECT_TRUE(contains(source,
                   "new NativeFunction2<Actor, SampleWornItem, UInt32, bool>(\"GetSampleWornItem\", "
                   "\"SampleActor\", papyrusSamplePluginActor::GetSampleWornItem, vm)"));
}

TEST(F4seEmitter, AGlobalRegistersAgainstTheReceiverTagAndTakesItAsTheFirstParameter) {
    const auto& source = textOf(emitSample(), "src/PapyrusSamplePluginUtil.cpp");
    EXPECT_TRUE(contains(source, "new NativeFunction1<StaticFunctionTag, BSFixedString, TESForm*>(\"Describe\""));
    EXPECT_TRUE(contains(source, "BSFixedString Describe(StaticFunctionTag * base, TESForm* akForm)"));
}

TEST(F4seEmitter, ALatentBindingUsesTheLatentTemplate) {
    EXPECT_TRUE(contains(textOf(emitSample(), "src/PapyrusSamplePluginUtil.cpp"),
                   "new LatentNativeFunction1<StaticFunctionTag, bool, float>(\"WaitFor\""));
}

TEST(F4seEmitter, NoWaitIsEmittedAsASeparateFlagStatementAfterTheRegistrations) {
    const auto& source = textOf(emitSample(), "src/PapyrusSamplePluginActor.cpp");
    EXPECT_TRUE(contains(source,
                   "vm->SetFunctionFlags(\"SampleActor\", \"GetTargets\", "
                   "IFunction::kFunctionFlag_NoWait);"));
    EXPECT_TRUE(source.find("SetFunctionFlags") > source.rfind("RegisterFunction"));
}

TEST(F4seEmitter, AStructIsDeclaredInTheCppAndExternedInTheHeader) {
    const auto result = emitSample();
    EXPECT_TRUE(contains(textOf(result, "src/PapyrusSamplePluginActor.cpp"),
                   "DECLARE_STRUCT(SampleWornItem, \"SampleActor\")"));
    EXPECT_TRUE(contains(textOf(result, "src/PapyrusSamplePluginActor.h"),
                   "DECLARE_EXTERN_STRUCT(SampleWornItem)"));
}

TEST(F4seEmitter, AStubBodyIsGuardedSoItCannotShipUnimplemented) {
    const auto& source = textOf(emitSample(), "src/PapyrusSamplePluginUtil.cpp");
    EXPECT_TRUE(contains(source, "#ifndef " + std::string(CppEmitter::kAllowStubsMacro)));
    EXPECT_TRUE(contains(source, "static_assert(false,"));
    EXPECT_TRUE(contains(source, std::string(CppEmitter::kUnimplementedMacro) + "(\"SampleUtil.Describe\");"));
    EXPECT_TRUE(contains(source, "return BSFixedString();"));
}

TEST(F4seEmitter, TheEmittedScriptDeclaresTheNativesWithTheirDefaults) {
    const auto& script = textOf(emitSample(), "Data/Scripts/Source/User/SampleActor.psc");
    EXPECT_TRUE(contains(script, "Scriptname SampleActor extends Actor Native Hidden"));
    EXPECT_TRUE(contains(script, "SampleWornItem Function GetSampleWornItem(int aiSlot, bool abFirst = false) native"));
    EXPECT_TRUE(contains(script, "ObjectReference[] Function GetTargets() native"));
    EXPECT_FALSE(contains(script, "latent"));
    EXPECT_FALSE(contains(script, "NoWait"));
}

TEST(F4seEmitter, AGlobalsOnlyModuleExtendsNothingAndDeclaresItsFunctionsGlobal) {
    const auto& script = textOf(emitSample(), "Data/Scripts/Source/User/SampleUtil.psc");
    EXPECT_TRUE(contains(script, "Scriptname SampleUtil Native Hidden"));
    EXPECT_FALSE(contains(script, "extends"));
    EXPECT_TRUE(contains(script, "string Function Describe(Form akForm) native global"));
}

TEST(F4seEmitter, AStructOwnedByAnotherScriptIsWrittenOwnerQualified) {
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
    ASSERT_TRUE(result.errors().empty());

    EXPECT_TRUE(contains(textOf(result, "Data/Scripts/Source/User/SampleUtil.psc"),
                   "SampleActor:SampleWornItem Function FirstWorn() native global"));
    EXPECT_TRUE(contains(textOf(result, "Data/Scripts/Source/User/SampleActor.psc"),
                   "SampleWornItem Function GetSampleWornItem"));
}

TEST(F4seEmitter, TheOriginalTargetExportsTheQueryEntryPointAndChecksTheRuntime) {
    const auto& main = textOf(emitSample(F4SETarget::Og_1_10_163), "src/main.cpp");
    EXPECT_TRUE(contains(main, "bool F4SEPlugin_Query(const F4SEInterface * f4se, PluginInfo * info)"));
    EXPECT_TRUE(contains(main, "f4se->runtimeVersion != CURRENT_RELEASE_RUNTIME"));
    EXPECT_FALSE(contains(main, "F4SEPluginVersionData"));
}

TEST(F4seEmitter, TheNextGenerationTargetExportsTheVersionDataInstead) {
    const auto& main = textOf(emitSample(F4SETarget::Ng_0_7_8), "src/main.cpp");
    EXPECT_TRUE(contains(main, "__declspec(dllexport) F4SEPluginVersionData F4SEPlugin_Version"));
    EXPECT_FALSE(contains(main, "F4SEPlugin_Query"));
}

TEST(F4seEmitter, BothTargetsRegisterThePapyrusFunctionsAndRefuseWhenThatFails) {
    for (const auto target : {F4SETarget::Og_1_10_163, F4SETarget::Ng_0_7_8}) {
        const auto& main = textOf(emitSample(target), "src/main.cpp");
        EXPECT_TRUE(contains(main, "bool F4SEPlugin_Load(const F4SEInterface * f4se)"));
        EXPECT_TRUE(contains(main, "g_papyrus->Register(RegisterAllFuncs)"));
        EXPECT_TRUE(contains(main, "return false;"));
    }
}

TEST(F4seEmitter, ADeclaredMessageSubscriptionBecomesADispatchCase) {
    const auto& main = textOf(emitSample(), "src/main.cpp");
    EXPECT_TRUE(contains(main, "case F4SEMessagingInterface::kMessage_GameDataReady:"));
    EXPECT_TRUE(contains(main, "OnGameDataReady(message);"));
    EXPECT_TRUE(contains(main, "g_messaging->RegisterListener(g_pluginHandle, \"F4SE\", OnF4SEMessage);"));
}

TEST(F4seEmitter, TheAggregatorCallsEveryModuleSoTheShellNeverChanges) {
    const auto& registrations = textOf(emitSample(), "src/SamplePluginRegistrations.cpp");
    EXPECT_TRUE(contains(registrations, "papyrusSamplePluginActor::RegisterFuncs(vm);"));
    EXPECT_TRUE(contains(registrations, "papyrusSamplePluginUtil::RegisterFuncs(vm);"));
    EXPECT_TRUE(contains(registrations, "return true;"));
}

TEST(F4seEmitter, TheBuildFileListsEveryEmittedTranslationUnit) {
    const auto& cmake = textOf(emitSample(), "CMakeLists.txt");
    EXPECT_TRUE(contains(cmake, "src/main.cpp"));
    EXPECT_TRUE(contains(cmake, "src/PapyrusSamplePluginActor.cpp"));
    EXPECT_TRUE(contains(cmake, "src/PapyrusSamplePluginUtil.cpp"));
    EXPECT_TRUE(contains(cmake, "src/SamplePluginRegistrations.cpp"));
    EXPECT_TRUE(contains(cmake, "find_package(f4se REQUIRED CONFIG)"));
}

TEST(F4seEmitter, EmittedCppReadBackAgreesWithTheEmittedPapyrus) {
    const auto plugin = sample();
    const F4SEEmitter emitter;
    const auto result = emitter.emit(plugin);
    const auto crossCheck = F4SEEmitter::roundTrip(plugin, result);

    EXPECT_TRUE(crossCheck.mismatches.empty());
    EXPECT_TRUE(crossCheck.cppOnly.empty());
    EXPECT_TRUE(crossCheck.pscOnly.empty());
    EXPECT_TRUE(crossCheck.matched == static_cast<int>(plugin.allNatives().size()));
}

TEST(F4seEmitter, ReadingBackRecoversLatencyAndFlagsThatThePapyrusCannotCarry) {
    const auto plugin = sample();
    const F4SEEmitter emitter;
    const auto result = emitter.emit(plugin);

    RegistrationExtractor extractor;
    std::vector<std::string> structNames;
    for (const auto* declared : plugin.allStructs()) structNames.push_back(declared->name);

    std::vector<fo4::f4se::NativeBinding> recovered;
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
            EXPECT_TRUE(native.functionName == "WaitFor");
        }
        if (native.noWait) {
            noWaitCount++;
            EXPECT_TRUE(native.functionName == "GetTargets");
        }
    }
    EXPECT_TRUE(latentCount == 1);
    EXPECT_TRUE(noWaitCount == 1);
}

TEST(F4seEmitter, EmittedCppHasBalancedBraces) {
    const auto result = emitSample();
    for (const auto& file : result.files) {
        const bool isCpp =
            file.relativePath.size() >= 4 &&
            file.relativePath.compare(file.relativePath.size() - 4, 4, ".cpp") == 0;
        const bool isH =
            file.relativePath.size() >= 2 &&
            file.relativePath.compare(file.relativePath.size() - 2, 2, ".h") == 0;
        if (!isCpp && !isH) continue;
        const auto text = fo4::f4se::CppScanner::blankComments(file.text);
        EXPECT_TRUE(countOf(text, "{") == countOf(text, "}"));
        EXPECT_TRUE(countOf(text, "(") == countOf(text, ")"));
    }
}

TEST(F4seEmitter, EveryIncludeIsOneTheTypeMapKnowsAbout) {
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
            ASSERT_TRUE(end != std::string::npos);
            const auto path = file.text.substr(pos, end - pos);
            pos = end + 1;
            if (path.rfind("f4se", 0) == 0) {
                EXPECT_TRUE(std::find(allowed.begin(), allowed.end(), path) != allowed.end());
            }
        }
    }
}

TEST(F4seEmitter, EveryRegistrationNamesAFunctionTheSameFileDefines) {
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
            EXPECT_TRUE(contains(file.text, " " + bare + "("));
        }
    }
}

TEST(F4seEmitter, EveryBodyMarkerIsBalancedAndUnique) {
    const auto result = emitSample();
    for (const auto& file : result.files) {
        if (!(file.relativePath.size() >= 4 &&
              file.relativePath.compare(file.relativePath.size() - 4, 4, ".cpp") == 0)) {
            continue;
        }
        const auto begins = countOf(file.text, RegionMerge::kBeginPrefix);
        const auto ends = countOf(file.text, RegionMerge::kEndPrefix);
        EXPECT_TRUE(begins == ends);
        EXPECT_TRUE(RegionMerge::read(file.text).size() == begins);
    }
}

TEST(F4seEmitter, ADuplicateRegistrationIsRefusedRatherThanEmitted) {
    auto duplicated = sample();
    duplicated.modules[0].natives.push_back(duplicated.modules[0].natives[0]);

    const F4SEEmitter emitter;
    const auto result = emitter.emit(duplicated);
    EXPECT_TRUE(result.files.empty());
    ASSERT_TRUE(result.errors().size() == 1);
    EXPECT_TRUE(result.errors()[0].code == kDuplicateNativeBinding);
}

TEST(F4seEmitter, AnUnmappableParameterTypeIsRefused) {
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
    EXPECT_TRUE(result.files.empty());
    ASSERT_TRUE(result.errors().size() == 1);
    EXPECT_TRUE(result.errors()[0].code == kUnmappedNativeType);
}

TEST(F4seEmitter, AnArityBeyondTheTemplateFamilyIsRefused) {
    PluginBinding wide;
    wide.name = "SamplePlugin";
    ModuleBinding module;
    module.name = "Wide";
    module.scriptName = "SampleWide";
    NativeBinding tooMany;
    tooMany.functionName = "TooMany";
    tooMany.papyrusClass = "SampleWide";
    tooMany.cppBaseType = F4SETypeMap::kStaticFunctionTag;
    for (int i = 0; i <= fo4::f4se::BindingValidator::kMaximumArity; i++) {
        tooMany.parameters.push_back(param("a" + std::to_string(i), "int"));
    }
    tooMany.cppFunctionName = "TooMany";
    module.natives.push_back(std::move(tooMany));
    wide.modules.push_back(std::move(module));

    const F4SEEmitter emitter;
    const auto result = emitter.emit(wide);
    ASSERT_TRUE(result.errors().size() == 1);
    EXPECT_TRUE(result.errors()[0].code == kNativeArityUnsupported);
}

TEST(F4seEmitter, ARequiredParameterAfterAnOptionalOneIsRefused) {
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
    ASSERT_TRUE(result.errors().size() == 1);
    EXPECT_TRUE(result.errors()[0].code == kArgumentCount);
}

TEST(F4seEmitter, AStructOwnedByNoEmittedScriptIsRefused) {
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
    ASSERT_TRUE(result.errors().size() == 1);
    EXPECT_TRUE(result.errors()[0].code == kStructOwnerMismatch);
}
