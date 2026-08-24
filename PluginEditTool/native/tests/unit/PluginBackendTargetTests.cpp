#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/app/PluginBackendTarget.hpp>
#include <fo4ide/plugin/IPluginBackend.hpp>

namespace {

class RoutingBackend final : public fo4ide::plugin::IPluginBackend {
public:
    std::string invokedOperation;
    std::string invokedBackendMethod;
    nlohmann::json invokedBackendArguments;
    std::vector<std::string> backendMethods;

    fo4ide::plugin::Capabilities capabilities() const noexcept override {
        return {.available = true};
    }

    fo4ide::Result<nlohmann::json> invoke(
        std::string_view operation,
        const nlohmann::json&) override {
        invokedOperation = operation;
        return nlohmann::json("invoke");
    }

    fo4ide::Result<nlohmann::json> invokeBackend(
        std::string_view method,
        const nlohmann::json& args) override {
        invokedBackendMethod = method;
        invokedBackendArguments = args;
        backendMethods.emplace_back(method);

        if (method == "SearchRecords") {
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
        return nlohmann::json("backend");
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
    CHECK(backend.invokedBackendMethod == "CopyAsOverride");
    REQUIRE(backend.invokedBackendArguments.size() == 4);
    CHECK(backend.invokedBackendArguments[0] == "Winner.esp");
    CHECK(backend.invokedBackendArguments[1] == "000800:Base.esp");
    CHECK(backend.invokedBackendArguments[2] == "Patch.esp");
    CHECK(backend.invokedBackendArguments[3] == false);
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
