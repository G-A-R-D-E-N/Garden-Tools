#include <filesystem>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/app/GuiEndpoint.hpp>

#include "../helpers/TempRoot.hpp"

namespace {

std::string makeToken() {
    std::string token(32, 'a');
    for (std::size_t i = 0; i < token.size(); ++i) {
        token[i] = "0123456789abcdef"[i % 16];
    }
    return token;
}

const std::string kToken = makeToken();

fo4ide::app::GuiEndpoint sampleEndpoint() {
    return {.pid = 4242, .port = 49735, .token = kToken, .instance = "C:/MO2/instance"};
}

}

TEST_CASE("GUI endpoint JSON round-trips through the parser") {
    const auto endpoint = sampleEndpoint();
    const auto parsed = fo4ide::app::parseGuiEndpoint(fo4ide::app::guiEndpointJson(endpoint));
    REQUIRE(parsed.has_value());
    CHECK(parsed->pid == 4242);
    CHECK(parsed->port == 49735);
    CHECK(parsed->token == kToken);
    CHECK(parsed->instance == "C:/MO2/instance");
}

TEST_CASE("GUI endpoint parser accepts a missing instance as empty") {
    nlohmann::json json = fo4ide::app::guiEndpointJson(sampleEndpoint());
    json.erase("instance");
    const auto parsed = fo4ide::app::parseGuiEndpoint(json);
    REQUIRE(parsed.has_value());
    CHECK(parsed->instance.empty());
}

TEST_CASE("GUI endpoint parser refuses malformed fields") {
    const auto base = fo4ide::app::guiEndpointJson(sampleEndpoint());

    CHECK_FALSE(fo4ide::app::parseGuiEndpoint(nlohmann::json::array()).has_value());

    auto zeroPid = base;
    zeroPid["pid"] = 0;
    CHECK_FALSE(fo4ide::app::parseGuiEndpoint(zeroPid).has_value());

    auto textPid = base;
    textPid["pid"] = "4242";
    CHECK_FALSE(fo4ide::app::parseGuiEndpoint(textPid).has_value());

    auto zeroPort = base;
    zeroPort["port"] = 0;
    CHECK_FALSE(fo4ide::app::parseGuiEndpoint(zeroPort).has_value());

    auto hugePort = base;
    hugePort["port"] = 65536;
    CHECK_FALSE(fo4ide::app::parseGuiEndpoint(hugePort).has_value());

    auto shortToken = base;
    shortToken["token"] = kToken.substr(0, 3);
    CHECK_FALSE(fo4ide::app::parseGuiEndpoint(shortToken).has_value());

    auto upperToken = base;
    upperToken["token"] = std::string(32, 'A');
    CHECK_FALSE(fo4ide::app::parseGuiEndpoint(upperToken).has_value());

    auto missingToken = base;
    missingToken.erase("token");
    CHECK_FALSE(fo4ide::app::parseGuiEndpoint(missingToken).has_value());
}

TEST_CASE("access tokens are 32 lowercase hex digits") {
    CHECK(fo4ide::app::isAccessToken(kToken));
    CHECK_FALSE(fo4ide::app::isAccessToken(kToken.substr(0, 16)));
    CHECK_FALSE(fo4ide::app::isAccessToken(kToken.substr(0, 31) + "g"));
    CHECK_FALSE(fo4ide::app::isAccessToken(""));
}

TEST_CASE("GUI endpoint file is written, read back and removed") {
    fo4ide_test::TempRoot root("fo4ide_gui_endpoint");
    const auto file = fo4ide::app::guiEndpointPath(root.path());
    CHECK(file.filename() == "gui-endpoint.json");

    auto written = fo4ide::app::writeGuiEndpoint(file, sampleEndpoint());
    REQUIRE(written.has_value());
    CHECK_FALSE(std::filesystem::exists(std::filesystem::path(file).concat(".tmp")));

    const auto read = fo4ide::app::readGuiEndpoint(file);
    REQUIRE(read.has_value());
    CHECK(read->port == 49735);
    CHECK(read->token == kToken);

    fo4ide::app::removeGuiEndpoint(file, sampleEndpoint().pid);
    CHECK_FALSE(std::filesystem::exists(file));
    CHECK(fo4ide::app::readGuiEndpoint(file).error().type == "GuiEndpointMissing");
}

// Detects: a GUI that exits deleting the endpoint file a second, still running GUI wrote, so --mcp --attach falls back to in-process and AI edits miss the open editor.
TEST_CASE("GUI endpoint file written by another process survives this one's removal") {
    fo4ide_test::TempRoot root("fo4ide_gui_endpoint");
    const auto file = fo4ide::app::guiEndpointPath(root.path());
    auto newer = sampleEndpoint();
    newer.pid = sampleEndpoint().pid + 1;
    REQUIRE(fo4ide::app::writeGuiEndpoint(file, newer).has_value());

    fo4ide::app::removeGuiEndpoint(file, sampleEndpoint().pid);
    const auto kept = fo4ide::app::readGuiEndpoint(file);
    REQUIRE(kept.has_value());
    CHECK(kept->pid == newer.pid);

    fo4ide::app::removeGuiEndpoint(file, newer.pid);
    CHECK_FALSE(std::filesystem::exists(file));
}

TEST_CASE("GUI endpoint reader rejects a file that is not JSON") {
    fo4ide_test::TempRoot root("fo4ide_gui_endpoint");
    const auto file = fo4ide::app::guiEndpointPath(root.path());
    std::ofstream(file) << "{not json";
    const auto read = fo4ide::app::readGuiEndpoint(file);
    REQUIRE_FALSE(read.has_value());
    CHECK(read.error().type == "GuiEndpointInvalid");
}

TEST_CASE("process liveness distinguishes this process from an impossible pid") {
    CHECK(fo4ide::app::isProcessAlive(fo4ide::app::currentProcessId()));
    CHECK_FALSE(fo4ide::app::isProcessAlive(0));
    CHECK_FALSE(fo4ide::app::isProcessAlive(2000000000u));
}
