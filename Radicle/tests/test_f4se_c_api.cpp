#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <fo4/c_api.h>
#include <fo4/c_api_f4se.h>
#include <fo4/f4se/binding_json.hpp>
#include <fo4/f4se/binding_validator.hpp>
#include <fo4/f4se/emitter.hpp>
#include <fo4/f4se/papyrus_header_emitter.hpp>
#include <fo4/f4se/region_merge.hpp>
#include <fo4/f4se/type_map.hpp>

namespace {

using Json = nlohmann::json;

const char* const kSample = R"({
  "name": "SamplePlugin", "author": "Tester", "versionMajor": 1, "versionMinor": 2, "versionPatch": 3,
  "target": "Og_1_10_163",
  "modules": [
    {"name": "Actor", "scriptName": "SampleActor", "extends": "Actor",
     "structs": [{"name": "SampleWornItem", "ownerScript": "SampleActor",
                  "members": [{"name": "item", "type": "Form"},
                              {"name": "slotIndex", "type": "int", "unsigned": true}]}],
     "natives": [
       {"functionName": "GetSampleWornItem", "papyrusClass": "SampleActor", "cppBaseType": "Actor",
        "returnType": "SampleWornItem", "cppFunctionName": "GetSampleWornItem",
        "parameters": [{"name": "aiSlot", "type": "int", "unsigned": true},
                       {"name": "abFirst", "type": "bool", "default": "false"}]},
       {"functionName": "GetTargets", "papyrusClass": "SampleActor", "cppBaseType": "Actor",
        "returnType": "ObjectReference[]", "noWait": true, "cppFunctionName": "GetTargets"}]},
    {"name": "Util", "scriptName": "SampleUtil",
     "natives": [
       {"functionName": "Describe", "papyrusClass": "SampleUtil", "cppBaseType": "StaticFunctionTag",
        "returnType": "string", "cppFunctionName": "Describe",
        "parameters": [{"name": "akForm", "type": "Form"}]},
       {"functionName": "WaitFor", "papyrusClass": "SampleUtil", "cppBaseType": "StaticFunctionTag",
        "returnType": "bool", "latent": true, "cppFunctionName": "WaitFor",
        "parameters": [{"name": "afSeconds", "type": "float"}]}]}],
  "messages": [{"messageName": "kMessage_GameDataReady", "handlerName": "OnGameDataReady"}]
})";

struct Reply {
    radicle_status status{RADICLE_STATUS_OK};
    std::string text;
    std::string error;
    bool hadBytes{false};
};

Reply takeReply(radicle_status status, uint8_t* bytes, size_t size) {
    Reply reply;
    reply.status = status;
    const char* error = radicle_last_error();
    if (error != nullptr) reply.error = error;
    reply.hadBytes = bytes != nullptr;
    if (bytes != nullptr) {
        reply.text.assign(reinterpret_cast<const char*>(bytes), size);
        radicle_free_bytes(bytes);
    }
    return reply;
}

Reply emit(const std::string& plugin, const char* existing = nullptr, int despite = 0) {
    uint8_t* bytes = nullptr;
    size_t size = 0;
    const auto status = radicle_f4se_emit(plugin.c_str(), existing, despite, &bytes, &size);
    return takeReply(status, bytes, size);
}

Reply roundTrip(const std::string& plugin) {
    uint8_t* bytes = nullptr;
    size_t size = 0;
    const auto status = radicle_f4se_round_trip(plugin.c_str(), nullptr, 0, &bytes, &size);
    return takeReply(status, bytes, size);
}

std::string fileText(const Json& result, const std::string& path) {
    for (const auto& file : result.at("files")) {
        if (file.at("path").get<std::string>() == path) return file.at("text").get<std::string>();
    }
    ADD_FAILURE() << path << " was not emitted";
    return {};
}

