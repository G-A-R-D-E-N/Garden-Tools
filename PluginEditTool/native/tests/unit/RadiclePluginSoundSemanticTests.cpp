#include <algorithm>
#include "../helpers/TempRoot.hpp"
#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>
#include <tuple>

#include <catch2/catch_test_macros.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "sound_record_fixture.hpp"

namespace {

namespace sound = fo4::test::sound;

struct Fixture {
    std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("fo4ide_sound_" + fo4ide_test::uniqueTempName());
    std::filesystem::path path;
    fo4ide::plugin::RadiclePluginBackend backend;

    explicit Fixture(std::vector<sound::Entry> entries = sound::fixture(),
                     std::string name = sound::k_plugin) : path(root / name) {
        std::filesystem::create_directories(root);
        fo4::write_plugin_file(sound::read(sound::build(entries)), path);
        REQUIRE(backend.invoke("environment.LoadMo2", nlohmann::json{
            {"plugins", nlohmann::json::array({{
                {"name", name}, {"path", path.string()}, {"source", "SoundTest"}, {"resolved", true}
            }})}
        }).has_value());
    }

    ~Fixture() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    auto grid(std::string_view signature) {
        return backend.invokeBackend("GetRecordsGrid", nlohmann::json::array({
            path.filename().string(), signature, 200, 0}));
    }

    auto edit(std::uint32_t id, std::string_view field, std::string_view value) {
        return backend.setFieldValue(nlohmann::json::array({
            path.filename().string(), key(id), field, value}));
    }

    std::string key(std::uint32_t id) const {
        return fo4::FormKey{fo4::PluginKey::from_filename(path.filename().string()),
                            fo4::LocalFormId{id}}.to_string();
    }

    nlohmann::json describe(std::uint32_t id, std::string_view field) {
        const auto result = static_cast<fo4ide::plugin::IPluginBackend&>(backend).describeElement(
            nlohmann::json::array({path.filename().string(), key(id), field}));
        REQUIRE(result.has_value());
        return nlohmann::json::parse(result->get<std::string>());
    }

    auto structural(std::uint32_t id, std::string_view field, bool add) {
        const auto arguments = nlohmann::json::array({path.filename().string(), key(id), field});
        auto& elements = static_cast<fo4ide::plugin::IPluginBackend&>(backend);
        return add ? elements.addElement(arguments) : elements.removeElement(arguments);
    }

    void checkNotPending() {
        const auto result = backend.invokeBackend("GetActivePlugins", nlohmann::json::array());
        REQUIRE(result.has_value());
        CHECK(nlohmann::json::parse(result->get<std::string>())[0].at("Pending") == false);
    }

    void save() {
        REQUIRE(backend.invokeBackend("SavePlugin",
            nlohmann::json::array({path.filename().string()})).has_value());
    }
};

sound::Entry& entry(std::vector<sound::Entry>& entries, std::uint32_t id) {
    const auto found = std::find_if(entries.begin(), entries.end(),
        [&](const auto& candidate) { return candidate.id == id; });
    REQUIRE(found != entries.end());
    return *found;
}

sound::Bytes& payload(std::vector<sound::Entry>& entries, std::uint32_t id, std::string_view tag) {
    auto& subs = entry(entries, id).subs;
    const auto found = std::find_if(subs.begin(), subs.end(),
        [&](const auto& candidate) { return candidate.first == tag; });
    REQUIRE(found != subs.end());
    return found->second;
}

nlohmann::json cell(const nlohmann::json& grid, std::uint32_t id, std::string_view field) {
    const auto& columns = grid.at("columns");
    const auto column = std::find_if(columns.begin(), columns.end(),
        [&](const auto& name) { return name.template get<std::string>() == field; });
    REQUIRE(column != columns.end());
    const auto index = static_cast<std::size_t>(column - columns.begin());
    const auto& rows = grid.at("rows");
    const auto row = std::find_if(rows.begin(), rows.end(),
        [&](const auto& candidate) { return candidate.at("formKey") == sound::key(id).to_string(); });
    if (row == rows.end()) { FAIL("Missing sound record " << id); }
    return {{"value", row->at("cells")[index]}, {"editable", row->at("editable")[index]}};
}

}

