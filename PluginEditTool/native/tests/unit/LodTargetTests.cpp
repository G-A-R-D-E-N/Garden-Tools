#include <chrono>
#include <cstddef>
#include <deque>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/app/LodTarget.hpp>

#include "../helpers/PluginFixtures.hpp"

namespace fs = std::filesystem;

TEST_CASE("LodTarget Commit of two empty ledgers answers an empty string, not a failure") {
    fo4ide::app::LodTarget target;

    auto result = target.invoke("Commit", nlohmann::json::array({"", ""}));
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    CHECK(result->get<std::string>().empty());
}

TEST_CASE("LodTarget Commit with no arguments answers an empty string") {
    fo4ide::app::LodTarget target;

    auto result = target.invoke("Commit", nlohmann::json::array());
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    CHECK(result->get<std::string>().empty());
}

TEST_CASE("LodTarget GenerateScoped refuses a world the load order does not carry") {
    const auto root = fs::temp_directory_path() / "fo4ide-lod-target-scoped";
    std::error_code code;
    fs::remove_all(root, code);
    fs::create_directories(root / "Data", code);
    const auto plugin = root / "ScopedTest.esm";
    const std::string bytes = plugin_fixtures_test::minimalPlugin(0, "");
    {
        std::ofstream out(plugin, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    fo4ide::app::LodTarget target;
    const auto args = nlohmann::json::array(
        {plugin.string(), (root / "Data").string(), std::string{}, std::string{"Nowhere"}, std::string{}, false, 4, 8});
    auto result = target.invoke("GenerateScoped", args);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("not found") != std::string::npos);

    fs::remove_all(root, code);
}

// Detects: a preview FIFO that must keep eight files, or one that deletes the newest path a tool is told to read.
TEST_CASE("rememberPreview keeps the newest eight preview files") {
    const auto root = fs::temp_directory_path() / "fo4ide-lod-preview-fifo";
    std::error_code code;
    fs::remove_all(root, code);
    fs::create_directories(root, code);

    std::deque<fs::path> previews;
    std::vector<fs::path> written;
    for (int index = 0; index < 9; ++index) {
        const auto path = root / ("preview-" + std::to_string(index) + ".btr");
        {
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out << "btr";
        }
        written.push_back(path);
        const auto deleted = fo4ide::app::rememberPreview(previews, path, 8);
        CHECK(deleted == (index == 8 ? std::size_t{1} : std::size_t{0}));
    }

    // Invariant: the oldest nine-file overflow is gone, and every newer preview is still readable.
    CHECK_FALSE(fs::exists(written.front()));
    for (std::size_t index = 1; index < written.size(); ++index) CHECK(fs::exists(written[index]));

    fo4ide::app::deletePreviews(previews);
    for (const auto& path : written) CHECK_FALSE(fs::exists(path));

    fs::remove_all(root, code);
}

// Detects (invariant): a drifted scoped order must be caught, because apply would land in a level slot.
TEST_CASE("LodTarget GenerateScoped reads world, ledger, apply and then the two levels") {
    const auto root = fs::temp_directory_path() / "fo4ide-lod-target-scoped-order";
    std::error_code code;
    fs::remove_all(root, code);
    fs::create_directories(root / "Data", code);
    const auto plugin = root / "ScopedOrderTest.esm";
    const std::string bytes = plugin_fixtures_test::minimalPlugin(0, "");
    {
        std::ofstream out(plugin, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    fo4ide::app::LodTarget target;
    // Constraint: apply is index 5, so an empty data folder must refuse this call as a write, not answer a dry run.
    const auto write = nlohmann::json::array({plugin.string(), std::string{}, std::string{}, std::string{"Nowhere"},
                                              std::string{"testworld|terrain|4|0|0 1"}, true, 8, 8});
    auto applied = target.invoke("GenerateScoped", write);
    REQUIRE_FALSE(applied.has_value());
    CHECK(applied.error().message.find("data folder") != std::string::npos);

    // Constraint: 99 is not a shipped level, so the bound at index 6 and 7 is what this call must read and refuse.
    const auto levels = nlohmann::json::array({plugin.string(), std::string{}, std::string{}, std::string{"Nowhere"},
                                               std::string{}, false, 99, 99});
    auto bounded = target.invoke("GenerateScoped", levels);
    REQUIRE_FALSE(bounded.has_value());
    CHECK(bounded.error().message.find("level bound") != std::string::npos);

    fs::remove_all(root, code);
}

// Invariant: a plugin edited after open must reload the session, so a path-only cache key fails here.
TEST_CASE("LodTarget reloads its LOD session after a plugin's write time changes") {
    const auto root = fs::temp_directory_path() / "fo4ide-lod-target-session";
    std::error_code code;
    fs::remove_all(root, code);
    fs::create_directories(root / "Data", code);
    const auto plugin = root / "SessionTest.esm";
    const std::string original = plugin_fixtures_test::minimalPlugin(0, "");
    {
        std::ofstream out(plugin, std::ios::binary | std::ios::trunc);
        out.write(original.data(), static_cast<std::streamsize>(original.size()));
    }

    fo4ide::app::LodTarget target;
    const auto args = nlohmann::json::array({plugin.string(), (root / "Data").string(), std::string{}});
    auto opened = target.invoke("Worlds", args);
    REQUIRE(opened.has_value());

    // Constraint: the rewrite keeps the byte count, so only the write time can tell the two loads apart.
    {
        std::ofstream out(plugin, std::ios::binary | std::ios::trunc);
        const std::string broken(original.size(), '\0');
        out.write(broken.data(), static_cast<std::streamsize>(broken.size()));
    }
    std::error_code stamp;
    const auto moved = fs::last_write_time(plugin, stamp) + std::chrono::seconds(5);
    REQUIRE_FALSE(stamp);
    fs::last_write_time(plugin, moved, stamp);
    REQUIRE_FALSE(stamp);

    auto reloaded = target.invoke("Worlds", args);
    REQUIRE_FALSE(reloaded.has_value());

    fs::remove_all(root, code);
}
