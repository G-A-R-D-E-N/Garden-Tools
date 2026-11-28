#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/archive/assets.hpp>
#include <fo4ide/assets/NifGltfService.hpp>
#include <fo4ide/assets/NifRender.hpp>
#include <fo4ide/assets/NifService.hpp>

#include "../helpers/NifFixture.hpp"
#include "../helpers/TempRoot.hpp"

namespace {

bool symlinkNotPermitted(const std::error_code& ec) {
    return ec.category() == std::system_category() && ec.value() == 1314;
}

std::filesystem::path tempRoot() {
    const auto id = fo4ide_test::uniqueTempName();
    return std::filesystem::temp_directory_path() / ("fo4ide_nif_gltf_test_" + id);
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

TEST_CASE("NifService glTF export writes a binary glTF and refuses to overwrite its NIF") {
    // Invariant, regression: the glTF export wrote nothing for a valid triangle NIF, or replaced the NIF it was reading.
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "tri.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::triangleNif());
    const auto before = fileBytes(nif);

    const auto out = root / "tri.glb";
    auto result = fo4ide::assets::exportNifGltf(nif, out);
    REQUIRE(result.has_value());
    CHECK(result->starts_with("RESULT: gltf export OK"));
    CHECK(fileBytes(out).starts_with("glTF"));

    auto refused = fo4ide::assets::exportNifGltf(nif, nif);
    CHECK_FALSE(refused.has_value());
    CHECK(fileBytes(nif) == before);
}

// Detects: a glTF import must refuse to replace an existing NIF without overwrite, and must write the NIF for a valid .glb.
TEST_CASE("NifService imports a glTF in-process and refuses to replace its NIF without overwrite") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "tri.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::triangleNif());
    const auto glb = root / "tri.glb";
    REQUIRE(fo4ide::assets::exportNifGltf(nif, glb).has_value());

    const auto out = root / "back.nif";
    touch(out, "old");
    fo4ide::assets::NifService service;
    auto refused = service.importGltf(glb, out);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "OutputExists");
    CHECK(fileBytes(out) == "old");

    auto imported = service.importGltf(glb, out, {}, true);
    REQUIRE(imported.has_value());
    CHECK(imported->starts_with("RESULT: import glTF OK (in-process)\nSaved: " + out.string()));
    CHECK(fileBytes(out).starts_with("Gamebryo File Format"));
}

// Invariant, detects: an existing .glb replaced by an export that did not ask for overwrite, from the MCP tool or the panel button.
TEST_CASE("NifService glTF export refuses an existing output unless overwrite is set") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "tri.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::triangleNif());
    const auto out = root / "tri.glb";
    touch(out, "old");

    auto refused = fo4ide::assets::exportNifGltf(nif, out);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "OutputExists");
    CHECK(fileBytes(out) == "old");

    auto replaced = fo4ide::assets::exportNifGltf(nif, out, true);
    REQUIRE(replaced.has_value());
    CHECK(fileBytes(out).starts_with("glTF"));
}

// Detects: a bare .glb name, whose parent path is empty, that must publish to the working folder instead of failing in create_directories, and a relative folder that must still be created.
TEST_CASE("NifService glTF export publishes to a bare relative name and to a relative folder") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "tree.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::treeNif("", {"textures\\armor\\a_d.dds"}));
    touch(root / "source" / "textures" / "armor" / "a_d.dds", "DDS source");
    const std::vector<std::filesystem::path> roots{root / "source"};
    const auto assets = fo4::archive::AssetResolver::open_roots(roots, {});
    fo4ide::assets::NifGltfOptions options;
    options.textures = fo4::nif::GltfTextureMode::copy;
    options.assets = &assets;

    struct RestoreWorkingFolder {
        std::filesystem::path path;
        ~RestoreWorkingFolder() {
            std::error_code ec;
            std::filesystem::current_path(path, ec);
        }
    } restore{std::filesystem::current_path()};
    std::filesystem::current_path(root);

    std::filesystem::path out;
    std::filesystem::path texture;
    SECTION("a bare file name is written to the working folder") {
        out = "tree.glb";
        texture = root / "textures" / "armor" / "a_d.dds";
    }
    SECTION("a relative folder is created under the working folder") {
        out = std::filesystem::path("out") / "tree.glb";
        texture = root / "out" / "textures" / "armor" / "a_d.dds";
    }

    auto result = fo4ide::assets::exportNifGltf(nif, out, false, options);
    REQUIRE(result.has_value());
    CHECK(result->starts_with("RESULT: gltf export OK"));
    CHECK(fileBytes(root / out).starts_with("glTF"));
    CHECK(fileBytes(texture) == "DDS source");
}

