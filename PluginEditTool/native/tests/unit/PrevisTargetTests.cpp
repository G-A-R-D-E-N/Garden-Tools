#include <filesystem>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/plugin/IPluginBackend.hpp>
#include <fo4ide/rpc/PrevisTarget.hpp>
#include <fo4ide/settings/Settings.hpp>

namespace {

class FakeBackend final : public fo4ide::plugin::IPluginBackend {
public:
    fo4ide::plugin::Capabilities capabilities() const noexcept override {
        return {.available = true, .readPlugins = true};
    }

    fo4ide::Result<nlohmann::json> invoke(std::string_view, const nlohmann::json&) override {
        return nlohmann::json::array();
    }

    std::filesystem::path dataDirectory() const override { return dataDirectory_; }

    std::filesystem::path dataDirectory_;
};

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        path_ = std::filesystem::temp_directory_path() / "fo4ide_previs_target_test";
        std::error_code error;
        std::filesystem::remove_all(path_, error);
        std::filesystem::create_directories(path_, error);
    }
    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }
    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

}

TEST_CASE("previs target refuses a scan without a Data folder") {
    FakeBackend backend;
    fo4ide::settings::SharedSettings settings;
    fo4ide::rpc::PrevisTarget target(backend, settings);

    const auto result = target.invoke("Scan", nlohmann::json::array());
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "PrevisDataDir");
}

TEST_CASE("previs target reports the configured Data folder") {
    FakeBackend backend;
    fo4ide::settings::SharedSettings settings;
    backend.dataDirectory_ = std::filesystem::path("D:/Fallout 4/Data");
    fo4ide::rpc::PrevisTarget target(backend, settings);

    const auto result = target.invoke("GetDataFolder", nlohmann::json::array());
    REQUIRE(result.has_value());
    CHECK((*result)["dataDir"] == "D:/Fallout 4/Data");
}

TEST_CASE("previs target refuses an unreadable Data folder") {
    FakeBackend backend;
    fo4ide::settings::SharedSettings settings;
    fo4ide::rpc::PrevisTarget target(backend, settings);

    const auto result = target.invoke("Scan", nlohmann::json::array({"Z:/no/such/folder"}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "PrevisDataDir");
}

TEST_CASE("previs target reports a missing load order") {
    TemporaryDirectory directory;
    FakeBackend backend;
    fo4ide::settings::SharedSettings settings;
    fo4ide::rpc::PrevisTarget target(backend, settings);

    const auto result = target.invoke("Scan", nlohmann::json::array({directory.path().string()}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "PrevisNoEnvironment");
}

TEST_CASE("previs target rejects unknown methods") {
    FakeBackend backend;
    fo4ide::settings::SharedSettings settings;
    fo4ide::rpc::PrevisTarget target(backend, settings);

    const auto result = target.invoke("Nope", nlohmann::json::array());
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "UnknownMethod");
}
