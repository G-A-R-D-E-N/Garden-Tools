#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <fo4ide/app/SwfTarget.hpp>

namespace {

namespace fs = std::filesystem;

std::vector<std::byte> rectBytes(int xMin, int xMax, int yMin, int yMax, int nbits) {
    std::vector<std::byte> out;
    std::uint32_t pending = 0;
    int filled = 0;
    const auto push = [&](std::uint32_t value, int count) {
        for (int index = 0; index < count; ++index) {
            pending = (pending << 1) | ((value >> (count - 1 - index)) & 1u);
            if (++filled == 8) {
                out.push_back(static_cast<std::byte>(pending));
                pending = 0;
                filled = 0;
            }
        }
    };
    const std::uint32_t mask = (nbits >= 32) ? 0xFFFFFFFFu : ((1u << nbits) - 1u);
    push(static_cast<std::uint32_t>(nbits), 5);
    push(static_cast<std::uint32_t>(xMin) & mask, nbits);
    push(static_cast<std::uint32_t>(xMax) & mask, nbits);
    push(static_cast<std::uint32_t>(yMin) & mask, nbits);
    push(static_cast<std::uint32_t>(yMax) & mask, nbits);
    if (filled != 0) push(0, 8 - filled);
    return out;
}

void appendU16(std::vector<std::byte>& out, int value) {
    out.push_back(static_cast<std::byte>(value & 0xFF));
    out.push_back(static_cast<std::byte>((value >> 8) & 0xFF));
}

void appendU32(std::vector<std::byte>& out, std::uint32_t value) {
    for (int part = 0; part < 4; ++part)
        out.push_back(static_cast<std::byte>((value >> (8 * part)) & 0xFFu));
}

void appendZ(std::vector<std::byte>& out, const char* text) {
    for (const char* cursor = text; *cursor != '\0'; ++cursor)
        out.push_back(static_cast<std::byte>(*cursor));
    out.push_back(static_cast<std::byte>(0));
}

void appendTag(std::vector<std::byte>& out, int code, const std::vector<std::byte>& payload) {
    if (payload.size() >= 0x3F) {
        const auto header = static_cast<std::uint16_t>(((code & 0x3FF) << 6) | 0x3F);
        appendU16(out, header);
        appendU32(out, static_cast<std::uint32_t>(payload.size()));
    } else {
        const auto header = static_cast<std::uint16_t>(((code & 0x3FF) << 6) |
                                                       static_cast<int>(payload.size()));
        appendU16(out, header);
    }
    out.insert(out.end(), payload.begin(), payload.end());
}

// Constraint: the bytes must match SwfFixture.Movie(withAbc:true), so the native target reads the movie the C# tests own.
std::vector<std::byte> fixtureMovie() {
    std::vector<std::byte> editText;
    appendU16(editText, 11);
    const auto bounds = rectBytes(0, 400, 0, 200, 12);
    editText.insert(editText.end(), bounds.begin(), bounds.end());
    editText.push_back(static_cast<std::byte>(0));
    editText.push_back(static_cast<std::byte>(0));
    appendZ(editText, "variable");

    const std::vector<std::byte> place{static_cast<std::byte>(0x06), static_cast<std::byte>(0x01),
                                       static_cast<std::byte>(0x00), static_cast<std::byte>(0x0B),
                                       static_cast<std::byte>(0x00), static_cast<std::byte>(0x00)};

    std::vector<std::byte> sprite;
    appendU16(sprite, 14);
    appendU16(sprite, 1);
    std::vector<std::byte> innerLabel;
    appendZ(innerLabel, "inner");
    appendTag(sprite, 43, innerLabel);
    const std::vector<std::byte> spritePlace{
        static_cast<std::byte>(0x06), static_cast<std::byte>(0x02), static_cast<std::byte>(0x00),
        static_cast<std::byte>(0x0B), static_cast<std::byte>(0x00), static_cast<std::byte>(0x00)};
    appendTag(sprite, 26, spritePlace);
    appendTag(sprite, 1, {});
    appendTag(sprite, 0, {});

    std::vector<std::byte> tags;
    appendTag(tags, 37, editText);
    std::vector<std::byte> startLabel;
    appendZ(startLabel, "start");
    appendTag(tags, 43, startLabel);
    appendTag(tags, 26, place);
    appendTag(tags, 1, {});
    appendTag(tags, 39, sprite);

    std::vector<std::byte> abc;
    appendU32(abc, 0);
    appendZ(abc, "edit");
    const std::uint8_t abcBody[] = {0x10, 0x00, 0x2E, 0x00, 0x00, 0x00, 0x00, 0x02, 0x03, 0x46, 0x6F,
                                    0x6F, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                    0x00, 0x01, 0x00, 0x01, 0x01, 0x00, 0x00, 0x02, 0x26, 0x48, 0x00,
                                    0x00};
    for (const auto byte : abcBody) abc.push_back(static_cast<std::byte>(byte));
    appendTag(tags, 82, abc);
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

nlohmann::json replyJson(const fo4ide::Result<nlohmann::json>& result) {
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    return nlohmann::json::parse(result->get<std::string>());
}

struct TempSwf {
    fs::path path;
    explicit TempSwf(const std::vector<std::byte>& bytes) {
        static int counter = 0;
        path = fs::temp_directory_path() /
               ("fo4ide-swf-target-test-" + std::to_string(++counter) + ".swf");
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        stream.close();
        REQUIRE(fs::exists(path));
    }
    ~TempSwf() {
        std::error_code code;
        fs::remove(path, code);
    }
};

}  // namespace

TEST_CASE("SwfTarget opens a movie and snapshots the document") {
    fo4ide::app::SwfTarget target;
    TempSwf file(fixtureMovie());

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
    fo4ide::app::SwfTarget target;
    TempSwf file(fixtureMovie());
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
    fo4ide::app::SwfTarget target;
    TempSwf file(fixtureMovie());
    replyJson(target.invoke("Open", nlohmann::json::array({file.path.string()})));

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

    fo4ide::app::SwfTarget reopened;
    replyJson(reopened.invoke("Open", nlohmann::json::array({file.path.string()})));
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
    fo4ide::app::SwfTarget target;
    const auto state = replyJson(target.invoke("State", nlohmann::json::array()));
    CHECK(state.contains("error"));
    const auto saved = replyJson(target.invoke("Save", nlohmann::json::array()));
    CHECK(saved.contains("error"));
}

// Constraint: flat tag 2 is the root PlaceObject2 at depth 1, so moving it must rewrite the depth and undo must restore it.
TEST_CASE("SwfTarget moves a root placement depth with undo") {
    fo4ide::app::SwfTarget target;
    TempSwf file(fixtureMovie());
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
    fo4ide::app::SwfTarget target;
    TempSwf file(fixtureMovie());
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

TEST_CASE("SwfTarget inserts a placement keyframe with undo") {
    fo4ide::app::SwfTarget target;
    TempSwf file(fixtureMovie());
    replyJson(target.invoke("Open", nlohmann::json::array({file.path.string()})));

    // Requires a sprite child: the root placement already authors its matrix.
    const auto inserted =
        replyJson(target.invoke("InsertKeyframe", nlohmann::json::array({6, 0})));
    CHECK(inserted.value("changed", false) == true);

    const auto undone = replyJson(target.invoke("Undo", nlohmann::json::array()));
    CHECK(undone.value("undo", 9) == 0);
    const auto matrix =
        replyJson(target.invoke("PlacementMatrix", nlohmann::json::array({6})));
    CHECK(matrix.value("hasScale", true) == false);

    const auto refusal =
        replyJson(target.invoke("InsertKeyframe", nlohmann::json::array({0, 0})));
    CHECK(refusal.contains("error"));
    CHECK(refusal.contains("status"));
}
