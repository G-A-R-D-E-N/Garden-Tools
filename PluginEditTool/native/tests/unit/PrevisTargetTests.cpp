#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

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
        // Reentrancy probe: a second Scan from inside the first must be refused, not queued.
        if (target != nullptr && !probed) {
            probed = true;
            const auto nested = target->invoke("Scan", nlohmann::json::array());
            busy_seen = !nested && nested.error().type == "PrevisScanBusy";
        }
        return plugins_;
    }

    std::filesystem::path dataDirectory() const override { return dataDirectory_; }

    nlohmann::json plugins_ = nlohmann::json::array();
    std::filesystem::path dataDirectory_;
    fo4ide::rpc::PrevisTarget* target{nullptr};
    bool probed{false};
    bool busy_seen{false};
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

void writeMinimalPlugin(const std::filesystem::path& path) {
    const std::vector<unsigned char> bytes{
        'T', 'E', 'S', '4', 18, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 131, 0, 0, 0,
        'H', 'E', 'D', 'R', 12, 0, 0, 0, 0x80, 0x3F, 0, 0, 0, 0, 0, 8, 0, 0};
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

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

TEST_CASE("previs asset roots keep the Data folder when no MO2 profile is configured") {
    TemporaryDirectory directory;
    fo4ide::settings::Settings settings;

    const auto roots = fo4ide::rpc::previsAssetRoots(settings, directory.path());
    REQUIRE(roots.size() == 1);
    CHECK(roots.front() == directory.path());
}

TEST_CASE("previs asset roots fall back when the MO2 profile cannot be read") {
    TemporaryDirectory directory;
    fo4ide::settings::Settings settings;
    settings.mo2InstancePath = directory.path() / "no-such-instance";

    const auto roots = fo4ide::rpc::previsAssetRoots(settings, directory.path());
    REQUIRE(roots.size() == 1);
    CHECK(roots.front() == directory.path());
}

TEST_CASE("previs target refuses a second concurrent scan") {
    TemporaryDirectory directory;
    FakeBackend backend;
    fo4ide::settings::SharedSettings settings;
    fo4ide::rpc::PrevisTarget target(backend, settings);
    backend.target = &target;

    const auto result = target.invoke("Scan", nlohmann::json::array({directory.path().string()}));
    REQUIRE_FALSE(result.has_value());
    CHECK(backend.busy_seen);
    CHECK(result.error().type == "PrevisNoEnvironment");

    // Regression: the busy flag must clear after the first scan, or every later scan is refused.
    const auto second = target.invoke("Scan", nlohmann::json::array({directory.path().string()}));
    REQUIRE_FALSE(second.has_value());
    CHECK(second.error().type != "PrevisScanBusy");
    CHECK(second.error().type == "PrevisNoEnvironment");
}

// Detects: a scan that hides a loaded plugin with unsaved edits, instead of naming it as not reflected, must fail.
// Regression: panel wizard must expose a CK-free workflow list without calling Creation Kit.
TEST_CASE("previs patch workflow lists native steps") {
    FakeBackend backend;
    fo4ide::settings::SharedSettings settings;
    fo4ide::rpc::PrevisTarget target(backend, settings);

    const auto workflow = target.invoke("PatchWorkflow", nlohmann::json::array());
    REQUIRE(workflow.has_value());
    REQUIRE(workflow->is_array());
    CHECK(workflow->size() >= 5);
    CHECK((*workflow)[0]["id"].get<std::string>() == "scan");
}

TEST_CASE("previs scan names the loaded plugins with unsaved edits that it does not read") {
    TemporaryDirectory directory;
    const auto plugin = directory.path() / "Pending.esp";
    writeMinimalPlugin(plugin);
    FakeBackend backend;
    backend.plugins_ = nlohmann::json(nlohmann::json::array({nlohmann::json{
        {"FilePath", plugin.string()}, {"Pending", true}}}).dump());
    fo4ide::settings::SharedSettings settings;
    fo4ide::rpc::PrevisTarget target(backend, settings);

    const auto result = target.invoke("Scan", nlohmann::json::array({directory.path().string()}));
    REQUIRE(result.has_value());
    CHECK((*result)["pendingPlugins"] == nlohmann::json::array({"Pending.esp"}));
    CHECK((*result)["summary"]["cells"] == 0);
}
