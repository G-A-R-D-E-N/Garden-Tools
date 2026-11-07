#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <fo4ide/assets/NifService.hpp>

#include "../helpers/NifFixture.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / ("fo4ide_nif_extra_test_" + std::to_string(id));
}

struct Cleanup {
    std::filesystem::path root;
    ~Cleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

void touch(const std::filesystem::path& path, std::string_view contents = {}) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    stream << contents;
}

std::string fileBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

}

// Detects: in-process geometry dropping the effect contract; effect meshes must not fall back to the basic material.
TEST_CASE("NifService geometry emits the effect shader contract only for the effect shape") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "effect.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::effectNif(fo4ide::test::effectShaderBlock(1U, 0x48U, "textures\\fx\\source.dds")));

    fo4ide::assets::NifService service;
    auto result = service.geometry(nif);

    REQUIRE(result.has_value());
    const auto parsed = nlohmann::json::parse(*result);
    REQUIRE(parsed.at("shapes").size() == 2);
    const auto& effectShape = parsed.at("shapes")[0];
    CHECK(effectShape.at("effectShader") == true);
    const auto& effect = effectShape.at("effect");
    CHECK(effect.at("baseColor") == nlohmann::json::array({0.25, 0.5, 0.75, 1.0}));
    CHECK(effect.at("baseColorScale") == 2.0);
    CHECK(effect.at("emissiveColor") == nlohmann::json::array({0.25, 0.5, 0.75}));
    CHECK(effect.at("emissiveMultiple") == 2.0);
    CHECK(effect.at("useFalloff") == true);
    CHECK(effect.at("vertexAlpha") == true);
    CHECK(effect.at("falloffStartAngle") == 0.75);
    CHECK(effect.at("falloffStopAngle") == 0.5);
    CHECK(effect.at("falloffStartOpacity") == 0.125);
    CHECK(effect.at("falloffStopOpacity") == 0.0625);
    CHECK(effect.at("softFalloffDepth") == 3.0);
    CHECK(effect.at("alphaBlend") == true);
    CHECK(effect.at("alphaSrcBlend") == 6);
    CHECK(effect.at("alphaDstBlend") == 0);
    REQUIRE(effectShape.at("textures").size() == 1);
    CHECK(effectShape.at("textures")[0].at("slot") == 0);
    CHECK(effectShape.at("textures")[0].at("path") == "textures\\fx\\source.dds");

    const auto& lighting = parsed.at("shapes")[1];
    CHECK_FALSE(lighting.contains("effectShader"));
    CHECK_FALSE(lighting.contains("effect"));
}

// Detects: a wrong flags2 bit or colour channel order; effect meshes must keep their tint and colours.
TEST_CASE("NifService geometry passes vertex colors of an effect shape whose shader flag asks for them") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "colored.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::coloredEffectNif());

    fo4ide::assets::NifService service;
    auto result = service.geometry(nif);

    REQUIRE(result.has_value());
    const auto parsed = nlohmann::json::parse(*result);
    REQUIRE(parsed.at("shapes").size() == 1);
    const auto& colors = parsed.at("shapes")[0].at("vertexColors");
    REQUIRE(colors.size() == 12);
    CHECK(colors[0] == 1.0);
    CHECK(colors[1] == 0.0);
    CHECK(colors[2] == 1.0);
    CHECK(colors[3] == 0.0);
}

// Detects: one malformed effect shader taking down the NIF; the rest of its geometry must still load.
TEST_CASE("NifService geometry keeps an effect shape with a malformed shader on its basic material") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "bad_effect.nif";
    auto truncated = fo4ide::test::effectShaderBlock(1U, 0x48U, "textures\\fx\\source.dds");
    truncated.resize(truncated.size() - 2);
    fo4ide::test::writeBytes(nif, fo4ide::test::effectNif(truncated));

    fo4ide::assets::NifService service;
    auto result = service.geometry(nif);

    REQUIRE(result.has_value());
    const auto parsed = nlohmann::json::parse(*result);
    REQUIRE(parsed.at("shapes").size() == 2);
    const auto& shape = parsed.at("shapes")[0];
    CHECK_FALSE(shape.contains("effectShader"));
    CHECK_FALSE(shape.contains("effect"));
    CHECK(shape.at("textures").empty());
}

