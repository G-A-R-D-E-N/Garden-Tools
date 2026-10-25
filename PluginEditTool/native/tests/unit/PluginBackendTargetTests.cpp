#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/app/PluginBackendTarget.hpp>
#include <fo4ide/core/JsonInt.hpp>
#include <fo4ide/plugin/IPluginBackend.hpp>

#include "../../src/app/ServiceTargetSupport.hpp"

namespace {

class RoutingBackend final : public fo4ide::plugin::IPluginBackend {
public:
    std::string invokedOperation;
    std::string invokedBackendMethod;
    nlohmann::json invokedBackendArguments;
    std::vector<std::string> backendMethods;
    std::vector<std::string> createAttempts;
    std::vector<std::string> discardAttempts;
    std::vector<std::string> loadedPlugins{"Patch.esp"};
    std::optional<fo4ide::Error> copyOverrideError;
    std::optional<fo4ide::Error> copyAsNewError;
    std::optional<fo4ide::Error> discardError;
    std::string copyOverrideResult{"Copied 000800:Base.esp as override in target."};
    std::string copyAsNewResult{"Copied 000800:Base.esp into target as NEW record 000800:target.esp."};
    std::string deepResult{"Deep-copied 1 of 1 record(s) rooted at 000800:Base.esp into target."};
    std::string remapResult{"Repointed 1 of 1 referencing record(s) from old to new in target."};
    std::optional<fo4ide::Error> copyAsWrapperError;
    std::string copyAsWrapperResult{"Copied 000804:Master.esm as wrapper in target; entries moved to NEW record 000805:target.esp ('W')."};
    mutable std::vector<std::pair<std::string, nlohmann::json>> typedCalls;

    fo4ide::plugin::Capabilities capabilities() const noexcept override {
        return {.available = true};
    }

    fo4ide::Result<nlohmann::json> getLeveledList(const nlohmann::json&) const override {
        return nlohmann::json("leveled");
    }

    fo4ide::Result<nlohmann::json> invoke(
        std::string_view operation,
        const nlohmann::json&) override {
        invokedOperation = operation;
        if (operation == "appInterop.GetPlugins") {
            return nlohmann::json(R"([{"Key":"Patch.esp"}])");
        }
        return nlohmann::json("invoke");
    }

    fo4ide::Result<nlohmann::json> invokeBackend(
        std::string_view method,
        const nlohmann::json& args) override {
        invokedBackendMethod = method;
        invokedBackendArguments = args;
        backendMethods.emplace_back(method);

        if (method == "SearchRecords") {
            const auto type = args.is_array() && args.size() > 1 && args[1].is_string()
                ? args[1].get<std::string>() : std::string{};
            if (type == "CELL") {
                return nlohmann::json(R"([{"FormKey":"000801:Patch.esp","EditorID":"CellOne","Type":"CELL","Plugin":"Patch.esp"},{"FormKey":"000802:Patch.esp","EditorID":"CellTwo","Type":"CELL","Plugin":"Patch.esp"}])");
            }
            if (type == "WRLD") {
                return nlohmann::json(R"([{"FormKey":"000900:Patch.esp","EditorID":"WorldOne","Type":"WRLD","Plugin":"Patch.esp"}])");
            }
            return nlohmann::json(nlohmann::json::array({
                {
                    {"FormKey", "000800:Patch.esp"},
                    {"EditorID", "MyRecord"},
                    {"Plugin", "Patch.esp"}
                }
            }).dump());
        }
        if (method == "GetRecordDetails") {
            const auto formKey = args.is_array() && !args.empty() && args[0].is_string()
                ? args[0].get<std::string>() : std::string{};
            return nlohmann::json(nlohmann::json{
                {"FormKey", formKey},
                {"EditorId", "MyRecord"},
                {"Signature", "MISC"},
                {"Plugin", "Patch.esp"}
            }.dump());
        }
        if (method == "GetConflictMatrix") {
            return nlohmann::json(nlohmann::json{
                {"Plugins", nlohmann::json::array({"Patch.esp"})},
                {"Rows", nlohmann::json::array({
                    {
                        {"Field", "Subrecords\\EDID[0]"},
                        {"DisplayLabel", "Editor ID"},
                        {"Values", nlohmann::json::array({"MyRecord"})},
                        {"EditKind", "Text"},
                        {"EnumOptions", nullptr},
                        {"IsSummary", false}
                    }
                })}
            }.dump());
        }
        if (method == "CopyAsOverride") {
            if (copyOverrideError) return std::unexpected(*copyOverrideError);
            return nlohmann::json(copyOverrideResult);
        }
        if (method == "CopyAsNewRecord") {
            if (copyAsNewError) return std::unexpected(*copyAsNewError);
            return nlohmann::json(copyAsNewResult);
        }
        return nlohmann::json("backend");
    }

