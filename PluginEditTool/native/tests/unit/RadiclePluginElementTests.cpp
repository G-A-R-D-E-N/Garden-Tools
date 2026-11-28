#include <chrono>
#include "../helpers/TempRoot.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/plugin/reader.hpp>
#include <fo4ide/plugin/IPluginBackend.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = fo4ide_test::uniqueTempName();
    return std::filesystem::temp_directory_path() /
           ("fo4ide_element_" + id);
}

struct Cleanup {
    std::filesystem::path root;
    ~Cleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

std::vector<std::byte> pluginBytes() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(3);
    hedr.u32(0x803);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto colors = fo4::test::begin_group(out, "CLFM", 0);
    auto color = fo4::test::begin_record(out, "CLFM", 0, 0x00000800, 131);
    fo4::test::end_record(out, color);
    color = fo4::test::begin_record(out, "CLFM", 0, 0x00000801, 131);
    fo4::test::end_record(out, color);
    fo4::test::end_group(out, colors);

    const auto races = fo4::test::begin_group(out, "RACE", 0);
    const auto race = fo4::test::begin_record(out, "RACE", 0, 0x00000802, 131);
    fo4::test::ByteWriter first;
    first.u32(0x00000800);
    fo4::test::write_subrecord(out, "AHCM", first.bytes);
    fo4::test::ByteWriter second;
    second.u32(0x00000801);
    fo4::test::write_subrecord(out, "AHCM", second.bytes);
    fo4::test::end_record(out, race);
    fo4::test::end_group(out, races);
    return out.bytes;
}

void writeBytes(const std::filesystem::path& path,
                const std::vector<std::byte>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    REQUIRE(stream.good());
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    REQUIRE(stream.good());
}

void load(fo4ide::plugin::RadiclePluginBackend& backend,
          const std::filesystem::path& path) {
    const auto result = backend.invoke(
        "environment.LoadMo2",
        nlohmann::json{{"plugins", nlohmann::json::array({{
            {"name", "Patch.esp"},
            {"path", path.string()},
            {"source", "ElementTest"},
            {"resolved", true}
        }})}});
    REQUIRE(result.has_value());
}

nlohmann::json parsed(const fo4ide::Result<nlohmann::json>& result) {
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    return nlohmann::json::parse(result->get<std::string>());
}

std::vector<std::uint32_t> rawLinks(const std::filesystem::path& path) {
    const auto plugin = fo4::read_plugin_file(path);
    const auto* race = fo4::find_record(
        plugin, fo4::FormKey::parse("000802:Patch.esp"));
    REQUIRE(race != nullptr);
    const auto signature = fo4::Signature::from_string("AHCM");
    std::vector<std::uint32_t> values;
    for (const auto& subrecord : race->subrecords) {
        if (subrecord.signature != signature) continue;
        REQUIRE(subrecord.data.size() == 4);
        std::uint32_t raw = 0;
        std::memcpy(&raw, subrecord.data.data(), sizeof(raw));
        values.push_back(raw);
    }
    return values;
}

}  // namespace

