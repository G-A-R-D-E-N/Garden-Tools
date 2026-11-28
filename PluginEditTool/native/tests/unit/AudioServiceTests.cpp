#include <algorithm>
#include <cstdint>
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

namespace {

void writePcmWav(const std::filesystem::path& path) {
    std::string bytes{"RIFF"};
    auto u16 = [&bytes](std::uint16_t value) {
        bytes.push_back(static_cast<char>(value & 0xFFU));
        bytes.push_back(static_cast<char>((value >> 8U) & 0xFFU));
    };
    auto u32 = [&bytes](std::uint32_t value) {
        for (int shift = 0; shift < 32; shift += 8) {
            bytes.push_back(static_cast<char>((value >> shift) & 0xFFU));
        }
    };
    u32(36);
    bytes += "WAVEfmt ";
    u32(16);
    u16(1);  // PCM
    u16(1);  // mono
    u32(8000);
    u32(16000);
    u16(2);
    u16(16);
    bytes += "data";
    u32(0);
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::string readText(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

class StagingRunner final : public fo4ide::process::Runner {
public:
    std::string payload{"partial"};
    int exitCode{1};
    std::size_t outputs{1};

    fo4ide::Result<fo4ide::process::Output> run(const fo4ide::process::Request& request) override {
        const auto count = request.arguments.empty()
            ? std::size_t{0}
            : std::min(outputs, request.arguments.size() - 1);
        for (std::size_t index = 1; index <= count; ++index) {
            std::ofstream stream(request.arguments[index], std::ios::binary | std::ios::trunc);
            stream << payload;
        }
        return fo4ide::process::Output{true, false, exitCode, {}, {}};
    }
};

}

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

TEST_CASE("Failed audio conversions keep an existing destination intact") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto encoder = root / "xWMAEncode.exe";
    const auto source = root / "voice.wav";
    const auto destination = root / "voice.xwm";
    touch(encoder);
    writePcmWav(source);
    touch(destination, "previous");
    fo4ide::settings::Settings settings;
    settings.xwmaEncodePath = encoder;
    fo4ide::settings::SharedSettings shared(settings);
    fo4ide::tools::Locator locator(shared, root / "fo4ide.exe");
    StagingRunner runner;
    fo4ide::audio::AudioService service(runner, locator);

    const auto failed = service.convertToXwm(source, destination);
    REQUIRE_FALSE(failed.has_value());
    CHECK(failed.error().type == "XwmaEncodeFailed");
    CHECK(readText(destination) == "previous");

    runner.payload = "encoded";
    runner.exitCode = 0;
    const auto converted = service.convertToXwm(source, destination);
    REQUIRE(converted.has_value());
    CHECK(readText(destination) == "encoded");
}

TEST_CASE("Failed FUZ extraction keeps existing outputs intact and reports only produced files") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    touch(root / "tools" / "audio" / "BmlFuzDecode.exe");
    const auto fuz = root / "voice.fuz";
    const auto xwm = root / "voice.xwm";
    const auto lip = root / "voice.lip";
    touch(fuz, "fuz");
    touch(xwm, "previous-xwm");
    touch(lip, "previous-lip");
    fo4ide::settings::SharedSettings shared;
    fo4ide::tools::Locator locator(shared, root / "fo4ide.exe");
    StagingRunner runner;
    runner.outputs = 2;
    fo4ide::audio::AudioService service(runner, locator);

    const auto failed = service.extractFuz(fuz, xwm, lip);
    REQUIRE_FALSE(failed.has_value());
    CHECK(failed.error().type == "FuzDecodeFailed");
    CHECK(readText(xwm) == "previous-xwm");
    CHECK(readText(lip) == "previous-lip");

    runner.exitCode = 0;
    runner.payload = "new-xwm";
    runner.outputs = 1;
    const auto extracted = service.extractFuz(fuz, xwm, lip);
    REQUIRE(extracted.has_value());
    CHECK(readText(xwm) == "new-xwm");
    CHECK(readText(lip) == "previous-lip");
    CHECK(extracted->find("no lip in this fuz") != std::string::npos);
}

