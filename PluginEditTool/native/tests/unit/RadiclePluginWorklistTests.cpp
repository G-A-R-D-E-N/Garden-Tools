#include <chrono>
#include "../helpers/TempRoot.hpp"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = fo4ide_test::uniqueTempName();
    return std::filesystem::temp_directory_path() / ("fo4ide_worklist_" + id);
}

struct Cleanup {
    std::filesystem::path root;
    ~Cleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

void writeBytes(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    REQUIRE(stream.good());
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    REQUIRE(stream.good());
}

std::vector<std::byte> zstring(std::string_view text) {
    std::vector<std::byte> bytes;
    bytes.reserve(text.size() + 1);
    for (const char ch : text) bytes.push_back(std::byte{static_cast<std::uint8_t>(ch)});
    bytes.push_back(std::byte{0});
    return bytes;
}

struct RecordSpec {
    std::uint32_t formId;
    std::string_view editorId;
};

struct GroupSpec {
    std::string_view signature;
    std::vector<RecordSpec> records;
};

std::vector<std::byte> makePlugin(std::initializer_list<std::string_view> masters,
                                  std::initializer_list<GroupSpec> groups) {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(0);
    hedr.u32(0x900);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    for (const auto master : masters) {
        fo4::test::write_subrecord(out, "MAST", zstring(master));
        fo4::test::ByteWriter data;
        data.u32(0);
        data.u32(0);
        fo4::test::write_subrecord(out, "DATA", data.bytes);
    }
    fo4::test::end_record(out, tes4);

    for (const auto& group : groups) {
        const auto grup = fo4::test::begin_group(out, group.signature, 0);
        for (const auto& spec : group.records) {
            const auto record = fo4::test::begin_record(out, group.signature, 0, spec.formId);
            fo4::test::write_subrecord(out, "EDID", zstring(spec.editorId));
            fo4::test::end_record(out, record);
        }
        fo4::test::end_group(out, grup);
    }
    return out.bytes;
}

nlohmann::json pluginEntry(std::string name, const std::filesystem::path& path) {
    return {{"name", name}, {"path", path.string()}, {"source", name}, {"resolved", true}};
}

nlohmann::json parseStringResult(const fo4ide::Result<nlohmann::json>& result) {
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    return nlohmann::json::parse(result->get<std::string>());
}

nlohmann::json worklist(fo4ide::plugin::RadiclePluginBackend& backend,
                        const nlohmann::json& arguments) {
    return parseStringResult(backend.invoke("backend.GetPluginWorklist", arguments));
}

struct Chain {
    std::filesystem::path root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::plugin::RadiclePluginBackend backend;

    Chain() {
        const auto base = root / "Base.esp";
        const auto mid = root / "Mid.esp";
        const auto top = root / "Top.esp";
        const auto orphan = root / "Orphan.esp";

        writeBytes(base, makePlugin({}, {
            {"WEAP", {{0x00000800, "Weapon"}, {0x00000801, "Untouched"}}},
            {"MISC", {{0x00000802, "Junk"}}},
        }));
        writeBytes(mid, makePlugin({"Base.esp"}, {
            {"WEAP", {{0x00000800, "WeaponMid"}, {0x01000800, "MidWeapon"}}},
            {"MISC", {{0x00000802, "JunkMid"}}},
        }));
        writeBytes(top, makePlugin({"Base.esp"}, {
            {"WEAP", {{0x00000800, "WeaponTop"}}},
        }));
        writeBytes(orphan, makePlugin({"Missing.esm"}, {
            {"WEAP", {{0x00000900, "MissingWeapon"}}},
        }));

        const nlohmann::json environment{
            {"plugins", nlohmann::json::array({
                pluginEntry("Base.esp", base),
                pluginEntry("Mid.esp", mid),
                pluginEntry("Top.esp", top),
                pluginEntry("Orphan.esp", orphan),
            })}
        };
        const auto loaded = backend.invoke("environment.LoadMo2", environment);
        REQUIRE(loaded.has_value());
        REQUIRE(loaded->at("loadedPlugins") == 4);
    }
};

}