TEST_CASE("Native ElementService handles the current raw conflict-matrix FormLink path") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, pluginBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    fo4ide::plugin::IPluginBackend& elementBackend = backend;

    const auto actions = parsed(elementBackend.describeElement(nlohmann::json::array({
        "Patch.esp", "000802:Patch.esp", "Subrecords\\AHCM[0]"
    })));
    CHECK(actions.at("count") == 2);
    CHECK(actions.at("canAdd") == true);
    CHECK(actions.at("canRemove") == true);
    CHECK(actions.at("canMoveUp") == false);
    CHECK(actions.at("canMoveDown") == true);
    REQUIRE(actions.at("templates").size() == 1);
    CHECK(actions.at("templates")[0] == "FormLink<CLFM>");

    const auto added = elementBackend.addElement(nlohmann::json::array({
        "Patch.esp", "000802:Patch.esp", "Subrecords\\AHCM[0]", "FormLink<CLFM>"
    }));
    REQUIRE(added.has_value());
    CHECK(rawLinks(path) == std::vector<std::uint32_t>{0x00000800u, 0x00000801u});

    const auto moved = elementBackend.moveElement(nlohmann::json::array({
        "Patch.esp", "000802:Patch.esp", "Subrecords\\AHCM[0]", 1
    }));
    REQUIRE(moved.has_value());
    CHECK(rawLinks(path) == std::vector<std::uint32_t>{0x00000800u, 0x00000801u});

    const auto removed = elementBackend.removeElement(nlohmann::json::array({
        "Patch.esp", "000802:Patch.esp", "Subrecords\\AHCM[1]"
    }));
    REQUIRE(removed.has_value());
    CHECK(rawLinks(path) == std::vector<std::uint32_t>{0x00000800u, 0x00000801u});

    const auto cleared = elementBackend.clearElement(nlohmann::json::array({
        "Patch.esp", "000802:Patch.esp", "RACE\\MaleHairColors"
    }));
    REQUIRE(cleared.has_value());
    CHECK(rawLinks(path) == std::vector<std::uint32_t>{0x00000800u, 0x00000801u});

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    CHECK(rawLinks(path).empty());
}

TEST_CASE("Native ElementService fails closed for unregistered raw subrecords") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, pluginBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    fo4ide::plugin::IPluginBackend& elementBackend = backend;

    const auto actions = parsed(elementBackend.describeElement(nlohmann::json::array({
        "Patch.esp", "000802:Patch.esp", "Subrecords\\EDID[0]"
    })));
    REQUIRE(actions.contains("error"));
    CHECK(actions.at("error").get<std::string>().find("not registered") != std::string::npos);
}

namespace {

using Bytes = std::vector<std::byte>;

struct Piece {
    std::string_view signature;
    Bytes data;
};

Bytes u32Bytes(std::uint32_t value) {
    fo4::test::ByteWriter out;
    out.u32(value);
    return out.bytes;
}

Bytes i32Bytes(std::int32_t value) {
    fo4::test::ByteWriter out;
    out.i32(value);
    return out.bytes;
}

Bytes floatBytes(std::initializer_list<float> values) {
    fo4::test::ByteWriter out;
    for (const float value : values) out.f32(value);
    return out.bytes;
}

Bytes propertyBytes(std::initializer_list<std::pair<std::uint32_t, float>> entries) {
    fo4::test::ByteWriter out;
    for (const auto& [actorValue, value] : entries) {
        out.u32(actorValue);
        out.f32(value);
    }
    return out.bytes;
}

Bytes cntoBytes(std::uint32_t item, std::int32_t count) {
    fo4::test::ByteWriter out;
    out.u32(item);
    out.i32(count);
    return out.bytes;
}

Bytes nam0Bytes(std::uint16_t points) {
    fo4::test::ByteWriter out;
    out.u8(0);
    out.u8(0);
    out.u16(points);
    return out.bytes;
}

Bytes modtBytes(std::initializer_list<std::uint32_t> textureWords) {
    fo4::test::ByteWriter out;
    out.u32(4);
    out.u32(static_cast<std::uint32_t>(textureWords.size() / 3));
    out.u32(0);
    out.u32(0);
    out.u32(0);
    for (const auto word : textureWords) out.u32(word);
    return out.bytes;
}

std::vector<std::byte> structPluginBytes(const std::vector<Piece>& furn, const std::vector<Piece>& must = {}) {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(2);
    hedr.u32(0x802);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto furnGroup = fo4::test::begin_group(out, "FURN", 0);
    const auto furnRecord = fo4::test::begin_record(out, "FURN", 0, 0x00000800, 131);
    for (const auto& piece : furn) fo4::test::write_subrecord(out, piece.signature, piece.data);
    fo4::test::end_record(out, furnRecord);
    fo4::test::end_group(out, furnGroup);

    const auto mustGroup = fo4::test::begin_group(out, "MUST", 0);
    const auto mustRecord = fo4::test::begin_record(out, "MUST", 0, 0x00000801, 131);
    for (const auto& piece : must) fo4::test::write_subrecord(out, piece.signature, piece.data);
    fo4::test::end_record(out, mustRecord);
    fo4::test::end_group(out, mustGroup);
    return out.bytes;
}

std::vector<Bytes> payloadsOf(const std::filesystem::path& path, std::string_view recordKey, std::string_view signature) {
    const auto plugin = fo4::read_plugin_file(path);
    const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(recordKey));
    REQUIRE(record != nullptr);
    std::vector<Bytes> values;
    for (const auto& subrecord : record->subrecords) {
        if (subrecord.signature.to_string() == signature) values.push_back(subrecord.data);
    }
    return values;
}

