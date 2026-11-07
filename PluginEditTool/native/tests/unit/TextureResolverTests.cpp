#include <bit>
#include "../helpers/TempRoot.hpp"
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/archive/ba2_texture.hpp>
#include <fo4ide/assets/TextureResolver.hpp>

namespace {

void appendU32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((value >> 8u) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((value >> 16u) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((value >> 24u) & 0xFFu));
}

void appendF32(std::vector<std::uint8_t>& out, float value) {
    appendU32(out, std::bit_cast<std::uint32_t>(value));
}

void appendBool(std::vector<std::uint8_t>& out, bool value) {
    out.push_back(value ? 1u : 0u);
}

void appendString(std::vector<std::uint8_t>& out, std::string_view value) {
    appendU32(out, static_cast<std::uint32_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
}

// A 4x4 R8G8B8A8_UNORM (DXGI 28) DDS with a DX10 header, laid out exactly as
// the C# DdsCodec writer emits it.
std::vector<std::uint8_t> rgba8Dds() {
    std::vector<std::uint8_t> dds{'D', 'D', 'S', ' '};
    auto u32 = [&](std::uint32_t value) { appendU32(dds, value); };
    u32(124);                                  // dwSize
    u32(0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x80000);  // flags
    u32(4);                                    // height
    u32(4);                                    // width
    u32(64);                                   // pitchOrLinearSize
    u32(1);                                    // depth
    u32(1);                                    // mipMapCount
    for (int i = 0; i < 11; ++i) u32(0);       // reserved[11]
    u32(32);                                   // DDS_PIXELFORMAT size
    u32(0x4);                                  // DDPF_FOURCC
    u32(0x30315844u);                          // 'DX10'
    u32(0);                                    // rgbBitCount
    u32(0);                                    // rMask
    u32(0);                                    // gMask
    u32(0);                                    // bMask
    u32(0);                                    // aMask
    u32(0x1000);                               // caps
    u32(0);                                    // caps2
    u32(0);                                    // caps3
    u32(0);                                    // caps4
    u32(0);                                    // reserved2
    u32(28);                                   // DXGI R8G8B8A8_UNORM
    u32(3);                                    // TEXTURE2D
    u32(0);                                    // miscFlag
    u32(1);                                    // arraySize
    u32(0);                                    // miscFlags2
    for (int i = 0; i < 16; ++i) {
        dds.push_back(static_cast<std::uint8_t>(i));
        dds.push_back(static_cast<std::uint8_t>(i + 16));
        dds.push_back(static_cast<std::uint8_t>(i + 32));
        dds.push_back(static_cast<std::uint8_t>(i + 48));
    }
    return dds;
}

// A complete version-0 BGSM whose only meaningful field is DiffuseTexture,
// following the native writer's field order exactly (header, textures, then
// the version-0 body). Everything else uses empty/default values.
std::vector<std::uint8_t> minimalBgsm(std::string_view diffuse) {
    std::vector<std::uint8_t> bytes;
    auto u32 = [&](std::uint32_t v) { appendU32(bytes, v); };
    auto f32 = [&](float v) { appendF32(bytes, v); };
    auto boolean = [&](bool v) { appendBool(bytes, v); };
    auto text = [&](std::string_view v) { appendString(bytes, v); };
    auto color3 = [&] { f32(0.0F); f32(0.0F); f32(0.0F); };

    u32(0x4D534742u);  // BGSM
    u32(0);            // version
    u32(0);            // tile flags
    f32(0.0F);         // uOffset
    f32(0.0F);         // vOffset
    f32(1.0F);         // uScale
    f32(1.0F);         // vScale
    f32(1.0F);         // alpha
    bytes.push_back(0);            // alphaBlendMode0
    u32(0);                        // alphaBlendMode1
    u32(0);                        // alphaBlendMode2
    bytes.push_back(0);            // alphaTestRef
    boolean(false);                // alphaTest
    boolean(true);                 // zBufferWrite
    boolean(true);                 // zBufferTest
    boolean(false);                // ssr
    boolean(false);                // wetSsr
    boolean(false);                // decal
    boolean(false);                // twoSided
    boolean(false);                // decalNoFade
    boolean(false);                // nonOccluder
    boolean(false);                // refraction
    boolean(false);                // refractionFalloff
    f32(0.0F);                     // refractionPower
    boolean(false);                // envMapping (version < 10)
    f32(0.0F);                     // envMapMaskScale
    boolean(false);                // grayscaleToPaletteColor

    text(diffuse);                 // DiffuseTexture
    text("");                     // NormalTexture
    text("");                     // SmoothSpecTexture
    text("");                     // GreyscaleTexture
    text("");                     // EnvmapTexture (version <= 2)
    text("");                     // GlowTexture
    text("");                     // InnerLayerTexture
    text("");                     // WrinklesTexture
    text("");                     // DisplacementTexture

    boolean(false);                // EnableEditorAlphaRef
    boolean(false);                // RimLighting (version < 8)
    f32(0.0F);                     // RimPower
    f32(0.0F);                     // BackLightPower
    boolean(false);                // SubsurfaceLighting
    f32(0.0F);                     // SubsurfaceLightingRolloff
    boolean(true);                 // SpecularEnabled
    color3();                      // SpecularColor
    f32(0.0F);                     // SpecularMult
    f32(0.0F);                     // Smoothness
    f32(0.0F);                     // FresnelPower
    f32(0.0F);                     // WetnessControlSpecScale
    f32(0.0F);                     // WetnessControlSpecPowerScale
    f32(0.0F);                     // WetnessControlSpecMinvar
    f32(0.0F);                     // WetnessControlEnvMapScale (version < 10)
    f32(0.0F);                     // WetnessControlFresnelPower
    f32(0.0F);                     // WetnessControlMetalness

    text("");                      // RootMaterialPath
    boolean(false);                // AnisoLighting
    boolean(false);                // EmitEnabled
    f32(0.0F);                     // EmittanceMult
    boolean(false);                // ModelSpaceNormals
    boolean(false);                // ExternalEmittance
    boolean(false);                // BackLighting (version < 8)
    boolean(true);                 // ReceiveShadows
    boolean(false);                // HideSecret
    boolean(true);                 // CastShadows
    boolean(false);                // DissolveFade
    boolean(false);                // AssumeShadowmask
    boolean(false);                // Glowmap
    boolean(false);                // EnvironmentMappingWindow (version < 7)
    boolean(false);                // EnvironmentMappingEye
    boolean(false);                // Hair
    color3();                      // HairTintColor
    boolean(false);                // Tree
    boolean(false);                // Facegen
    boolean(false);                // SkinTint
    boolean(false);                // Tessellate
    f32(0.0F);                     // DisplacementTextureBias (version < 3)
    f32(0.0F);                     // DisplacementTextureScale
    f32(0.0F);                     // TessellationPnScale
    f32(0.0F);                     // TessellationBaseFactor
    f32(0.0F);                     // TessellationFadeDistance
    f32(0.0F);                     // GrayscaleToPaletteScale
    return bytes;
}

std::filesystem::path tempRoot() {
    const auto id = fo4ide_test::uniqueTempName();
    return std::filesystem::temp_directory_path() /
           ("fo4ide_texture_resolver_" + id);
}

struct Cleanup {
    std::filesystem::path root;
    ~Cleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

void writeFile(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
}

void writeBa2TextureArchive(const std::filesystem::path& path,
                            std::vector<fo4::archive::Ba2TextureEntry> entries) {
    fo4::archive::Ba2TextureArchive archive;
    archive.version = 8;
    archive.entries = std::move(entries);
    fo4::archive::write_ba2_texture_file(archive, path);
}

std::vector<std::byte> asBytes(const std::vector<std::uint8_t>& bytes) {
    std::vector<std::byte> out(bytes.size());
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        out[i] = static_cast<std::byte>(bytes[i]);
    }
    return out;
}

fo4::archive::Ba2TextureEntry textureEntry(std::string path,
                                           const std::vector<std::uint8_t>& payload) {
    fo4::archive::Ba2TextureEntry entry;
    entry.name_hash = 0x11111111u;
    entry.extension_hash = 0x00736464u;
    entry.directory_hash = 0x22222222u;
    entry.path = std::move(path);
    entry.texture = {4, 4, 1, 28, 0, 8};
    entry.chunks.push_back(fo4::archive::make_stored_ba2_texture_chunk(asBytes(payload)));
    return entry;
}

}  // namespace

TEST_CASE("Texture resolver finds loose DDS files under a texture root") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    // Mixed-case on-disk layout, like real FO4 data; the resolver must match
    // it case-insensitively the way the C# oracle does on Windows.
    writeFile(root / "Textures" / "Armor" / "Preview.dds", rgba8Dds());

