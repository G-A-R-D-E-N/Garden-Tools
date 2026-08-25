#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/f4se/CrossCheck.hpp>
#include <fo4ide/f4se/TypeMap.hpp>

namespace {

using fo4ide::f4se::CrossCheck;
using fo4ide::f4se::CrossCheckResult;
using fo4ide::f4se::F4SETypeMap;
using fo4ide::f4se::NativeBinding;
using fo4ide::f4se::NativeParameter;
using fo4ide::f4se::OracleNative;
using fo4ide::f4se::PapyrusTypeText;

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

}  // namespace

TEST_CASE("F4SE cross check: matching signatures agree") {
    const auto result = CrossCheck::compare(
        {binding("Actor", "IsProtected", "bool")},
        {native("Actor", "IsProtected", "bool")});
    CHECK(result.agrees());
    CHECK(result.matched == 1);
}

TEST_CASE("F4SE cross check: an owner qualified struct name matches the bare cpp typedef") {
    const auto result = CrossCheck::compare(
        {binding("ObjectReference", "ApplyMaterialSwap", "RemapData[]")},
        {native("ObjectReference", "ApplyMaterialSwap", "MatSwap:RemapData[]")});
    CHECK(result.mismatches.empty());
}

TEST_CASE("F4SE cross check: two different qualified struct names still disagree") {
    const auto result = CrossCheck::compare(
        {binding("X", "F", "A:Thing")},
        {native("X", "F", "B:Thing")});
    REQUIRE(result.mismatches.size() == 1);
    CHECK(result.mismatches[0].what == "return");
}

TEST_CASE("F4SE cross check: a differing arity is reported and stops further comparison") {
    const auto result = CrossCheck::compare(
        {binding("Actor", "F", "None", "TESForm", {"int"})},
        {native("Actor", "F")});
    REQUIRE(result.mismatches.size() == 1);
    CHECK(result.mismatches[0].what == "arity");
}

TEST_CASE("F4SE cross check: a differing parameter type is reported by position") {
    const auto result = CrossCheck::compare(
        {binding("Actor", "F", "None", "TESForm", {"int", "float"})},
        {native("Actor", "F", "None", false, {"int", "bool"})});
    REQUIRE(result.mismatches.size() == 1);
    CHECK(result.mismatches[0].what == "parameter 1");
}

TEST_CASE("F4SE cross check: a differing global flag is reported") {
    const auto result = CrossCheck::compare(
        {binding("Game", "GetPlayer", "Actor", F4SETypeMap::kStaticFunctionTag)},
        {native("Game", "GetPlayer", "Actor")});
    REQUIRE(result.mismatches.size() == 1);
    CHECK(result.mismatches[0].what == "global");
}

TEST_CASE("F4SE cross check: a registration with no declaration is reported as cpp only") {
    const auto result = CrossCheck::compare(
        {binding("F4SE", "TestInventoryFunc", "None", F4SETypeMap::kStaticFunctionTag,
                 {"ObjectReference"})},
        {native("F4SE", "GetVersion", "int", true)});
    REQUIRE(result.cppOnly.size() == 1);
    CHECK(result.cppOnly[0].functionName == "TestInventoryFunc");
    REQUIRE(result.pscOnly.size() == 1);
    CHECK(result.pscOnly[0].name == "GetVersion");
    CHECK_FALSE(result.agrees());
}

TEST_CASE("F4SE cross check: latency and no wait are counted but never compared") {
    auto latent = binding("UI", "Set", "bool", F4SETypeMap::kStaticFunctionTag, {"string"});
    latent.isLatent = true;
    latent.noWait = true;

    const auto result = CrossCheck::compare(
        {latent},
        {native("UI", "Set", "bool", true, {"string"})});
    CHECK(result.mismatches.empty());
    CHECK(result.latentOnlyInCpp == 1);
    CHECK(result.noWaitOnlyInCpp == 1);
}

TEST_CASE("F4SE cross check: class and function names match case insensitively") {
    const auto result = CrossCheck::compare(
        {binding("actor", "isprotected", "bool")},
        {native("Actor", "IsProtected", "bool")});
    CHECK(result.matched == 1);
    CHECK(result.agrees());
}