TEST_CASE("Sound semantic reads retain native structural refusals", "[sound-semantic]") {
    auto entries = sound::fixture();
    std::string signature;
    std::string error;
    SECTION("AutoWeapon count mismatch") {
        payload(entries, sound::k_auto, "ITMC") = sound::words({3});
        signature = "SNDR";
        error = "ScalarReadFailed";
    }
    SECTION("Incomplete packed music track") {
        payload(entries, sound::k_music, "TNAM").push_back(std::byte{0});
        signature = "MUSC";
        error = "FormLinkReadFailed";
    }
    Fixture fixture{entries};
    CAPTURE(signature, error);
    const auto result = fixture.grid(signature);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == error);
    fixture.checkNotPending();
}

TEST_CASE("Sound semantic grids list a wrong-size descriptor link and miscounted footsteps as unavailable", "[sound-semantic]") {
    auto entries = sound::fixture();
    payload(entries, sound::k_marker, "SDSC").push_back(std::byte{0});
    payload(entries, sound::k_set, "XCNT") = sound::words({0, 0, 0, 0, 0});
    Fixture fixture{entries};
    for (const auto& [signature, id, field] : {std::tuple{"SOUN", sound::k_marker, "SOUN\\SoundDescriptor"},
            std::tuple{"FSTS", sound::k_set, "FSTS\\Footsteps"},
            std::tuple{"FSTS", sound::k_set, "FSTS\\WalkingFootsteps"}}) {
        CAPTURE(signature);
        const auto result = fixture.grid(signature);
        REQUIRE(result.has_value());
        const auto grid = nlohmann::json::parse(result->get<std::string>());
        CHECK(cell(grid, id, field).at("value") == "<opaque>");
        CHECK(cell(grid, id, field).at("editable") == false);
    }
    fixture.checkNotPending();
}

