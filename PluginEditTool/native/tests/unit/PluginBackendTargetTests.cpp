#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/app/PluginBackendTarget.hpp>
#include <fo4ide/plugin/IPluginBackend.hpp>

namespace {

class RoutingBackend final : public fo4ide::plugin::IPluginBackend {
public:
    std::string invokedOperation;
    std::string invokedBackendMethod;

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
        const nlohmann::json&) override {
        invokedBackendMethod = method;
        return nlohmann::json("backend");
    }
};

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