// Detects: a copy export that must refuse to replace a texture file beside the .glb without overwrite, and must write the resolved DDS when overwrite is set.
TEST_CASE("NifService glTF copy export refuses to replace an existing texture file unless overwrite is set") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "tree.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::treeNif("", {"textures\\armor\\a_d.dds"}));
    touch(root / "source" / "textures" / "armor" / "a_d.dds", "DDS source");
    const std::vector<std::filesystem::path> roots{root / "source"};
    const auto assets = fo4::archive::AssetResolver::open_roots(roots, {});
    fo4ide::assets::NifGltfOptions options;
    options.textures = fo4::nif::GltfTextureMode::copy;
    options.assets = &assets;

    const auto out = root / "out" / "tree.glb";
    const auto texture = root / "out" / "textures" / "armor" / "a_d.dds";
    touch(texture, "old");

    auto refused = fo4ide::assets::exportNifGltf(nif, out, false, options);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "OutputExists");
    CHECK(fileBytes(texture) == "old");
    CHECK_FALSE(std::filesystem::exists(out));

    auto replaced = fo4ide::assets::exportNifGltf(nif, out, true, options);
    REQUIRE(replaced.has_value());
    CHECK(fileBytes(texture) == "DDS source");
}

// Detects: a copy export that fails part way through publication must restore every replaced file and leave no new or backup file behind, not keep a new .glb or some new textures.
TEST_CASE("NifService glTF copy export rolls back every replaced file when publication fails part way") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "tree.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::treeNif("", {"textures\\armor\\a_d.dds", "textures\\armor\\b_n.dds"}));
    touch(root / "source" / "textures" / "armor" / "a_d.dds", "DDS source a");
    touch(root / "source" / "textures" / "armor" / "b_n.dds", "DDS source b");
    const std::vector<std::filesystem::path> roots{root / "source"};
    const auto assets = fo4::archive::AssetResolver::open_roots(roots, {});
    fo4ide::assets::NifGltfOptions options;
    options.textures = fo4::nif::GltfTextureMode::copy;
    options.assets = &assets;

    const auto out = root / "out" / "tree.glb";
    const auto first = root / "out" / "textures" / "armor" / "a_d.dds";
    const auto second = root / "out" / "textures" / "armor" / "b_n.dds";
    touch(first, "old a");
    SECTION("a directory at the second texture") {
        touch(out, "old glb");
        touch(second / "keep.txt", "inside");
    }
    SECTION("a directory at the glb after both textures were replaced") {
        touch(second, "old b");
        touch(out / "keep.txt", "inside");
    }

    const auto listing = [&] {
        std::vector<std::string> names;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(root / "out")) {
            names.push_back(entry.path().lexically_relative(root).generic_string());
        }
        std::sort(names.begin(), names.end());
        return names;
    };
    const auto before = listing();
    auto failed = fo4ide::assets::exportNifGltf(nif, out, true, options);
    REQUIRE_FALSE(failed.has_value());
    CHECK(failed.error().type == "NifExportFailed");
    CHECK(listing() == before);
    CHECK(fileBytes(first) == "old a");
    if (std::filesystem::is_directory(out)) {
        CHECK(fileBytes(second) == "old b");
        CHECK(fileBytes(out / "keep.txt") == "inside");
    } else {
        CHECK(fileBytes(out) == "old glb");
        CHECK(fileBytes(second / "keep.txt") == "inside");
    }
}

// Detects: a failed glTF publication that must remove every folder it created, children before parents, so no out/ tree is left when the .glb path is a folder that an earlier texture write made.
TEST_CASE("NifService glTF export removes the nested folders it created when publication fails") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "tree.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::treeNif("", {"textures\\armor\\a_d.dds", "textures\\armor\\sub\\b_n.dds"}));
    touch(root / "source" / "textures" / "armor" / "a_d.dds", "DDS source a");
    touch(root / "source" / "textures" / "armor" / "sub" / "b_n.dds", "DDS source b");
    const std::vector<std::filesystem::path> roots{root / "source"};
    const auto assets = fo4::archive::AssetResolver::open_roots(roots, {});
    fo4ide::assets::NifGltfOptions options;
    options.textures = fo4::nif::GltfTextureMode::copy;
    options.assets = &assets;

    const auto out = root / "out" / "textures";
    auto failed = fo4ide::assets::exportNifGltf(nif, out, false, options);
    REQUIRE_FALSE(failed.has_value());
    CHECK(failed.error().type == "NifExportFailed");
    CHECK_FALSE(std::filesystem::exists(root / "out"));
}