std::vector<std::string> signaturesOf(const std::filesystem::path& path, std::string_view recordKey) {
    const auto plugin = fo4::read_plugin_file(path);
    const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(recordKey));
    REQUIRE(record != nullptr);
    std::vector<std::string> values;
    for (const auto& subrecord : record->subrecords) values.push_back(subrecord.signature.to_string());
    return values;
}

std::uint32_t wordAt(const Bytes& data, std::size_t offset) {
    REQUIRE(data.size() >= offset + 4);
    std::uint32_t value = 0;
    std::memcpy(&value, data.data() + offset, sizeof(value));
    return value;
}

void save(fo4ide::plugin::RadiclePluginBackend& backend) {
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
}

constexpr std::string_view k_furnKey = "000800:Patch.esp";
constexpr std::string_view k_mustKey = "000801:Patch.esp";

nlohmann::json elementArgs(std::string_view record, std::string_view path) {
    return nlohmann::json::array({"Patch.esp", std::string(record), std::string(path)});
}

nlohmann::json moveArgs(std::string_view record, std::string_view path, int delta) {
    return nlohmann::json::array({"Patch.esp", std::string(record), std::string(path), delta});
}

}  // namespace

// Detects: add, remove and move on a struct group must keep the pairs intact and reach disk only through save.
TEST_CASE("Native ElementService adds removes and moves FURN marker pairs with a saved round trip") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, structPluginBytes({{"MNAM", u32Bytes(0)}, {"WBDT", Bytes(1)},
                                        {"ENAM", i32Bytes(-1)}, {"NAM0", nam0Bytes(12)},
                                        {"ENAM", i32Bytes(4)}, {"NAM0", nam0Bytes(3)},
                                        {"ENAM", i32Bytes(7)}, {"NAM0", nam0Bytes(5)}}));

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    fo4ide::plugin::IPluginBackend& element = backend;

    const auto first = parsed(element.describeElement(elementArgs(k_furnKey, "FURN\\Markers[0]")));
    CHECK(first.at("count") == 3);
    CHECK(first.at("elementType") == "Struct");
    CHECK(first.at("canAdd") == true);
    CHECK(first.at("canRemove") == true);
    CHECK(first.at("canMoveUp") == false);
    CHECK(first.at("canMoveDown") == true);
    const auto whole = parsed(element.describeElement(elementArgs(k_furnKey, "FURN\\Markers")));
    CHECK(whole.at("count") == 3);
    CHECK(whole.at("canRemove") == false);
    CHECK(whole.at("canAdd") == true);

    REQUIRE(element.moveElement(moveArgs(k_furnKey, "FURN\\Markers[0]", 1)).has_value());
    REQUIRE(element.addElement(elementArgs(k_furnKey, "FURN\\Markers[1]")).has_value());
    REQUIRE(element.removeElement(elementArgs(k_furnKey, "FURN\\Markers[0]")).has_value());
    REQUIRE(element.moveElement(moveArgs(k_furnKey, "FURN\\Markers[2]", -1)).has_value());
    REQUIRE(element.addElement(elementArgs(k_furnKey, "FURN\\Markers")).has_value());

    CHECK(payloadsOf(path, k_furnKey, "ENAM") == std::vector<Bytes>{i32Bytes(-1), i32Bytes(4), i32Bytes(7)});

    save(backend);
    CHECK(signaturesOf(path, k_furnKey) == std::vector<std::string>{
        "MNAM", "WBDT", "ENAM", "NAM0", "ENAM", "NAM0", "ENAM", "NAM0", "ENAM", "NAM0"});
    CHECK(payloadsOf(path, k_furnKey, "ENAM") ==
          std::vector<Bytes>{i32Bytes(0), i32Bytes(7), i32Bytes(-1), i32Bytes(0)});
    CHECK(payloadsOf(path, k_furnKey, "NAM0") ==
          std::vector<Bytes>{nam0Bytes(0), nam0Bytes(5), nam0Bytes(12), nam0Bytes(0)});

    const auto outOfRange = element.removeElement(elementArgs(k_furnKey, "FURN\\Markers[9]"));
    REQUIRE_FALSE(outOfRange.has_value());
    CHECK(outOfRange.error().type == "ElementIndexOutOfRange");
    const auto beyondEnd = element.addElement(elementArgs(k_furnKey, "FURN\\Markers[9]"));
    REQUIRE_FALSE(beyondEnd.has_value());
    CHECK(beyondEnd.error().type == "ElementIndexOutOfRange");
    const auto pastStart = element.moveElement(moveArgs(k_furnKey, "FURN\\Markers[0]", -1));
    REQUIRE_FALSE(pastStart.has_value());
    CHECK(pastStart.error().type == "ElementIndexOutOfRange");
}