TEST_CASE("Sound semantic rows distinguish variants and retain packed indices", "[sound-semantic]") {
    Fixture fixture;
    const auto result = fixture.grid("SNDR");
    REQUIRE(result.has_value());
    const auto grid = nlohmann::json::parse(result->get<std::string>());
    CHECK(cell(grid, sound::k_std, "SNDR\\FrequencyShift").at("value") == "-2");
    CHECK(cell(grid, sound::k_std, "SNDR\\Sounds[1]").at("value") == "b.wav");
    CHECK(cell(grid, sound::k_std, "SNDR\\Category").at("value") == sound::key(sound::k_snct).to_string());
    CHECK(cell(grid, sound::k_std, "SNDR\\AlternateSoundFor").at("value") == sound::key(sound::k_compound).to_string());
    CHECK(cell(grid, sound::k_std, "SNDR\\OutputModel").at("value") == sound::key(sound::k_sopm).to_string());
    for (const auto& [field, expected] : {std::pair{"ValuesUnknown", "1"}, {"Looping", "8"},
            {"Sidechain", "2"}, {"RumbleSendValue", "3"}, {"FrequencyVariance", "3"},
            {"Priority", "50"}, {"DbVariance", "4"}, {"StaticAttenuation", "10000"}}) {
        CHECK(cell(grid, sound::k_std, std::string("SNDR\\") + field).at("value") == expected);
    }
    CHECK(cell(grid, sound::k_std, "SNDR\\BaseDescriptor").at("editable") == false);
    CHECK(cell(grid, sound::k_auto, "SNDR\\FrequencyShift").at("editable") == false);
    CHECK(cell(grid, sound::k_auto, "SNDR\\BaseDescriptor").at("value") == sound::key(sound::k_std).to_string());
    CHECK(cell(grid, sound::k_auto, "SNDR\\RateOfFireRpm[1]").at("value") == "900");
    CHECK(cell(grid, sound::k_auto, "SNDR\\RateOfFireFile[1]").at("value") == "r2.wav");
    const auto music = fixture.grid("MUSC");
    REQUIRE(music.has_value());
    const auto musicGrid = nlohmann::json::parse(music->get<std::string>());
    CHECK(cell(musicGrid, sound::k_music, "MUSC\\Flags").at("value") == "65");
    CHECK(cell(musicGrid, sound::k_music, "MUSC\\Priority").at("value") == "50");
    CHECK(cell(musicGrid, sound::k_music, "MUSC\\Ducking").at("value") == "300");
    CHECK(std::stod(cell(musicGrid, sound::k_music, "MUSC\\FadeDuration").at("value").get<std::string>()) == 3.0);
    CHECK(cell(musicGrid, sound::k_music, "MUSC\\Tracks\\Count").at("value") == "2");
    CHECK(cell(musicGrid, sound::k_music, "MUSC\\Tracks[1]").at("value") == fo4::FormKey{}.to_string());
    CHECK(cell(musicGrid, sound::k_music, "MUSC\\Tracks[1]").at("editable") == true);
    const auto steps = fixture.grid("FSTS");
    REQUIRE(steps.has_value());
    CHECK(cell(nlohmann::json::parse(steps->get<std::string>()), sound::k_set,
               "FSTS\\Footsteps[3]").at("value") == sound::key(sound::k_step2).to_string());
    const auto marker = fixture.grid("SOUN");
    REQUIRE(marker.has_value());
    const auto markerGrid = nlohmann::json::parse(marker->get<std::string>());
    CHECK(cell(markerGrid, sound::k_marker, "SOUN\\SoundDescriptor").at("value") == sound::key(sound::k_std).to_string());
    CHECK(std::stod(cell(markerGrid, sound::k_marker, "SOUN\\RepeatMinTime").at("value").get<std::string>()) == 2.5);
    CHECK(std::stod(cell(markerGrid, sound::k_marker, "SOUN\\RepeatMaxTime").at("value").get<std::string>()) == 4.0);
    CHECK(cell(markerGrid, sound::k_marker, "SOUN\\RepeatStackable").at("value") == "1");
    CHECK(cell(markerGrid, sound::k_marker_old, "SOUN\\RepeatStackable").at("editable") == false);
    const auto footstep = fixture.grid("FSTP");
    REQUIRE(footstep.has_value());
    const auto footstepGrid = nlohmann::json::parse(footstep->get<std::string>());
    CHECK(cell(footstepGrid, sound::k_step, "FSTP\\Tag").at("value") == "Left");
    CHECK(cell(footstepGrid, sound::k_step, "FSTP\\ImpactDataSet").at("value") == sound::key(sound::k_ipds).to_string());
}