std::vector<std::string> filePaths(const Json& result) {
    std::vector<std::string> paths;
    for (const auto& file : result.at("files")) paths.push_back(file.at("path").get<std::string>());
    return paths;
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

std::string utilCppGolden() {
    std::string text;
    text += "#include \"PapyrusSamplePluginUtil.h\"\n\n";
    text += "#include \"f4se/PapyrusVM.h\"\n";
    text += "#include \"f4se/PapyrusNativeFunctions.h\"\n";
    text += "#include \"f4se/PapyrusArgs.h\"\n";
    text += "#include \"f4se/GameForms.h\"\n";
    text += "#include \"f4se/GameReferences.h\"\n";
    text += "#include \"f4se/GameObjects.h\"\n";
    text += "#include \"f4se/GameData.h\"\n";
    text += "#include \"f4se/GameRTTI.h\"\n\n";
    text += "#ifndef F4SE_BINDING_ALLOW_STUBS\n";
    text += "#define F4SE_BINDING_UNIMPLEMENTED(name) \\\n";
    text += "\tstatic_assert(false, \"F4SE binding '\" name \"' has no implementation. \" \\\n";
    text += "\t\t\"Write one, or define F4SE_BINDING_ALLOW_STUBS to build without it.\")\n";
    text += "#else\n";
    text += "#define F4SE_BINDING_UNIMPLEMENTED(name) ((void)0)\n";
    text += "#endif\n\n";
    text += "namespace papyrusSamplePluginUtil\n{\n";
    text += "\tBSFixedString Describe(StaticFunctionTag * base, TESForm* akForm)\n";
    text += "\t{\n";
    text += "\t\t#pragma region FO4IDE_BODY Describe\n";
    text += "\t\tF4SE_BINDING_UNIMPLEMENTED(\"SampleUtil.Describe\");\n";
    text += "\t\treturn BSFixedString();\n";
    text += "\t\t#pragma endregion FO4IDE_BODY Describe\n";
    text += "\t}\n\n";
    text += "\tbool WaitFor(StaticFunctionTag * base, float afSeconds)\n";
    text += "\t{\n";
    text += "\t\t#pragma region FO4IDE_BODY WaitFor\n";
    text += "\t\tF4SE_BINDING_UNIMPLEMENTED(\"SampleUtil.WaitFor\");\n";
    text += "\t\treturn bool();\n";
    text += "\t\t#pragma endregion FO4IDE_BODY WaitFor\n";
    text += "\t}\n\n";
    text += "}\n\n";
    text += "void papyrusSamplePluginUtil::RegisterFuncs(VirtualMachine * vm)\n{\n";
    text += "\tvm->RegisterFunction(\n";
    text += "\t\tnew NativeFunction1<StaticFunctionTag, BSFixedString, TESForm*>(\"Describe\", "
            "\"SampleUtil\", papyrusSamplePluginUtil::Describe, vm));\n";
    text += "\tvm->RegisterFunction(\n";
    text += "\t\tnew LatentNativeFunction1<StaticFunctionTag, bool, float>(\"WaitFor\", "
            "\"SampleUtil\", papyrusSamplePluginUtil::WaitFor, vm));\n";
    text += "}\n";
    return text;
}

}

TEST(F4seCApi, EmitReturnsEveryFileTheCsharpEmitterWrites) {
    const auto reply = emit(kSample);
    ASSERT_EQ(reply.status, RADICLE_STATUS_OK);
    const auto result = Json::parse(reply.text);
    EXPECT_TRUE(result.at("success").get<bool>());
    EXPECT_TRUE(result.at("diagnostics").empty());
    EXPECT_TRUE(result.at("orphanedBodies").empty());
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
    EXPECT_EQ(filePaths(result), expected);
}

