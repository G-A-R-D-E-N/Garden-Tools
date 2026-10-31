#include <cstdint>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <fo4ide/app/SwfTarget.hpp>
#include <fo4ide/core/PayloadLimits.hpp>
#include <fo4ide/settings/Settings.hpp>

#include "SwfTargetTestSupport.hpp"

using namespace swft_target_test;


TEST_CASE("SwfTarget opens a movie and snapshots the document") {
    RootedTarget rooted;
    auto& target = rooted.target;
    TempSwf file(fixtureMovie());
    allowPath(rooted, file.path);

    const auto opened =
        replyJson(target.invoke("Open", nlohmann::json::array({file.path.string()})));
    CHECK_FALSE(opened.contains("error"));
    CHECK(opened.value("revision", 1u) == 0u);

    const auto state = replyJson(target.invoke("State", nlohmann::json::array()));
    CHECK(state.value("undo", 9) == 0);
    CHECK(state.value("redo", 9) == 0);
    CHECK(state.value("dirty", true) == false);

    const auto characters =
        replyJson(target.invoke("Characters", nlohmann::json::array()));
    CHECK(characters.contains("characters"));
}

TEST_CASE("SwfTarget renames a label with undo and redo tracking state") {
    RootedTarget rooted;
    auto& target = rooted.target;
    TempSwf file(fixtureMovie());
    allowPath(rooted, file.path);
    replyJson(target.invoke("Open", nlohmann::json::array({file.path.string()})));

    const auto renamed =
        replyJson(target.invoke("RenameLabel", nlohmann::json::array({0, 0, "menu"})));
    CHECK(renamed.value("changed", false) == true);
    CHECK(renamed.value("undo", 0) == 1);
    CHECK(renamed.value("dirty", false) == true);

    const auto labels = replyJson(target.invoke("FrameLabels", nlohmann::json::array({0})));
    REQUIRE(labels.contains("labels"));
    REQUIRE(labels.at("labels").size() == 1);
    CHECK(labels.at("labels")[0].value("name", "") == "menu");

    const auto undone = replyJson(target.invoke("Undo", nlohmann::json::array()));
    CHECK(undone.value("undo", 9) == 0);
    CHECK(undone.value("redo", 9) == 1);
    const auto restored = replyJson(target.invoke("FrameLabels", nlohmann::json::array({0})));
    CHECK(restored.at("labels")[0].value("name", "") == "start");

    const auto redone = replyJson(target.invoke("Redo", nlohmann::json::array()));
    CHECK(redone.value("undo", 0) == 1);
    const auto again = replyJson(target.invoke("FrameLabels", nlohmann::json::array({0})));
    CHECK(again.at("labels")[0].value("name", "") == "menu");

    const auto refusal =
        replyJson(target.invoke("RenameLabel", nlohmann::json::array({0, 9, "menu"})));
    CHECK(refusal.contains("error"));
    CHECK(refusal.contains("status"));
}

