#include <fo4ide/archive/ArchiveService.hpp>
#include "../helpers/TempRoot.hpp"
#include <fo4ide/settings/Settings.hpp>
#include <fo4ide/tools/ToolLocator.hpp>
#include <fo4ide/archive/RadicleArchiveBackend.hpp>
#include <fo4ide/app/ArchiveTarget.hpp>
#include <fo4ide/mcp/McpServer.hpp>
#include <fo4ide/settings/Settings.hpp>

#include "../../apps/fo4ide/McpCatalog.hpp"
#include "../helpers/ServiceTargetFakes.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <fo4/archive/ba2.hpp>
#include <fo4/archive/ba2_texture.hpp>

namespace {

std::filesystem::path tempRoot() {
    const auto id = fo4ide_test::uniqueTempName();
    return std::filesystem::temp_directory_path() /
           ("fo4ide_radicle_archive_" + id);
}

struct Cleanup {
    std::filesystem::path root;
    ~Cleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

std::vector<std::byte> bytes(std::string_view text) {
    std::vector<std::byte> out;
    out.reserve(text.size());
    for (const char ch : text) {
        out.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
    }
    return out;
}

std::vector<std::byte> readBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    REQUIRE(stream.good());
    const auto end = stream.tellg();
    REQUIRE(end >= 0);
    std::vector<std::byte> out(static_cast<std::size_t>(end));
    stream.seekg(0, std::ios::beg);
    if (!out.empty()) {
        stream.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
        REQUIRE(stream.good());
    }
    return out;
}

fo4::archive::Ba2GeneralEntry entry(std::string path, std::string_view payload,
                                    bool compress = false) {
    const auto data = bytes(payload);
    return {
        0x11111111u,
        0x22222222u,
        0x33333333u,
        0u,
        std::move(path),
        {compress ? fo4::archive::make_compressed_ba2_chunk(data)
                  : fo4::archive::make_stored_ba2_chunk(data)}
    };
}

std::filesystem::path writeArchive(
    const std::filesystem::path& root,
    std::string_view name,
    std::vector<fo4::archive::Ba2GeneralEntry> entries) {
    fo4::archive::Ba2GeneralArchive archive;
    archive.entries = std::move(entries);
    const auto path = root / std::string(name);
    fo4::archive::write_ba2_general_file(archive, path);
    return path;
}

nlohmann::json listArguments(const std::filesystem::path& path,
                             std::string filter = {},
                             std::string mode = "simple") {
    return {
        {"archivePath", path.string()},
        {"filter", std::move(filter)},
        {"limit", 2000},
        {"filterMode", std::move(mode)}
    };
}

void appendU32(std::vector<std::byte>& out, std::uint32_t value) {
    out.push_back(static_cast<std::byte>(value & 0xFFu));
    out.push_back(static_cast<std::byte>((value >> 8u) & 0xFFu));
    out.push_back(static_cast<std::byte>((value >> 16u) & 0xFFu));
    out.push_back(static_cast<std::byte>((value >> 24u) & 0xFFu));
}

// A 4x4 R8G8B8A8_UNORM (DXGI 28) DDS with a DX10 header, laid out exactly as
// the C# DdsCodec writer emits it (see the TextureCodecTests fixture).
std::vector<std::byte> rgba8Dds() {
    std::vector<std::byte> dds;
    for (const unsigned char ch : {'D', 'D', 'S', ' '}) {
        dds.push_back(static_cast<std::byte>(ch));
    }
    appendU32(dds, 124);                                 // dwSize
    appendU32(dds, 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x80000);  // flags
    appendU32(dds, 4);                                   // height
    appendU32(dds, 4);                                   // width
    appendU32(dds, 64);                                  // pitchOrLinearSize
    appendU32(dds, 1);                                   // depth
    appendU32(dds, 1);                                   // mipMapCount
    for (int i = 0; i < 11; ++i) appendU32(dds, 0);      // reserved[11]
    appendU32(dds, 32);                                  // DDS_PIXELFORMAT size
    appendU32(dds, 0x4);                                 // DDPF_FOURCC
    appendU32(dds, 0x30315844u);                         // 'DX10'
    appendU32(dds, 0);                                   // rgbBitCount
    appendU32(dds, 0);                                   // rMask
    appendU32(dds, 0);                                   // gMask
    appendU32(dds, 0);                                   // bMask
    appendU32(dds, 0);                                   // aMask
    appendU32(dds, 0x1000);                              // caps
    appendU32(dds, 0);                                   // caps2
    appendU32(dds, 0);                                   // caps3
    appendU32(dds, 0);                                   // caps4
    appendU32(dds, 0);                                   // reserved2
    appendU32(dds, 28);                                  // DXGI R8G8B8A8_UNORM
    appendU32(dds, 3);                                   // TEXTURE2D
    appendU32(dds, 0);                                   // miscFlag
    appendU32(dds, 1);                                   // arraySize
    appendU32(dds, 0);                                   // miscFlags2
    for (int i = 0; i < 16; ++i) {
        dds.push_back(static_cast<std::byte>(i));
        dds.push_back(static_cast<std::byte>(i + 16));
        dds.push_back(static_cast<std::byte>(i + 32));
        dds.push_back(static_cast<std::byte>(i + 48));
    }
    return dds;
}

fo4::archive::Ba2TextureEntry textureEntry(std::string path, std::vector<std::byte> payload,
                                           bool compress = false) {
    fo4::archive::Ba2TextureEntry entry;
    entry.name_hash = 0x11111111u;
    entry.extension_hash = 0x00736464u;
    entry.directory_hash = 0x22222222u;
    entry.path = std::move(path);
    entry.texture = {4, 4, 1, 28, 0, 8};
    entry.chunks.push_back(compress ? fo4::archive::make_compressed_ba2_texture_chunk(payload)
                                    : fo4::archive::make_stored_ba2_texture_chunk(payload));
    return entry;
}

std::filesystem::path writeTextureArchive(
    const std::filesystem::path& root,
    std::string_view name,
    std::vector<fo4::archive::Ba2TextureEntry> entries) {
    fo4::archive::Ba2TextureArchive archive;
    archive.version = 8;
    archive.entries = std::move(entries);
    const auto path = root / std::string(name);
    fo4::archive::write_ba2_texture_file(archive, path);
    return path;
}

}

