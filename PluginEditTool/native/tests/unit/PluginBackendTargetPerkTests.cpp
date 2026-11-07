#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/app/PluginBackendTarget.hpp>
#include <fo4ide/plugin/IPluginBackend.hpp>

namespace {

class PerkRoutingBackend final : public fo4ide::plugin::IPluginBackend {
public:
    std::string invokedOperation;
    mutable std::vector<std::pair<std::string, nlohmann::json>> typedCalls;

    fo4ide::plugin::Capabilities capabilities() const noexcept override {
        return {.available = true};
    }

    fo4ide::Result<nlohmann::json> invoke(std::string_view operation, const nlohmann::json&) override {
        invokedOperation = operation;
        return nlohmann::json("invoke");
    }

    fo4ide::Result<nlohmann::json> getPerkLinks(const nlohmann::json& args) const override {
        return recordTypedCall("GetPerkLinks", args);
    }

    fo4ide::Result<nlohmann::json> setPerkNext(const nlohmann::json& args) override {
        return recordTypedCall("SetPerkNext", args);
    }

    fo4ide::Result<nlohmann::json> setPerkSound(const nlohmann::json& args) override {
        return recordTypedCall("SetPerkSound", args);
    }

    fo4ide::Result<nlohmann::json> setPerkSwf(const nlohmann::json& args) override {
        return recordTypedCall("SetPerkSwf", args);
    }

private:
    fo4ide::Result<nlohmann::json> recordTypedCall(std::string_view name, const nlohmann::json& args) const {
        typedCalls.emplace_back(std::string(name), args);
        return nlohmann::json(std::string(name));
    }
};

class BarePerkBackend final : public fo4ide::plugin::IPluginBackend {
public:
    fo4ide::plugin::Capabilities capabilities() const noexcept override {
        return {.available = true};
    }

    fo4ide::Result<nlohmann::json> invoke(std::string_view, const nlohmann::json&) override {
        return nlohmann::json("invoke");
    }
};

}  // namespace

// Detects: PluginBackendTarget must route GetPerkLinks, SetPerkNext, SetPerkSound and SetPerkSwf to their typed backend methods with the arguments unchanged, and must not fall through to invoke.
TEST_CASE("PluginBackendTarget routes PERK link reads and writes to their typed backend methods") {
    PerkRoutingBackend backend;
    fo4ide::app::PluginBackendTarget target(backend, "backend");
    const auto args = nlohmann::json::array({"Patch.esp", "000800:Patch.esp", "000B00:Fallout4.esm"});

    for (const auto* method : {"GetPerkLinks", "SetPerkNext", "SetPerkSound", "SetPerkSwf"}) {
        const auto result = target.invoke(method, args);
        REQUIRE(result.has_value());
        CHECK(*result == method);
        REQUIRE_FALSE(backend.typedCalls.empty());
        CHECK(backend.typedCalls.back().first == method);
        CHECK(backend.typedCalls.back().second == args);
    }
    CHECK(backend.typedCalls.size() == 4u);
    CHECK(backend.invokedOperation.empty());
}

// Detects: PluginBackendTarget must pass an empty PERK sound or swf argument through as written, so the backend can read it as a removal.
TEST_CASE("PluginBackendTarget forwards an empty PERK sound or swf unchanged") {
    PerkRoutingBackend backend;
    fo4ide::app::PluginBackendTarget target(backend, "backend");

    for (const auto* method : {"SetPerkSound", "SetPerkSwf"}) {
        const auto args = nlohmann::json::array({"Patch.esp", "000800:Patch.esp", ""});
        REQUIRE(target.invoke(method, args).has_value());
        REQUIRE_FALSE(backend.typedCalls.empty());
        CHECK(backend.typedCalls.back().second == args);
        CHECK(backend.typedCalls.back().second[2] == "");
    }
}

// Detects: IPluginBackend defaults must refuse PERK link reads and the sound, swf and next writes as PerkBackendUnavailable.
TEST_CASE("IPluginBackend defaults refuse PERK link reads and writes as unavailable") {
    BarePerkBackend backend;
    fo4ide::plugin::IPluginBackend& base = backend;
    const auto args = nlohmann::json::array({"Patch.esp", "000800:Patch.esp"});

    const auto unavailable = [](const fo4ide::Result<nlohmann::json>& result) {
        REQUIRE_FALSE(result.has_value());
        return result.error().type;
    };
    CHECK(unavailable(base.getPerkLinks(args)) == "PerkBackendUnavailable");
    CHECK(unavailable(base.setPerkNext(args)) == "PerkBackendUnavailable");
    CHECK(unavailable(base.setPerkSound(args)) == "PerkBackendUnavailable");
    CHECK(unavailable(base.setPerkSwf(args)) == "PerkBackendUnavailable");
}
