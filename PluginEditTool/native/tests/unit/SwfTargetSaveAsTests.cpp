#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <fo4ide/app/SwfTarget.hpp>

#include "SwfTargetTestSupport.hpp"

using namespace swft_target_test;

// Detects: Save As over another existing movie must back it up even when an older .bak sits beside it.
TEST_CASE("SwfTarget Save As over another file keeps it in a fresh backup") {
    const fs::path dir = uniqueDir("fo4ide-swf-saveas-backup");
    const RemoveAll cleanup{dir};
    RootedTarget rooted;
    auto& target = rooted.target;
    *rooted.roots = {gameDataFolder(dir / "Data")};
    fs::create_directories(dir / "out");
    const fs::path destination = dir / "out" / "menu.swf";
    const std::vector<std::byte> existing{std::byte{1}, std::byte{2}, std::byte{3}};
    const std::vector<std::byte> olderBackup{std::byte{9}};
    writeBytes(destination, existing);
    writeBytes(fs::path(destination.string() + ".bak"), olderBackup);

    REQUIRE_FALSE(replyJson(target.invoke("OpenArchived", nlohmann::json::array({"interface/test.swf"}))).contains("error"));
    replyJson(target.invoke("RenameLabel", nlohmann::json::array({0, 0, "menu"})));
    rooted.platform.saveSelection = destination;
    const auto saveArgs = nlohmann::json::array({"Save the SWF movie", "SWF movie (*.swf)|*.swf"});
    REQUIRE_FALSE(replyJson(target.invoke("SaveAs", saveArgs)).contains("error"));

    CHECK(readFileBytes(fs::path(destination.string() + ".bak")) == olderBackup);
    CHECK(readFileBytes(fs::path(destination.string() + ".bak1")) == existing);
}