TEST_CASE("Radicle archive backend lists GNRL entries using the existing UI contract") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = writeArchive(root, "test.ba2", {
        entry("Meshes\\Weapons\\One.nif", "mesh"),
        entry("Scripts\\Quest.pex", std::string(2048, 'A'), true),
        entry("Textures\\Ignored.dds", "texture")
    });

    fo4ide::archive::RadicleArchiveBackend backend;
    const auto result = backend.invoke("list", listArguments(path, "scripts"));
    REQUIRE(result.has_value());
    CHECK(result->at("archiveName") == "test.ba2");
    CHECK(result->at("totalCount") == 3);
    CHECK(result->at("shownCount") == 1);
    CHECK_FALSE(result->at("truncated").get<bool>());
    REQUIRE(result->at("entries").size() == 1);
    CHECK(result->at("entries")[0].at("path") == "Scripts\\Quest.pex");
    CHECK(result->at("entries")[0].at("size") == 2048);
}

TEST_CASE("Null archive backend is genuinely unavailable") {
    fo4ide::archive::NullArchiveBackend backend;
    CHECK_FALSE(backend.available());
    const auto result = backend.invoke("list", nlohmann::json::array());
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "ArchiveBackendUnavailable");
}

TEST_CASE("Radicle archive backend extracts selected entries under the chosen root") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = writeArchive(root, "extract.ba2", {
        entry("Meshes\\Sub\\Thing.nif", "mesh payload"),
        entry("Scripts\\Quest.pex", "script payload")
    });
    const auto output = root / "out";

    fo4ide::archive::RadicleArchiveBackend backend;
    const nlohmann::json args{
        {"archivePath", path.string()},
        {"innerPaths", nlohmann::json::array({"Meshes\\Sub\\Thing.nif", "Scripts\\Quest.pex"})},
        {"outputDirectory", output.string()}
    };
    const auto result = backend.invoke("extractSelected", args);
    REQUIRE(result.has_value());
    CHECK(result->get<std::string>().starts_with("Extracted 2"));
    CHECK(readBytes(output / "Meshes" / "Sub" / "Thing.nif") == bytes("mesh payload"));
    CHECK(readBytes(output / "Scripts" / "Quest.pex") == bytes("script payload"));
}