// Detects: adding, removing or moving an item run must keep the stored item count and the item links right in the saved file.
TEST_CASE("Native ElementService keeps the FURN item count in step through add remove and move") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, structPluginBytes({{"COCT", u32Bytes(2)}, {"CNTO", cntoBytes(0x900, 5)},
                                        {"CNTO", cntoBytes(0x901, 1)}, {"MNAM", u32Bytes(0)}, {"WBDT", Bytes(1)}}));

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    fo4ide::plugin::IPluginBackend& element = backend;

    REQUIRE(element.addElement(elementArgs(k_furnKey, "FURN\\Items")).has_value());
    REQUIRE(element.removeElement(elementArgs(k_furnKey, "FURN\\Items[0]")).has_value());
    REQUIRE(element.moveElement(moveArgs(k_furnKey, "FURN\\Items[0]", 1)).has_value());

    save(backend);
    CHECK(signaturesOf(path, k_furnKey) == std::vector<std::string>{"COCT", "CNTO", "CNTO", "MNAM", "WBDT"});
    CHECK(payloadsOf(path, k_furnKey, "COCT") == std::vector<Bytes>{u32Bytes(2)});
    const auto items = payloadsOf(path, k_furnKey, "CNTO");
    REQUIRE(items.size() == 2);
    CHECK(wordAt(items[0], 0) == 0u);
    CHECK(wordAt(items[1], 0) == 0x901u);
}

// Detects: a move to the last slot of a segmented MODT array must insert at that slot, so the texture bytes and the header count stay in step on disk.
TEST_CASE("Native ElementService moves a segmented MODT texture to the last slot") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, structPluginBytes({{"MNAM", u32Bytes(0)}, {"WBDT", Bytes(1)},
                                        {"MODT", modtBytes({1, 2, 3, 4, 5, 6, 7, 8, 9})}}));

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    fo4ide::plugin::IPluginBackend& element = backend;

    const auto textures = parsed(element.describeElement(elementArgs(k_furnKey, "FURN\\ModelInfoTextures[1]")));
    CHECK(textures.at("count") == 3);
    CHECK(textures.at("canMoveDown") == true);

    REQUIRE(element.moveElement(moveArgs(k_furnKey, "FURN\\ModelInfoTextures[1]", 1)).has_value());

    save(backend);
    CHECK(payloadsOf(path, k_furnKey, "MODT") == std::vector<Bytes>{modtBytes({1, 2, 3, 7, 8, 9, 4, 5, 6})});
}