    fo4ide::Result<nlohmann::json> createPlugin(
        const nlohmann::json& args) override {
        std::string name;
        if (args.is_array() && !args.empty() && args[0].is_string()) {
            name = args[0].get<std::string>();
        }
        createAttempts.push_back(name);
        if (std::find(loadedPlugins.begin(), loadedPlugins.end(), name) != loadedPlugins.end()) {
            return std::unexpected(fo4ide::Error{"PluginAlreadyLoaded", "already loaded"});
        }
        loadedPlugins.push_back(name);
        return nlohmann::json("created");
    }

    fo4ide::Result<void> discardCreatedPlugin(std::string_view name) override {
        discardAttempts.emplace_back(name);
        if (discardError) return std::unexpected(*discardError);
        const auto it = std::find(loadedPlugins.begin(), loadedPlugins.end(), name);
        if (it != loadedPlugins.end()) loadedPlugins.erase(it);
        return {};
    }

    fo4ide::Result<nlohmann::json> deepCopyAsOverride(
        const nlohmann::json& args) override {
        invokedBackendMethod = "DeepCopyAsOverride";
        invokedBackendArguments = args;
        backendMethods.emplace_back(invokedBackendMethod);
        return nlohmann::json(deepResult);
    }

    fo4ide::Result<nlohmann::json> changeReferencingRecords(
        const nlohmann::json& args) override {
        invokedBackendMethod = "ChangeReferencingRecords";
        invokedBackendArguments = args;
        backendMethods.emplace_back(invokedBackendMethod);
        return nlohmann::json(remapResult);
    }

    fo4ide::Result<nlohmann::json> copyAsWrapper(const nlohmann::json& args) override {
        invokedBackendMethod = "CopyAsWrapper";
        invokedBackendArguments = args;
        backendMethods.emplace_back("CopyAsWrapper");
        if (copyAsWrapperError) return std::unexpected(*copyAsWrapperError);
        return nlohmann::json(copyAsWrapperResult);
    }

    fo4ide::Result<nlohmann::json> createPlacedObject(const nlohmann::json& args) override {
        return recordTypedCall("CreatePlacedObject", args);
    }

    fo4ide::Result<nlohmann::json> createInteriorCell(const nlohmann::json& args) override {
        return recordTypedCall("CreateInteriorCell", args);
    }

    fo4ide::Result<nlohmann::json> setLeveledListSettings(const nlohmann::json& args) override {
        return recordTypedCall("SetLeveledListSettings", args);
    }

    fo4ide::Result<nlohmann::json> editLeveledListEntry(const nlohmann::json& args) override {
        return recordTypedCall("EditLeveledListEntry", args);
    }

    fo4ide::Result<nlohmann::json> getNpc(const nlohmann::json& args) const override {
        return recordTypedCall("GetNpc", args);
    }

    fo4ide::Result<nlohmann::json> editNpcEntries(const nlohmann::json& args) override {
        return recordTypedCall("EditNpcEntries", args);
    }

    fo4ide::Result<nlohmann::json> getFaction(const nlohmann::json& args) const override {
        return recordTypedCall("GetFaction", args);
    }

    fo4ide::Result<nlohmann::json> editFactionEntries(const nlohmann::json& args) override {
        return recordTypedCall("EditFactionEntries", args);
    }

    fo4ide::Result<nlohmann::json> addPackageProcedure(const nlohmann::json& args) override {
        return recordTypedCall("AddPackageProcedure", args);
    }

    fo4ide::Result<nlohmann::json> removePackageProcedure(const nlohmann::json& args) override {
        return recordTypedCall("RemovePackageProcedure", args);
    }

    fo4ide::Result<nlohmann::json> addSoundFile(const nlohmann::json& args) override {
        return recordTypedCall("AddSoundFile", args);
    }

    fo4ide::Result<nlohmann::json> removeSoundFile(const nlohmann::json& args) override {
        return recordTypedCall("RemoveSoundFile", args);
    }