TEST_CASE("SwfTarget applies an ABC text edit and saves it back to disk") {
    RootedTarget rooted;
    auto& target = rooted.target;
    TempSwf file(fixtureMovie());
    allowPath(rooted, file.path);
    const auto opened =
        replyJson(target.invoke("Open", nlohmann::json::array({file.path.string()})));
    INFO(opened.dump());
    CHECK_FALSE(opened.contains("error"));

    const auto inserted = replyJson(
        target.invoke("InsertAbcInstruction", nlohmann::json::array({0, 0, 0, "nop"})));
    CHECK(inserted.value("changed", false) == true);

    const auto listing =
        replyJson(target.invoke("Disassemble", nlohmann::json::array({0, 0})));
    REQUIRE(listing.contains("text"));
    CHECK(listing.at("text").get<std::string>().find("nop") != std::string::npos);

    const auto matrixCheck =
        replyJson(target.invoke("Placements", nlohmann::json::array()));
    REQUIRE(matrixCheck.contains("placements"));
    REQUIRE_FALSE(matrixCheck.at("placements").empty());
    const int tagIndex = matrixCheck.at("placements")[0].value("tagIndex", -1);
    const auto matrix =
        replyJson(target.invoke("PlacementMatrix", nlohmann::json::array({tagIndex})));
    CHECK(matrix.value("hasScale", true) == false);
    CHECK(matrix.value("scaleX", 0) == 0x10000);

    const auto saved = replyJson(target.invoke("Save", nlohmann::json::array()));
    CHECK(saved.value("saved", "") == file.path.string());

    RootedTarget reopenedRooted;
    auto& reopened = reopenedRooted.target;
    allowPath(reopenedRooted, file.path);
    const auto reopenedOpen =
        replyJson(reopened.invoke("Open", nlohmann::json::array({file.path.string()})));
    INFO(reopenedOpen.dump());
    CHECK_FALSE(reopenedOpen.contains("error"));
    const auto again =
        replyJson(reopened.invoke("Disassemble", nlohmann::json::array({0, 0})));
    CHECK(again.at("text").get<std::string>().find("nop") != std::string::npos);
    const auto againLabels =
        replyJson(reopened.invoke("FrameLabels", nlohmann::json::array({0})));
    CHECK(againLabels.at("labels")[0].value("name", "") == "start");

    const auto outOfRange =
        replyJson(reopened.invoke("ShapeSvg", nlohmann::json::array({-1})));
    CHECK(outOfRange.contains("error"));
    CHECK(outOfRange.at("error").get<std::string>().find("0..65535") != std::string::npos);
    CHECK_FALSE(outOfRange.contains("svg"));
}

TEST_CASE("SwfTarget refuses calls with no open document") {
    RootedTarget rooted;
    auto& target = rooted.target;
    const auto state = replyJson(target.invoke("State", nlohmann::json::array()));
    CHECK(state.contains("error"));
    const auto saved = replyJson(target.invoke("Save", nlohmann::json::array()));
    CHECK(saved.contains("error"));
}

// Constraint: flat tag 2 is the root PlaceObject2 at depth 1, so moving it must rewrite the depth and undo must restore it.
TEST_CASE("SwfTarget moves a root placement depth with undo") {
    RootedTarget rooted;
    auto& target = rooted.target;
    TempSwf file(fixtureMovie());
    allowPath(rooted, file.path);
    replyJson(target.invoke("Open", nlohmann::json::array({file.path.string()})));

    const auto moved = replyJson(target.invoke("SetDepth", nlohmann::json::array({2, 9})));
    CHECK(moved.value("changed", false) == true);

    const auto placements = replyJson(target.invoke("Placements", nlohmann::json::array()));
    REQUIRE(placements.contains("placements"));
    int depth = -1;
    for (const auto& row : placements.at("placements")) {
        if (row.value("tagIndex", -1) == 2) depth = row.value("depth", -1);
    }
    CHECK(depth == 9);

    const auto undone = replyJson(target.invoke("Undo", nlohmann::json::array()));
    CHECK(undone.value("undo", 9) == 0);
    const auto restored = replyJson(target.invoke("Placements", nlohmann::json::array()));
    for (const auto& row : restored.at("placements")) {
        if (row.value("tagIndex", -1) == 2) depth = row.value("depth", -1);
    }
    CHECK(depth == 1);

    const auto refusal = replyJson(target.invoke("SetDepth", nlohmann::json::array({1, 3})));
    CHECK(refusal.contains("error"));
    CHECK(refusal.contains("status"));
}

// Constraint: flat tag 6 is the sprite PlaceObject2 child, so the transform must splice its MATRIX and undo must clear it.
TEST_CASE("SwfTarget rewrites a sprite placement matrix with undo") {
    RootedTarget rooted;
    auto& target = rooted.target;
    TempSwf file(fixtureMovie());
    allowPath(rooted, file.path);
    replyJson(target.invoke("Open", nlohmann::json::array({file.path.string()})));

    const auto before =
        replyJson(target.invoke("PlacementMatrix", nlohmann::json::array({6})));
    CHECK(before.value("x", -1) == 0);

    const auto applied = replyJson(target.invoke(
        "SetTransform", nlohmann::json::array({6, 40, 60, 0x10000, 0x10000, 0, 0})));
    CHECK(applied.value("changed", false) == true);
    const auto matrix =
        replyJson(target.invoke("PlacementMatrix", nlohmann::json::array({6})));
    CHECK(matrix.value("x", -1) == 40);
    CHECK(matrix.value("y", -1) == 60);
    CHECK(matrix.value("hasScale", true) == false);

    const auto undone = replyJson(target.invoke("Undo", nlohmann::json::array()));
    CHECK(undone.value("undo", 9) == 0);
    const auto restored =
        replyJson(target.invoke("PlacementMatrix", nlohmann::json::array({6})));
    CHECK(restored.value("x", -1) == 0);

    const auto refusal = replyJson(target.invoke(
        "SetTransform", nlohmann::json::array({0, 40, 60, 0x10000, 0x10000, 0, 0})));
    CHECK(refusal.contains("error"));
    CHECK(refusal.contains("status"));
}