TEST_CASE("Plugin worklist lists every added and overridden record with depth in one call") {
    Chain chain;
    const auto result = worklist(chain.backend, nlohmann::json::array({"Mid.esp"}));

    CHECK(result.at("Plugin") == "Mid.esp");
    CHECK(result.at("Total") == 3);
    CHECK(result.at("Offset") == 0);
    const auto& counts = result.at("Counts");
    CHECK(counts.at("Added") == 1);
    CHECK(counts.at("Overrides") == 2);
    CHECK(counts.at("Winning") == 2);
    CHECK(counts.at("Losing") == 1);
    CHECK(counts.at("Conflicted") == 2);

    const auto& rows = result.at("Rows");
    REQUIRE(rows.size() == 3);

    const auto& weapon = rows[0];
    CHECK(weapon.at("FormKey") == "000800:Base.esp");
    CHECK(weapon.at("EditorID") == "WeaponMid");
    CHECK(weapon.at("Type") == "WEAP");
    CHECK(weapon.at("Kind") == "Override");
    CHECK(weapon.at("OverrideDepth") == 1);
    CHECK(weapon.at("Origin") == "Base.esp");
    CHECK(weapon.at("OriginLoaded") == true);
    CHECK(weapon.at("Winner") == "Top.esp");
    CHECK(weapon.at("IsWinner") == false);
    CHECK(weapon.at("OverriddenBy") == nlohmann::json::array({"Top.esp"}));
    CHECK(weapon.at("Plugins") == nlohmann::json::array({"Base.esp", "Mid.esp", "Top.esp"}));

    const auto& added = rows[1];
    CHECK(added.at("FormKey") == "000800:Mid.esp");
    CHECK(added.at("Kind") == "Added");
    CHECK(added.at("OverrideDepth") == 0);
    CHECK(added.at("Origin") == "Mid.esp");
    CHECK(added.at("OriginLoaded") == true);
    CHECK(added.at("IsWinner") == true);
    CHECK(added.at("OverriddenBy").empty());

    const auto& junk = rows[2];
    CHECK(junk.at("FormKey") == "000802:Base.esp");
    CHECK(junk.at("Type") == "MISC");
    CHECK(junk.at("Kind") == "Override");
    CHECK(junk.at("OverrideDepth") == 1);
    CHECK(junk.at("Winner") == "Mid.esp");
    CHECK(junk.at("IsWinner") == true);
}

TEST_CASE("Plugin worklist filters by kind and signature and pages without changing counts") {
    Chain chain;

    const auto losing = worklist(chain.backend, nlohmann::json::array({"Mid.esp", "losing"}));
    CHECK(losing.at("Total") == 1);
    REQUIRE(losing.at("Rows").size() == 1);
    CHECK(losing.at("Rows")[0].at("FormKey") == "000800:Base.esp");
    CHECK(losing.at("Counts").at("Losing") == 1);
    CHECK(losing.at("Counts").at("Added") == 1);

    const auto added = worklist(chain.backend, nlohmann::json::array({"Mid.esp", "added"}));
    REQUIRE(added.at("Rows").size() == 1);
    CHECK(added.at("Rows")[0].at("FormKey") == "000800:Mid.esp");

    const auto overrides = worklist(chain.backend, nlohmann::json::array({"Mid.esp", "overrides"}));
    CHECK(overrides.at("Total") == 2);

    const auto winning = worklist(chain.backend, nlohmann::json::array({"Mid.esp", "winning"}));
    CHECK(winning.at("Total") == 2);

    const auto conflicted =
        worklist(chain.backend, nlohmann::json::array({"Mid.esp", "conflicted"}));
    CHECK(conflicted.at("Total") == 2);

    const auto misc = worklist(chain.backend, nlohmann::json::array({"Mid.esp", "all", "MISC"}));
    CHECK(misc.at("Total") == 1);
    CHECK(misc.at("Counts").at("Overrides") == 1);
    CHECK(misc.at("Counts").at("Added") == 0);
    REQUIRE(misc.at("Rows").size() == 1);
    CHECK(misc.at("Rows")[0].at("FormKey") == "000802:Base.esp");
    CHECK(worklist(chain.backend, nlohmann::json::array({"Mid.esp", "all", "misc"})).at("Total") == 1);

    const auto page = worklist(chain.backend, nlohmann::json::array({"Mid.esp", "", "", 1, 1}));
    CHECK(page.at("Total") == 3);
    CHECK(page.at("Offset") == 1);
    REQUIRE(page.at("Rows").size() == 1);
    CHECK(page.at("Rows")[0].at("FormKey") == "000800:Mid.esp");

    const auto past = worklist(chain.backend, nlohmann::json::array({"Mid.esp", "", "", 10, 50}));
    CHECK(past.at("Total") == 3);
    CHECK(past.at("Offset") == 3);
    CHECK(past.at("Rows").empty());
}