TEST_CASE("Radicle archive backend extracts into a non-ANSI output folder") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = writeArchive(root, "extract.ba2", {entry("Meshes\\Thing.nif", "mesh payload")});
    const auto output = root / std::filesystem::path(std::u16string{0x041C, 0x043E, 0x0434, 0x044B});
    const auto text = output.u8string();

    fo4ide::archive::RadicleArchiveBackend backend;
    const nlohmann::json args{
        {"archivePath", path.string()},
        {"innerPaths", nlohmann::json::array({"Meshes\\Thing.nif"})},
        {"outputDirectory", std::string(text.begin(), text.end())}
    };
    const auto result = backend.invoke("extractSelected", args);
    REQUIRE(result.has_value());
    CHECK(readBytes(output / "Meshes" / "Thing.nif") == bytes("mesh payload"));
}

// Detects: guarded bulk extraction must reject protected filenames before writing any archive entry.
TEST_CASE("Radicle archive backend guards bulk extraction names without changing desktop extraction") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = writeArchive(root, "protected.ba2", {
        entry("safe\\good.txt", "good"),
        entry("bin\\Payload.EXE", "executable"),
        entry("ModOrganizer.ini", "settings")
    });
    fo4ide::archive::RadicleArchiveBackend backend;
    for (const auto& operation : {"extractSelected", "extractAll"}) {
        for (const auto& protectedName : {"bin\\Payload.EXE", "ModOrganizer.ini"}) {
            const auto output = root / operation / protectedName;
            nlohmann::json args{
                {"archivePath", path.string()},
                {"outputDirectory", output.string()},
                {"innerPaths", nlohmann::json::array({"safe\\good.txt", protectedName})},
                {"filter", operation == std::string_view{"extractAll"} ? protectedName : ""},
                {"limit", 5000}, {"filterMode", "simple"},
                {"protectNames", true}
            };
            const auto result = backend.invoke(operation, args);
            REQUIRE_FALSE(result.has_value());
            CHECK_FALSE(std::filesystem::exists(output / "safe" / "good.txt"));
        }
    }
    const auto output = root / "desktop";
    const auto result = backend.invoke("extractSelected", {
        {"archivePath", path.string()}, {"outputDirectory", output.string()},
        {"innerPaths", nlohmann::json::array({"bin\\Payload.EXE"})}
    });
    REQUIRE(result.has_value());
    CHECK(readBytes(output / "bin" / "Payload.EXE") == bytes("executable"));
}