// Detects: inspect losing data; the BSX value and connect points must be reported.
TEST_CASE("NifService inspect reports BSX flags, connect points and connect children in-process") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "connect.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::connectNif());

    fo4ide::assets::NifService service;
    auto result = service.inspect(nif);

    REQUIRE(result.has_value());
    const auto parsed = nlohmann::json::parse(*result);
    CHECK(parsed.at("hasBSX") == true);
    CHECK(parsed.at("bsxFlags") == 0x80);
    REQUIRE(parsed.at("connectPoints").size() == 1);
    const auto& point = parsed.at("connectPoints")[0];
    CHECK(point.at("parent") == "Bip01");
    CHECK(point.at("name") == "Weapon");
    CHECK(point.at("rotation") == nlohmann::json::array({0.5, 0.25, 0.125, 0.0625}));
    CHECK(point.at("translation") == nlohmann::json::array({1.0, 2.0, 3.0}));
    CHECK(point.at("scale") == 2.0);
    REQUIRE(parsed.at("connectChildren").size() == 1);
    CHECK(parsed.at("connectChildren")[0].at("skinned") == true);
    CHECK(parsed.at("connectChildren")[0].at("names") == nlohmann::json::array({"Hand"}));
}

// Detects: inspect losing shape data; the effect shader and alpha flags of a shape must be named.
TEST_CASE("NifService inspect reports the effect name and alpha flags of an effect shape in-process") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "effect.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::effectNif(fo4ide::test::effectShaderBlock(1U, 0x48U, "textures\\fx\\source.dds")));

    fo4ide::assets::NifService service;
    auto result = service.inspect(nif);

    REQUIRE(result.has_value());
    const auto parsed = nlohmann::json::parse(*result);
    REQUIRE(parsed.at("shapes").size() == 2);
    const auto& effectShape = parsed.at("shapes")[0];
    CHECK(effectShape.at("effectShader") == true);
    CHECK(effectShape.at("effectName") == "Fx");
    CHECK(effectShape.at("alphaFlags") == 0x0D);
    CHECK(effectShape.at("alphaThreshold") == 0);
    CHECK_FALSE(parsed.at("shapes")[1].contains("alphaFlags"));
}

// Detects: inspect losing armor data; the segment count and SSF path must be reported.
TEST_CASE("NifService inspect counts segments and sub-segments of a BSSubIndexTriShape in-process") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "subindex.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::subIndexNif());

    fo4ide::assets::NifService service;
    auto result = service.inspect(nif);

    REQUIRE(result.has_value());
    const auto parsed = nlohmann::json::parse(*result);
    REQUIRE(parsed.at("shapes").size() == 1);
    const auto& shape = parsed.at("shapes")[0];
    CHECK(shape.at("segments") == 1);
    CHECK(shape.at("subSegments") == 2);
    CHECK(shape.at("ssfFile") == "Seg.ssf");
}

// Detects: a segment read that drops the SSF sidecar or its shape entry; the sidecar must be found and listed beside the NIF.
TEST_CASE("NifService segments reads the segment table and its SSF sidecar beside the NIF") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "seg.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::subIndexNif());
    touch(root / "Seg.ssf", R"({"Tri":{"BaseBoneName":"Generic","DeltaBones":[{"BoneDeltaList":[3],"BoneName":"Bip01 Spine"}],"uiNumDeltas":1}})");

    fo4ide::assets::NifService service;
    auto result = service.segments(nif, 0);

    REQUIRE(result.has_value());
    const auto parsed = nlohmann::json::parse(*result);
    CHECK(parsed.at("name") == "Tri");
    CHECK(parsed.at("totalSegments") == 3);
    CHECK(parsed.at("segments").size() == 1);
    CHECK(parsed.at("segments")[0].at("subSegments").size() == 2);
    CHECK(parsed.at("segmentStarts") == nlohmann::json::array({0}));
    CHECK(parsed.at("sidecar").at("found") == true);
    CHECK(parsed.at("sidecar").at("listed") == true);
    CHECK(parsed.at("sidecar").at("baseBone") == "Generic");
    CHECK(parsed.at("sidecar").at("deltas")[0].at("bone") == "Bip01 Spine");
}

// Detects: a malformed sibling .ssf failing the whole segment read, so a fine NIF's table could not be opened; the table must load and the sidecar must carry the error.
TEST_CASE("NifService segments keeps the table when its SSF sidecar is malformed") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "seg.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::subIndexNif());
    touch(root / "Seg.ssf", "{");

    fo4ide::assets::NifService service;
    auto result = service.segments(nif, 0);

    REQUIRE(result.has_value());
    const auto parsed = nlohmann::json::parse(*result);
    CHECK(parsed.at("totalSegments") == 3);
    CHECK(parsed.at("sidecar").at("found") == true);
    CHECK(parsed.at("sidecar").at("listed") == false);
    CHECK_FALSE(parsed.at("sidecar").at("error").get<std::string>().empty());
}