    fo4ide::Result<nlohmann::json> setSoundBase(const nlohmann::json& args) override {
        return recordTypedCall("SetSoundBase", args);
    }

    fo4ide::Result<nlohmann::json> createSound(const nlohmann::json& args) override {
        return recordTypedCall("CreateSound", args);
    }

    fo4ide::Result<nlohmann::json> getWeaponDamageTypes(const nlohmann::json& args) const override {
        return recordTypedCall("GetWeaponDamageTypes", args);
    }

    fo4ide::Result<nlohmann::json> recordTypedCall(std::string_view name, const nlohmann::json& args) const {
        typedCalls.emplace_back(std::string(name), args);
        return nlohmann::json(std::string(name));
    }

    fo4ide::Result<nlohmann::json> conditionFunctionParams() const override {
        return nlohmann::json(R"({"GetDistance":[{"label":"Reference","kind":"record","types":"PlacedObject,PlacedNpc"}]})");
    }
};

nlohmann::json parsed(const fo4ide::Result<nlohmann::json>& result) {
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    return nlohmann::json::parse(result->get<std::string>());
}

}

TEST_CASE("PluginBackendTarget routes backend namespace through invokeBackend") {
    RoutingBackend backend;
    fo4ide::app::PluginBackendTarget target(backend, "backend");

    const auto result = target.invoke("CreateRecord", nlohmann::json::array());
    REQUIRE(result.has_value());
    CHECK(*result == "backend");
    CHECK(backend.invokedBackendMethod == "CreateRecord");
    CHECK(backend.invokedOperation.empty());
}

TEST_CASE("PluginBackendTarget preserves namespaced invoke for non-backend targets") {
    RoutingBackend backend;
    fo4ide::app::PluginBackendTarget target(backend, "masters");

    const auto result = target.invoke("List", nlohmann::json::array());
    REQUIRE(result.has_value());
    CHECK(*result == "invoke");
    CHECK(backend.invokedOperation == "masters.List");
    CHECK(backend.invokedBackendMethod.empty());
}

TEST_CASE("Legacy SavePatch maps to native SavePlugin in-place") {
    RoutingBackend backend;
    fo4ide::app::PluginBackendTarget target(backend, "backend");

    const auto result = target.invoke("SavePatch", nlohmann::json::array({"Patch.esp"}));
    REQUIRE(result.has_value());
    CHECK(backend.invokedBackendMethod == "SavePlugin");
    REQUIRE(backend.invokedBackendArguments.size() == 2);
    CHECK(backend.invokedBackendArguments[0] == "Patch.esp");
    CHECK(backend.invokedBackendArguments[1] == "");
}

TEST_CASE("Legacy RenumberFormId resolves EditorID and expands the local hex id") {
    RoutingBackend backend;
    fo4ide::app::PluginBackendTarget target(backend, "backend");

    const auto result = target.invoke(
        "RenumberFormId", nlohmann::json::array({"Patch.esp", "MyRecord", "0xF99"}));
    REQUIRE(result.has_value());
    REQUIRE(backend.backendMethods.size() == 2);
    CHECK(backend.backendMethods[0] == "SearchRecords");
    CHECK(backend.backendMethods[1] == "RenumberRecord");
    REQUIRE(backend.invokedBackendArguments.size() == 3);
    CHECK(backend.invokedBackendArguments[0] == "Patch.esp");
    CHECK(backend.invokedBackendArguments[1] == "000800:Patch.esp");
    CHECK(backend.invokedBackendArguments[2] == "000F99:Patch.esp");
}

TEST_CASE("Legacy ResolveConflict maps to copy-as-override without forced overwrite") {
    RoutingBackend backend;
    fo4ide::app::PluginBackendTarget target(backend, "backend");

    const auto result = target.invoke(
        "ResolveConflict",
        nlohmann::json::array({"000800:Base.esp", "Winner.esp", "Patch.esp"}));
    REQUIRE(result.has_value());
    REQUIRE(backend.createAttempts.size() == 1);
    CHECK(backend.createAttempts[0] == "Patch.esp");
    CHECK(backend.discardAttempts.empty());
    CHECK(backend.invokedBackendMethod == "CopyAsOverride");
    REQUIRE(backend.invokedBackendArguments.size() == 4);
    CHECK(backend.invokedBackendArguments[0] == "Winner.esp");
    CHECK(backend.invokedBackendArguments[1] == "000800:Base.esp");
    CHECK(backend.invokedBackendArguments[2] == "Patch.esp");
    CHECK(backend.invokedBackendArguments[3] == false);
}