// Detects: MCP bulk extraction must enforce protected names through the real catalog and archive backend.
TEST_CASE("MCP archive extraction refuses protected entries and accepts safe entries") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = writeArchive(root, "mcp.ba2", {
        entry("safe\\good.txt", "good"), entry("bin\\Payload.EXE", "executable"),
        entry("ModOrganizer.ini", "settings")
    });
    fo4ide::settings::Settings settings;
    settings.mo2InstancePath = root;
    fo4ide::settings::SharedSettings shared(settings);
    fo4ide::app::test::FakeRunner runner;
    fo4ide::app::test::FakePlatform platform;
    fo4ide::tools::Locator locator(shared, root / "fo4ide");
    fo4ide::archive::RadicleArchiveBackend backend;
    fo4ide::archive::ArchiveService service(runner, locator, backend);
    fo4ide::rpc::Dispatcher rpc;
    REQUIRE(rpc.registerTarget("archive", std::make_shared<fo4ide::app::ArchiveTarget>(service, platform)).has_value());
    fo4ide::mcp::Registry registry;
    REQUIRE(fo4ide::app::registerCatalogTools(registry, rpc, {}, shared, [] { return std::vector<std::filesystem::path>{}; }));
    for (const auto& name : {"bin\\Payload.EXE", "ModOrganizer.ini"}) {
        const auto selected = root / "selected";
        const auto denied = registry.call("archive_extract_selected", {
            {"archive_path", path.string()}, {"out_dir", selected.string()},
            {"inner_paths_json", nlohmann::json::array({"safe\\good.txt", name}).dump()}
        });
        CHECK(denied.isError);
        CHECK_FALSE(std::filesystem::exists(selected / "safe" / "good.txt"));
        const auto all = root / "all";
        const auto deniedAll = registry.call("archive_extract_all", {
            {"archive_path", path.string()}, {"out_dir", all.string()}, {"filter", name}
        });
        CHECK(deniedAll.isError);
        const auto protectedPath = std::string_view{name} == "ModOrganizer.ini"
            ? all / "ModOrganizer.ini" : all / "bin" / "Payload.EXE";
        CHECK_FALSE(std::filesystem::exists(protectedPath));
    }
    const auto safe = root / "safe-output";
    const auto accepted = registry.call("archive_extract_selected", {
        {"archive_path", path.string()}, {"out_dir", safe.string()},
        {"inner_paths_json", nlohmann::json::array({"safe\\good.txt"}).dump()}
    });
    REQUIRE_FALSE(accepted.isError);
    CHECK(readBytes(safe / "safe" / "good.txt") == bytes("good"));
    const auto safeAll = root / "safe-all";
    const auto acceptedAll = registry.call("archive_extract_all", {
        {"archive_path", path.string()}, {"out_dir", safeAll.string()}, {"filter", "good.txt"}
    });
    REQUIRE_FALSE(acceptedAll.isError);
    CHECK(readBytes(safeAll / "safe" / "good.txt") == bytes("good"));
    const auto unfiltered = root / "all-unfiltered";
    const auto rejectedAll = registry.call("archive_extract_all", {
        {"archive_path", path.string()}, {"out_dir", unfiltered.string()}
    });
    CHECK(rejectedAll.isError);
    CHECK_FALSE(std::filesystem::exists(unfiltered / "safe" / "good.txt"));
}

TEST_CASE("Radicle archive backend rejects traversal before writing extraction output") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = writeArchive(root, "unsafe.ba2", {
        entry("..\\escape.txt", "escape"),
        entry("safe\\good.txt", "good")
    });
    const auto output = root / "out";

    fo4ide::archive::RadicleArchiveBackend backend;
    const nlohmann::json args{
        {"archivePath", path.string()},
        {"outputDirectory", output.string()},
        {"filter", ""},
        {"limit", 5000},
        {"filterMode", "simple"}
    };
    const auto result = backend.invoke("extractAll", args);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "ArchiveRequestInvalid");
    CHECK_FALSE(std::filesystem::exists(root / "escape.txt"));
    CHECK_FALSE(std::filesystem::exists(output / "safe" / "good.txt"));
}

