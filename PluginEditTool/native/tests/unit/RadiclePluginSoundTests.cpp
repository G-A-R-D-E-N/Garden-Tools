#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/plugin/reader.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path soundTempRoot() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / ("fo4ide_sound_edit_" + std::to_string(id));
}

struct SoundCleanup {
    std::filesystem::path root;
    ~SoundCleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

void soundWriteBytes(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    REQUIRE(stream.good());
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(stream.good());
}

std::vector<std::byte> soundReadBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    REQUIRE(stream.good());
    const std::vector<char> raw((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    std::vector<std::byte> bytes;
    bytes.reserve(raw.size());
    for (const char ch : raw) bytes.push_back(std::byte{static_cast<std::uint8_t>(ch)});
    return bytes;
}

void soundHeader(fo4::test::ByteWriter& out) {
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(0);
    hedr.u32(0x900);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);
}

std::vector<std::byte> soundZ(std::string_view text) {
    std::vector<std::byte> out;
    for (const char ch : text) out.push_back(std::byte{static_cast<std::uint8_t>(ch)});
    out.push_back(std::byte{0});
    return out;
}

std::vector<std::byte> soundDword(std::uint32_t value) {
    std::vector<std::byte> out(4, std::byte{0});
    std::memcpy(out.data(), &value, 4);
    return out;
}

std::vector<std::byte> soundPlugin() {
    fo4::test::ByteWriter out;
    soundHeader(out);
    const auto sndr = fo4::test::begin_group(out, "SNDR", 0);
    const auto auto_record = fo4::test::begin_record(out, "SNDR", 0, 0x800);
    fo4::test::write_subrecord(out, "EDID", soundZ("Auto"));
    fo4::test::write_subrecord(out, "CNAM", soundDword(0xED157AE3u));
    fo4::test::write_subrecord(out, "ANAM", soundZ("a.wav"));
    fo4::test::end_record(out, auto_record);
    const auto standard_record = fo4::test::begin_record(out, "SNDR", 0, 0x801);
    fo4::test::write_subrecord(out, "EDID", soundZ("Standard"));
    fo4::test::write_subrecord(out, "CNAM", soundDword(0x1EEF540Au));
    fo4::test::write_subrecord(out, "ANAM", soundZ("b.wav"));
    fo4::test::end_record(out, standard_record);
    fo4::test::end_group(out, sndr);
    const auto misc = fo4::test::begin_group(out, "MISC", 0);
    const auto misc_record = fo4::test::begin_record(out, "MISC", 0, 0x802);
    fo4::test::write_subrecord(out, "EDID", soundZ("Other"));
    fo4::test::end_record(out, misc_record);
    fo4::test::end_group(out, misc);
    return out.bytes;
}

nlohmann::json soundPluginEntry(std::string name, const std::filesystem::path& path) {
    return {{"name", std::move(name)}, {"path", path.string()}, {"source", "test"}, {"resolved", true}};
}

void soundLoad(fo4ide::plugin::RadiclePluginBackend& backend, const std::filesystem::path& path) {
    const nlohmann::json environment{{"plugins", nlohmann::json::array({soundPluginEntry("Snd.esp", path)})}};
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());
}

bool soundPending(fo4ide::plugin::RadiclePluginBackend& backend) {
    const auto plugins = backend.invoke("appInterop.GetPlugins", nlohmann::json::array());
    REQUIRE(plugins.has_value());
    return nlohmann::json::parse(plugins->get<std::string>()).at(0).at("Pending").get<bool>();
}

}  // namespace

TEST_CASE("Native sound structure edits add, remove, set and create through save and re-read") {
    const auto root = soundTempRoot();
    SoundCleanup cleanup{root};
    const auto path = root / "Snd.esp";
    soundWriteBytes(path, soundPlugin());
    fo4ide::plugin::RadiclePluginBackend backend;
    soundLoad(backend, path);
    const std::string autoKey = "000800:Snd.esp";
    REQUIRE(backend.addSoundFile(nlohmann::json::array({"Snd.esp", autoKey, "c.wav"})).has_value());
    REQUIRE(backend.removeSoundFile(nlohmann::json::array({"Snd.esp", autoKey, 1})).has_value());
    REQUIRE(backend.setSoundBase(nlohmann::json::array({"Snd.esp", autoKey, "000801:Snd.esp"})).has_value());
    REQUIRE(backend.setSoundBase(nlohmann::json::array({"Snd.esp", autoKey, nullptr})).has_value());
    REQUIRE(backend.setSoundBase(nlohmann::json::array({"Snd.esp", autoKey, "000801:Snd.esp"})).has_value());
    const auto marker = backend.createSound(nlohmann::json::array({"Snd.esp", "SOUN", "NewMarker"}));
    REQUIRE(marker.has_value());
    const auto steps = backend.createSound(nlohmann::json::array({"Snd.esp", "FSTS", "NewSteps"}));
    REQUIRE(steps.has_value());
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Snd.esp"})).has_value());
    const auto saved = fo4::read_plugin_file(path);
    const auto* record = fo4::find_record(saved, fo4::FormKey::parse(autoKey));
    REQUIRE(record != nullptr);
    bool has_file = false;
    bool has_base = false;
    for (const auto& sub : record->subrecords) {
        if (sub.signature.to_string() == "ANAM") {
            const std::string_view text(reinterpret_cast<const char*>(sub.data.data()), sub.data.size());
            if (text == std::string_view{"a.wav\0", 6}) has_file = true;
        }
        if (sub.signature.to_string() == "BNAM" && sub.data.size() == 4) has_base = true;
    }
    CHECK(has_file);
    CHECK(has_base);
    CHECK_FALSE(soundPending(backend));
}

TEST_CASE("Native sound refusals leave the plugin and file untouched") {
    const auto root = soundTempRoot();
    SoundCleanup cleanup{root};
    const auto path = root / "Snd.esp";
    soundWriteBytes(path, soundPlugin());
    const auto before = soundReadBytes(path);
    fo4ide::plugin::RadiclePluginBackend backend;
    soundLoad(backend, path);
    const auto refuse = [](const fo4ide::Result<nlohmann::json>& result, const char* type) {
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().type == type);
    };
    refuse(backend.addSoundFile(nlohmann::json::array({"Snd.esp", "000800:Snd.esp", ""})), "InvalidSoundFile");
    refuse(backend.addSoundFile(nlohmann::json::array({"Missing.esp", "000800:Snd.esp", "x.wav"})),
           "PluginNotLoaded");
    refuse(backend.addSoundFile(nlohmann::json::array({"Snd.esp", "000802:Snd.esp", "x.wav"})),
           "SoundWriteFailed");
    refuse(backend.removeSoundFile(nlohmann::json::array({"Snd.esp", "000800:Snd.esp", 9})), "SoundWriteFailed");
    refuse(backend.setSoundBase(nlohmann::json::array({"Snd.esp", "000801:Snd.esp", "000800:Snd.esp"})),
           "SoundWriteFailed");
    refuse(backend.createSound(nlohmann::json::array({"Snd.esp", "MISC", "Nope"})), "SoundRecordUnsupported");
    refuse(backend.createSound(nlohmann::json::array({"Snd.esp", "SOUN", ""})), "InvalidEditorId");
    CHECK_FALSE(soundPending(backend));
    CHECK(soundReadBytes(path) == before);
}