TEST_CASE("Plugin worklist reports overrides whose origin master is not loaded") {
    Chain chain;
    const auto result = worklist(chain.backend, nlohmann::json::array({"Orphan.esp"}));
    REQUIRE(result.at("Rows").size() == 1);
    const auto& row = result.at("Rows")[0];
    CHECK(row.at("FormKey") == "000900:Missing.esm");
    CHECK(row.at("Kind") == "Override");
    CHECK(row.at("OverrideDepth") == 0);
    CHECK(row.at("Origin") == "Missing.esm");
    CHECK(row.at("OriginLoaded") == false);
    CHECK(row.at("IsWinner") == true);
    CHECK(result.at("Counts").at("Conflicted") == 0);
}

TEST_CASE("Plugin worklist distinguishes a loaded origin from a missing origin record") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto base = root / "Base.esp";
    const auto patch = root / "Patch.esp";
    writeBytes(base, makePlugin({}, {}));
    writeBytes(patch, makePlugin({"Base.esp"}, {{"WEAP", {{0x00000800, "Override"}}}}));
    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", nlohmann::json{
        {"plugins", nlohmann::json::array({pluginEntry("Base.esp", base),
                                          pluginEntry("Patch.esp", patch)})}
    }).has_value());
    const auto result = worklist(backend, nlohmann::json::array({"Patch.esp"}));
    REQUIRE(result.at("Rows").size() == 1);
    CHECK(result.at("Rows")[0].at("OriginLoaded") == true);
    CHECK(result.at("Rows")[0].at("OverrideDepth") == 0);
    CHECK(result.at("Rows")[0].at("Plugins") == nlohmann::json::array({"Patch.esp"}));
}

TEST_CASE("Plugin worklist bounds unsigned pagination without signed wrapping") {
    Chain chain;
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    const auto past = worklist(chain.backend,
        nlohmann::json::array({"Mid.esp", "all", "", 1, maximum}));
    CHECK(past.at("Total") == 3);
    CHECK(past.at("Offset") == 3);
    CHECK(past.at("Rows").empty());
    const auto all = worklist(chain.backend,
        nlohmann::json::array({"Mid.esp", "all", "", maximum, 0}));
    CHECK(all.at("Rows").size() == 3);

    const auto path = chain.root / "Large.esp";
    std::vector<RecordSpec> records;
    for (std::uint32_t id = 0x800; id < 0x800 + 5001; ++id) records.push_back({id, "Large"});
    writeBytes(path, makePlugin({}, {{"WEAP", records}}));
    REQUIRE(chain.backend.invoke("environment.LoadMo2", nlohmann::json{
        {"plugins", nlohmann::json::array({pluginEntry("Large.esp", path)})}
    }).has_value());
    const auto bounded = worklist(chain.backend,
        nlohmann::json::array({"Large.esp", "all", "", maximum, 0}));
    CHECK(bounded.at("Total") == 5001);
    CHECK(bounded.at("Rows").size() == 5000);
    CHECK(worklist(chain.backend, nlohmann::json::array({"Large.esp"})).at("Rows").size() == 200);
}