TEST_CASE("ConvertFromXwm refuses an output extension that disagrees with the selected format") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto encoder = root / "xWMAEncode.exe";
    const auto source = root / "voice.xwm";
    touch(encoder);
    touch(source, "original");
    fo4ide::settings::Settings settings;
    settings.xwmaEncodePath = encoder;
    fo4ide::settings::SharedSettings shared(settings);
    fo4ide::tools::Locator locator(shared, root / "fo4ide.exe");
    FakeRunner runner;
    fo4ide::audio::AudioService service(runner, locator);

    const auto result = service.convertFromXwm(source, root / "result.flac", "mp3");
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "AudioFormatMismatch");
    CHECK(runner.calls == 0);
}

TEST_CASE("ConvertFromXwm refuses a target extension that is not a plain extension") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto encoder = root / "xWMAEncode.exe";
    const auto source = root / "voice.xwm";
    touch(encoder);
    touch(source, "original");
    fo4ide::settings::Settings settings;
    settings.xwmaEncodePath = encoder;
    fo4ide::settings::SharedSettings shared(settings);
    fo4ide::tools::Locator locator(shared, root / "fo4ide.exe");
    FakeRunner runner;
    fo4ide::audio::AudioService service(runner, locator);

    for (const std::string extension : {"x\\..\\..\\evil.mp3", "../evil", "wa:v", "toolongext1", "mp 3"}) {
        const auto result = service.convertFromXwm(source, {}, extension);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().type == "AudioInvalidTargetExtension");
    }
    CHECK(runner.calls == 0);
    CHECK(std::filesystem::exists(source));
}

TEST_CASE("ConvertToXwm refuses a batch whose sources map to one output") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto encoder = root / "xWMAEncode.exe";
    const auto source = root / "clip";
    const auto output = root / "out";
    touch(encoder);
    touch(source / "voice.wav", "wav");
    touch(source / "voice.mp3", "mp3");
    fo4ide::settings::Settings settings;
    settings.xwmaEncodePath = encoder;
    fo4ide::settings::SharedSettings shared(settings);
    fo4ide::tools::Locator locator(shared, root / "fo4ide.exe");
    FakeRunner runner;
    fo4ide::audio::AudioService service(runner, locator);

    const auto result = service.convertToXwm(source, output);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "AudioOutputCollision");
    CHECK(runner.calls == 0);
    CHECK_FALSE(std::filesystem::exists(output / "voice.xwm"));
}

TEST_CASE("MakeFuz refuses an explicitly selected lip file that does not exist") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    touch(root / "tools" / "audio" / "BmlFuzEncode.exe");
    const auto source = root / "voice.wav";
    touch(source, "wav");
    fo4ide::settings::Settings settings;
    fo4ide::settings::SharedSettings shared(settings);
    fo4ide::tools::Locator locator(shared, root / "fo4ide.exe");
    FakeRunner runner;
    fo4ide::audio::AudioService service(runner, locator);

    const auto result = service.makeFuz(source, root / "missing.lip", root / "voice.fuz", false);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "LipFileNotFound");
    CHECK(runner.calls == 0);
}

TEST_CASE("MakeFuz and ExtractFuz refuse outputs that alias their inputs") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    touch(root / "tools" / "audio" / "BmlFuzEncode.exe");
    touch(root / "tools" / "audio" / "BmlFuzDecode.exe");
    const auto source = root / "voice.wav";
    const auto fuz = root / "voice.fuz";
    touch(source, "wav");
    touch(fuz, "fuz");
    fo4ide::settings::Settings settings;
    fo4ide::settings::SharedSettings shared(settings);
    fo4ide::tools::Locator locator(shared, root / "fo4ide.exe");
    FakeRunner runner;
    fo4ide::audio::AudioService service(runner, locator);

    const auto encoded = service.makeFuz(source, {}, source, false);
    REQUIRE_FALSE(encoded.has_value());
    CHECK(encoded.error().type == "AudioOutputMatchesSource");

    const auto extracted = service.extractFuz(fuz, fuz);
    REQUIRE_FALSE(extracted.has_value());
    CHECK(extracted.error().type == "AudioOutputMatchesSource");
    CHECK(runner.calls == 0);
}