    fo4ide::assets::TextureResolver resolver(root / "cache");
    const auto url = resolver.getTexturePngDataUrl("", "armor/preview.dds", root.string());
    CHECK(url.starts_with("data:image/png;base64,"));

    // The textures\\-prefixed relative path also resolves.
    const auto url2 = resolver.getTexturePngDataUrl("", "TEXTURES\\Armor\\Preview.dds", root.string());
    CHECK(url2.starts_with("data:image/png;base64,"));
}

TEST_CASE("Texture resolver resolves from a BA2 under session roots") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBa2TextureArchive(root / "data" / "textures.ba2", {
        textureEntry("Textures\\Armor\\FromArchive.dds", rgba8Dds())
    });

    fo4ide::assets::TextureResolver resolver(root / "cache");
    resolver.setSessionRoots({root / "data"});

    const auto url = resolver.getTexturePngDataUrl("", "textures\\armor\\fromarchive.dds", "");
    CHECK(url.starts_with("data:image/png;base64,"));

    // A second resolution for a different texture returns empty, and the
    // previously extracted DDS remains cached under the resolver's cache dir.
    const auto missing = resolver.getTexturePngDataUrl("", "textures\\missing.dds", "");
    CHECK(missing.empty());
}

TEST_CASE("Texture resolver caches the PNG and reuses it on repeat lookups") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto cacheDir = root / "cache";
    writeFile(root / "Textures" / "Cached.dds", rgba8Dds());

    fo4ide::assets::TextureResolver resolver(cacheDir);
    const auto first = resolver.getTexturePngDataUrl("", "cached.dds", root.string());
    REQUIRE(first.starts_with("data:image/png;base64,"));

    std::size_t pngCount = 0;
    for (const auto& entry : std::filesystem::directory_iterator(cacheDir)) {
        if (entry.path().extension() == ".png") ++pngCount;
    }
    CHECK(pngCount == 1);

    // The second call must return the identical cached PNG.
    const auto second = resolver.getTexturePngDataUrl("", "cached.dds", root.string());
    CHECK(second == first);
    pngCount = 0;
    for (const auto& entry : std::filesystem::directory_iterator(cacheDir)) {
        if (entry.path().extension() == ".png") ++pngCount;
    }
    CHECK(pngCount == 1);
}