TEST_CASE("Sound semantic mutations preserve unrelated records and subrecords", "[sound-semantic]") {
    auto entries = sound::fixture();
    entry(entries, sound::k_std).subs.push_back({"ZZZZ", sound::raw({0x31, 0x82, 0x17})});
    Fixture fixture{entries};
    const auto before = fo4::read_plugin_file(fixture.path);
    REQUIRE(fixture.edit(sound::k_std, "SNDR\\FrequencyShift", "-5").has_value());
    REQUIRE(fixture.edit(sound::k_std, "SNDR\\Sounds[1]", "changed.wav").has_value());
    REQUIRE(fixture.edit(sound::k_std, "SNDR\\Looping", "1").has_value());
    REQUIRE(fixture.edit(sound::k_compound, "SNDR\\Descriptors[1]", sound::key(sound::k_std).to_string()).has_value());
    REQUIRE(fixture.edit(sound::k_auto, "SNDR\\RateOfFireRpm[1]", "950").has_value());
    REQUIRE(fixture.edit(sound::k_auto, "SNDR\\RateOfFireFile[0]", "new.wav").has_value());
    REQUIRE(fixture.edit(sound::k_music, "MUSC\\Tracks[1]", sound::key(sound::k_must).to_string()).has_value());
    REQUIRE(fixture.edit(sound::k_set, "FSTS\\Footsteps[3]", sound::key(sound::k_step).to_string()).has_value());
    REQUIRE(fixture.edit(sound::k_marker, "SOUN\\RepeatStackable", "0").has_value());
    REQUIRE(fixture.edit(sound::k_marker, "SOUN\\SoundDescriptor", sound::key(sound::k_auto).to_string()).has_value());
    REQUIRE(fixture.edit(sound::k_step, "FSTP\\Tag", "Rear").has_value());
    REQUIRE(fixture.edit(sound::k_step, "FSTP\\ImpactDataSet", "NULL").has_value());
    fixture.save();
    const auto after = fo4::read_plugin_file(fixture.path);
    const auto expected = sound::snapshot(before);
    const auto actual = sound::snapshot(after);
    for (const auto& [key, bytes] : expected) {
        if (key == sound::key(sound::k_std) || key == sound::key(sound::k_compound) ||
            key == sound::key(sound::k_auto) || key == sound::key(sound::k_music) || key == sound::key(sound::k_set) ||
            key == sound::key(sound::k_marker) || key == sound::key(sound::k_step)) continue;
        CHECK(actual.at(key) == bytes);
    }
    CHECK(sound::payloads(after, sound::k_std, "BNAM") ==
          std::vector<sound::Bytes>{sound::raw({0xFB, 3, 50, 4, 0x10, 0x27})});
    CHECK(sound::payloads(after, sound::k_std, "ANAM") ==
          std::vector<sound::Bytes>{sound::zstr("a.wav"), sound::zstr("changed.wav")});
    CHECK(sound::payloads(after, sound::k_std, "LNAM") ==
          std::vector<sound::Bytes>{sound::raw({1, 1, 2, 3})});
    CHECK(sound::payloads(after, sound::k_auto, "INTV") ==
          std::vector<sound::Bytes>{sound::words({600}), sound::words({950})});
    CHECK(sound::payloads(after, sound::k_auto, "FNAM") ==
          std::vector<sound::Bytes>{sound::zstr("new.wav"), sound::zstr("r2.wav")});
    CHECK(sound::payloads(after, sound::k_compound, "DNAM") ==
          std::vector<sound::Bytes>{sound::words({sound::k_std}), sound::words({sound::k_std})});
    CHECK(sound::payloads(after, sound::k_music, "TNAM") ==
          std::vector<sound::Bytes>{sound::words({sound::k_must, sound::k_must})});
    CHECK(sound::payloads(after, sound::k_set, "DATA") ==
          std::vector<sound::Bytes>{sound::words({sound::k_step, sound::k_step, sound::k_step, sound::k_step, sound::k_step})});
    CHECK(sound::payloads(after, sound::k_marker, "REPT") ==
          std::vector<sound::Bytes>{sound::raw({0, 0, 0x20, 0x40, 0, 0, 0x80, 0x40, 0})});
    CHECK(sound::payloads(after, sound::k_marker, "SDSC") == std::vector<sound::Bytes>{sound::words({sound::k_auto})});
    CHECK(sound::payloads(after, sound::k_step, "ANAM") == std::vector<sound::Bytes>{sound::zstr("Rear")});
    CHECK(sound::payloads(after, sound::k_step, "DATA") == std::vector<sound::Bytes>{sound::words({0})});
    for (const auto id : {sound::k_std, sound::k_compound, sound::k_auto, sound::k_music,
                         sound::k_set, sound::k_marker, sound::k_step}) {
        CHECK(sound::order(after, id) == sound::order(before, id));
        for (const auto& sub : fo4::find_record(before, sound::key(id))->subrecords) {
            const auto tag = sub.signature.to_string();
            const bool edited = (id == sound::k_std && (tag == "BNAM" || tag == "ANAM" || tag == "LNAM")) ||
                (id == sound::k_compound && tag == "DNAM") ||
                (id == sound::k_auto && (tag == "INTV" || tag == "FNAM")) ||
                (id == sound::k_music && tag == "TNAM") || (id == sound::k_set && tag == "DATA") ||
                (id == sound::k_marker && (tag == "REPT" || tag == "SDSC")) ||
                (id == sound::k_step && (tag == "ANAM" || tag == "DATA"));
            if (!edited) CHECK(sound::payloads(after, id, tag.c_str()) == sound::payloads(before, id, tag.c_str()));
        }
    }
}