// Detects: a segment edit that is rejected, lands at the wrong offset, or is not read back; the edited field must survive a write and read cycle.
TEST_CASE("NifService applies a segment edit in-process and reads the new table back") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "seg.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::subIndexNif());
    const auto before = fileBytes(nif);

    fo4ide::assets::NifService service;
    auto table = nlohmann::json::parse(*service.segments(nif, 0));
    table["primitives"] = 9;
    table["segments"][0]["subSegments"][1]["unused"] = 7;
    auto result = service.applyEdits(nif, nlohmann::json::array({{{"id", 0}, {"key", "segments"}, {"value", table}}}));

    REQUIRE(result.has_value());
    CHECK(result->find("set OK (in-process)") != std::string::npos);
    CHECK(fileBytes(nif) != before);

    const auto back = nlohmann::json::parse(*service.segments(nif, 0));
    CHECK(back.at("primitives") == 9);
    CHECK(back.at("segments")[0].at("subSegments")[1].at("unused") == 7);
    CHECK(back.at("totalSegments") == 3);
}

// Detects: a segment read or write on a shape without a segment table; the read must name the block type and the write must be refused with the NIF byte-identical.
TEST_CASE("NifService refuses segment reads and edits on a shape without a segment table") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "tri.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::triangleNif());
    const auto before = fileBytes(nif);

    fo4ide::assets::NifService service;
    auto read = service.segments(nif, 0);
    REQUIRE_FALSE(read.has_value());
    CHECK(read.error().message.find("is not a BSSubIndexTriShape") != std::string::npos);

    const nlohmann::json table{{"primitives", 1},
                               {"totalSegments", 0},
                               {"segments", nlohmann::json::array()},
                               {"segmentStarts", nlohmann::json::array()},
                               {"userData", nlohmann::json::array()},
                               {"ssfFile", ""}};
    auto result = service.applyEdits(nif, nlohmann::json::array({{{"id", 0}, {"key", "segments"}, {"value", table}}}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("key 'segments' needs a BSSubIndexTriShape") != std::string::npos);
    CHECK(fileBytes(nif) == before);
}

// Detects: a malformed segment value or a non-string key that is written or throws instead of being refused; each must return NifEditFailed and keep the NIF byte-identical.
TEST_CASE("NifService refuses malformed segment edits with NifEditFailed and keeps the NIF") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "seg.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::subIndexNif());
    const auto before = fileBytes(nif);

    fo4ide::assets::NifService service;
    const auto table = nlohmann::json::parse(*service.segments(nif, 0));
    const auto refused = [&](const nlohmann::json& edits) {
        auto result = service.applyEdits(nif, edits);
        return !result.has_value() && result.error().type == "NifEditFailed";
    };
    auto negative = table;
    negative["primitives"] = -1;
    auto text = table;
    text["primitives"] = "9";

    CHECK(refused(nlohmann::json::array({{{"id", 0}, {"key", "segments"}, {"value", negative}}})));
    CHECK(refused(nlohmann::json::array({{{"id", 0}, {"key", "segments"}, {"value", text}}})));
    CHECK(refused(nlohmann::json::array({{{"id", 0}, {"key", 5}, {"value", "x"}}})));
    CHECK(fileBytes(nif) == before);
}

// Detects: a non-finite segment cut offset written into the shared data; the edit must be refused and the NIF must stay byte-identical.
TEST_CASE("NifService refuses a non-finite segment cut offset and keeps the NIF") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "seg.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::subIndexNif());
    const auto before = fileBytes(nif);

    fo4ide::assets::NifService service;
    auto table = nlohmann::json::parse(*service.segments(nif, 0));
    table["userData"] = nlohmann::json::array({{{"userIndex", 0}, {"boneId", 1},
                                                {"cutOffsets", nlohmann::json::array({std::numeric_limits<float>::infinity()})}}});
    auto result = service.applyEdits(nif, nlohmann::json::array({{{"id", 0}, {"key", "segments"}, {"value", table}}}));

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "NifEditFailed");
    CHECK(result.error().message.find("finite") != std::string::npos);
    CHECK(fileBytes(nif) == before);
}