#if !defined(_WIN32)
TEST_CASE("Radicle archive backend refuses to write through a symlinked output path") {
    namespace fs = std::filesystem;
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = writeArchive(root, "link.ba2", {entry("A.txt", "payload")});

    // The extraction target is a symlink to an innocent file elsewhere; the
    // backend must refuse instead of following it.
    const auto victim = root / "victim.txt";
    std::ofstream victimStream(victim, std::ios::binary);
    victimStream << "innocent";
    victimStream.close();
    const auto output = root / "out";
    fs::create_directories(output);
    const auto linkPath = output / "A.txt";
    fs::create_symlink(victim, linkPath);

    fo4ide::archive::RadicleArchiveBackend backend;
    const nlohmann::json args{
        {"archivePath", path.string()},
        {"innerPath", "A.txt"},
        {"outputPath", linkPath.string()}
    };
    const auto result = backend.invoke("extractFile", args);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "ArchiveRequestInvalid");
    // The symlink must survive untouched and the victim file unharmed.
    REQUIRE(fs::is_symlink(linkPath));
    CHECK(readBytes(victim) == bytes("innocent"));
}
#endif

TEST_CASE("Radicle archive backend reports invalid regex filters") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = writeArchive(root, "regex.ba2", {entry("A.txt", "one")});

    fo4ide::archive::RadicleArchiveBackend backend;
    const auto result = backend.invoke("list", listArguments(path, "[", "regex"));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "ArchiveFilterInvalid");
}

TEST_CASE("Radicle archive backend compares content rather than only names and sizes") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto a = writeArchive(root, "a.ba2", {
        entry("Same.txt", "same"),
        entry("Changed.txt", "aaaa"),
        entry("Removed.txt", "removed")
    });
    const auto b = writeArchive(root, "b.ba2", {
        entry("same.TXT", "same"),
        entry("Changed.txt", "bbbb"),
        entry("Added.txt", "added")
    });

    fo4ide::archive::RadicleArchiveBackend backend;
    const auto result = backend.invoke("compare", {
        {"archiveA", a.string()},
        {"archiveB", b.string()}
    });
    REQUIRE(result.has_value());
    CHECK(result->at("identicalCount") == 1);
    CHECK(result->at("added") == nlohmann::json::array({"Added.txt"}));
    CHECK(result->at("removed") == nlohmann::json::array({"Removed.txt"}));
    CHECK(result->at("changed") == nlohmann::json::array({"Changed.txt"}));
}

TEST_CASE("Radicle archive backend lists DX10 texture entries using the existing UI contract") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = writeTextureArchive(root, "textures.ba2", {
        textureEntry("Textures\\Armor\\Preview.dds", bytes("dds-payload")),
        textureEntry("Textures\\Armor\\Iron.dds", bytes("iron-payload"), true)
    });

    fo4ide::archive::RadicleArchiveBackend backend;
    const auto result = backend.invoke("list", listArguments(path, "iron"));
    REQUIRE(result.has_value());
    CHECK(result->at("archiveName") == "textures.ba2");
    CHECK(result->at("totalCount") == 2);
    CHECK(result->at("shownCount") == 1);
    REQUIRE(result->at("entries").size() == 1);
    CHECK(result->at("entries")[0].at("path") == "Textures\\Armor\\Iron.dds");
    CHECK(result->at("entries")[0].at("size") == 12);
}

TEST_CASE("Radicle archive backend extracts a compressed DX10 texture entry") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const std::string payload(4096, 'Q');
    const auto path = writeTextureArchive(root, "extract.ba2", {
        textureEntry("Textures\\Armor\\Iron.dds", bytes(payload), true)
    });
    const auto output = root / "Iron.dds";

    fo4ide::archive::RadicleArchiveBackend backend;
    const auto result = backend.invoke("extractFile", {
        {"archivePath", path.string()},
        {"innerPath", "textures/armor/IRON.dds"},
        {"outputPath", output.string()}
    });
    REQUIRE(result.has_value());
    const auto extracted = readBytes(output);
    REQUIRE(extracted.size() == 128 + payload.size());
    CHECK(extracted[0] == std::byte{'D'});
    CHECK(extracted[1] == std::byte{'D'});
    CHECK(extracted[2] == std::byte{'S'});
    CHECK(extracted[3] == std::byte{' '});
    CHECK(std::vector<std::byte>(extracted.begin() + 128, extracted.end()) == bytes(payload));
}

