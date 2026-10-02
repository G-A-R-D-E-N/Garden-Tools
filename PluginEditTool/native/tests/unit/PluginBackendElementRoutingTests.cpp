#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/app/PluginBackendTarget.hpp>
#include <fo4ide/plugin/IPluginBackend.hpp>

namespace {

class ElementRoutingBackend final : public fo4ide::plugin::IPluginBackend {
public:
    std::string routed;

    fo4ide::plugin::Capabilities capabilities() const noexcept override {
        return {.available = true};
    }

    fo4ide::Result<nlohmann::json> invoke(
        std::string_view operation,
        const nlohmann::json&) override {
        routed = std::string(operation);
        return nlohmann::json("fallback");
    }

    fo4ide::Result<nlohmann::json> describeElement(const nlohmann::json&) const override {
        const_cast<ElementRoutingBackend*>(this)->routed = "DescribeElement";
        return nlohmann::json(R"({"count":2})");
    }

    fo4ide::Result<nlohmann::json> addElement(const nlohmann::json&) override {
        routed = "AddElement";
        return nlohmann::json("added");
    }

    fo4ide::Result<nlohmann::json> removeElement(const nlohmann::json&) override {
        routed = "RemoveElement";
        return nlohmann::json("removed");
    }

    fo4ide::Result<nlohmann::json> moveElement(const nlohmann::json&) override {
        routed = "MoveElement";
        return nlohmann::json("moved");
    }

    fo4ide::Result<nlohmann::json> clearElement(const nlohmann::json&) override {
        routed = "ClearElement";
        return nlohmann::json("cleared");
    }

    fo4ide::Result<nlohmann::json> cleanUnusedMasters(const nlohmann::json& args) override {
        routed = "CleanUnusedMasters:" + args.dump();
        return nlohmann::json("cleaned");
    }
};

}  // namespace

TEST_CASE("PluginBackendTarget routes legacy ElementService calls to native element seams") {
    ElementRoutingBackend backend;
    fo4ide::app::PluginBackendTarget target(backend, "backend");

    REQUIRE(target.invoke("DescribeElement", nlohmann::json::array()).has_value());
    CHECK(backend.routed == "DescribeElement");
    REQUIRE(target.invoke("AddElement", nlohmann::json::array()).has_value());
    CHECK(backend.routed == "AddElement");
    REQUIRE(target.invoke("RemoveElement", nlohmann::json::array()).has_value());
    CHECK(backend.routed == "RemoveElement");
    REQUIRE(target.invoke("MoveElement", nlohmann::json::array()).has_value());
    CHECK(backend.routed == "MoveElement");
    REQUIRE(target.invoke("ClearElement", nlohmann::json::array()).has_value());
    CHECK(backend.routed == "ClearElement");
}

TEST_CASE("PluginBackendTarget routes the masters panel Clean Masters call") {
    ElementRoutingBackend backend;
    fo4ide::app::PluginBackendTarget target(backend, "masters");

    REQUIRE(target.invoke("CleanUnused", nlohmann::json::array({"Mod.esp", false})).has_value());
    CHECK(backend.routed == R"(CleanUnusedMasters:["Mod.esp",false])");
    REQUIRE(target.invoke("List", nlohmann::json::array({"Mod.esp"})).has_value());
    CHECK(backend.routed == "masters.List");
}