TEST_CASE("Texture resolver resolves loose NIFs and NIFs inside BA2s") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeFile(root / "Meshes" / "Weapons" / "Loose.nif", {'n', 'i', 'f'});
    writeBa2TextureArchive(root / "data" / "meshes.ba2", {
        textureEntry("Meshes\\Armor\\InArchive.nif", {'n', 'i', 'f'})
    });

    fo4ide::assets::TextureResolver resolver(root / "cache");
    resolver.setSessionRoots({root, root / "data"});

    const auto loose = resolver.resolveNif("weapons/loose.nif");
    REQUIRE(loose.has_value());
    CHECK(std::filesystem::equivalent(*loose, root / "Meshes" / "Weapons" / "Loose.nif"));

    // "meshes\\" prefixed relative paths strip the prefix under roots.
    const auto prefixed = resolver.resolveNif("meshes\\weapons\\loose.nif");
    REQUIRE(prefixed.has_value());

    const auto archived = resolver.resolveNif("meshes\\armor\\inarchive.nif");
    REQUIRE(archived.has_value());
    CHECK(archived->filename().string().starts_with("ba2_"));
}

TEST_CASE("Texture resolver follows a BGSM material to its diffuse texture") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    // The diffuse reference inside the BGSM is the prefixed form; the resolver
    // must follow it through the NIF-relative directory walk up to the root.
    writeFile(root / "Meshes" / "Armor" / "Suit.nif", {'n', 'i', 'f'});
    writeFile(root / "Materials" / "Armor" / "Suit.bgsm",
              minimalBgsm("textures\\armor\\suit_diffuse.dds"));
    writeFile(root / "Textures" / "Armor" / "suit_diffuse.dds", rgba8Dds());

    fo4ide::assets::TextureResolver resolver(root / "cache");
    const auto nifPath = (root / "Meshes" / "Armor" / "Suit.nif").string();
    const auto url = resolver.getTexturePngDataUrl(nifPath, "Materials\\Armor\\Suit.bgsm", "");
    CHECK(url.starts_with("data:image/png;base64,"));
}

