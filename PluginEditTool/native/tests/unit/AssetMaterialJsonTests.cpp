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
        child, [&](const std::string& path) -> std::shared_ptr<const MaterialFile> {
            if (path == "materials\\root.bgsm") return std::make_shared<const MaterialFile>(root);
            return nullptr;
        });
    CHECK(json["diffuse"] == "textures\\root_d.dds");
    CHECK(json["normal"] == "textures\\child_n.dds");
}

TEST_CASE("a cyclic RootMaterialPath chain stops at the depth cap", "[assets][material]") {
    const auto child = bgsm("", "", "materials\\loop.bgsm");
    int loads = 0;
    const auto json = fo4ide::assets::bgsmMaterialWithRoots(
        child, [&](const std::string&) -> std::shared_ptr<const MaterialFile> {
            ++loads;
            return std::make_shared<const MaterialFile>(bgsm("", "", "materials\\loop.bgsm"));
        });
    CHECK(loads == 4);
    CHECK(json["diffuse"] == "");
}

TEST_CASE("BGEM maps BaseTexture to diffuse and keeps the normal and glow slots", "[assets][material]") {
    MaterialFile file;
    file.format = fo4::material::MaterialFormat::bgem;
    add(file, "BaseTexture", FieldValue{std::string{"textures\\fx\\base.dds"}});
    add(file, "NormalTexture", FieldValue{std::string{"textures\\fx\\n.dds"}});
    add(file, "GlowTexture", FieldValue{std::string{"textures\\fx\\g.dds"}});
    add(file, "DiffuseTexture", FieldValue{std::string{"textures\\ignored.dds"}});
    const auto json = fo4ide::assets::bgemMaterialJson(file);
    CHECK(json["format"] == "BGEM");
    CHECK(json["diffuse"] == "textures\\fx\\base.dds");
    CHECK(json["normal"] == "textures\\fx\\n.dds");
    CHECK(json["glow"] == "textures\\fx\\g.dds");
    CHECK(json["rootMaterialPath"] == "");
}

// Constraint, detects: BGEM/BGSM TileFlags dropped or the U and V bits swapped, so clamped effect textures tile in the viewer.
TEST_CASE("material TileFlags reach the JSON as tileU and tileV", "[assets][material]") {
    MaterialFile file;
    file.format = fo4::material::MaterialFormat::bgem;
    add(file, "TileFlags", FieldValue{std::uint32_t{2}});
    const auto json = fo4ide::assets::bgemMaterialJson(file);
    CHECK(json["tileU"] == true);
    CHECK(json["tileV"] == false);

    MaterialFile vOnly = bgsm("", "", "");
    add(vOnly, "TileFlags", FieldValue{std::uint32_t{1}});
    const auto bgsmJson = fo4ide::assets::bgsmMaterialJson(vOnly);
    CHECK(bgsmJson["tileU"] == false);
    CHECK(bgsmJson["tileV"] == true);

    MaterialFile none;
    none.format = fo4::material::MaterialFormat::bgem;
    const auto absent = fo4ide::assets::bgemMaterialJson(none);
    CHECK(absent["tileU"] == true);
    CHECK(absent["tileV"] == true);
}

TEST_CASE("material texture wins over the NIF slot when it resolves", "[assets][material]") {
    const fo4ide::assets::TextureExists exists = [](const std::string& path) {
        return path == "textures\\own_d.dds" || path == "textures\\nif_d.dds";
    };
    CHECK(fo4ide::assets::mergeMaterialTexture("textures\\own_d.dds", "textures\\nif_d.dds", exists) ==
          "textures\\own_d.dds");
}

TEST_CASE("NIF slot wins when the material texture does not resolve but the NIF one does", "[assets][material]") {
    const fo4ide::assets::TextureExists exists = [](const std::string& path) {
        return path == "textures\\nif_d.dds";
    };
    CHECK(fo4ide::assets::mergeMaterialTexture("textures\\missing_d.dds", "textures\\nif_d.dds", exists) ==
          "textures\\nif_d.dds");
}

TEST_CASE("material texture is kept when neither side resolves, matching GE", "[assets][material]") {
    const fo4ide::assets::TextureExists exists = [](const std::string&) { return false; };
    CHECK(fo4ide::assets::mergeMaterialTexture("textures\\own_d.dds", "textures\\nif_d.dds", exists) ==
          "textures\\own_d.dds");
}

TEST_CASE("empty sides fall back to the other side and equal paths keep the NIF spelling", "[assets][material]") {
    const fo4ide::assets::TextureExists exists = [](const std::string&) { return false; };
    CHECK(fo4ide::assets::mergeMaterialTexture("", "textures\\nif_d.dds", exists) == "textures\\nif_d.dds");
    CHECK(fo4ide::assets::mergeMaterialTexture("textures\\own_d.dds", "", exists) == "textures\\own_d.dds");
    CHECK(fo4ide::assets::mergeMaterialTexture("TEXTURES/Same_D.dds", "textures\\same_d.dds", exists) ==
          "textures\\same_d.dds");
}

TEST_CASE("BGSM diffuse from the material overrides the NIF slot 0 and normal fills slot 1", "[assets][material]") {
    nlohmann::json shape = {
        {"name", "wall"},
        {"textures", nlohmann::json::array({{{"slot", 0}, {"path", "textures\\nif_d.dds"}}})},
    };
    const nlohmann::json material = {{"diffuse", "textures\\own_d.dds"}, {"normal", "textures\\own_n.dds"}};
    const fo4ide::assets::TextureExists exists = [](const std::string& path) {
        return path == "textures\\own_d.dds" || path == "textures\\own_n.dds" || path == "textures\\nif_d.dds";
    };
    fo4ide::assets::mergeMaterialSlots(shape, material, exists);
    REQUIRE(shape["textures"].size() == 2);
    CHECK(shape["textures"][0]["path"] == "textures\\own_d.dds");
    CHECK(shape["textures"][1] == nlohmann::json{{"slot", 1}, {"path", "textures\\own_n.dds"}});
}

TEST_CASE("an unresolved BGSM diffuse keeps the NIF slot 0 texture", "[assets][material]") {
    nlohmann::json shape = {
        {"name", "wall"},
        {"textures", nlohmann::json::array({{{"slot", 0}, {"path", "textures\\nif_d.dds"}}})},
    };
    const nlohmann::json material = {{"diffuse", "textures\\gone_d.dds"}, {"normal", ""}};
    const fo4ide::assets::TextureExists exists = [](const std::string& path) {
        return path == "textures\\nif_d.dds";
    };
    fo4ide::assets::mergeMaterialSlots(shape, material, exists);
    REQUIRE(shape["textures"].size() == 1);
    CHECK(shape["textures"][0]["path"] == "textures\\nif_d.dds");
}

TEST_CASE("merging a shape without a textures array creates the slots from the material", "[assets][material]") {
    nlohmann::json shape = {{"name", "glass"}};
    const nlohmann::json material = {{"diffuse", "textures\\glass_d.dds"}, {"normal", ""}};
    const fo4ide::assets::TextureExists exists = [](const std::string&) { return true; };
    fo4ide::assets::mergeMaterialSlots(shape, material, exists);
    REQUIRE(shape["textures"].size() == 1);
    CHECK(shape["textures"][0] == nlohmann::json{{"slot", 0}, {"path", "textures\\glass_d.dds"}});
}