// Detects: a scalar list and a strided struct array must support add, remove and move, including a move that lands in the middle.
TEST_CASE("Native ElementService edits scalar and strided counted arrays with a saved round trip") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, structPluginBytes(
        {{"PRPS", propertyBytes({{0x900, 1.5f}, {0x901, 2.5f}})}, {"MNAM", u32Bytes(0)}, {"WBDT", Bytes(1)}},
        {{"FNAM", floatBytes({1.0f, 2.0f, 3.0f})}}));

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    fo4ide::plugin::IPluginBackend& element = backend;

    const auto cues = parsed(element.describeElement(elementArgs(k_mustKey, "MUST\\CuePoints[1]")));
    CHECK(cues.at("count") == 3);
    CHECK(cues.at("elementType") == "Element");
    CHECK(cues.at("canAdd") == true);
    CHECK(cues.at("canRemove") == true);
    CHECK(cues.at("canMoveUp") == true);
    CHECK(cues.at("canMoveDown") == true);

    REQUIRE(element.moveElement(moveArgs(k_mustKey, "MUST\\CuePoints[0]", 1)).has_value());
    REQUIRE(element.removeElement(elementArgs(k_mustKey, "MUST\\CuePoints[2]")).has_value());
    REQUIRE(element.addElement(elementArgs(k_mustKey, "MUST\\CuePoints[0]")).has_value());

    REQUIRE(element.moveElement(moveArgs(k_furnKey, "FURN\\Properties[0]", 1)).has_value());
    REQUIRE(element.addElement(elementArgs(k_furnKey, "FURN\\Properties")).has_value());
    REQUIRE(element.removeElement(elementArgs(k_furnKey, "FURN\\Properties[0]")).has_value());

    CHECK(payloadsOf(path, k_mustKey, "FNAM") == std::vector<Bytes>{floatBytes({1.0f, 2.0f, 3.0f})});

    save(backend);
    CHECK(payloadsOf(path, k_mustKey, "FNAM") == std::vector<Bytes>{floatBytes({0.0f, 2.0f, 1.0f})});
    CHECK(payloadsOf(path, k_furnKey, "PRPS") ==
          std::vector<Bytes>{propertyBytes({{0x900, 1.5f}, {0, 0.0f}})});
}

// Detects: a plain scalar row or an unknown path must keep the old refusal instead of being treated as a list.
TEST_CASE("Native ElementService refuses scalar rows and unknown paths as lists") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, structPluginBytes({{"MNAM", u32Bytes(0)}, {"WBDT", Bytes(1)}}));

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    fo4ide::plugin::IPluginBackend& element = backend;

    const auto scalar = element.addElement(elementArgs(k_furnKey, "FURN\\MarkerIndex"));
    REQUIRE_FALSE(scalar.has_value());
    CHECK(scalar.error().type == "ElementPathUnsupported");
    const auto unknown = element.removeElement(elementArgs(k_furnKey, "FURN\\NoSuchList[0]"));
    REQUIRE_FALSE(unknown.has_value());
    CHECK(unknown.error().type == "ElementPathUnsupported");
    const auto described = parsed(element.describeElement(elementArgs(k_furnKey, "FURN\\MarkerIndex")));
    CHECK(described.contains("error"));
    save(backend);
    CHECK(signaturesOf(path, k_furnKey) == std::vector<std::string>{"MNAM", "WBDT"});
}