int spriteRowCount(const nlohmann::json& placements) {
    int rows = 0;
    for (const auto& row : placements.at("placements")) {
        if (row.value("insideSprite", false)) ++rows;
    }
    return rows;
}

TEST_CASE("SwfTarget inserts a placement keyframe with undo") {
    RootedTarget rooted;
    auto& target = rooted.target;
    TempSwf file(fixtureMovie());
    allowPath(rooted, file.path);
    replyJson(target.invoke("Open", nlohmann::json::array({file.path.string()})));

    const auto before = replyJson(target.invoke("Placements", nlohmann::json::array()));
    REQUIRE(before.contains("placements"));
    CHECK(spriteRowCount(before) == 1);

    // Requires a sprite child: the root placement already authors its matrix.
    const auto inserted =
        replyJson(target.invoke("InsertKeyframe", nlohmann::json::array({6, 0})));
    CHECK(inserted.value("changed", false) == true);

    // Requires the spliced move to list: the sprite placement must appear twice until undo.
    const auto after = replyJson(target.invoke("Placements", nlohmann::json::array()));
    CHECK(spriteRowCount(after) == 2);

    const auto undone = replyJson(target.invoke("Undo", nlohmann::json::array()));
    CHECK(undone.value("undo", 9) == 0);
    const auto restored = replyJson(target.invoke("Placements", nlohmann::json::array()));
    CHECK(spriteRowCount(restored) == 1);
    const auto matrix =
        replyJson(target.invoke("PlacementMatrix", nlohmann::json::array({6})));
    CHECK(matrix.value("hasScale", true) == false);

    const auto refusal =
        replyJson(target.invoke("InsertKeyframe", nlohmann::json::array({0, 0})));
    CHECK(refusal.contains("error"));
    CHECK(refusal.contains("status"));

    // Requires the frame to exist: frame 9 is past the sprite's single frame.
    const auto pastEnd =
        replyJson(target.invoke("InsertKeyframe", nlohmann::json::array({6, 9})));
    CHECK(pastEnd.contains("error"));
    CHECK(pastEnd.contains("status"));
}

// Constraint: the first save must freeze the on-disk original, so a later save never rewrites the backup.
TEST_CASE("SwfTarget keeps the first-save backup across later saves") {
    RootedTarget rooted;
    auto& target = rooted.target;
    TempSwf file(fixtureMovie());
    allowPath(rooted, file.path);
    replyJson(target.invoke("Open", nlohmann::json::array({file.path.string()})));

    const auto original = fixtureMovie();
    CHECK(readFileBytes(file.path) == original);

    replyJson(target.invoke("RenameLabel", nlohmann::json::array({0, 0, "menu"})));
    const auto first = replyJson(target.invoke("Save", nlohmann::json::array()));
    CHECK(first.value("saved", "") == file.path.string());
    CHECK(readFileBytes(fs::path(file.path.string() + ".bak")) == original);

    replyJson(target.invoke("RenameLabel", nlohmann::json::array({0, 0, "submenu"})));
    const auto second = replyJson(target.invoke("Save", nlohmann::json::array()));
    CHECK(second.value("saved", "") == file.path.string());
    CHECK(readFileBytes(fs::path(file.path.string() + ".bak")) == original);

    RootedTarget reopenedRooted;
    auto& reopened = reopenedRooted.target;
    allowPath(reopenedRooted, file.path);
    replyJson(reopened.invoke("Open", nlohmann::json::array({file.path.string()})));
    const auto labels = replyJson(reopened.invoke("FrameLabels", nlohmann::json::array({0})));
    CHECK(labels.at("labels")[0].value("name", "") == "submenu");
}