TEST_CASE("Radicle archive backend previews a DDS entry as a PNG data URL") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = writeTextureArchive(root, "preview.ba2", {
        textureEntry("Textures\\Preview.dds", rgba8Dds())
    });

    fo4ide::archive::RadicleArchiveBackend backend;
    const auto result = backend.invoke("previewTexture", {
        {"archivePath", path.string()},
        {"innerPath", "Textures\\Preview.dds"}
    });
    REQUIRE(result.has_value());
    const auto url = result->at("dataUrl").get<std::string>();
    CHECK(url.starts_with("data:image/png;base64,"));
    CHECK(result->at("bytes").get<std::size_t>() > 0);
}

TEST_CASE("Radicle archive backend rejects previewing a non-DDS entry") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = writeArchive(root, "nope.ba2", {entry("A.txt", "not a texture")});

    fo4ide::archive::RadicleArchiveBackend backend;
    const auto result = backend.invoke("previewTexture", {
        {"archivePath", path.string()},
        {"innerPath", "A.txt"}
    });
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "ArchiveRequestInvalid");
}

TEST_CASE("Radicle archive backend compares GNRL and DX10 archives by content") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto general = writeArchive(root, "general.ba2", {
        entry("Shared.txt", "same payload")
    });
    const auto texture = writeTextureArchive(root, "texture.ba2", {
        textureEntry("Shared.dds", bytes("same payload"))
    });

    fo4ide::archive::RadicleArchiveBackend backend;
    const auto result = backend.invoke("compare", {
        {"archiveA", general.string()},
        {"archiveB", texture.string()}
    });
    REQUIRE(result.has_value());
    CHECK(result->at("identicalCount") == 0);
    CHECK(result->at("removed") == nlohmann::json::array({"Shared.txt"}));
    CHECK(result->at("added") == nlohmann::json::array({"Shared.dds"}));
}

TEST_CASE("Radicle archive backend lists and extracts Windows-1252 entry names as UTF-8") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = writeArchive(root, "voices.ba2", {entry("Sound\\Voice\\MarXa_F.fuz", "voice")});
    auto data = readBytes(path);
    const std::string_view placeholder = "MarXa_F";
    const auto at = std::search(data.begin(), data.end(), placeholder.begin(), placeholder.end(),
                                [](std::byte b, char c) { return b == static_cast<std::byte>(c); });
    REQUIRE(at != data.end());
    *(at + 3) = std::byte{0xED};
    std::ofstream(path, std::ios::binary | std::ios::trunc)
        .write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    const std::string name = "Sound\\Voice\\Mar\xC3\xAD" "a_F.fuz";

    fo4ide::archive::RadicleArchiveBackend backend;
    const auto listed = backend.invoke("list", listArguments(path));
    REQUIRE(listed.has_value());
    CHECK(listed->at("entries")[0].at("path") == name);
    CHECK_NOTHROW(listed->dump());

    const auto output = root / "out";
    const nlohmann::json args{
        {"archivePath", path.string()},
        {"innerPaths", nlohmann::json::array({name})},
        {"outputDirectory", output.string()}
    };
    const auto extracted = backend.invoke("extractSelected", args);
    REQUIRE(extracted.has_value());
    CHECK(readBytes(output / "Sound" / "Voice" / std::filesystem::path(std::u8string(u8"Mar\u00EDa_F.fuz"))) == bytes("voice"));
}

TEST_CASE("Radicle archive backend refuses to extract over its own archive") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = writeArchive(root, "self.ba2", {entry("A.txt", "payload")});
    const auto before = readBytes(path);
    fo4ide::archive::RadicleArchiveBackend backend;

    const auto direct = backend.invoke("extractFile", {
        {"archivePath", path.string()},
        {"innerPath", "A.txt"},
        {"outputPath", path.string()}
    });
    REQUIRE_FALSE(direct.has_value());
    CHECK(direct.error().type == "ArchiveRequestInvalid");
    CHECK(readBytes(path) == before);
}