TEST(F4seCApi, EmittedFilesAreByteIdenticalToTheCsharpGoldens) {
    const auto result = Json::parse(emit(kSample).text);

    EXPECT_EQ(fileText(result, "src/PapyrusSamplePluginUtil.cpp"), utilCppGolden());
    EXPECT_EQ(fileText(result, "src/PapyrusSamplePluginUtil.h"),
              "#pragma once\n\nclass VirtualMachine;\n\nnamespace papyrusSamplePluginUtil\n{\n"
              "\tvoid RegisterFuncs(VirtualMachine * vm);\n}\n");
    EXPECT_EQ(fileText(result, "Data/Scripts/Source/User/SampleUtil.psc"),
              "Scriptname SampleUtil Native Hidden\n\n"
              "string Function Describe(Form akForm) native global\n"
              "bool Function WaitFor(float afSeconds) native global\n");
    EXPECT_EQ(fileText(result, "src/SamplePluginRegistrations.h"),
              "#pragma once\n\nclass VirtualMachine;\n\nbool RegisterAllFuncs(VirtualMachine * vm);\n");
    EXPECT_EQ(fileText(result, "src/SamplePluginRegistrations.cpp"),
              "#include \"SamplePluginRegistrations.h\"\n\n#include \"f4se/PapyrusVM.h\"\n"
              "#include \"PapyrusSamplePluginActor.h\"\n#include \"PapyrusSamplePluginUtil.h\"\n\n"
              "bool RegisterAllFuncs(VirtualMachine * vm)\n{\n"
              "\tpapyrusSamplePluginActor::RegisterFuncs(vm);\n"
              "\tpapyrusSamplePluginUtil::RegisterFuncs(vm);\n\treturn true;\n}\n");

    const auto cmake = fileText(result, "CMakeLists.txt");
    EXPECT_TRUE(contains(cmake, "project(SamplePlugin VERSION 1.2.3 LANGUAGES CXX)\n\n"));
    EXPECT_TRUE(contains(cmake,
                         "add_library(SamplePlugin SHARED\n\tsrc/main.cpp\n"
                         "\tsrc/PapyrusSamplePluginActor.cpp\n\tsrc/PapyrusSamplePluginUtil.cpp\n"
                         "\tsrc/SamplePluginRegistrations.cpp\n)\n\n"));
}

TEST(F4seCApi, TheActorModuleKeepsTheStructAndTheOwnerQualifiedShape) {
    const auto result = Json::parse(emit(kSample).text);
    const auto source = fileText(result, "src/PapyrusSamplePluginActor.cpp");
    EXPECT_TRUE(contains(source,
                         "new NativeFunction2<Actor, SampleWornItem, UInt32, bool>(\"GetSampleWornItem\", "
                         "\"SampleActor\", papyrusSamplePluginActor::GetSampleWornItem, vm)"));
    EXPECT_TRUE(contains(source, "DECLARE_STRUCT(SampleWornItem, \"SampleActor\")"));
    EXPECT_TRUE(contains(source,
                         "vm->SetFunctionFlags(\"SampleActor\", \"GetTargets\", "
                         "IFunction::kFunctionFlag_NoWait);"));
    EXPECT_TRUE(contains(fileText(result, "Data/Scripts/Source/User/SampleActor.psc"),
                         "SampleWornItem Function GetSampleWornItem(int aiSlot, bool abFirst = false) native"));
}

TEST(F4seCApi, TheNextGenerationTargetExportsTheVersionData) {
    auto plugin = Json::parse(kSample);
    plugin["target"] = 1;
    const auto main = fileText(Json::parse(emit(plugin.dump()).text), "src/main.cpp");
    EXPECT_TRUE(contains(main, "__declspec(dllexport) F4SEPluginVersionData F4SEPlugin_Version"));
    EXPECT_TRUE(contains(main, "\t\t\"Tester\",\n"));
    EXPECT_FALSE(contains(main, "F4SEPlugin_Query"));
}

TEST(F4seCApi, ADuplicateRegistrationIsAResultNotAnErrorStatus) {
    auto plugin = Json::parse(kSample);
    auto& natives = plugin["modules"][0]["natives"];
    const auto duplicate = natives[0];
    natives.push_back(duplicate);
    const auto reply = emit(plugin.dump());
    ASSERT_EQ(reply.status, RADICLE_STATUS_OK);
    const auto result = Json::parse(reply.text);
    EXPECT_FALSE(result.at("success").get<bool>());
    EXPECT_TRUE(result.at("files").empty());
    ASSERT_EQ(result.at("diagnostics").size(), 1u);
    EXPECT_EQ(result.at("diagnostics")[0].at("code").get<std::string>(), "GRA0050");
    EXPECT_EQ(result.at("diagnostics")[0].at("severity").get<std::string>(), "error");
    EXPECT_EQ(result.at("diagnostics")[0].at("message").get<std::string>(),
              "SampleActor.GetSampleWornItem is registered 2 times.");
}