TEST_CASE("SwfTarget reports the Radicle message when an ABC edit fails") {
    RootedTarget rooted;
    auto& target = rooted.target;
    TempSwf file(fixtureMovie());
    allowPath(rooted, file.path);
    replyJson(target.invoke("Open", nlohmann::json::array({file.path.string()})));

    const auto bad = replyJson(
        target.invoke("InsertAbcInstruction", nlohmann::json::array({0, 0, 0, "foo"})));
    REQUIRE(bad.contains("error"));
    CHECK_FALSE(bad.at("error").get<std::string>().empty());
}

std::vector<std::byte> latinLabelMovie() {
    std::vector<std::byte> tags;
    const std::vector<std::byte> label{static_cast<std::byte>('s'), static_cast<std::byte>('t'),
                                       static_cast<std::byte>(0xE9), static_cast<std::byte>('r'),
                                       static_cast<std::byte>('t'), static_cast<std::byte>(0)};
    appendTag(tags, 43, label);
    appendTag(tags, 1, {});
    appendTag(tags, 0, {});

    std::vector<std::byte> head;
    head.push_back(static_cast<std::byte>('F'));
    head.push_back(static_cast<std::byte>('W'));
    head.push_back(static_cast<std::byte>('S'));
    head.push_back(static_cast<std::byte>(10));
    appendU32(head, static_cast<std::uint32_t>(8 + 9 + 4 + tags.size()));
    const auto frame = rectBytes(0, 11000, 0, 8000, 15);
    head.insert(head.end(), frame.begin(), frame.end());
    appendU16(head, 0x0C00);
    appendU16(head, 1);
    head.insert(head.end(), tags.begin(), tags.end());
    return head;
}

TEST_CASE("SwfTarget replaces non-UTF-8 bytes when dumping labels") {
    RootedTarget rooted;
    auto& target = rooted.target;
    TempSwf file(latinLabelMovie());
    allowPath(rooted, file.path);
    const auto opened =
        replyJson(target.invoke("Open", nlohmann::json::array({file.path.string()})));
    REQUIRE_FALSE(opened.contains("error"));
    const auto labels = replyJson(target.invoke("FrameLabels", nlohmann::json::array({0})));
    REQUIRE(labels.contains("labels"));
    REQUIRE(labels.at("labels").size() == 1);
    CHECK(labels.at("labels")[0].value("name", "") == "st\xEF\xBF\xBDrt");
}

TEST_CASE("SwfTarget refuses paths outside its roots") {
    RootedTarget rooted;
    auto& target = rooted.target;
    TempSwf file(fixtureMovie());

    // Constraint: an empty root list must refuse every open, like the C# host with no folders set.
    const auto unrooted =
        replyJson(target.invoke("Open", nlohmann::json::array({file.path.string()})));
    CHECK(unrooted.contains("error"));

    *rooted.roots = {file.path.parent_path() / "fo4ide-no-such-root"};
    const auto elsewhere =
        replyJson(target.invoke("Open", nlohmann::json::array({file.path.string()})));
    CHECK(elsewhere.contains("error"));

    const fs::path sub = file.path.parent_path() / "fo4ide-swf-guard-sub";
    std::error_code code;
    fs::create_directories(sub, code);
    *rooted.roots = {sub};
    const auto escaped = replyJson(
        target.invoke("Open", nlohmann::json::array({(sub / ".." / file.path.filename()).string()})));
    CHECK(escaped.contains("error"));
    fs::remove_all(sub, code);

    allowPath(rooted, file.path);
    const auto opened =
        replyJson(target.invoke("Open", nlohmann::json::array({file.path.string()})));
    CHECK_FALSE(opened.contains("error"));
}