TEST_CASE("Plugin worklist updates unsaved chains and restores them on discard") {
    Chain chain;
    REQUIRE(chain.backend.invoke("backend.CopyAsOverride",
        nlohmann::json::array({"Base.esp", "000801:Base.esp", "Top.esp", false})).has_value());
    const auto pending = worklist(chain.backend, nlohmann::json::array({"Top.esp"}));
    REQUIRE(pending.at("Rows").size() == 2);
    CHECK(pending.at("Counts").at("Overrides") == 2);
    CHECK(pending.at("Rows")[1].at("FormKey") == "000801:Base.esp");
    CHECK(pending.at("Rows")[1].at("OverrideDepth") == 1);
    const auto base = worklist(chain.backend, nlohmann::json::array({"Base.esp", "losing"}));
    CHECK(base.at("Total") == 3);
    REQUIRE(chain.backend.invoke("backend.DiscardPlugin",
        nlohmann::json::array({"Top.esp"})).has_value());
    CHECK(worklist(chain.backend, nlohmann::json::array({"Top.esp"})).at("Total") == 1);
    CHECK(worklist(chain.backend, nlohmann::json::array({"Base.esp", "losing"})).at("Total") == 2);
}

TEST_CASE("Plugin worklist rejects unknown plugins filters and signatures") {
    Chain chain;

    const auto missing =
        chain.backend.invoke("backend.GetPluginWorklist", nlohmann::json::array({"Nope.esp"}));
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().type == "PluginNotLoaded");

    const auto empty = chain.backend.invoke("backend.GetPluginWorklist", nlohmann::json::array());
    REQUIRE_FALSE(empty.has_value());
    CHECK(empty.error().type == "PluginMissing");

    const auto filter = chain.backend.invoke(
        "backend.GetPluginWorklist", nlohmann::json::array({"Mid.esp", "everything"}));
    REQUIRE_FALSE(filter.has_value());
    CHECK(filter.error().type == "InvalidWorklistFilter");

    const auto signature = chain.backend.invoke(
        "backend.GetPluginWorklist", nlohmann::json::array({"Mid.esp", "all", "WEAPON"}));
    REQUIRE_FALSE(signature.has_value());
    CHECK(signature.error().type == "InvalidSignature");
}

TEST_CASE("Plugin worklist covers a real plugin fixture") {
    const auto path =
        std::filesystem::path{FO4IDE_RADICLE_TESTDATA_DIR} / "FW_HeatherSurvivalPatch.esp";
    REQUIRE(std::filesystem::exists(path));

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({pluginEntry("FW_HeatherSurvivalPatch.esp", path)})}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    const auto result =
        worklist(backend, nlohmann::json::array({"FW_HeatherSurvivalPatch.esp"}));
    CHECK(result.at("Total") == 4);
    const auto& counts = result.at("Counts");
    CHECK(counts.at("Added").get<std::size_t>() + counts.at("Overrides").get<std::size_t>() == 4);
    CHECK(counts.at("Winning") == 4);
    CHECK(counts.at("Losing") == 0);

    bool sawOwnMgef = false;
    for (const auto& row : result.at("Rows")) {
        CHECK(row.at("IsWinner") == true);
        if (row.at("FormKey") == "000800:FW_HeatherSurvivalPatch.esp") {
            sawOwnMgef = true;
            CHECK(row.at("Type") == "MGEF");
            CHECK(row.at("Kind") == "Added");
        }
        if (row.at("Kind") == "Override") {
            CHECK(row.at("OriginLoaded") == false);
            CHECK(row.at("OverrideDepth") == 0);
        }
    }
    CHECK(sawOwnMgef);
}
