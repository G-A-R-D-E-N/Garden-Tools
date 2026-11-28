#include <chrono>
#include "../helpers/TempRoot.hpp"
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/plugin/reader.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = fo4ide_test::uniqueTempName();
    return std::filesystem::temp_directory_path() / ("fo4ide_backend_hygiene_" + id);
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

std::string readText(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    REQUIRE(stream.good());
    return std::string{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

std::vector<std::byte> makeTarget() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(1);
    hedr.u32(0x811);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto group = fo4::test::begin_group(out, "MISC", 0);
    const auto record = fo4::test::begin_record(out, "MISC", 0, 0x00000810);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"TargetLocal\0", 12});
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    return out.bytes;
}

nlohmann::json pluginEntry(std::string name, const std::filesystem::path& path) {
    return {
        {"name", std::move(name)},
        {"path", path.string()},
        {"source", "test"},
        {"resolved", true}
    };
}

nlohmann::json entryWith(const char* key, nlohmann::json value, const std::filesystem::path& path) {
    auto entry = pluginEntry("Patch.esp", path);
    entry[key] = std::move(value);
    return entry;
}

nlohmann::json environmentFor(const nlohmann::json& entry) {
    return nlohmann::json{{"plugins", nlohmann::json::array({entry})}};
}

std::filesystem::path touchedPath;

void touchTouchedPathLater() {
    const auto stamp = std::filesystem::last_write_time(touchedPath);
    std::filesystem::last_write_time(touchedPath, stamp + std::chrono::seconds(2));
}

struct ReadWindowHookGuard {
    explicit ReadWindowHookGuard(fo4ide::plugin::LoadReadWindowHook hook) {
        fo4ide::plugin::setLoadReadWindowHook(hook);
    }
    ~ReadWindowHookGuard() { fo4ide::plugin::setLoadReadWindowHook(nullptr); }
    ReadWindowHookGuard(const ReadWindowHookGuard&) = delete;
    ReadWindowHookGuard& operator=(const ReadWindowHookGuard&) = delete;
};

}

TEST_CASE("Native LoadMo2 rejects a wrong-typed environment field without throwing") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, makeTarget());

    fo4ide::plugin::RadiclePluginBackend backend;
    const auto badName = backend.invoke(
        "environment.LoadMo2", environmentFor(entryWith("name", 5, path)));
    REQUIRE_FALSE(badName.has_value());
    CHECK(badName.error().type == "InvalidEnvironment");

    const auto badResolved = backend.invoke(
        "environment.LoadMo2", environmentFor(entryWith("resolved", "yes", path)));
    REQUIRE_FALSE(badResolved.has_value());
    CHECK(badResolved.error().type == "InvalidEnvironment");
}

TEST_CASE("requirePluginUnchangedSince refuses a stamp that no longer matches the file") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, makeTarget());
    const auto stamp = std::filesystem::last_write_time(path);
    REQUIRE(fo4ide::plugin::requirePluginUnchangedSince(path, stamp).has_value());

    std::filesystem::last_write_time(path, stamp + std::chrono::seconds(2));
    const auto stale = fo4ide::plugin::requirePluginUnchangedSince(path, stamp);
    REQUIRE_FALSE(stale.has_value());
    CHECK(stale.error().type == "PluginChangedOnDisk");
    CHECK(stale.error().message == path.string() + " changed while it was being loaded; try again");
}

TEST_CASE("Native LoadMo2 refuses a plugin whose stamp moves while it is read") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, makeTarget());
    touchedPath = path;
    const ReadWindowHookGuard guard(&touchTouchedPathLater);

    fo4ide::plugin::RadiclePluginBackend backend;
    const auto result = backend.invoke(
        "environment.LoadMo2", environmentFor(pluginEntry("Patch.esp", path)));
    REQUIRE(result.has_value());
    CHECK(result->at("loadedPlugins") == 0);
    CHECK(result->at("failedPlugins") == 1);
    REQUIRE(result->at("errors").size() == 1);
    CHECK(result->at("errors")[0].at("error") ==
          path.string() + " changed while it was being loaded; try again");
}

TEST_CASE("Native OpenPlugin refuses a plugin whose stamp moves while it is read") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, makeTarget());
    touchedPath = path;

    fo4ide::plugin::RadiclePluginBackend backend;
    {
        const ReadWindowHookGuard guard(&touchTouchedPathLater);
        const auto opened = backend.invoke(
            "backend.OpenPlugin", nlohmann::json::array({path.string()}));
        REQUIRE_FALSE(opened.has_value());
        CHECK(opened.error().type == "PluginChangedOnDisk");
        CHECK(opened.error().message ==
              path.string() + " changed while it was being loaded; try again");
    }
    CHECK(backend.invoke(
        "backend.OpenPlugin", nlohmann::json::array({path.string()})).has_value());
}

TEST_CASE("Native SavePlugin Save As refuses to replace a different existing plugin without overwrite") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    const auto other = root / "Other.esp";
    writeBytes(path, makeTarget());
    writeBytes(other, std::vector<std::byte>{std::byte{0x78}, std::byte{0x79}});
    const auto before = readText(other);

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke(
        "environment.LoadMo2", environmentFor(pluginEntry("Patch.esp", path))).has_value());

    const auto refused = backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp", other.string()}));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "SaveTargetExists");
    CHECK(readText(other) == before);

    const auto invalid = backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp", other.string(), "yes"}));
    REQUIRE_FALSE(invalid.has_value());
    CHECK(invalid.error().type == "InvalidOverwriteFlag");
    CHECK(readText(other) == before);
}

TEST_CASE("Native SavePlugin Save As replaces a different existing plugin with overwrite") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    const auto other = root / "Other.esp";
    writeBytes(path, makeTarget());
    writeBytes(other, std::vector<std::byte>{std::byte{0x78}, std::byte{0x79}});
    const auto before = readText(other);

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke(
        "environment.LoadMo2", environmentFor(pluginEntry("Patch.esp", path))).has_value());

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp", other.string(), true})).has_value());
    CHECK(readText(other) != before);
    const auto saved = fo4::read_plugin_file(other);
    CHECK(fo4::find_record(saved, fo4::FormKey::parse("000810:Other.esp")) != nullptr);
}

TEST_CASE("Native SavePlugin writes to the loaded path and to a new path without overwrite") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    const auto fresh = root / "Fresh.esp";
    writeBytes(path, makeTarget());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke(
        "environment.LoadMo2", environmentFor(pluginEntry("Patch.esp", path))).has_value());

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp", path.string()})).has_value());
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp", fresh.string()})).has_value());
    CHECK(std::filesystem::exists(fresh));
    CHECK(fo4::read_plugin_file(fresh).key.filename() == "Fresh.esp");
}