// Constraint: an edit colour must parse as hexadecimal in full, with no partial or signed read.
TEST_CASE("SwfTarget refuses an edit colour that is not hexadecimal in full") {
    RootedTarget rooted;
    auto& target = rooted.target;
    TempSwf file(fixtureMovie());
    allowPath(rooted, file.path);
    replyJson(target.invoke("Open", nlohmann::json::array({file.path.string()})));

    for (const char* color : {"12zz", "-1", "1 2"}) {
        const auto refused = replyJson(target.invoke(
            "EditEditText", nlohmann::json::array({0, "edit", color, 0, 400, 0, 200})));
        INFO(color);
        CHECK(refused.contains("error"));
    }
    const auto state = replyJson(target.invoke("State", nlohmann::json::array()));
    CHECK(state.value("undo", 9) == 0);

    const auto accepted = replyJson(target.invoke(
        "EditEditText", nlohmann::json::array({0, "edit", "0xFF00FF00", 0, 400, 0, 200})));
    CHECK(accepted.value("changed", false) == true);
}

// Constraint: roots are read on each Open, so the MO2 instance set by a settings change or --mo2 opens without a restart.
TEST_CASE("SwfTarget reads the MO2 instance root from settings on each Open") {
    const fs::path instance = uniqueDir("fo4ide-swf-mo2");
    const RemoveAll cleanup{instance};
    const fs::path movie = instance / "mods" / "X" / "Interface" / "foo.swf";
    fs::create_directories(movie.parent_path());
    writeBytes(movie, fixtureMovie());

    fo4ide::settings::SharedSettings settings;
    fo4ide::app::SwfTarget target([&settings] { return fo4ide::app::swfRootsFor(settings.snapshot()); });
    const auto refused = replyJson(target.invoke("Open", nlohmann::json::array({movie.string()})));
    CHECK(refused.contains("error"));

    settings.update([&instance](fo4ide::settings::Settings& current) { current.mo2InstancePath = instance; });
    const auto opened = replyJson(target.invoke("Open", nlohmann::json::array({movie.string()})));
    CHECK_FALSE(opened.contains("error"));
}

// Constraint: the configured Fallout 4 folder is itself a root, as ToolPaths.RequireDataOrProjectPath treats Fallout4Path.
TEST_CASE("SwfTarget treats the configured Fallout 4 folder as a root") {
    const fs::path game = uniqueDir("fo4ide-swf-game");
    const RemoveAll cleanup{game};
    const fs::path movie = game / "Interface" / "foo.swf";
    fs::create_directories(movie.parent_path());
    writeBytes(movie, fixtureMovie());

    fo4ide::settings::Settings settings;
    settings.fallout4Path = game;
    const auto roots = fo4ide::app::swfRootsFor(settings);
    REQUIRE(roots.size() == 4u);
    CHECK(roots[1] == game);
    fo4ide::app::SwfTarget target([roots] { return roots; });
    const auto opened = replyJson(target.invoke("Open", nlohmann::json::array({movie.string()})));
    CHECK_FALSE(opened.contains("error"));
}

// Constraint: Save writes the canonical path that Open authorised, not the spelling the caller passed.
TEST_CASE("SwfTarget saves to the canonical path it opened") {
    const fs::path dir = uniqueDir("fo4ide-swf-canonical");
    const RemoveAll cleanup{dir};
    const fs::path movie = dir / "movie.swf";
    writeBytes(movie, fixtureMovie());
    RootedTarget rooted;
    auto& target = rooted.target;
    *rooted.roots = {dir};

    const fs::path dotted = dir / "." / "movie.swf";
    REQUIRE_FALSE(replyJson(target.invoke("Open", nlohmann::json::array({dotted.string()}))).contains("error"));
    const auto saved = replyJson(target.invoke("Save", nlohmann::json::array()));
    CHECK(saved.value("saved", "") == fs::weakly_canonical(movie).string());
}

