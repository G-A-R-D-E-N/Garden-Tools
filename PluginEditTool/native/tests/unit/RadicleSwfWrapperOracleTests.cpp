#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "SwfTargetTestSupport.hpp"

namespace {

using namespace swft_target_test;

using Bytes = std::vector<std::byte>;

Bytes bytesOf(std::initializer_list<unsigned> values) {
    Bytes out;
    for (const auto value : values) out.push_back(static_cast<std::byte>(value));
    return out;
}

Bytes shapeTag() {
    Bytes payload;
    appendU16(payload, 5);
    const auto bounds = rectBytes(0, 1000, 0, 1000, 15);
    payload.insert(payload.end(), bounds.begin(), bounds.end());
    for (int index = 0; index < 4; ++index) payload.push_back(std::byte{0});
    Bytes out;
    appendTag(out, 2, payload);
    return out;
}

Bytes bitmapTag() {
    Bytes payload;
    appendU16(payload, 20);
    payload.push_back(std::byte{5});
    appendU16(payload, 2);
    appendU16(payload, 2);
    const auto zlib = bytesOf({0x78, 0x01, 0x01, 0x10, 0x00, 0xEF, 0xFF});
    payload.insert(payload.end(), zlib.begin(), zlib.end());
    payload.insert(payload.end(), 16, std::byte{0});
    const auto adler = bytesOf({0x00, 0x10, 0x00, 0x01});
    payload.insert(payload.end(), adler.begin(), adler.end());
    Bytes out;
    appendTag(out, 36, payload);
    return out;
}

Bytes jpegTag() {
    Bytes payload;
    appendU16(payload, 23);
    const auto jpeg = bytesOf({0xFF, 0xD8, 0xFF, 0xD9});
    payload.insert(payload.end(), jpeg.begin(), jpeg.end());
    Bytes out;
    appendTag(out, 21, payload);
    return out;
}

Bytes abcTag() {
    Bytes abc;
    appendU32(abc, 0);
    appendZ(abc, "edit");
    for (const unsigned byte : {0x10u, 0x00u, 0x2Eu, 0x00u, 0x00u, 0x00u, 0x00u, 0x02u, 0x03u, 0x46u, 0x6Fu,
                                    0x6Fu, 0x00u, 0x00u, 0x00u, 0x01u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
                                    0x00u, 0x01u, 0x00u, 0x01u, 0x01u, 0x00u, 0x00u, 0x02u, 0x26u, 0x48u, 0x00u,
                                    0x00u}) {
        abc.push_back(static_cast<std::byte>(byte));
    }
    Bytes out;
    appendTag(out, 82, abc);
    return out;
}

struct Parts {
    bool abc = false;
    bool shape = false;
    bool bitmap = false;
    bool jpeg = false;
};

Bytes movieWith(const Parts& parts) {
    Bytes editText;
    appendU16(editText, 11);
    const auto bounds = rectBytes(0, 400, 0, 200, 12);
    editText.insert(editText.end(), bounds.begin(), bounds.end());
    editText.push_back(std::byte{0});
    editText.push_back(std::byte{0});
    appendZ(editText, "variable");

    const auto place = bytesOf({0x06, 0x01, 0x00, 0x0B, 0x00, 0x00});
    Bytes sprite;
    appendU16(sprite, 14);
    appendU16(sprite, 1);
    Bytes innerLabel;
    appendZ(innerLabel, "inner");
    appendTag(sprite, 43, innerLabel);
    appendTag(sprite, 26, bytesOf({0x06, 0x02, 0x00, 0x0B, 0x00, 0x00}));
    appendTag(sprite, 1, {});
    appendTag(sprite, 0, {});

    Bytes tags;
    const auto append = [&](const Bytes& more) { tags.insert(tags.end(), more.begin(), more.end()); };
    if (parts.shape) append(shapeTag());
    if (parts.bitmap) append(bitmapTag());
    if (parts.jpeg) append(jpegTag());
    appendTag(tags, 37, editText);
    Bytes startLabel;
    appendZ(startLabel, "start");
    appendTag(tags, 43, startLabel);
    appendTag(tags, 26, place);
    appendTag(tags, 1, {});
    appendTag(tags, 39, sprite);
    if (parts.abc) append(abcTag());
    appendTag(tags, 0, {});

    Bytes head = bytesOf({'F', 'W', 'S', 10});
    appendU32(head, static_cast<std::uint32_t>(8 + 9 + 4 + tags.size()));
    const auto frame = rectBytes(0, 11000, 0, 8000, 15);
    head.insert(head.end(), frame.begin(), frame.end());
    appendU16(head, 0x0C00);
    appendU16(head, 1);
    head.insert(head.end(), tags.begin(), tags.end());
    return head;
}

struct OpenMovie {
    RootedTarget rooted;
    TempSwf file;
    explicit OpenMovie(const Parts& parts) : file(movieWith(parts)) {
        allowPath(rooted, file.path);
        const auto opened = replyJson(rooted.target.invoke("Open", nlohmann::json::array({file.path.string()})));
        REQUIRE_FALSE(opened.contains("error"));
    }

    nlohmann::json call(std::string_view method, const nlohmann::json& args = nlohmann::json::array()) {
        return replyJson(rooted.target.invoke(method, args));
    }

    int tagIndexOf(const std::string& kind) {
        const auto response = call("Characters");
        for (const auto& item : response.at("characters")) {
            if (item.at("kind") == kind) return item.at("tagIndex").get<int>();
        }
        FAIL("no character of kind " << kind << "; response: " << response.dump());
    }
};

}  // namespace

