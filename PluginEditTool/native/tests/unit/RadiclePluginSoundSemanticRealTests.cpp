#include <algorithm>
#include "../helpers/TempRoot.hpp"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>
#include <fo4/environment/sound.hpp>
#include <fo4/plugin/mutation.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/plugin/writer.hpp>
#include <fo4/text/encoding.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

namespace {

struct TempRoot {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("fo4ide_sound_real_" + fo4ide_test::uniqueTempName());
    ~TempRoot() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

nlohmann::json parse(const fo4ide::Result<nlohmann::json>& result) {
    REQUIRE(result.has_value());
    return nlohmann::json::parse(result->get<std::string>());
}

std::string value(const nlohmann::json& grid, const fo4::FormKey& key, std::string_view path) {
    const auto& columns = grid.at("columns");
    const auto column = std::find_if(columns.begin(), columns.end(), [&](const auto& name) {
        return name.template get<std::string>() == path;
    });
    REQUIRE(column != columns.end());
    const auto& rows = grid.at("rows");
    const auto row = std::find_if(rows.begin(), rows.end(),
        [&](const auto& candidate) { return candidate.at("formKey") == key.to_string(); });
    if (row == rows.end()) { FAIL("Missing official sound override " << key.to_string()); }
    const auto index = static_cast<std::size_t>(column - columns.begin());
    CHECK(row->at("editable")[index] == true);
    return row->at("cells")[index].template get<std::string>();
}

}

TEST_CASE("Official normal and AutoWeapon sounds edit through semantic backend", "[sound-real]") {
    const auto* dir = std::getenv("FO4_DATA_DIR");
    if (dir == nullptr) SKIP("Set FO4_DATA_DIR for official sound acceptance");
    const auto master = std::filesystem::path{dir} / "Fallout4.esm";
    REQUIRE(std::filesystem::is_regular_file(master));
    const std::filesystem::path sources[]{master};
    const auto source = fo4::env::Environment::load(sources);
    const fo4::env::RecordRef* normal = nullptr;
    const fo4::env::RecordRef* automatic = nullptr;
    for (const auto* ref : source.winners_of(fo4::Signature::from_string("SNDR"))) {
        const auto data = fo4::env::decode_sound_descriptor(source, *ref);
        if (normal == nullptr && data.playback && !data.files.empty()) normal = ref;
        if (automatic == nullptr && data.base_descriptor && !data.rate_files.empty()) automatic = ref;
        if (normal != nullptr && automatic != nullptr) break;
    }
    REQUIRE(normal != nullptr);
    REQUIRE(automatic != nullptr);
    const auto normalKey = normal->record->form_key;
    const auto autoKey = automatic->record->form_key;
    CAPTURE(normalKey.to_string(), autoKey.to_string());
    const auto originalNormal = fo4::env::decode_sound_descriptor(source, *normal);
    const auto originalAuto = fo4::env::decode_sound_descriptor(source, *automatic);
    auto patch = fo4::make_empty_plugin("Acceptance.esp");
    REQUIRE(fo4::copy_record_as_override(patch, source.plugin(0), normalKey, false) == fo4::RecordCopyStatus::inserted);
    REQUIRE(fo4::copy_record_as_override(patch, source.plugin(0), autoKey, false) == fo4::RecordCopyStatus::inserted);
    TempRoot root;
    std::filesystem::create_directories(root.path);
    const auto path = root.path / "Acceptance.esp";
    fo4::write_plugin_file(patch, path);
    const auto shift = originalNormal.playback->frequency_shift == 1 ? "2" : "1";
    const auto rpm = originalAuto.rate_files[0].rpm == 123 ? "124" : "123";
    {
        fo4ide::plugin::RadiclePluginBackend backend;
        REQUIRE(backend.invoke("environment.LoadMo2", nlohmann::json{
            {"plugins", nlohmann::json::array({
                {{"name", "Fallout4.esm"}, {"path", master.string()}, {"source", "SoundReal"}, {"resolved", true}},
                {{"name", "Acceptance.esp"}, {"path", path.string()}, {"source", "SoundReal"}, {"resolved", true}}
            })}
        }).has_value());
        const auto grid = parse(backend.invokeBackend("GetRecordsGrid",
            nlohmann::json::array({"Acceptance.esp", "SNDR", 200, 0})));
        CHECK(value(grid, normalKey, "SNDR\\FrequencyShift") == std::to_string(originalNormal.playback->frequency_shift));
        CHECK(value(grid, normalKey, "SNDR\\Sounds[0]") == fo4::text::windows1252_to_utf8(originalNormal.files[0]));
        CHECK(value(grid, autoKey, "SNDR\\BaseDescriptor") == originalAuto.base_descriptor->to_string());
        for (std::size_t index = 0; index < originalAuto.rate_files.size(); ++index) {
            const auto suffix = "[" + std::to_string(index) + "]";
            CHECK(value(grid, autoKey, "SNDR\\RateOfFireRpm" + suffix) == std::to_string(originalAuto.rate_files[index].rpm));
            CHECK(value(grid, autoKey, "SNDR\\RateOfFireFile" + suffix) ==
                  fo4::text::windows1252_to_utf8(originalAuto.rate_files[index].file));
        }
        REQUIRE(backend.setFieldValue(nlohmann::json::array({
            "Acceptance.esp", normalKey.to_string(), "SNDR\\FrequencyShift", shift})).has_value());
        REQUIRE(backend.setFieldValue(nlohmann::json::array({
            "Acceptance.esp", normalKey.to_string(), "SNDR\\Sounds[0]", "semantic-acceptance.wav"})).has_value());
        REQUIRE(backend.setFieldValue(nlohmann::json::array({
            "Acceptance.esp", autoKey.to_string(), "SNDR\\RateOfFireRpm[0]", rpm})).has_value());
        REQUIRE(backend.setFieldValue(nlohmann::json::array({
            "Acceptance.esp", autoKey.to_string(), "SNDR\\RateOfFireFile[0]", "rate-acceptance.wav"})).has_value());
        REQUIRE(backend.invokeBackend("SavePlugin", nlohmann::json::array({"Acceptance.esp"})).has_value());
    }
    const std::filesystem::path saved[]{path};
    const auto after = fo4::env::Environment::load(saved);
    REQUIRE(after.winner(normalKey) != nullptr);
    REQUIRE(after.winner(autoKey) != nullptr);
    const auto normalData = fo4::env::decode_sound_descriptor(after, *after.winner(normalKey));
    REQUIRE(normalData.playback.has_value());
    CHECK(normalData.playback->frequency_shift == std::stoi(shift));
    CHECK(normalData.playback->frequency_variance == originalNormal.playback->frequency_variance);
    CHECK(normalData.playback->priority == originalNormal.playback->priority);
    CHECK(normalData.playback->db_variance == originalNormal.playback->db_variance);
    CHECK(normalData.playback->attenuation_hundredths == originalNormal.playback->attenuation_hundredths);
    REQUIRE(normalData.files.size() == originalNormal.files.size());
    CHECK(normalData.files[0] == "semantic-acceptance.wav");
    for (std::size_t index = 1; index < normalData.files.size(); ++index) CHECK(normalData.files[index] == originalNormal.files[index]);
    const auto autoData = fo4::env::decode_sound_descriptor(after, *after.winner(autoKey));
    REQUIRE(autoData.rate_files.size() == originalAuto.rate_files.size());
    CHECK(autoData.rate_files[0].rpm == static_cast<std::uint32_t>(std::stoul(rpm)));
    CHECK(autoData.rate_files[0].file == "rate-acceptance.wav");
    for (std::size_t index = 1; index < autoData.rate_files.size(); ++index) {
        CHECK(autoData.rate_files[index].rpm == originalAuto.rate_files[index].rpm);
        CHECK(autoData.rate_files[index].file == originalAuto.rate_files[index].file);
    }
    for (const auto& key : {normalKey, autoKey}) {
        const auto* before = fo4::find_record(patch, key);
        const auto* rewritten = after.winner(key)->record;
        REQUIRE(rewritten->subrecords.size() == before->subrecords.size());
        for (std::size_t index = 0; index < before->subrecords.size(); ++index) {
            const auto& old = before->subrecords[index];
            CHECK(rewritten->subrecords[index].signature == old.signature);
            const bool changed = key == normalKey
                ? old.signature == fo4::Signature::from_string("BNAM") || old.signature == fo4::Signature::from_string("ANAM")
                : old.signature == fo4::Signature::from_string("INTV") || old.signature == fo4::Signature::from_string("FNAM");
            if (!changed) CHECK(rewritten->subrecords[index].data == old.data);
        }
    }
}