TEST_CASE("Texture resolver does not scan archives above a NIF that has no Data ancestor") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBa2TextureArchive(root / "elsewhere" / "textures.ba2", {
        textureEntry("Textures\\Stray.dds", rgba8Dds())
    });
    writeFile(root / "loose" / "meshes" / "thing.nif", {'n', 'i', 'f'});

    fo4ide::assets::TextureResolver resolver(root / "cache");
    const auto nifPath = (root / "loose" / "meshes" / "thing.nif").string();
    CHECK(resolver.getTexturePngDataUrl(nifPath, "textures\\stray.dds", "").empty());
}

TEST_CASE("Texture resolver still finds archives in the Data folder above a NIF") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBa2TextureArchive(root / "Game" / "Data" / "textures.ba2", {
        textureEntry("Textures\\InData.dds", rgba8Dds())
    });
    writeFile(root / "Game" / "Data" / "meshes" / "thing.nif", {'n', 'i', 'f'});

    fo4ide::assets::TextureResolver resolver(root / "cache");
    const auto nifPath = (root / "Game" / "Data" / "meshes" / "thing.nif").string();
    CHECK(resolver.getTexturePngDataUrl(nifPath, "textures\\indata.dds", "")
              .starts_with("data:image/png;base64,"));
}

TEST_CASE("Texture resolver scans an explicit texture root that has no Data ancestor") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBa2TextureArchive(root / "Unpacked" / "textures.ba2", {
        textureEntry("Textures\\Chosen.dds", rgba8Dds())
    });
    writeFile(root / "loose" / "meshes" / "thing.nif", {'n', 'i', 'f'});

    fo4ide::assets::TextureResolver resolver(root / "cache");
    const auto nifPath = (root / "loose" / "meshes" / "thing.nif").string();
    CHECK(resolver.getTexturePngDataUrl(nifPath, "textures\\chosen.dds", (root / "Unpacked").string())
              .starts_with("data:image/png;base64,"));
}

TEST_CASE("Texture resolver scans an explicit texture root without a Data ancestor only at its top level") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBa2TextureArchive(root / "Unpacked" / "nested" / "textures.ba2", {
        textureEntry("Textures\\Nested.dds", rgba8Dds())
    });
    writeFile(root / "loose" / "meshes" / "thing.nif", {'n', 'i', 'f'});

    fo4ide::assets::TextureResolver resolver(root / "cache");
    const auto nifPath = (root / "loose" / "meshes" / "thing.nif").string();
    CHECK(resolver.getTexturePngDataUrl(nifPath, "textures\\nested.dds", (root / "Unpacked").string())
              .empty());
}

TEST_CASE("Texture resolver still scans a folder recursively after a shallow scan of it") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto data = root / "Game" / "Data";
    writeBa2TextureArchive(data / "nested" / "textures.ba2", {
        textureEntry("Textures\\Deep.dds", rgba8Dds())
    });
    writeFile(root / "loose" / "thing.nif", {'n', 'i', 'f'});
    writeFile(data / "meshes" / "thing.nif", {'n', 'i', 'f'});

    fo4ide::assets::TextureResolver resolver(root / "cache");
    CHECK(resolver.getTexturePngDataUrl((root / "loose" / "thing.nif").string(), "textures\\deep.dds",
                                        data.string() + " ").empty());
    CHECK(resolver.getTexturePngDataUrl((data / "meshes" / "thing.nif").string(), "textures\\deep.dds", "")
              .starts_with("data:image/png;base64,"));
}
