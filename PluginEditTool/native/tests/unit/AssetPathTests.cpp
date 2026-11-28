#include <catch2/catch_test_macros.hpp>

#include <fo4ide/assets/AssetPath.hpp>

using fo4ide::assets::isAbsoluteOnAnyHost;
using fo4ide::assets::stripDevAbsolutePrefix;

TEST_CASE("isAbsoluteOnAnyHost recognises drive, rooted and UNC paths", "[asset_path]") {
    CHECK(isAbsoluteOnAnyHost("c:\\projects\\a.bgsm"));
    CHECK(isAbsoluteOnAnyHost("D:/Games/Data"));
    CHECK(isAbsoluteOnAnyHost("\\materials\\a.bgsm"));
    CHECK(isAbsoluteOnAnyHost("/materials/a.bgsm"));
    CHECK(isAbsoluteOnAnyHost("\\\\server\\share\\a.bgsm"));
    CHECK_FALSE(isAbsoluteOnAnyHost("materials\\a.bgsm"));
    CHECK_FALSE(isAbsoluteOnAnyHost("c:relative\\a.bgsm"));
    CHECK_FALSE(isAbsoluteOnAnyHost("1:\\a.bgsm"));
    CHECK_FALSE(isAbsoluteOnAnyHost(""));
}

TEST_CASE("stripDevAbsolutePrefix cuts an absolute build path at the materials marker", "[asset_path]") {
    const auto stripped = stripDevAbsolutePrefix(
        "c:\\projects\\fallout4\\build\\pc\\data\\materials\\interiors\\industrial\\metaldoorindustrial01.bgsm",
        {"materials\\", "textures\\"});
    CHECK(stripped == "materials\\interiors\\industrial\\metaldoorindustrial01.bgsm");
}

TEST_CASE("stripDevAbsolutePrefix falls back to the textures marker in order", "[asset_path]") {
    const auto stripped = stripDevAbsolutePrefix(
        "c:\\build\\textures\\armor\\a_d.dds", {"materials\\", "textures\\"});
    CHECK(stripped == "textures\\armor\\a_d.dds");
}

TEST_CASE("stripDevAbsolutePrefix tries markers in list order before scanning the path", "[asset_path]") {
    const auto stripped = stripDevAbsolutePrefix(
        "c:\\build\\textures\\x\\materials\\m.bgsm", {"materials\\", "textures\\"});
    CHECK(stripped == "materials\\m.bgsm");
}

TEST_CASE("stripDevAbsolutePrefix requires a segment boundary before the marker", "[asset_path]") {
    const auto stripped = stripDevAbsolutePrefix("c:\\build\\oldmaterials\\m.bgsm", {"materials\\"});
    CHECK(stripped == "c:\\build\\oldmaterials\\m.bgsm");
}

TEST_CASE("stripDevAbsolutePrefix matches the marker case-insensitively with either slash", "[asset_path]") {
    CHECK(stripDevAbsolutePrefix("C:/Build/MATERIALS/Interiors/a.bgsm", {"materials\\"}) ==
          "MATERIALS/Interiors/a.bgsm");
    CHECK(stripDevAbsolutePrefix("c:\\build/Materials\\a.bgsm", {"materials\\"}) == "Materials\\a.bgsm");
    CHECK(stripDevAbsolutePrefix("c:\\build\\Materials/a.bgsm", {"materials/"}) == "Materials/a.bgsm");
}

TEST_CASE("stripDevAbsolutePrefix strips UNC paths too", "[asset_path]") {
    CHECK(stripDevAbsolutePrefix("\\\\server\\share\\meshes\\materials\\a.bgsm", {"materials\\"}) ==
          "materials\\a.bgsm");
}

TEST_CASE("stripDevAbsolutePrefix returns relative paths unchanged", "[asset_path]") {
    CHECK(stripDevAbsolutePrefix("materials\\a.bgsm", {"materials\\"}) == "materials\\a.bgsm");
    CHECK(stripDevAbsolutePrefix("", {"materials\\"}) == "");
}

TEST_CASE("stripDevAbsolutePrefix returns an absolute path without a marker unchanged", "[asset_path]") {
    const std::string path = "c:\\projects\\fallout4\\a.bgsm";
    CHECK(stripDevAbsolutePrefix(path, {"materials\\", "textures\\"}) == path);
    CHECK(stripDevAbsolutePrefix(path, {}) == path);
}