TEST_CASE("Radicle archive backend refuses a selected or all extraction onto the source archive") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = writeArchive(root, "self.ba2", {
        entry("A.txt", "payload"),
        entry("self.ba2", "replacement")
    });
    const auto before = readBytes(path);
    fo4ide::archive::RadicleArchiveBackend backend;

    const auto selected = backend.invoke("extractSelected", {
        {"archivePath", path.string()},
        {"innerPaths", nlohmann::json::array({"self.ba2"})},
        {"outputDirectory", root.string()}
    });
    REQUIRE_FALSE(selected.has_value());
    CHECK(selected.error().type == "ArchiveRequestInvalid");

    const auto all = backend.invoke("extractAll", {
        {"archivePath", path.string()},
        {"outputDirectory", root.string()},
        {"filter", ""},
        {"limit", 0},
        {"filterMode", "simple"}
    });
    REQUIRE_FALSE(all.has_value());
    CHECK(all.error().type == "ArchiveRequestInvalid");
    CHECK(readBytes(path) == before);
    CHECK_FALSE(std::filesystem::exists(root / "A.txt"));
}

namespace {

struct PackRunner final : fo4ide::process::Runner {
    int exitCode{0};
    std::string payload;
    int calls{0};
    std::filesystem::path staged;

    fo4ide::Result<fo4ide::process::Output> run(const fo4ide::process::Request& request) override {
        ++calls;
        for (const auto& argument : request.arguments) {
            if (argument.starts_with("-c=")) staged = argument.substr(3);
        }
        if (!payload.empty() && !staged.empty()) {
            std::ofstream(staged, std::ios::binary) << payload;
        }
        return fo4ide::process::Output{true, false, exitCode, {}, {}};
    }
};

}

TEST_CASE("Radicle archive backend refuses to replace an existing output without overwrite") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = writeArchive(root, "extract.ba2", {entry("A.txt", "from archive")});
    const auto output = root / "out";
    std::filesystem::create_directories(output);
    std::ofstream(output / "A.txt", std::ios::binary | std::ios::trunc) << "edited";
    fo4ide::archive::RadicleArchiveBackend backend;

    const nlohmann::json selected{
        {"archivePath", path.string()},
        {"innerPaths", nlohmann::json::array({"A.txt"})},
        {"outputDirectory", output.string()}
    };
    const auto refused = backend.invoke("extractSelected", selected);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "ArchiveRequestInvalid");
    CHECK(readBytes(output / "A.txt") == bytes("edited"));

    const nlohmann::json all{
        {"archivePath", path.string()},
        {"outputDirectory", output.string()},
        {"filter", ""},
        {"limit", 0},
        {"filterMode", "simple"}
    };
    const auto refusedAll = backend.invoke("extractAll", all);
    REQUIRE_FALSE(refusedAll.has_value());
    CHECK(refusedAll.error().type == "ArchiveRequestInvalid");
    CHECK(readBytes(output / "A.txt") == bytes("edited"));

    auto overwriteSelected = selected;
    overwriteSelected["overwrite"] = true;
    const auto replaced = backend.invoke("extractSelected", overwriteSelected);
    REQUIRE(replaced.has_value());
    CHECK(readBytes(output / "A.txt") == bytes("from archive"));

    auto overwriteAll = all;
    overwriteAll["overwrite"] = true;
    const auto replacedAll = backend.invoke("extractAll", overwriteAll);
    REQUIRE(replacedAll.has_value());
}