namespace {

std::vector<std::filesystem::path> stagingFolders() {
    std::vector<std::filesystem::path> found;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::temp_directory_path(), ec)) {
        if (entry.path().filename().string().starts_with("fo4ide-gltf-")) found.push_back(entry.path());
    }
    return found;
}

}

// Detects: a copy export whose texture folder is a link out of the output must refuse the write, so a new or existing texture behind the link is never written.
TEST_CASE("NifService glTF copy export refuses a texture folder that links out of the output") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "tree.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::treeNif("", {"textures\\armor\\a_d.dds"}));
    touch(root / "source" / "textures" / "armor" / "a_d.dds", "DDS source");
    const std::vector<std::filesystem::path> roots{root / "source"};
    const auto assets = fo4::archive::AssetResolver::open_roots(roots, {});
    fo4ide::assets::NifGltfOptions options;
    options.textures = fo4::nif::GltfTextureMode::copy;
    options.assets = &assets;

    const auto elsewhere = root / "elsewhere";
    const auto target = elsewhere / "armor" / "a_d.dds";
    std::filesystem::create_directories(root / "out");
    std::error_code linkEc;
    std::filesystem::create_directory_symlink(elsewhere, root / "out" / "textures", linkEc);
    if (symlinkNotPermitted(linkEc)) SKIP("creating symlinks is not permitted here");
    REQUIRE_FALSE(linkEc);
    const auto out = root / "out" / "tree.glb";

    bool overwrite = false;
    std::string before;
    SECTION("a new texture behind the link is not created") {}
    SECTION("an existing texture behind the link is kept") {
        touch(target, "old");
        overwrite = true;
        before = "old";
    }

    auto refused = fo4ide::assets::exportNifGltf(nif, out, overwrite, options);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "InvalidPath");
    CHECK(std::filesystem::exists(target) == !before.empty());
    CHECK(fileBytes(target) == before);
    CHECK_FALSE(std::filesystem::exists(out));
}

// Detects: a glTF export that must remove the staging folder it created, so a successful copy export leaves no fo4ide-gltf folder in the temp folder.
TEST_CASE("NifService glTF copy export leaves no staging folder behind") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "tree.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::treeNif("", {"textures\\armor\\a_d.dds"}));
    touch(root / "source" / "textures" / "armor" / "a_d.dds", "DDS source");
    const std::vector<std::filesystem::path> roots{root / "source"};
    const auto assets = fo4::archive::AssetResolver::open_roots(roots, {});
    fo4ide::assets::NifGltfOptions options;
    options.textures = fo4::nif::GltfTextureMode::copy;
    options.assets = &assets;

    const auto staged = stagingFolders();
    const auto out = root / "out" / "tree.glb";
    auto result = fo4ide::assets::exportNifGltf(nif, out, false, options);
    REQUIRE(result.has_value());
    CHECK(fileBytes(root / "out" / "textures" / "armor" / "a_d.dds") == "DDS source");
    CHECK(stagingFolders().size() == staged.size());
}

// Detects: an unknown textures argument that must be refused, or a blank one that must keep the png default.
TEST_CASE("glTF texture mode names map to the export modes and reject others") {
    CHECK(fo4ide::assets::parseGltfTextureMode("") == fo4::nif::GltfTextureMode::png);
    CHECK(fo4ide::assets::parseGltfTextureMode("png") == fo4::nif::GltfTextureMode::png);
    CHECK(fo4ide::assets::parseGltfTextureMode("copy") == fo4::nif::GltfTextureMode::copy);
    CHECK(fo4ide::assets::parseGltfTextureMode("png") == fo4::nif::GltfTextureMode::png);
    CHECK(fo4ide::assets::parseGltfTextureMode("embed") == fo4::nif::GltfTextureMode::embed);
    CHECK(fo4ide::assets::parseGltfTextureMode("uri") == fo4::nif::GltfTextureMode::uri);
    CHECK_FALSE(fo4ide::assets::parseGltfTextureMode("dds").has_value());
}