TEST_CASE("Legacy ResolveConflict forwards an explicit allow-losing opt-in") {
    RoutingBackend backend;
    fo4ide::app::PluginBackendTarget target(backend, "backend");

    const auto result = target.invoke(
        "ResolveConflict",
        nlohmann::json::array({"000800:Base.esp", "Winner.esp", "Patch.esp", true}));
    REQUIRE(result.has_value());
    REQUIRE(backend.invokedBackendArguments.size() == 5);
    CHECK(backend.invokedBackendArguments[3] == false);
    CHECK(backend.invokedBackendArguments[4] == true);
}

TEST_CASE("Native copy workflows create a missing target before mutation") {
    RoutingBackend backend;
    fo4ide::app::PluginBackendTarget target(backend, "backend");

    auto result = target.invoke(
        "CopyAsOverride",
        nlohmann::json::array({"Winner.esp", "000800:Base.esp", "NewPatch.esp", false}));
    REQUIRE(result.has_value());
    REQUIRE(backend.createAttempts.size() == 1);
    CHECK(backend.createAttempts[0] == "NewPatch.esp");
    CHECK(backend.invokedBackendMethod == "CopyAsOverride");
    CHECK(backend.discardAttempts.empty());

    result = target.invoke(
        "CopyAsNewRecord",
        nlohmann::json::array({"Winner.esp", "000800:Base.esp", "DuplicatePatch.esp", "NewRecord"}));
    REQUIRE(result.has_value());
    REQUIRE(backend.createAttempts.size() == 2);
    CHECK(backend.createAttempts[1] == "DuplicatePatch.esp");
    CHECK(backend.invokedBackendMethod == "CopyAsNewRecord");
    CHECK(backend.discardAttempts.empty());
}

TEST_CASE("Native write targets normalize a missing plugin extension") {
    RoutingBackend backend;
    fo4ide::app::PluginBackendTarget target(backend, "backend");

    const auto result = target.invoke(
        "CopyAsOverride",
        nlohmann::json::array({"Winner.esp", "000800:Base.esp", "ExtensionlessPatch", false}));
    REQUIRE(result.has_value());
    REQUIRE(backend.createAttempts.size() == 1);
    CHECK(backend.createAttempts[0] == "ExtensionlessPatch.esp");
    REQUIRE(backend.invokedBackendArguments.size() >= 3);
    CHECK(backend.invokedBackendArguments[2] == "ExtensionlessPatch.esp");
}

TEST_CASE("Deep copy and reference remap create targets only on apply") {
    RoutingBackend backend;
    fo4ide::app::PluginBackendTarget target(backend, "backend");

    auto result = target.invoke(
        "DeepCopyAsOverride",
        nlohmann::json::array({"Winner.esp", "000800:Base.esp", "DeepPatch.esp", false, false, 200}));
    REQUIRE(result.has_value());
    CHECK(backend.createAttempts.empty());
    CHECK(backend.invokedBackendMethod == "DeepCopyAsOverride");

    result = target.invoke(
        "DeepCopyAsOverride",
        nlohmann::json::array({"Winner.esp", "000800:Base.esp", "DeepPatch.esp", true, false, 200}));
    REQUIRE(result.has_value());
    REQUIRE(backend.createAttempts.size() == 1);
    CHECK(backend.createAttempts[0] == "DeepPatch.esp");
    CHECK(backend.discardAttempts.empty());

    result = target.invoke(
        "ChangeReferencingRecords",
        nlohmann::json::array({"000800:Base.esp", "000801:Base.esp", "ReferencePatch.esp", false, 300}));
    REQUIRE(result.has_value());
    REQUIRE(backend.createAttempts.size() == 1);
    CHECK(backend.invokedBackendMethod == "ChangeReferencingRecords");

    result = target.invoke(
        "ChangeReferencingRecords",
        nlohmann::json::array({"000800:Base.esp", "000801:Base.esp", "ReferencePatch.esp", true, 300}));
    REQUIRE(result.has_value());
    REQUIRE(backend.createAttempts.size() == 2);
    CHECK(backend.createAttempts[1] == "ReferencePatch.esp");
    CHECK(backend.discardAttempts.empty());
}