TEST_CASE("Oracle SWF ABC RPCs return the fields the panel reads and the refusal shape") {
    OpenMovie movie({.abc = true});

    const auto blocks = movie.call("AbcBlocks").at("blocks");
    REQUIRE(blocks.size() >= 1);
    const auto& block = blocks[0];
    CHECK(block.at("abc") == 0);
    CHECK(block.at("name").is_string());
    CHECK(block.at("classes").is_array());
    CHECK(block.at("scripts").is_array());
    const nlohmann::json* withBody = nullptr;
    for (const auto& method : block.at("methods")) {
        CHECK(method.at("name").is_string());
        CHECK(method.at("returnType").is_string());
        if (method.at("hasBody") == true && withBody == nullptr) withBody = &method;
    }
    REQUIRE(withBody != nullptr);

    const auto listing = movie.call("Disassemble", nlohmann::json::array({0, withBody->at("index")}));
    REQUIRE(listing.contains("text"));
    CHECK_FALSE(listing.at("text").get<std::string>().empty());

    const auto refusal = movie.call("Disassemble", nlohmann::json::array({0, 9999}));
    CHECK_FALSE(refusal.at("error").get<std::string>().empty());
    CHECK(refusal.contains("status"));
}

TEST_CASE("Oracle SWF AbcBlocks answers the no document refusal before any Open") {
    RootedTarget rooted;
    const auto reply = replyJson(rooted.target.invoke("AbcBlocks", nlohmann::json::array()));
    CHECK(reply.at("error") == "no SWF document is open");
}

TEST_CASE("Oracle SWF Timeline resolves a timeline index to its sprite character id") {
    OpenMovie movie(Parts{});
    CHECK(movie.call("Timeline", nlohmann::json::array({1})).at("characterId") == 14);
    CHECK(movie.call("Timeline", nlohmann::json::array({0})).at("characterId") == 0);
}

TEST_CASE("Oracle SWF RemoveLabel reports the change and counts rename plus remove as two undo steps") {
    OpenMovie movie(Parts{});
    CHECK(movie.call("RenameLabel", nlohmann::json::array({0, 0, "menu"})).at("changed") == true);
    CHECK(movie.call("RemoveLabel", nlohmann::json::array({0, 0})).at("changed") == true);
    CHECK(movie.call("FrameLabels", nlohmann::json::array({0})).at("labels").empty());
    CHECK(movie.call("State").at("undo") == 2);
}

TEST_CASE("Oracle SWF ShapeSvg returns the svg field for a shape and refuses non shapes and bad ids") {
    OpenMovie movie({.shape = true});

    const auto svg = movie.call("ShapeSvg", nlohmann::json::array({5}));
    REQUIRE(svg.contains("svg"));
    CHECK(svg.at("svg").get<std::string>().rfind("<svg", 0) == 0);

    const auto nonShape = movie.call("ShapeSvg", nlohmann::json::array({11}));
    CHECK_FALSE(nonShape.at("error").get<std::string>().empty());
    CHECK_FALSE(nonShape.contains("svg"));

    for (const int id : {70000, -1}) {
        const auto outOfRange = movie.call("ShapeSvg", nlohmann::json::array({id}));
        CHECK(outOfRange.at("error").get<std::string>().find("0..65535") != std::string::npos);
        CHECK_FALSE(outOfRange.contains("svg"));
    }
}

TEST_CASE("Oracle SWF FrameSvg replies a well formed svg for any frame and refuses a negative one") {
    OpenMovie movie({.shape = true});

    const auto root = movie.call("FrameSvg", nlohmann::json::array({0}));
    REQUIRE(root.contains("svg"));
    CHECK(root.at("svg").get<std::string>().rfind("<svg", 0) == 0);

    const auto beyond = movie.call("FrameSvg", nlohmann::json::array({999}));
    CHECK_FALSE(beyond.contains("error"));
    CHECK(beyond.at("svg").get<std::string>().rfind("<svg", 0) == 0);

    const auto negative = movie.call("FrameSvg", nlohmann::json::array({-1}));
    CHECK(negative.at("error").get<std::string>().find("must not be negative") != std::string::npos);
    CHECK_FALSE(negative.contains("svg"));
}

TEST_CASE("Oracle SWF BitmapPng returns base64 PNG bytes for a bitmap and refuses an edit text") {
    OpenMovie movie({.bitmap = true});

    const auto reply = movie.call("BitmapPng", nlohmann::json::array({movie.tagIndexOf("Bitmap")}));
    REQUIRE(reply.contains("png"));
    CHECK_FALSE(reply.contains("error"));
    const auto png = reply.at("png").get<std::string>();
    CHECK(png.rfind("iVBORw0KGgo", 0) == 0);

    const auto refusal = movie.call("BitmapPng", nlohmann::json::array({movie.tagIndexOf("EditText")}));
    CHECK_FALSE(refusal.at("error").get<std::string>().empty());
    CHECK_FALSE(refusal.contains("png"));
}

TEST_CASE("Oracle SWF BitmapJpeg returns the base64 JPEG stream for a JPEG bitmap and refuses an edit text") {
    OpenMovie movie({.jpeg = true});

    const auto reply = movie.call("BitmapJpeg", nlohmann::json::array({movie.tagIndexOf("Bitmap")}));
    REQUIRE(reply.contains("jpeg"));
    CHECK_FALSE(reply.contains("error"));
    CHECK(reply.at("jpeg") == "/9j/2Q==");

    const auto refusal = movie.call("BitmapJpeg", nlohmann::json::array({movie.tagIndexOf("EditText")}));
    CHECK_FALSE(refusal.at("error").get<std::string>().empty());
    CHECK_FALSE(refusal.contains("jpeg"));
}