// Detects: a glTF buffer that climbs out of its folder, directly or through a directory link, must be refused before any NIF is written.
TEST_CASE("NifService glTF import refuses a buffer that leaves the glTF folder and writes no NIF") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto folder = root / "proj";
    std::filesystem::create_directories(folder);
    touch(root / "outside.bin", "outside");
    touch(root / "elsewhere" / "linked.bin", "outside");
    std::error_code linkEc;
    std::filesystem::create_directory_symlink(root / "elsewhere", folder / "link", linkEc);
    if (symlinkNotPermitted(linkEc)) SKIP("creating symlinks is not permitted here");
    REQUIRE_FALSE(linkEc);
    const auto out = root / "mesh.nif";
    fo4ide::assets::NifService service;

    touch(folder / "parent.gltf", R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":7,"uri":"../outside.bin"}]})");
    auto climbing = service.importGltf(folder / "parent.gltf", out);
    REQUIRE_FALSE(climbing.has_value());
    CHECK(climbing.error().message.find("climbs out of") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(out));

    touch(folder / "link.gltf", R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":7,"uri":"link/linked.bin"}]})");
    auto linked = service.importGltf(folder / "link.gltf", out);
    REQUIRE_FALSE(linked.has_value());
    CHECK(linked.error().message.find("outside the glTF folder") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(out));

    std::filesystem::create_symlink(root / "outside.bin", folder / "file-link.bin", linkEc);
    if (symlinkNotPermitted(linkEc)) SKIP("creating symlinks is not permitted here");
    REQUIRE_FALSE(linkEc);
    touch(folder / "file.gltf", R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":7,"uri":"file-link.bin"}]})");
    auto fileLinked = service.importGltf(folder / "file.gltf", out);
    REQUIRE_FALSE(fileLinked.has_value());
    CHECK(fileLinked.error().message.find("outside the glTF folder") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(out));
}

TEST_CASE("NifService glTF import refuses an output that is its source, its skeleton or its buffer even with overwrite") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::assets::NifService service;

    const auto nif = root / "tri.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::triangleNif());
    const auto glb = root / "tri.glb";
    REQUIRE(fo4ide::assets::exportNifGltf(nif, glb).has_value());
    const auto glbBytes = fileBytes(glb);
    auto onSource = service.importGltf(glb, glb, {}, true);
    REQUIRE_FALSE(onSource.has_value());
    CHECK(fileBytes(glb) == glbBytes);

    const auto skeleton = root / "skeleton.nif";
    fo4ide::test::writeBytes(skeleton, fo4ide::test::triangleNif());
    const auto skeletonBytes = fileBytes(skeleton);
    fo4ide::assets::NifImportOptions withSkeleton;
    withSkeleton.skeleton = skeleton;
    auto onSkeleton = service.importGltf(glb, skeleton, withSkeleton, true);
    REQUIRE_FALSE(onSkeleton.has_value());
    CHECK(fileBytes(skeleton) == skeletonBytes);

    const auto folder = root / "proj";
    std::filesystem::create_directories(folder);
    const float positions[9]{0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    const std::uint16_t indices[3]{0, 1, 2};
    {
        std::ofstream bin(folder / "mesh.bin", std::ios::binary);
        bin.write(reinterpret_cast<const char*>(positions), sizeof positions);
        bin.write(reinterpret_cast<const char*>(indices), sizeof indices);
    }
    const auto buffer = fileBytes(folder / "mesh.bin");
    touch(folder / "mesh.gltf", R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"name":"Tri","mesh":0}],"meshes":[{"name":"Tri","primitives":[{"attributes":{"POSITION":0},"indices":1}]}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6}],"buffers":[{"byteLength":42,"uri":"mesh.bin"}]})");
    auto onBuffer = service.importGltf(folder / "mesh.gltf", folder / "mesh.bin", {}, true);
    REQUIRE_FALSE(onBuffer.has_value());
    CHECK(fileBytes(folder / "mesh.bin") == buffer);
}

// Detects: a buffer inside the glTF folder, named with a percent-encoded space, must still be read, or the folder guard refuses ordinary shared assets.
TEST_CASE("NifService glTF import reads a buffer inside its folder through a percent-encoded name") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto folder = root / "proj";
    std::filesystem::create_directories(folder);
    const float positions[9]{0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    const std::uint16_t indices[3]{0, 1, 2};
    {
        std::ofstream bin(folder / "mesh data.bin", std::ios::binary);
        bin.write(reinterpret_cast<const char*>(positions), sizeof positions);
        bin.write(reinterpret_cast<const char*>(indices), sizeof indices);
    }
    touch(folder / "mesh.gltf", R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"name":"Tri","mesh":0}],"meshes":[{"name":"Tri","primitives":[{"attributes":{"POSITION":0},"indices":1}]}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6}],"buffers":[{"byteLength":42,"uri":"mesh%20data.bin"}]})");
    const auto out = root / "mesh.nif";
    fo4ide::assets::NifService service;

    auto imported = service.importGltf(folder / "mesh.gltf", out);
    REQUIRE(imported.has_value());
    CHECK(fileBytes(out).starts_with("Gamebryo File Format"));
}