// Constraint: a refused Open leaves no document open, as the C# host disposes the current document first.
TEST_CASE("SwfTarget closes the open document when a later Open is refused") {
    const fs::path inside = uniqueDir("fo4ide-swf-inside");
    const RemoveAll insideCleanup{inside};
    const fs::path outside = uniqueDir("fo4ide-swf-outside");
    const RemoveAll outsideCleanup{outside};
    writeBytes(inside / "movie.swf", fixtureMovie());
    writeBytes(outside / "stray.swf", fixtureMovie());
    RootedTarget rooted;
    auto& target = rooted.target;
    *rooted.roots = {inside};

    REQUIRE_FALSE(replyJson(target.invoke("Open", nlohmann::json::array({(inside / "movie.swf").string()})))
                      .contains("error"));
    const auto refused = replyJson(target.invoke("Open", nlohmann::json::array({(outside / "stray.swf").string()})));
    CHECK(refused.contains("error"));
    const auto state = replyJson(target.invoke("State", nlohmann::json::array()));
    CHECK(state.contains("error"));
}

// Constraint: an SWF over the body cap is refused before it is read, so a huge file never reaches memory.
TEST_CASE("SwfTarget refuses an SWF over the size limit before reading it") {
    const fs::path dir = uniqueDir("fo4ide-swf-large");
    const RemoveAll cleanup{dir};
    const fs::path large = dir / "large.swf";
    writeBytes(large, {});
    fs::resize_file(large, fo4ide::core::kMaxSwfFileBytes + 1);
    RootedTarget rooted;
    auto& target = rooted.target;
    *rooted.roots = {dir};

    const auto refused = replyJson(target.invoke("Open", nlohmann::json::array({large.string()})));
    REQUIRE(refused.contains("error"));
    CHECK(refused.at("error").get<std::string>().find("limit") != std::string::npos);
}

// Constraint: a non-integer, out-of-range, negative or wrong-typed argument is refused before dispatch, so no edit runs on a defaulted index.
TEST_CASE("SwfTarget refuses malformed arguments without editing the document") {
    TempSwf file(fixtureMovie());
    RootedTarget rooted;
    auto& target = rooted.target;
    allowPath(rooted, file.path);
    replyJson(target.invoke("Open", nlohmann::json::array({file.path.string()})));

    const std::vector<std::pair<std::string, nlohmann::json>> cases{
        {"SetDepth", nlohmann::json::array({2, 70000})},
        {"SetDepth", nlohmann::json::array({2, 1e30})},
        {"SetDepth", nlohmann::json::array({2, 9.5})},
        {"SetDepth", nlohmann::json::array({2, "9"})},
        {"SetDepth", nlohmann::json::array({-1, 9})},
        {"SetDepth", nlohmann::json::array({2})},
        {"RenameLabel", nlohmann::json::array({0, "0", "menu"})},
        {"RenameLabel", nlohmann::json::array({0, 0, 42})},
        {"InsertKeyframe", nlohmann::json::array({6, 1.5})},
        {"EditEditText", nlohmann::json::array({0, 5, "", 0, 400, 0, 200})},
    };
    for (const auto& [method, args] : cases) {
        INFO(method << " " << args.dump());
        CHECK(replyJson(target.invoke(method, args)).contains("error"));
    }
    const auto state = replyJson(target.invoke("State", nlohmann::json::array()));
    CHECK(state.value("undo", 9) == 0);
    CHECK(state.value("dirty", true) == false);
}

// Constraint: an edit whose written movie cannot reopen is refused, and the document keeps its previous labels.
TEST_CASE("SwfTarget refuses an edit whose result cannot reopen and keeps the previous movie", "[slow]") {
    const fs::path dir = uniqueDir("fo4ide-swf-cap");
    const RemoveAll cleanup{dir};
    const fs::path movie = dir / "cap.swf";
    writeBytes(movie, tagCapMovie());
    RootedTarget rooted;
    auto& target = rooted.target;
    *rooted.roots = {dir};

    REQUIRE_FALSE(replyJson(target.invoke("Open", nlohmann::json::array({movie.string()}))).contains("error"));
    const auto added = replyJson(target.invoke("AddLabel", nlohmann::json::array({0, "cap", 0})));
    CHECK(added.contains("error"));
    const auto labels = replyJson(target.invoke("FrameLabels", nlohmann::json::array({0})));
    REQUIRE(labels.contains("labels"));
    CHECK(labels.at("labels").empty());
    const auto state = replyJson(target.invoke("State", nlohmann::json::array()));
    CHECK(state.value("undo", 9) == 0);
}

