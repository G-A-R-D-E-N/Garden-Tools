#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <fo4/f4se/cross_check.hpp>
#include <fo4/f4se/type_map.hpp>

namespace {

using fo4::f4se::CrossCheck;
using fo4::f4se::CrossCheckResult;
using fo4::f4se::F4SETypeMap;
using fo4::f4se::NativeBinding;
using fo4::f4se::NativeParameter;
using fo4::f4se::OracleNative;
using fo4::f4se::PapyrusTypeText;

PapyrusTypeText t(const std::string& written) {
    if (written.size() >= 2 && written.compare(written.size() - 2, 2, "[]") == 0) {
        return PapyrusTypeText{written.substr(0, written.size() - 2), true};
    }
    return PapyrusTypeText{written, false};
}

NativeBinding binding(std::string cls, std::string name, std::string ret = "None",
                      std::string cppBase = "TESForm",
                      const std::vector<std::string>& parameters = {}) {
    NativeBinding out;
    out.functionName = std::move(name);
    out.papyrusClass = std::move(cls);
    out.cppBaseType = std::move(cppBase);
    out.returnType = t(ret);
    for (std::size_t i = 0; i < parameters.size(); i++) {
        NativeParameter parameter;
        parameter.name = "a" + std::to_string(i);
        parameter.type = t(parameters[i]);
        out.parameters.push_back(std::move(parameter));
    }
    out.cppFunctionName = "papyrusX::" + out.functionName;
    return out;
}

OracleNative native(std::string cls, std::string name, std::string ret = "None",
                    bool global = false, const std::vector<std::string>& parameters = {}) {
    OracleNative out;
    out.script = std::move(cls);
    out.name = std::move(name);
    out.returnType = t(ret);
    out.isGlobal = global;
    for (const auto& parameter : parameters) out.parameterTypes.push_back(t(parameter));
    return out;
}

}

TEST(F4seCrossCheck, MatchingSignaturesAgree) {
    const auto result = CrossCheck::compare(
        {binding("Actor", "IsProtected", "bool")},
        {native("Actor", "IsProtected", "bool")});
    EXPECT_TRUE(result.agrees());
    EXPECT_TRUE(result.matched == 1);
}

TEST(F4seCrossCheck, AnOwnerQualifiedStructNameMatchesTheBareCppTypedef) {
    const auto result = CrossCheck::compare(
        {binding("ObjectReference", "ApplyMaterialSwap", "RemapData[]")},
        {native("ObjectReference", "ApplyMaterialSwap", "MatSwap:RemapData[]")});
    EXPECT_TRUE(result.mismatches.empty());
}

TEST(F4seCrossCheck, TwoDifferentQualifiedStructNamesStillDisagree) {
    const auto result = CrossCheck::compare(
        {binding("X", "F", "A:Thing")},
        {native("X", "F", "B:Thing")});
    ASSERT_TRUE(result.mismatches.size() == 1);
    EXPECT_TRUE(result.mismatches[0].what == "return");
}

TEST(F4seCrossCheck, ADifferingArityIsReportedAndStopsFurtherComparison) {
    const auto result = CrossCheck::compare(
        {binding("Actor", "F", "None", "TESForm", {"int"})},
        {native("Actor", "F")});
    ASSERT_TRUE(result.mismatches.size() == 1);
    EXPECT_TRUE(result.mismatches[0].what == "arity");
}

TEST(F4seCrossCheck, ADifferingParameterTypeIsReportedByPosition) {
    const auto result = CrossCheck::compare(
        {binding("Actor", "F", "None", "TESForm", {"int", "float"})},
        {native("Actor", "F", "None", false, {"int", "bool"})});
    ASSERT_TRUE(result.mismatches.size() == 1);
    EXPECT_TRUE(result.mismatches[0].what == "parameter 1");
}

TEST(F4seCrossCheck, ADifferingGlobalFlagIsReported) {
    const auto result = CrossCheck::compare(
        {binding("Game", "GetPlayer", "Actor", F4SETypeMap::kStaticFunctionTag)},
        {native("Game", "GetPlayer", "Actor")});
    ASSERT_TRUE(result.mismatches.size() == 1);
    EXPECT_TRUE(result.mismatches[0].what == "global");
}

TEST(F4seCrossCheck, ARegistrationWithNoDeclarationIsReportedAsCppOnly) {
    const auto result = CrossCheck::compare(
        {binding("F4SE", "TestInventoryFunc", "None", F4SETypeMap::kStaticFunctionTag,
                 {"ObjectReference"})},
        {native("F4SE", "GetVersion", "int", true)});
    ASSERT_TRUE(result.cppOnly.size() == 1);
    EXPECT_TRUE(result.cppOnly[0].functionName == "TestInventoryFunc");
    ASSERT_TRUE(result.pscOnly.size() == 1);
    EXPECT_TRUE(result.pscOnly[0].name == "GetVersion");
    EXPECT_FALSE(result.agrees());
}

TEST(F4seCrossCheck, LatencyAndNoWaitAreCountedButNeverCompared) {
    auto latent = binding("UI", "Set", "bool", F4SETypeMap::kStaticFunctionTag, {"string"});
    latent.isLatent = true;
    latent.noWait = true;

    const auto result = CrossCheck::compare(
        {latent},
        {native("UI", "Set", "bool", true, {"string"})});
    EXPECT_TRUE(result.mismatches.empty());
    EXPECT_TRUE(result.latentOnlyInCpp == 1);
    EXPECT_TRUE(result.noWaitOnlyInCpp == 1);
}

TEST(F4seCrossCheck, ClassAndFunctionNamesMatchCaseInsensitively) {
    const auto result = CrossCheck::compare(
        {binding("actor", "isprotected", "bool")},
        {native("Actor", "IsProtected", "bool")});
    EXPECT_TRUE(result.matched == 1);
    EXPECT_TRUE(result.agrees());
}