TEST(F4seCApi, EmitDespiteErrorsStillWritesFilesButIsNotASuccess) {
    auto plugin = Json::parse(kSample);
    plugin["modules"][1]["natives"][0]["parameters"][0]["type"] = "SomeModsScript";
    const auto refused = Json::parse(emit(plugin.dump()).text);
    EXPECT_TRUE(refused.at("files").empty());
    EXPECT_EQ(refused.at("diagnostics")[0].at("code").get<std::string>(), "GRA0052");

    const auto forced = Json::parse(emit(plugin.dump(), nullptr, 1).text);
    EXPECT_FALSE(forced.at("files").empty());
    EXPECT_FALSE(forced.at("success").get<bool>());
}

TEST(F4seCApi, AnExistingBodyIsPreservedAndAnOrphanIsReported) {
    const auto first = Json::parse(emit(kSample).text);
    const std::string path = "src/PapyrusSamplePluginUtil.cpp";
    auto edited = fileText(first, path);
    const std::string stub =
        "\t\tF4SE_BINDING_UNIMPLEMENTED(\"SampleUtil.Describe\");\n\t\treturn BSFixedString();";
    const auto at = edited.find(stub);
    ASSERT_NE(at, std::string::npos);
    edited.replace(at, stub.size(), "\t\treturn BSFixedString(\"hand\");");
    edited += "#pragma region FO4IDE_BODY Gone\nx\n#pragma endregion FO4IDE_BODY Gone\n";

    const auto existing = Json{{path, edited}}.dump();
    const auto second = Json::parse(emit(kSample, existing.c_str()).text);
    const auto merged = fileText(second, path);
    EXPECT_TRUE(contains(merged, "return BSFixedString(\"hand\");"));
    EXPECT_FALSE(contains(merged, "F4SE_BINDING_UNIMPLEMENTED(\"SampleUtil.Describe\")"));
    ASSERT_EQ(second.at("orphanedBodies").size(), 1u);
    EXPECT_EQ(second.at("orphanedBodies")[0].get<std::string>(), path + ": Gone");
}

TEST(F4seCApi, RoundTripAgreesAndRecoversLatencyAndNoWait) {
    const auto reply = roundTrip(kSample);
    ASSERT_EQ(reply.status, RADICLE_STATUS_OK);
    const auto check = Json::parse(reply.text).at("roundTrip");
    EXPECT_TRUE(check.at("agrees").get<bool>());
    EXPECT_EQ(check.at("matched").get<int>(), 4);
    EXPECT_EQ(check.at("recovered").get<int>(), 4);
    EXPECT_EQ(check.at("declared").get<int>(), 4);
    EXPECT_EQ(check.at("latentOnlyInCpp").get<int>(), 1);
    EXPECT_EQ(check.at("noWaitOnlyInCpp").get<int>(), 1);
    EXPECT_TRUE(check.at("mismatches").empty());
    EXPECT_TRUE(check.at("cppOnly").empty());
    EXPECT_TRUE(check.at("pscOnly").empty());
}

TEST(F4seCApi, AMalformedPluginIsAParseErrorWithItsReason) {
    for (const std::string& broken : {std::string("{"), std::string("[]"),
                                     std::string(R"({"modules": []})"),
                                     std::string(R"({"name": "P", "modules": [{"name": "M"}]})"),
                                     std::string(R"({"name": "P", "target": "Mars"})"),
                                     std::string(R"({"name": "P", "versionMajor": "one"})")}) {
        const auto reply = emit(broken);
        EXPECT_EQ(reply.status, RADICLE_STATUS_PARSE_ERROR) << broken;
        EXPECT_FALSE(reply.hadBytes) << broken;
        EXPECT_NE(reply.error, "") << broken;
    }
    const auto missingName = emit(R"({"modules": []})");
    EXPECT_TRUE(contains(missingName.error, "\"name\" is required")) << missingName.error;
}