// Detects: set_furniture_markers must write one SNAM element per marker at the authoring position and the enabled bits into MNAM.
TEST_CASE("Native set_furniture_markers writes SNAM and the MNAM interaction bits with a saved round trip") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    constexpr std::uint32_t upper = 0xFFC00000u;
    writeBytes(path, structPluginBytes({{"MNAM", u32Bytes(upper | 0x3Fu)}, {"WBDT", Bytes(1)},
                                        {"XMRK", Bytes(1)}, {"APPR", Bytes(4)}}));

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    fo4ide::plugin::IPluginBackend& element = backend;

    const auto markers = nlohmann::json::array({
        {{"offsetX", 1.0}, {"offsetY", 2.0}, {"offsetZ", 3.0}, {"rotationZ", -90.0}, {"entryTypes", 11}},
        {{"enabled", false}}});
    const auto written = element.setFurnitureMarkers(
        nlohmann::json::array({"Patch.esp", std::string(k_furnKey), markers.dump()}));
    REQUIRE(written.has_value());
    CHECK(signaturesOf(path, k_furnKey) == std::vector<std::string>{"MNAM", "WBDT", "XMRK", "APPR"});

    save(backend);
    CHECK(signaturesOf(path, k_furnKey) == std::vector<std::string>{"MNAM", "WBDT", "XMRK", "SNAM", "APPR"});
    const auto snam = payloadsOf(path, k_furnKey, "SNAM");
    REQUIRE(snam.size() == 1);
    REQUIRE(snam[0].size() == 48);
    CHECK(wordAt(snam[0], 0) == 0x3F800000u);
    CHECK(wordAt(snam[0], 4) == 0x40000000u);
    CHECK(wordAt(snam[0], 8) == 0x40400000u);
    CHECK(wordAt(snam[0], 12) == 0xC2B40000u);
    CHECK(std::to_integer<std::uint8_t>(snam[0][20]) == 11);
    CHECK(std::to_integer<std::uint8_t>(snam[0][44]) == 255);
    CHECK(wordAt(payloadsOf(path, k_furnKey, "MNAM").at(0), 0) == (upper | 0x1u));

    const auto replaced = element.setFurnitureMarkers(
        nlohmann::json::array({"Patch.esp", std::string(k_furnKey), markers}));
    REQUIRE(replaced.has_value());
    save(backend);
    CHECK(payloadsOf(path, k_furnKey, "SNAM").at(0).size() == 48);

    const auto cleared = element.setFurnitureMarkers(
        nlohmann::json::array({"Patch.esp", std::string(k_furnKey), "[]"}));
    REQUIRE(cleared.has_value());
    save(backend);
    CHECK(signaturesOf(path, k_furnKey) == std::vector<std::string>{"MNAM", "WBDT", "XMRK", "APPR"});
    CHECK(wordAt(payloadsOf(path, k_furnKey, "MNAM").at(0), 0) == upper);

    const auto defaulted = element.setFurnitureMarkers(
        nlohmann::json::array({"Patch.esp", std::string(k_furnKey), nlohmann::json(nullptr)}));
    REQUIRE(defaulted.has_value());
    save(backend);
    CHECK(payloadsOf(path, k_furnKey, "SNAM").at(0).size() == 24);
    CHECK(wordAt(payloadsOf(path, k_furnKey, "MNAM").at(0), 0) == (upper | 0x1u));
}

// Detects: invalid marker input and non-FURN records must be refused with the plugin left as it was.
TEST_CASE("Native set_furniture_markers refuses invalid input and other record types") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, structPluginBytes({{"MNAM", u32Bytes(0)}, {"XMRK", Bytes(1)}}, {{"FNAM", floatBytes({1.0f})}}));

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    fo4ide::plugin::IPluginBackend& element = backend;

    const auto call = [&](std::string_view record, const nlohmann::json& markers) {
        return element.setFurnitureMarkers(nlohmann::json::array({"Patch.esp", std::string(record), markers}));
    };
    CHECK_FALSE(call(k_furnKey, nlohmann::json::array({{{"entryTypes", 256}}})).has_value());
    CHECK_FALSE(call(k_furnKey, nlohmann::json::array({{{"offsetX", "1"}}})).has_value());
    CHECK_FALSE(call(k_furnKey, nlohmann::json::array({{{"enabled", 1}}})).has_value());
    CHECK_FALSE(call(k_furnKey, nlohmann::json::array({1})).has_value());
    CHECK_FALSE(call(k_furnKey, "not json").has_value());
    CHECK_FALSE(call(k_furnKey, nlohmann::json::array_t(23, nlohmann::json::object())).has_value());
    const auto wrongType = call(k_mustKey, nlohmann::json::array({nlohmann::json::object()}));
    REQUIRE_FALSE(wrongType.has_value());
    CHECK(wrongType.error().type == "WrongRecordType");
    const auto missing = call("000999:Patch.esp", nlohmann::json::array());
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().type == "RecordNotFound");

    save(backend);
    CHECK(signaturesOf(path, k_furnKey) == std::vector<std::string>{"MNAM", "XMRK"});
    CHECK(payloadsOf(path, k_furnKey, "MNAM").at(0) == u32Bytes(0));
}