// Constraint: a Redo whose result cannot reopen steps back, so State, FrameLabels and Save keep the movie before it.
TEST_CASE("SwfTarget steps back a Redo whose result cannot reopen, so Save writes a movie that reopens", "[slow]") {
    const fs::path dir = uniqueDir("fo4ide-swf-redo");
    const RemoveAll cleanup{dir};
    const fs::path movie = dir / "cap.swf";
    writeBytes(movie, tagCapMovie());
    RootedTarget rooted;
    auto& target = rooted.target;
    *rooted.roots = {dir};

    REQUIRE_FALSE(replyJson(target.invoke("Open", nlohmann::json::array({movie.string()}))).contains("error"));
    CHECK(replyJson(target.invoke("AddLabel", nlohmann::json::array({0, "cap", 0}))).contains("error"));
    CHECK(replyJson(target.invoke("Redo", nlohmann::json::array())).contains("error"));
    const auto state = replyJson(target.invoke("State", nlohmann::json::array()));
    CHECK(state.value("undo", 9) == 0);
    CHECK(state.value("redo", 9) == 1);
    CHECK(state.value("dirty", true) == false);
    const auto labels = replyJson(target.invoke("FrameLabels", nlohmann::json::array({0})));
    REQUIRE(labels.contains("labels"));
    CHECK(labels.at("labels").empty());

    const auto saved = replyJson(target.invoke("Save", nlohmann::json::array()));
    REQUIRE(saved.contains("saved"));
    RootedTarget reopenedRooted;
    allowPath(reopenedRooted, movie);
    const auto reopened =
        replyJson(reopenedRooted.target.invoke("Open", nlohmann::json::array({movie.string()})));
    CHECK_FALSE(reopened.contains("error"));
}

// Constraint: an unknown method is named as unknown even with no document open, so a typo is not reported as a missing document.
TEST_CASE("SwfTarget names an unknown method even when no document is open") {
    RootedTarget rooted;
    const auto reply = rooted.target.invoke("NoSuchMethod", nlohmann::json::array());
    REQUIRE_FALSE(reply.has_value());
    CHECK(reply.error().type == "UnknownMethod");
}

// Constraint: a first save whose backup copy fails is refused, and the file on disk is left as it was.
TEST_CASE("SwfTarget does not report a save when the first-save backup cannot be copied") {
#ifdef _WIN32
    SKIP("the dangling backup needs symlink support, and no Windows failure of the copy alone was verified");
#else
    const fs::path dir = uniqueDir("fo4ide-swf-backup");
    const RemoveAll cleanup{dir};
    const fs::path swfPath = dir / "movie.swf";
    const auto original = fixtureMovie();
    writeBytes(swfPath, original);
    RootedTarget rooted;
    auto& target = rooted.target;
    *rooted.roots = {dir};
    REQUIRE_FALSE(replyJson(target.invoke("Open", nlohmann::json::array({swfPath.string()}))).contains("error"));

    fs::create_symlink(dir / "missing" / "movie.swf.bak", dir / "movie.swf.bak");
    const auto saved = replyJson(target.invoke("Save", nlohmann::json::array()));
    CHECK(saved.contains("error"));
    CHECK_FALSE(saved.contains("saved"));
    CHECK(readFileBytes(swfPath) == original);
#endif
}

// Constraint: a save whose atomic write fails is refused, and no save is reported.
TEST_CASE("SwfTarget does not report a save when the atomic write fails") {
    const fs::path dir = uniqueDir("fo4ide-swf-write");
    const RemoveAll cleanup{dir};
    const fs::path swfPath = dir / "movie.swf";
    writeBytes(swfPath, fixtureMovie());
    writeBytes(dir / "movie.swf.bak", fixtureMovie());
    RootedTarget rooted;
    auto& target = rooted.target;
    *rooted.roots = {dir};
    REQUIRE_FALSE(replyJson(target.invoke("Open", nlohmann::json::array({swfPath.string()}))).contains("error"));

    fs::remove(swfPath);
    fs::create_directory(swfPath);
    const auto saved = replyJson(target.invoke("Save", nlohmann::json::array()));
    CHECK(saved.contains("error"));
    CHECK_FALSE(saved.contains("saved"));
    CHECK(fs::is_directory(swfPath));
}