TEST(F4seCApi, AStructNameWithAngleBracketsIsRefusedAtTheBridge) {
    auto plugin = Json::parse(kSample);
    std::string nested = "A";
    for (int depth = 0; depth < 4000; depth++) nested = "VMArray<" + nested + ">";
    for (const std::string& name : {std::string("A<B>"), std::string("A>"), nested}) {
        plugin["modules"][0]["structs"][0]["name"] = name;
        const auto reply = emit(plugin.dump());
        EXPECT_EQ(reply.status, RADICLE_STATUS_PARSE_ERROR) << name.substr(0, 16);
        EXPECT_FALSE(reply.hadBytes);
        EXPECT_TRUE(contains(reply.error, "must not contain '<' or '>'")) << reply.error;
        EXPECT_EQ(roundTrip(plugin.dump()).status, RADICLE_STATUS_PARSE_ERROR);
    }
}

TEST(F4seCApi, AnEmptyExistingStringMeansNoExistingFiles) {
    const auto without = emit(kSample);
    const auto empty = emit(kSample, "");
    ASSERT_EQ(empty.status, RADICLE_STATUS_OK);
    EXPECT_EQ(empty.text, without.text);
}

TEST(F4seFidelity, NativeDuplicatesAreKeyedOnTheClassAndFunctionPair) {
    auto plugin = Json::parse(kSample);
    plugin["modules"] = Json::array();
    plugin["modules"].push_back(
        Json::parse("{\"name\": \"M\", \"scriptName\": \"Zeta\", \"natives\": ["
                    "{\"functionName\": \"c\", \"papyrusClass\": \"a\\nb\", \"cppBaseType\": \"StaticFunctionTag\"},"
                    "{\"functionName\": \"b\\nc\", \"papyrusClass\": \"a\", \"cppBaseType\": \"StaticFunctionTag\"}]}"));
    const auto parsed = fo4::f4se::bindingJson::parsePlugin(plugin.dump());
    ASSERT_TRUE(parsed.plugin.has_value());
    for (const auto& diagnostic : fo4::f4se::BindingValidator::validate(*parsed.plugin)) {
        EXPECT_NE(diagnostic.code, "GRA0050") << diagnostic.message;
    }
}

TEST(F4seCApi, MalformedExistingFilesAreAParseError) {
    EXPECT_EQ(emit(kSample, "[]").status, RADICLE_STATUS_PARSE_ERROR);
    EXPECT_EQ(emit(kSample, R"({"a": 1})").status, RADICLE_STATUS_PARSE_ERROR);
    EXPECT_EQ(emit(kSample, "{").status, RADICLE_STATUS_PARSE_ERROR);
}

