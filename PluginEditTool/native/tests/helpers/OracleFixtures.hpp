#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/plugin/reader.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "TempRoot.hpp"
#include "plugin_bytes.hpp"

namespace oracle_fixture {

inline std::filesystem::path tempRoot() {
    return std::filesystem::temp_directory_path() / ("fo4ide_oracle_" + fo4ide_test::uniqueTempName());
}

struct Cleanup {
    std::filesystem::path root;
    ~Cleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

inline void writeBytes(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    REQUIRE(stream.good());
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(stream.good());
}

inline std::string readFile(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

inline void writeHeader(fo4::test::ByteWriter& out, float version, std::uint32_t flags,
                        std::uint32_t nextFormId, std::initializer_list<std::string_view> masters = {}) {
    const auto tes4 = fo4::test::begin_record(out, "TES4", flags, 0, 131);
    fo4::test::ByteWriter hedr;
    hedr.f32(version);
    hedr.u32(0);
    hedr.u32(nextFormId);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    for (const auto master : masters) {
        const std::string name = std::string(master) + '\0';
        fo4::test::write_subrecord(out, "MAST", std::string_view{name});
        fo4::test::ByteWriter data;
        data.u32(0);
        data.u32(0);
        fo4::test::write_subrecord(out, "DATA", data.bytes);
    }
    fo4::test::end_record(out, tes4);
}

inline void writeNamedRecord(fo4::test::ByteWriter& out, std::string_view type, std::uint32_t rawId,
                             const std::string& editorId) {
    const auto record = fo4::test::begin_record(out, type, 0, rawId, 131);
    const auto data = editorId + '\0';
    fo4::test::write_subrecord(out, "EDID", std::string_view{data});
    fo4::test::end_record(out, record);
}

inline std::vector<std::byte> miscPlugin(float version, std::uint32_t nextFormId, bool hasMaster,
                                         const std::vector<std::uint32_t>& localIds,
                                         std::uint32_t flags = 0) {
    fo4::test::ByteWriter out;
    if (hasMaster) {
        writeHeader(out, version, flags, nextFormId, {"Base.esm"});
    } else {
        writeHeader(out, version, flags, nextFormId);
    }
    const auto group = fo4::test::begin_group(out, "MISC", 0);
    for (const auto local : localIds) {
        writeNamedRecord(out, "MISC", (hasMaster ? 0x01000000u : 0u) | local,
                         "Record" + std::to_string(local));
    }
    fo4::test::end_group(out, group);
    return out.bytes;
}

inline nlohmann::json pluginEntry(const std::string& name, const std::filesystem::path& path) {
    return {{"name", name}, {"path", path.string()}, {"source", "test"}, {"resolved", true}};
}

inline void loadPlugins(fo4ide::plugin::RadiclePluginBackend& backend,
                        const std::vector<std::pair<std::string, std::filesystem::path>>& plugins) {
    nlohmann::json entries = nlohmann::json::array();
    for (const auto& [name, path] : plugins) entries.push_back(pluginEntry(name, path));
    const auto loaded = backend.invoke("environment.LoadMo2", nlohmann::json{{"plugins", entries}});
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->at("loadedPlugins") == plugins.size());
}

inline void loadPatch(fo4ide::plugin::RadiclePluginBackend& backend, const std::filesystem::path& path) {
    loadPlugins(backend, {{"Patch.esp", path}});
}

inline void savePatch(fo4ide::plugin::RadiclePluginBackend& backend) {
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
}

inline std::string textResult(const fo4ide::Result<nlohmann::json>& result) {
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    return result->get<std::string>();
}

inline nlohmann::json parseStringResult(const fo4ide::Result<nlohmann::json>& result) {
    return nlohmann::json::parse(textResult(result));
}

inline const fo4::Record* recordAt(const fo4::Plugin& plugin, std::uint32_t local) {
    return fo4::find_record(plugin, fo4::FormKey{plugin.key, fo4::LocalFormId{local}});
}

inline std::string editorIdOf(const fo4::Record& record) {
    for (const auto& subrecord : record.subrecords) {
        if (subrecord.signature != fo4::sig_edid) continue;
        const auto* begin = reinterpret_cast<const char*>(subrecord.data.data());
        const auto* nul = std::find(begin, begin + subrecord.data.size(), '\0');
        return std::string(begin, nul);
    }
    return {};
}

inline std::uint32_t formLinkOf(const fo4::Record& record, std::string_view signature) {
    const auto wanted = fo4::Signature::from_string(signature);
    for (const auto& subrecord : record.subrecords) {
        if (subrecord.signature != wanted) continue;
        REQUIRE(subrecord.data.size() == 4);
        std::uint32_t value = 0;
        std::memcpy(&value, subrecord.data.data(), sizeof(value));
        return value;
    }
    FAIL("missing FormLink subrecord");
}

}  // namespace oracle_fixture