TEST_CASE("ArchiveService stages the packed archive and keeps the previous one on failure") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto source = root / "src";
    std::filesystem::create_directories(source);
    const auto output = root / "packed.ba2";
    std::ofstream(output, std::ios::binary) << "old";
    std::ofstream(root / "Archive2.exe", std::ios::binary) << "stub";

    fo4ide::settings::Settings settings;
    settings.archive2Path = root / "Archive2.exe";
    fo4ide::settings::SharedSettings shared(settings);
    fo4ide::tools::Locator locator(shared, root / "fo4ide.exe");
    fo4ide::archive::RadicleArchiveBackend backend;
    PackRunner runner;
    fo4ide::archive::ArchiveService service(runner, locator, backend);

    runner.payload = "partial";
    runner.exitCode = 1;
    const auto failed = service.pack({source}, output, "General", root, true);
    REQUIRE_FALSE(failed.has_value());
    CHECK(readBytes(output) == bytes("old"));

    runner.payload = "new-archive";
    runner.exitCode = 0;
    const auto packed = service.pack({source}, output, "General", root, true);
    REQUIRE(packed.has_value());
    CHECK(readBytes(output) == bytes("new-archive"));
    CHECK_FALSE(std::filesystem::exists(output.string() + ".previous"));
    for (const auto& item : std::filesystem::directory_iterator(root)) {
        CHECK_FALSE(item.path().filename().string().starts_with("packed.packing-"));
    }
}

TEST_CASE("ArchiveService refuses a pack source whose path contains a comma") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto source = root / "A,B";
    std::filesystem::create_directories(source);
    std::ofstream(root / "Archive2.exe", std::ios::binary) << "stub";

    fo4ide::settings::Settings settings;
    settings.archive2Path = root / "Archive2.exe";
    fo4ide::settings::SharedSettings shared(settings);
    fo4ide::tools::Locator locator(shared, root / "fo4ide.exe");
    fo4ide::archive::RadicleArchiveBackend backend;
    PackRunner runner;
    fo4ide::archive::ArchiveService service(runner, locator, backend);

    const auto result = service.pack({source}, root / "out.ba2", "General", root, true);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "ArchiveSourceAmbiguous");
    CHECK(runner.calls == 0);
}

// Detects: the catalog must expose archive_list so the MCP surface can read a BA2 index with filter and limit through the real archive target.
TEST_CASE("MCP archive_list lists, filters and caps BA2 entries through the real catalog") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = writeArchive(root, "list.ba2", {
        entry("Meshes\\One.nif", "mesh"), entry("Scripts\\Quest.pex", "script"), entry("Scripts\\Other.pex", "other")
    });
    fo4ide::settings::Settings settings;
    settings.mo2InstancePath = root;
    fo4ide::settings::SharedSettings shared(settings);
    fo4ide::app::test::FakeRunner runner;
    fo4ide::app::test::FakePlatform platform;
    fo4ide::tools::Locator locator(shared, root / "fo4ide");
    fo4ide::archive::RadicleArchiveBackend backend;
    fo4ide::archive::ArchiveService service(runner, locator, backend);
    fo4ide::rpc::Dispatcher rpc;
    REQUIRE(rpc.registerTarget("archive", std::make_shared<fo4ide::app::ArchiveTarget>(service, platform)).has_value());
    fo4ide::mcp::Registry registry;
    REQUIRE(fo4ide::app::registerCatalogTools(registry, rpc, {}, shared, [] { return std::vector<std::filesystem::path>{}; }));

    const auto all = registry.call("archive_list", {{"archive_path", path.string()}});
    REQUIRE_FALSE(all.isError);
    const auto listed = nlohmann::json::parse(all.text);
    CHECK(listed.at("totalCount") == 3);
    CHECK(listed.at("entries").size() == 3);

    const auto filtered = registry.call("archive_list", {{"archive_path", path.string()}, {"filter", "scripts"}});
    REQUIRE_FALSE(filtered.isError);
    CHECK(nlohmann::json::parse(filtered.text).at("entries").size() == 2);

    const auto capped = registry.call("archive_list", {{"archive_path", path.string()}, {"limit", 1}});
    REQUIRE_FALSE(capped.isError);
    const auto cappedBody = nlohmann::json::parse(capped.text);
    CHECK(cappedBody.at("entries").size() == 1);
    CHECK(cappedBody.at("truncated").get<bool>());
}
