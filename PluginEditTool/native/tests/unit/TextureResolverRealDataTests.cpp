#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <fo4/nif/nif.hpp>
#include <fo4ide/assets/MaterialJson.hpp>
#include <fo4ide/assets/NifRender.hpp>
#include <fo4ide/assets/TextureResolver.hpp>

#include "../helpers/TempRoot.hpp"

namespace {

std::optional<std::filesystem::path> dataDirFromEnv() {
    const char* value = std::getenv("FO4_DATA_DIR");
    if (value == nullptr || *value == '\0') return std::nullopt;
    const std::filesystem::path path(value);
    if (!std::filesystem::is_directory(path)) return std::nullopt;
    return path;
}

fo4::nif::NifFile loadNifFile(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    const std::vector<char> raw((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    return fo4::nif::read_nif(std::as_bytes(std::span(raw)));
}

struct Tally {
    std::size_t shapes{0};
    std::size_t withDiffuse{0};
    std::size_t materialNotResolved{0};
    std::size_t noTextureReference{0};
};

void tallyNif(const std::filesystem::path& nifPath,
              fo4ide::assets::TextureResolver& resolver,
              fo4ide::assets::BgsmMemo& memo,
              std::map<std::string, Tally>& byShader,
              std::string_view label) {
    const auto text = fo4ide::assets::renderGeometry(nifPath);
    REQUIRE(text.has_value());
    auto geometry = nlohmann::json::parse(*text);
    const auto nif = loadNifFile(nifPath);
    const auto shapes = fo4::nif::read_shapes(nif);
    fo4ide::assets::attachShapeMaterials(geometry, nifPath, resolver, memo);
    REQUIRE(geometry["shapes"].size() == shapes.size());

    std::map<std::string, Tally> local;
    for (std::size_t i = 0; i < shapes.size(); ++i) {
        const auto& shape = shapes[i];
        const auto& json = geometry["shapes"][i];
        const auto shaderType = shape.shader_ref >= 0
            ? nif.blocks().at(static_cast<std::size_t>(shape.shader_ref)).type
            : std::string("(no shader)");

        std::string diffuse;
        if (json.contains("material") && json["material"].is_object()) {
            diffuse = json["material"].value("diffuse", std::string{});
        }
        if (diffuse.empty() && !shape.textures.empty()) diffuse = shape.textures.front();

        Tally& tally = local[shaderType];
        ++tally.shapes;
        if (!diffuse.empty()) {
            ++tally.withDiffuse;
        } else if (!shape.material.empty()) {
            ++tally.materialNotResolved;
        } else {
            ++tally.noTextureReference;
        }
    }

    for (const auto& [shaderType, tally] : local) {
        std::cout << "[probe] " << label << " shader=" << shaderType << " shapes=" << tally.shapes
                  << " withDiffuse=" << tally.withDiffuse
                  << " materialNotResolved=" << tally.materialNotResolved
                  << " noTextureReference=" << tally.noTextureReference << '\n';
        Tally& total = byShader[shaderType];
        total.shapes += tally.shapes;
        total.withDiffuse += tally.withDiffuse;
        total.materialNotResolved += tally.materialNotResolved;
        total.noTextureReference += tally.noTextureReference;
    }
}

}  // namespace

TEST_CASE("TextureResolverReal.AbsoluteBuildMachineBgsmPathResolvesToDataRootMaterial", "[real-data][slow]") {
    const auto data = dataDirFromEnv();
    if (!data) SKIP("Fallout 4 Data folder not found; set FO4_DATA_DIR to it");

    fo4ide_test::TempRoot temp("fo4ide_real_data_material");
    fo4ide::assets::TextureResolver resolver(temp.path() / "cache");
    resolver.setSessionRoots({*data});

    const auto relative = resolver.resolveMaterialPath(
        "", "materials\\interiors\\industrial\\metaldoorindustrial01.bgsm", "");
    REQUIRE(relative.has_value());

    const auto absolute = resolver.resolveMaterialPath(
        "", "c:\\projects\\fallout4\\build\\pc\\data\\materials\\interiors\\industrial\\metaldoorindustrial01.bgsm", "");
    std::cout << "[probe] absolute BGSM resolves=" << (absolute.has_value() ? "yes" : "no") << '\n';
    CHECK(absolute.has_value());
    if (absolute && relative) CHECK(*absolute == *relative);
}

TEST_CASE("TextureResolverReal.NifShapesByShaderTypeWithAndWithoutDiffusePath", "[real-data][slow][probe]") {
    const auto data = dataDirFromEnv();
    if (!data) SKIP("Fallout 4 Data folder not found; set FO4_DATA_DIR to it");

    fo4ide_test::TempRoot temp("fo4ide_real_data_probe");
    fo4ide::assets::TextureResolver resolver(temp.path() / "cache");
    resolver.setSessionRoots({*data});
    fo4ide::assets::BgsmMemo memo;

    const std::vector<std::string> nifs{
        "Meshes\\Interiors\\Industrial\\Doors\\IndMetalDoorLoad01.nif",
        "Meshes\\Interiors\\Vault\\Vault111\\Rooms\\V111RWall01.nif",
        "Meshes\\Interiors\\Vault\\Vault111\\Rooms\\V111RWallCrR01.nif",
        "Meshes\\Interiors\\Vault\\Vault111\\Doors\\V111Door01.nif",
        "Meshes\\Interiors\\Vault\\Vault111\\Rooms\\V111RPit2WallMid01.nif",
        "Meshes\\Interiors\\Vault\\Vault111\\Rooms\\V111GearRoomGate01.nif",
    };

    std::map<std::string, Tally> byShader;
    std::size_t loaded = 0;
    for (const auto& rel : nifs) {
        const auto path = resolver.resolveNif(rel);
        if (!path) {
            std::cout << "[probe] not found: " << rel << '\n';
            continue;
        }
        ++loaded;
        tallyNif(*path, resolver, memo, byShader, rel);
    }
    REQUIRE(loaded > 0);

    std::cout << "[probe] TOTAL\n";
    for (const auto& [shaderType, tally] : byShader) {
        std::cout << "[probe]   shader=" << shaderType << " shapes=" << tally.shapes
                  << " withDiffuse=" << tally.withDiffuse
                  << " withoutDiffuse=" << (tally.shapes - tally.withDiffuse)
                  << " (materialNotResolved=" << tally.materialNotResolved
                  << ", noTextureReference=" << tally.noTextureReference << ")\n";
    }
}
