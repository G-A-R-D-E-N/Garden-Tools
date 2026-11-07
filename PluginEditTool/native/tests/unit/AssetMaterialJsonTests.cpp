#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <fo4ide/assets/MaterialJson.hpp>

#include <fo4/material/material_file.hpp>

#include <optional>
#include <string>

namespace {

using fo4::material::FieldValue;
using fo4::material::MaterialField;
using fo4::material::MaterialFile;
using fo4::material::RawBool;

void add(MaterialFile& file, const char* name, FieldValue value) {
    file.fields.push_back(MaterialField{name, std::move(value)});
}

MaterialFile bgsm(const std::string& diffuse, const std::string& normal, const std::string& root) {
    MaterialFile file;
    file.format = fo4::material::MaterialFormat::bgsm;
    add(file, "DiffuseTexture", FieldValue{diffuse});
    add(file, "NormalTexture", FieldValue{normal});
    add(file, "SmoothSpecTexture", FieldValue{std::string{}});
    add(file, "GlowTexture", FieldValue{std::string{}});
    add(file, "RootMaterialPath", FieldValue{root});
    add(file, "AlphaBlendMode0", FieldValue{std::uint8_t{0}});
    add(file, "AlphaTestRef", FieldValue{std::uint8_t{128}});
    add(file, "AlphaTest", FieldValue{RawBool{1}});
    add(file, "TwoSided", FieldValue{RawBool{0}});
    add(file, "EmitEnabled", FieldValue{RawBool{1}});
    add(file, "EmittanceColor", FieldValue{fo4::material::Color3{1.0F, 0.5F, 0.0F}});
    add(file, "EmittanceMult", FieldValue{2.0F});
    add(file, "Smoothness", FieldValue{0.6F});
    return file;
}

}  // namespace

TEST_CASE("BGSM texture slots and flags reach the material object", "[assets][material]") {
    const auto file = bgsm("textures\\a_d.dds", "textures\\a_n.dds", "");
    const auto json = fo4ide::assets::bgsmMaterialJson(file);
    CHECK(json["diffuse"] == "textures\\a_d.dds");
    CHECK(json["normal"] == "textures\\a_n.dds");
    CHECK(json["glow"] == "");
    CHECK(json["alphaTest"] == true);
    CHECK(json["alphaTestRef"] == 128);
    CHECK(json["alphaBlend"] == false);
    CHECK(json["twoSided"] == false);
    CHECK(json["emitEnabled"] == true);
    CHECK(json["emittanceColor"] == nlohmann::json::array({1.0F, 0.5F, 0.0F}));
    CHECK(json["emittanceMult"].get<float>() == 2.0F);
    CHECK(json["smoothness"].get<float>() == Catch::Approx(0.6F));
}

TEST_CASE("empty child texture slots take the RootMaterialPath values", "[assets][material]") {
    const auto child = bgsm("", "textures\\child_n.dds", "materials\\root.bgsm");
    const auto root = bgsm("textures\\root_d.dds", "textures\\root_n.dds", "");
    const auto json = fo4ide::assets::bgsmMaterialWithRoots(
        child, [&](const std::string& path) -> std::optional<MaterialFile> {
            if (path == "materials\\root.bgsm") return root;
            return std::nullopt;
        });
    CHECK(json["diffuse"] == "textures\\root_d.dds");
    CHECK(json["normal"] == "textures\\child_n.dds");
}

TEST_CASE("a cyclic RootMaterialPath chain stops at the depth cap", "[assets][material]") {
    const auto child = bgsm("", "", "materials\\loop.bgsm");
    int loads = 0;
    const auto json = fo4ide::assets::bgsmMaterialWithRoots(
        child, [&](const std::string&) -> std::optional<MaterialFile> {
            ++loads;
            return bgsm("", "", "materials\\loop.bgsm");
        });
    CHECK(loads == 4);
    CHECK(json["diffuse"] == "");
}
