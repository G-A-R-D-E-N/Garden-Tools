#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/app/AudioTarget.hpp>
#include <fo4ide/audio/AudioService.hpp>
#include <fo4ide/settings/Settings.hpp>
#include <fo4ide/tools/ToolLocator.hpp>

#include "../helpers/ServiceTargetFakes.hpp"

using namespace fo4ide::app::test;

TEST_CASE("ConvertToXwm refuses to re-encode an xwm source onto itself") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto encoder = root / "xWMAEncode.exe";
    const auto source = root / "x.xwm";
    touch(encoder);
    touch(source, "original");
    fo4ide::settings::Settings settings;
    settings.xwmaEncodePath = encoder;
    fo4ide::settings::SharedSettings shared(settings);
    fo4ide::tools::Locator locator(shared, root / "fo4ide.exe");
    FakeRunner runner;
    fo4ide::audio::AudioService service(runner, locator);

    const auto result = service.convertToXwm(source);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "AudioOutputMatchesSource");
    CHECK(runner.last.executable.empty());
    std::ifstream stream(source, std::ios::binary);
    const std::string contents((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    CHECK(contents == "original");
}

TEST_CASE("ConvertFromXwm refuses to write its output over the source and runs nothing") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto encoder = root / "xWMAEncode.exe";
    const auto source = root / "x.xwm";
    touch(encoder);
    touch(source, "original");
    fo4ide::settings::Settings settings;
    settings.xwmaEncodePath = encoder;
    fo4ide::settings::SharedSettings shared(settings);
    fo4ide::tools::Locator locator(shared, root / "fo4ide.exe");
    FakeRunner runner;
    fo4ide::audio::AudioService service(runner, locator);

    const auto result = service.convertFromXwm(source, source);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "AudioOutputMatchesSource");
    CHECK(runner.last.executable.empty());
    std::ifstream stream(source, std::ios::binary);
    const std::string contents((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    CHECK(contents == "original");
}

TEST_CASE("AudioTarget refuses a dropped payload that decodes past the 16 MiB cap") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::settings::SharedSettings settings;
    fo4ide::tools::Locator locator(settings, root / "fo4ide.exe");
    FakeRunner runner;
    FakePlatform platform;
    fo4ide::audio::AudioService service(runner, locator);
    fo4ide::app::AudioTarget target(service, platform);

    const std::string payload(22369623, 'A');
    const auto result = target.invoke("StageDroppedFile", nlohmann::json::array({"clip.wav", payload}));
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    CHECK(result->get<std::string>().starts_with("ERR:"));
}

TEST_CASE("AudioTarget stages a dropped payload at the 16 MiB cap") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::settings::SharedSettings settings;
    fo4ide::tools::Locator locator(settings, root / "fo4ide.exe");
    FakeRunner runner;
    FakePlatform platform;
    fo4ide::audio::AudioService service(runner, locator);
    fo4ide::app::AudioTarget target(service, platform);

    const std::string payload(22369622, 'A');
    const auto result = target.invoke("StageDroppedFile", nlohmann::json::array({"clip.wav", payload}));
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    const auto path = std::filesystem::path(result->get<std::string>());
    REQUIRE(std::filesystem::is_regular_file(path));
    CHECK(std::filesystem::file_size(path) == 16U * 1024U * 1024U);
    std::error_code ec;
    std::filesystem::remove_all(path.parent_path(), ec);
}