TEST_CASE("Sound semantic refusals and unchanged edits stage nothing", "[sound-semantic]") {
    Fixture fixture;
    SECTION("Unchanged indexed rate") {
        CHECK(fixture.edit(sound::k_auto, "SNDR\\RateOfFireRpm[1]", "900").has_value());
    }
    SECTION("Wrong target type") {
        CHECK_FALSE(fixture.edit(sound::k_std, "SNDR\\Category", sound::key(sound::k_misc).to_string()).has_value());
    }
    SECTION("Nonnullable target") { CHECK_FALSE(fixture.edit(sound::k_std, "SNDR\\Category", "NULL").has_value()); }
    SECTION("Inapplicable variant") { CHECK_FALSE(fixture.edit(sound::k_auto, "SNDR\\FrequencyShift", "1").has_value()); }
    SECTION("Missing index") { CHECK_FALSE(fixture.edit(sound::k_auto, "SNDR\\RateOfFireRpm[2]", "1").has_value()); }
    SECTION("Read-only count") { CHECK_FALSE(fixture.edit(sound::k_music, "MUSC\\Tracks\\Count", "1").has_value()); }
    SECTION("Legacy absent repeat flag") { CHECK_FALSE(fixture.edit(sound::k_marker_old, "SOUN\\RepeatStackable", "1").has_value()); }
    fixture.checkNotPending();
}

TEST_CASE("Sound structural capabilities preserve coupling and native defaults", "[sound-semantic]") {
    Fixture fixture;
    CHECK(fixture.describe(sound::k_std, "Authoring\\CNAM[0]").at("canRemove") == false);
    CHECK(fixture.describe(sound::k_auto, "Authoring\\ITMC[0]").at("canRemove") == false);
    CHECK(fixture.describe(sound::k_set, "Authoring\\DATA[0]").at("canRemove") == false);
    CHECK_FALSE(fixture.structural(sound::k_std, "Authoring\\CNAM[0]", false).has_value());
    CHECK_FALSE(fixture.structural(sound::k_set, "Authoring\\DATA[0]", false).has_value());
    fixture.checkNotPending();
    CHECK(fixture.describe(sound::k_std, "Authoring\\NNAM").at("canAdd") == true);
    REQUIRE(fixture.structural(sound::k_std, "Authoring\\NNAM", true).has_value());
    fixture.save();
    CHECK(sound::payloads(fo4::read_plugin_file(fixture.path), sound::k_std, "NNAM") ==
          std::vector<sound::Bytes>{sound::zstr("")});
}

TEST_CASE("Sound protected records expose no editable semantic cells", "[sound-semantic]") {
    Fixture fixture{sound::fixture(), "Fallout4.esm"};
    const auto result = fixture.grid("SNDR");
    REQUIRE(result.has_value());
    const auto grid = nlohmann::json::parse(result->get<std::string>());
    for (const auto& row : grid.at("rows")) {
        for (const auto& editable : row.at("editable")) CHECK(editable == false);
    }
    const auto refused = fixture.edit(sound::k_std, "SNDR\\FrequencyShift", "1");
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "PluginReadOnly");
    fixture.checkNotPending();
}

TEST_CASE("Sound semantic EDID edits store Windows-1252 and refuse unrepresentable text", "[sound-semantic]") {
    Fixture fixture;
    const auto result = fixture.grid("SNDR");
    REQUIRE(result.has_value());
    CHECK(cell(nlohmann::json::parse(result->get<std::string>()), sound::k_std, "EDID").at("editable") == true);
    SECTION("Representable text is stored as Windows-1252") {
        REQUIRE(fixture.edit(sound::k_std, "EDID", "Caf\xC3\xA9").has_value());
        fixture.save();
        CHECK(sound::payloads(fo4::read_plugin_file(fixture.path), sound::k_std, "EDID") ==
              std::vector<sound::Bytes>{sound::zstr("Caf\xE9")});
    }
    SECTION("Unrepresentable text is refused without a pending edit") {
        const auto refused = fixture.edit(sound::k_std, "EDID", "\xE6\xBC\xA2");
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().type == "FieldWriteFailed");
        fixture.checkNotPending();
    }
}