TEST_CASE("Failed mutation removes a newly auto-created target") {
    RoutingBackend backend;
    backend.copyOverrideError = fo4ide::Error{"CopyFailed", "source record missing"};
    fo4ide::app::PluginBackendTarget target(backend, "backend");

    const auto result = target.invoke(
        "CopyAsOverride",
        nlohmann::json::array({"Winner.esp", "000800:Base.esp", "FailedPatch.esp", false}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "CopyFailed");
    REQUIRE(backend.discardAttempts.size() == 1);
    CHECK(backend.discardAttempts[0] == "FailedPatch.esp");
    CHECK(std::find(backend.loadedPlugins.begin(), backend.loadedPlugins.end(), "FailedPatch.esp") ==
          backend.loadedPlugins.end());
}

TEST_CASE("Semantic no-op removes newly auto-created deep-copy and remap targets") {
    RoutingBackend backend;
    fo4ide::app::PluginBackendTarget target(backend, "backend");

    backend.deepResult = "Deep copy is blocked by nested record 000801:Base.esp.";
    auto result = target.invoke(
        "DeepCopyAsOverride",
        nlohmann::json::array({"Winner.esp", "000800:Base.esp", "BlockedDeep.esp", true, false, 200}));
    REQUIRE(result.has_value());
    CHECK(result->get<std::string>().starts_with("Deep copy is blocked"));
    REQUIRE(backend.discardAttempts.size() == 1);
    CHECK(backend.discardAttempts[0] == "BlockedDeep.esp");

    backend.remapResult = "Nothing references 000800:Base.esp; nothing to change.";
    result = target.invoke(
        "ChangeReferencingRecords",
        nlohmann::json::array({"000800:Base.esp", "000801:Base.esp", "EmptyRemap.esp", true, 300}));
    REQUIRE(result.has_value());
    CHECK(result->get<std::string>().starts_with("Nothing references"));
    REQUIRE(backend.discardAttempts.size() == 2);
    CHECK(backend.discardAttempts[1] == "EmptyRemap.esp");
}

TEST_CASE("Existing targets are never discarded when their mutation fails") {
    RoutingBackend backend;
    backend.copyOverrideError = fo4ide::Error{"CopyFailed", "source record missing"};
    fo4ide::app::PluginBackendTarget target(backend, "backend");

    const auto result = target.invoke(
        "CopyAsOverride",
        nlohmann::json::array({"Winner.esp", "000800:Base.esp", "Patch.esp", false}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "CopyFailed");
    CHECK(backend.discardAttempts.empty());
}

TEST_CASE("Rollback failure is surfaced instead of hiding an orphan target") {
    RoutingBackend backend;
    backend.copyOverrideError = fo4ide::Error{"CopyFailed", "source record missing"};
    backend.discardError = fo4ide::Error{"DiscardFailed", "filesystem refused deletion"};
    fo4ide::app::PluginBackendTarget target(backend, "backend");

    const auto result = target.invoke(
        "CopyAsOverride",
        nlohmann::json::array({"Winner.esp", "000800:Base.esp", "Orphan.esp", false}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "TargetRollbackFailed");
    CHECK(result.error().message.find("CopyFailed") != std::string::npos);
    CHECK(result.error().message.find("filesystem refused deletion") != std::string::npos);
}

TEST_CASE("Legacy FormKey label resolution uses native record details") {
    RoutingBackend backend;
    fo4ide::app::PluginBackendTarget target(backend, "backend");

    const auto labels = parsed(target.invoke(
        "ResolveFormKeyLabels",
        nlohmann::json::array({"000800:Patch.esp, 000801:Patch.esp"})));
    CHECK(labels.at("000800:Patch.esp") == "MyRecord [000800:Patch.esp]");
    CHECK(labels.at("000801:Patch.esp") == "MyRecord [000801:Patch.esp]");
}

TEST_CASE("Legacy GetRecordTree projects the native conflict matrix into field nodes") {
    RoutingBackend backend;
    fo4ide::app::PluginBackendTarget target(backend, "backend");

    const auto tree = parsed(target.invoke(
        "GetRecordTree", nlohmann::json::array({"Patch.esp", "MyRecord"})));
    REQUIRE(tree.size() == 1);
    CHECK(tree[0].at("key") == "Subrecords\\EDID[0]");
    CHECK(tree[0].at("label") == "Editor ID");
    CHECK(tree[0].at("value") == "MyRecord");
    CHECK(tree[0].at("editKind") == "Text");
    CHECK(tree[0].at("hasChildren") == false);
}

TEST_CASE("Legacy condition editor gets parameter metadata from the backend capability") {
    RoutingBackend backend;
    fo4ide::app::PluginBackendTarget target(backend, "backend");

    const auto params = parsed(target.invoke("GetConditionFunctionParams", nlohmann::json::array()));
    REQUIRE(params.contains("GetDistance"));
    CHECK(params.at("GetDistance")[0].at("kind") == "record");
}

TEST_CASE("Legacy root condition paths route to native CTDA operations") {
    RoutingBackend backend;
    fo4ide::app::PluginBackendTarget target(backend, "backend");

    auto result = target.invoke(
        "GetConditionsAt",
        nlohmann::json::array({"Patch.esp", "000800:Patch.esp", "Conditions"}));
    REQUIRE(result.has_value());
    CHECK(backend.invokedBackendMethod == "GetConditions");
    REQUIRE(backend.invokedBackendArguments.size() == 2);
    CHECK(backend.invokedBackendArguments[0] == "Patch.esp");
    CHECK(backend.invokedBackendArguments[1] == "000800:Patch.esp");

    result = target.invoke(
        "SetConditionsAt",
        nlohmann::json::array({"Patch.esp", "000800:Patch.esp", "", "[]"}));
    REQUIRE(result.has_value());
    CHECK(backend.invokedBackendMethod == "SetConditions");
    REQUIRE(backend.invokedBackendArguments.size() == 3);
    CHECK(backend.invokedBackendArguments[0] == "Patch.esp");
    CHECK(backend.invokedBackendArguments[1] == "000800:Patch.esp");
    CHECK(backend.invokedBackendArguments[2] == "[]");
}

TEST_CASE("Nested condition paths route to the native scoped condition operations") {
    RoutingBackend backend;
    fo4ide::app::PluginBackendTarget target(backend, "backend");

    auto result = target.invoke(
        "GetConditionsAt",
        nlohmann::json::array({"Patch.esp", "000800:Patch.esp", "Effects[2].Conditions"}));
    REQUIRE(result.has_value());
    CHECK(backend.invokedBackendMethod == "GetConditionsAtPath");
    REQUIRE(backend.invokedBackendArguments.size() == 3);
    CHECK(backend.invokedBackendArguments[0] == "Patch.esp");
    CHECK(backend.invokedBackendArguments[1] == "000800:Patch.esp");
    CHECK(backend.invokedBackendArguments[2] == "Effects[2].Conditions");

    result = target.invoke(
        "SetConditionsAt",
        nlohmann::json::array({"Patch.esp", "000800:Patch.esp", "Effects[1].Conditions", "[]"}));
    REQUIRE(result.has_value());
    CHECK(backend.invokedBackendMethod == "SetConditionsAtPath");
    REQUIRE(backend.invokedBackendArguments.size() == 4);
    CHECK(backend.invokedBackendArguments[0] == "Patch.esp");
    CHECK(backend.invokedBackendArguments[1] == "000800:Patch.esp");
    CHECK(backend.invokedBackendArguments[2] == "Effects[1].Conditions");
    CHECK(backend.invokedBackendArguments[3] == "[]");
}

TEST_CASE("Cell search routes through the native record index and honors its limit") {
    RoutingBackend backend;
    fo4ide::app::PluginBackendTarget target(backend, "cell");

    const auto cells = parsed(target.invoke("SearchCells", nlohmann::json::array({"cell", 1})));
    REQUIRE(cells.size() == 1);
    CHECK(cells[0].at("Type") == "CELL");
    CHECK(backend.invokedBackendMethod == "SearchRecords");
    CHECK(backend.invokedBackendArguments[1] == "CELL");

    const auto worlds = parsed(target.invoke("SearchWorldspaces", nlohmann::json::array({"world", 100})));
    REQUIRE(worlds.size() == 1);
    CHECK(worlds[0].at("Type") == "WRLD");
    CHECK(backend.invokedBackendArguments[1] == "WRLD");
}

TEST_CASE("Cell GetPlugins reuses the native environment plugin list") {
    RoutingBackend backend;
    fo4ide::app::PluginBackendTarget target(backend, "cell");

    const auto plugins = parsed(target.invoke("GetPlugins", nlohmann::json::array()));
    REQUIRE(plugins.size() == 1);
    CHECK(plugins[0].at("Key") == "Patch.esp");
    CHECK(backend.invokedOperation == "appInterop.GetPlugins");
}

TEST_CASE("PluginBackendTarget routes GetLeveledList to the native backend") {
    RoutingBackend backend;
    fo4ide::app::PluginBackendTarget target(backend, "backend");

    const auto result = target.invoke("GetLeveledList", nlohmann::json::array({"000800:Patch.esp"}));
    REQUIRE(result.has_value());
    CHECK(*result == "leveled");
}

TEST_CASE("JSON int helpers refuse or saturate values outside int") {
    using fo4ide::core::jsonInt;
    using fo4ide::core::jsonIntSaturated;
    CHECK(jsonInt(nlohmann::json(-2147483648LL)) == -2147483647 - 1);
    CHECK_FALSE(jsonInt(nlohmann::json(4294967263ULL)).has_value());
    CHECK_FALSE(jsonInt(nlohmann::json(-2147483649LL)).has_value());
    CHECK_FALSE(jsonInt(nlohmann::json(1.0)).has_value());
    CHECK_FALSE(jsonInt(nlohmann::json(true)).has_value());
    CHECK(jsonIntSaturated(nlohmann::json(18446744073709551615ULL)) == 2147483647);
    CHECK(jsonIntSaturated(nlohmann::json(-9223372036854775807LL - 1)) == -2147483647 - 1);
    CHECK(jsonIntSaturated(nlohmann::json(42)) == 42);
}

TEST_CASE("Service number arguments truncate in-range floats and refuse the rest") {
    using fo4ide::app::support::intNumberArgument;
    const auto args = nlohmann::json::array({12.9, 1e20, -1e20, 7, 4294967297ULL, "3"});
    CHECK(intNumberArgument(args, 0, -1) == 12);
    CHECK(intNumberArgument(args, 1, -1) == -1);
    CHECK(intNumberArgument(args, 2, -1) == -1);
    CHECK(intNumberArgument(args, 3, -1) == 7);
    CHECK(intNumberArgument(args, 4, -1) == -1);
    CHECK(intNumberArgument(args, 5, -1) == -1);
    CHECK(intNumberArgument(nlohmann::json::array(), 0, -1) == -1);
    CHECK(intNumberArgument(args, 1, -1, true) == 2147483647);
    CHECK(intNumberArgument(args, 2, -1, true) == -2147483647 - 1);
    CHECK(intNumberArgument(args, 4, -1, true) == 2147483647);
    CHECK(intNumberArgument(args, 0, -1, true) == 12);
    CHECK(intNumberArgument(nlohmann::json::array({-2147483648.5}), 0, -1) == -2147483647 - 1);
    CHECK(intNumberArgument(nlohmann::json::array({2147483647.5}), 0, -1) == 2147483647);
    CHECK(intNumberArgument(nlohmann::json::array({2147483648.0}), 0, -1) == -1);
}

TEST_CASE("CopyAsWrapper creates a missing target before mutation and normalises the extension") {
    RoutingBackend backend;
    fo4ide::app::PluginBackendTarget target(backend, "backend");

    const auto result = target.invoke(
        "CopyAsWrapper",
        nlohmann::json::array({"Master.esm", "000804:Master.esm", "NewWrap", "W"}));
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    CHECK(result->get<std::string>().starts_with("Copied "));
    REQUIRE(backend.createAttempts.size() == 1);
    CHECK(backend.createAttempts[0] == "NewWrap.esp");
    CHECK(backend.invokedBackendMethod == "CopyAsWrapper");
    REQUIRE(backend.invokedBackendArguments.size() >= 3);
    CHECK(backend.invokedBackendArguments[2] == "NewWrap.esp");
    CHECK(backend.discardAttempts.empty());
}

TEST_CASE("CopyAsWrapper removes a newly created target on a non-success result or error and keeps an existing target") {
    RoutingBackend backend;
    fo4ide::app::PluginBackendTarget target(backend, "backend");

    backend.copyAsWrapperResult = "LOSES: Target.esp loads before Later.esp, which also override 000804:Master.esm.";
    auto result = target.invoke(
        "CopyAsWrapper",
        nlohmann::json::array({"Master.esm", "000804:Master.esm", "FreshWrap.esp", "W"}));
    REQUIRE(result.has_value());
    CHECK(*result == backend.copyAsWrapperResult);
    REQUIRE(backend.discardAttempts.size() == 1);
    CHECK(backend.discardAttempts[0] == "FreshWrap.esp");

    backend.copyAsWrapperError = fo4ide::Error{"WrapperUnsupportedType", "no"};
    result = target.invoke(
        "CopyAsWrapper",
        nlohmann::json::array({"Master.esm", "000804:Master.esm", "FreshWrap2.esp", "W"}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "WrapperUnsupportedType");
    REQUIRE(backend.discardAttempts.size() == 2);
    CHECK(backend.discardAttempts[1] == "FreshWrap2.esp");

    result = target.invoke(
        "CopyAsWrapper",
        nlohmann::json::array({"Master.esm", "000804:Master.esm", "Patch.esp", "W"}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "WrapperUnsupportedType");
    CHECK(backend.discardAttempts.size() == 2);
}

TEST_CASE("PluginBackendTarget routes the typed record methods and the strings namespace to the backend") {
    RoutingBackend backend;
    fo4ide::app::PluginBackendTarget target(backend, "backend");
    const auto args = nlohmann::json::array({"Patch.esp", "000800:Patch.esp"});
    const std::vector<std::string> methods{
        "SetLeveledListSettings", "EditLeveledListEntry", "GetNpc", "EditNpcEntries", "GetFaction",
        "EditFactionEntries", "GetWeaponDamageTypes", "CreatePlacedObject", "CreateInteriorCell",
        "AddSoundFile", "RemoveSoundFile", "SetSoundBase", "CreateSound",
        "AddPackageProcedure", "RemovePackageProcedure"};
    for (const auto& method : methods) {
        const auto result = target.invoke(method, args);
        REQUIRE(result.has_value());
        CHECK(*result == method);
        REQUIRE_FALSE(backend.typedCalls.empty());
        CHECK(backend.typedCalls.back().first == method);
        CHECK(backend.typedCalls.back().second == args);
    }
    CHECK(backend.invokedBackendMethod.empty());

    fo4ide::app::PluginBackendTarget strings(backend, "strings");
    const auto text = strings.invoke("GetStrings", nlohmann::json::array({"Patch.esp", "", "STRINGS", "", 0, 200}));
    REQUIRE(text.has_value());
    CHECK(backend.invokedOperation == "strings.GetStrings");
}

TEST_CASE("IPluginBackend defaults report distinct unavailable errors for the typed record methods") {
    fo4ide::plugin::NullPluginBackend backend;
    fo4ide::plugin::IPluginBackend& base = backend;
    const auto args = nlohmann::json::array();
    const auto unavailable = [](const fo4ide::Result<nlohmann::json>& result, std::string_view type) {
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().type == type);
    };

    unavailable(base.copyAsWrapper(args), "CopyAsWrapperUnavailable");
    unavailable(base.createPlacedObject(args), "CellBackendUnavailable");
    unavailable(base.createInteriorCell(args), "CellBackendUnavailable");
    unavailable(base.addSoundFile(args), "SoundBackendUnavailable");
    unavailable(base.addPackageProcedure(args), "PackageBackendUnavailable");
    unavailable(base.removePackageProcedure(args), "PackageBackendUnavailable");
    unavailable(base.removeSoundFile(args), "SoundBackendUnavailable");
    unavailable(base.setSoundBase(args), "SoundBackendUnavailable");
    unavailable(base.createSound(args), "SoundBackendUnavailable");
    unavailable(base.getLeveledList(args), "LeveledListBackendUnavailable");
    unavailable(base.setLeveledListSettings(args), "LeveledListBackendUnavailable");
    unavailable(base.editLeveledListEntry(args), "LeveledListBackendUnavailable");
    unavailable(base.getNpc(args), "NpcBackendUnavailable");
    unavailable(base.editNpcEntries(args), "NpcBackendUnavailable");
    unavailable(base.getFaction(args), "FactionBackendUnavailable");
    unavailable(base.editFactionEntries(args), "FactionBackendUnavailable");
    unavailable(base.getWeaponDamageTypes(args), "WeaponBackendUnavailable");
}