TEST(F4seCApi, NullArgumentsAreRefusedAndTheOutputsAreCleared) {
    uint8_t stale = 0;
    uint8_t* bytes = &stale;
    size_t size = 99;
    EXPECT_EQ(radicle_f4se_emit(nullptr, nullptr, 0, &bytes, &size), RADICLE_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(bytes, nullptr);
    EXPECT_EQ(size, 0u);
    EXPECT_EQ(radicle_f4se_emit(kSample, nullptr, 0, nullptr, &size), RADICLE_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(radicle_f4se_emit(kSample, nullptr, 0, &bytes, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(radicle_f4se_round_trip(nullptr, nullptr, 0, &bytes, &size),
              RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST(F4seCApi, TheExportReadsTheSameBindingTheParserBuilds) {
    const auto parsed = fo4::f4se::bindingJson::parsePlugin(kSample);
    ASSERT_TRUE(parsed.plugin.has_value());
    const auto direct = fo4::f4se::F4SEEmitter().emit(*parsed.plugin);
    const auto result = Json::parse(emit(kSample).text);
    ASSERT_EQ(result.at("files").size(), direct.files.size());
    for (const auto& file : direct.files) {
        EXPECT_EQ(fileText(result, file.relativePath), file.text) << file.relativePath;
    }
}

TEST(F4seFidelity, DuplicateDiagnosticsKeepFirstSeenOrderAndTheFirstSpelling) {
    auto plugin = Json::parse(kSample);
    plugin["modules"] = Json::array();
    plugin["modules"].push_back(
        Json::parse(R"({"name": "M", "scriptName": "Zeta", "natives": [
            {"functionName": "Foo", "papyrusClass": "Zeta", "cppBaseType": "StaticFunctionTag"},
            {"functionName": "foo", "papyrusClass": "ZETA", "cppBaseType": "StaticFunctionTag"},
            {"functionName": "Bar", "papyrusClass": "Alpha", "cppBaseType": "StaticFunctionTag"},
            {"functionName": "BAR", "papyrusClass": "alpha", "cppBaseType": "StaticFunctionTag"}]})"));
    const auto parsed = fo4::f4se::bindingJson::parsePlugin(plugin.dump());
    ASSERT_TRUE(parsed.plugin.has_value());

    std::vector<std::string> messages;
    for (const auto& diagnostic : fo4::f4se::BindingValidator::validate(*parsed.plugin)) {
        if (diagnostic.code == "GRA0050") messages.push_back(diagnostic.message);
    }
    const std::vector<std::string> expected = {"Zeta.Foo is registered 2 times.",
                                               "Alpha.Bar is registered 2 times."};
    EXPECT_EQ(messages, expected);
}

TEST(F4seFidelity, OrphanedBodiesAreListedInTheOrderTheyAppearedInTheOldFile) {
    const std::string old =
        "#pragma region FO4IDE_BODY Zulu\nz\n#pragma endregion FO4IDE_BODY Zulu\n"
        "#pragma region FO4IDE_BODY Alpha\na\n#pragma endregion FO4IDE_BODY Alpha\n";
    const auto orphans = fo4::f4se::RegionMerge::orphaned("", old);
    const std::vector<std::string> expected = {"Zulu", "Alpha"};
    EXPECT_EQ(orphans, expected);
}

TEST(F4seFidelity, AnUnterminatedPragmaLineIsNotAMarkerAndDoesNotThrow) {
    EXPECT_NO_THROW({
        EXPECT_TRUE(fo4::f4se::RegionMerge::read("#pragma").empty());
        EXPECT_TRUE(fo4::f4se::RegionMerge::read("#pragma\nvoid f() {}\n").empty());
    });
}

TEST(F4seFidelity, AMarkerNeedsASeparatorBetweenEachWord) {
    EXPECT_TRUE(fo4::f4se::RegionMerge::read(
                    "#pragma regionFO4IDE_BODY A\nbody\n#pragma endregion FO4IDE_BODY A\n")
                    .empty());
    EXPECT_TRUE(fo4::f4se::RegionMerge::read(
                    "#pragma region FO4IDE_BODY\nbody\n#pragma endregion FO4IDE_BODY\n")
                    .empty());
    EXPECT_EQ(fo4::f4se::RegionMerge::read(
                  "#pragma region FO4IDE_BODY A\nbody\n#pragma endregion FO4IDE_BODY A\n")
                  .size(),
              1u);
}

TEST(F4seFidelity, MergingWithNothingPreservedReturnsTheGeneratedTextUntouched) {
    EXPECT_EQ(fo4::f4se::RegionMerge::merge("a\r\nb", ""), "a\r\nb");
}

TEST(F4seFidelity, ABlankTypeNameHasNoNameRefusal) {
    const fo4::f4se::F4SETypeMap map(std::vector<std::string>{});
    std::optional<fo4::f4se::CppTypeRef> cpp;
    std::string refusal;
    EXPECT_FALSE(map.tryToCpp(fo4::f4se::PapyrusTypeText{"  ", false}, false, cpp, &refusal));
    EXPECT_EQ(refusal, "the Papyrus type has no name");
}

TEST(F4seFidelity, ABlankExtendsClauseIsOmittedFromTheScriptHeader) {
    fo4::f4se::ModuleBinding module;
    module.scriptName = "SampleBlank";
    module.extends = "  ";
    EXPECT_EQ(fo4::f4se::PapyrusHeaderEmitter::headerLineFor(module),
              "Scriptname SampleBlank Native Hidden");
    module.extends = "Actor";
    EXPECT_EQ(fo4::f4se::PapyrusHeaderEmitter::headerLineFor(module),
              "Scriptname SampleBlank extends Actor Native Hidden");
}
