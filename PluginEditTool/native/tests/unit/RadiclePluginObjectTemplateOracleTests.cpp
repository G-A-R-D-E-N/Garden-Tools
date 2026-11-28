#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/plugin/field_access.hpp>
#include <fo4/plugin/mutation.hpp>
#include <fo4/plugin/reader.hpp>

#include "../helpers/OracleFixtures.hpp"

namespace {

using namespace oracle_fixture;
using fo4ide::plugin::RadiclePluginBackend;

using Bytes = std::vector<std::byte>;
using Row = std::pair<std::string, Bytes>;
using Rows = std::vector<Row>;

const auto kKey = fo4::FormKey::parse("000800:Patch.esp");

Bytes count(std::uint32_t value) {
    fo4::test::ByteWriter out;
    out.u32(value);
    return out.bytes;
}

Bytes obts(std::uint16_t parent, std::uint8_t level, std::uint32_t includes = 0) {
    fo4::test::ByteWriter out;
    out.u32(includes);
    out.u32(0);
    out.u8(level);
    out.u8(0);
    out.u8(0);
    out.u8(0);
    out.u16(parent);
    out.u8(0);
    out.u8(0);
    out.u8(0);
    out.u8(0);
    for (std::uint32_t include = 0; include < includes; ++include) {
        out.u32(0x00000900u + include);
        out.u8(1);
        out.u8(0);
        out.u8(1);
    }
    return out.bytes;
}

Bytes text(std::string_view value) {
    const std::string stored = std::string(value) + '\0';
    const auto* begin = reinterpret_cast<const std::byte*>(stored.data());
    return {begin, begin + stored.size()};
}

Bytes makePlugin(std::string_view type, const Rows& rows) {
    fo4::test::ByteWriter out;
    writeHeader(out, 1.0f, 0, 0x801);
    const auto group = fo4::test::begin_group(out, type, 0);
    const auto record = fo4::test::begin_record(out, type, 0, 0x800, 131);
    fo4::test::write_subrecord(out, "EDID", text("Host"));
    for (const auto& [signature, data] : rows) fo4::test::write_subrecord(out, signature, data);
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    return out.bytes;
}

Rows rowsOf(const fo4::Record& record) {
    Rows rows;
    for (const auto& subrecord : record.subrecords) {
        rows.emplace_back(subrecord.signature.to_string(), subrecord.data);
    }
    return rows;
}

std::vector<std::string> signaturesOf(const fo4::Record& record) {
    std::vector<std::string> signatures;
    for (const auto& subrecord : record.subrecords) signatures.push_back(subrecord.signature.to_string());
    return signatures;
}

const fo4::Record& hostIn(const fo4::Plugin& loaded) {
    const auto* record = fo4::find_record(loaded, kKey);
    REQUIRE(record != nullptr);
    return *record;
}

nlohmann::json args(const char* path, const char* record = "000800:Patch.esp") {
    return nlohmann::json::array({"Patch.esp", record, path});
}

std::size_t rowCount(const fo4::Plugin& loaded, std::string_view path) {
    return fo4::record_field_count(loaded, kKey, path).value_or(~std::size_t{0});
}

std::size_t templateCount(const fo4::Plugin& loaded, std::string_view type) {
    std::size_t count = 0;
    REQUIRE(fo4::record_list_size(loaded, kKey, std::string(type) + "\\ObjectTemplateCombinations", count) ==
            fo4::FieldStatus::ok);
    return count;
}

bool pending(RadiclePluginBackend& backend) {
    return parseStringResult(backend.invoke("backend.GetActivePlugins", nlohmann::json::array()))
        .at(0).at("Pending").get<bool>();
}

struct Session {
    std::filesystem::path root = tempRoot();
    Cleanup cleanup{root};
    std::filesystem::path path = root / "Patch.esp";
    RadiclePluginBackend backend;

    explicit Session(const Bytes& bytes) {
        writeBytes(path, bytes);
        loadPatch(backend, path);
    }

    fo4ide::plugin::IPluginBackend& elements() { return backend; }

    fo4::Plugin saved() {
        savePatch(backend);
        return fo4::read_plugin_file(path);
    }
};

const Rows kFollowers = {{"DNAM", Bytes(4, std::byte{7})}, {"FNAM", Bytes(2, std::byte{9})}};

Rows host(const Rows& block = {}) {
    Rows rows = block;
    rows.insert(rows.end(), kFollowers.begin(), kFollowers.end());
    return rows;
}

const Rows kThreeLinked = {
    {"OBTE", count(3)},
    {"OBTS", obts(0xFFFF, 1)},
    {"OBTF", {}},
    {"FULL", count(0x3A1)},
    {"OBTS", obts(0, 2, 1)},
    {"OBTF", {}},
    {"OBTS", obts(1, 3)},
    {"STOP", {}},
};

const Rows kThreeFree = {
    {"OBTE", count(3)},
    {"OBTS", obts(0xFFFF, 1)},
    {"OBTF", {}},
    {"FULL", count(0x3A1)},
    {"OBTS", obts(0xFFFF, 2, 1)},
    {"OBTF", {}},
    {"OBTS", obts(0xFFFF, 3)},
    {"STOP", {}},
};

void expectRefused(Session& session, const nlohmann::json& request, const char* type, bool remove = false) {
    const auto result = remove ? session.elements().removeElement(request) : session.elements().addElement(request);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == type);
    CHECK_FALSE(pending(session.backend));
}

}  // namespace

TEST_CASE("Oracle object templates add to a weapon without OBTE and round trip through the saved binary") {
    Session session(makePlugin("WEAP", host()));

    const auto described = parseStringResult(session.elements().describeElement(args("Authoring\\OBTE")));
    CHECK(described.at("canAdd") == true);
    CHECK(described.at("count") == 0);
    CHECK(described.at("elementType") == "ObjectTemplate");

    const auto added = session.elements().addElement(args("ObjectTemplates"));
    REQUIRE(added.has_value());
    CHECK(added->get<std::string>().find("Added ObjectTemplate 0") == 0);
    CHECK(pending(session.backend));

    const auto disk = session.saved();
    const auto& record = hostIn(disk);
    CHECK(signaturesOf(record) == std::vector<std::string>{"EDID", "OBTE", "OBTS", "STOP", "DNAM", "FNAM"});
    CHECK(record.subrecords[1].data == count(1));
    CHECK(record.subrecords[2].data == obts(0xFFFF, 0));
    CHECK(templateCount(disk, "WEAP") == 1);
    CHECK(rowCount(disk, "WEAP\\ObjectTemplateIncludeMod") == 0);
    CHECK(rowCount(disk, "WEAP\\ObjectTemplatePropertyValue1") == 0);
    CHECK(rowCount(disk, "WEAP\\ObjectTemplateKeyword") == 0);

    RadiclePluginBackend reload;
    loadPatch(reload, session.path);
    const auto reloaded = parseStringResult(
        static_cast<fo4ide::plugin::IPluginBackend&>(reload).describeElement(args("ObjectTemplates")));
    CHECK(reloaded.at("count") == 1);
}

TEST_CASE("Oracle object templates append after existing combinations and keep them byte for byte") {
    Session session(makePlugin("WEAP", host(kThreeLinked)));

    const auto described = parseStringResult(session.elements().describeElement(args("WEAP\\ObjectTemplates")));
    CHECK(described.at("count") == 3);
    REQUIRE(session.elements().addElement(args("Authoring\\OBTS")).has_value());

    const auto disk = session.saved();
    Rows expected = {{"EDID", text("Host")}, {"OBTE", count(4)}};
    expected.insert(expected.end(), kThreeLinked.begin() + 1, kThreeLinked.end() - 1);
    expected.push_back({"OBTS", obts(0xFFFF, 0)});
    expected.push_back({"STOP", {}});
    expected.insert(expected.end(), kFollowers.begin(), kFollowers.end());
    CHECK(rowsOf(hostIn(disk)) == expected);
    CHECK(templateCount(disk, "WEAP") == 4);
    CHECK(rowCount(disk, "WEAP\\ObjectTemplateIncludeMod") == 1);
    CHECK(rowCount(disk, "WEAP\\ObjectTemplateKeyword") == 0);
}

TEST_CASE("Oracle object templates add to armor furniture and NPC records at the schema position") {
    struct Case {
        const char* type;
        Rows rows;
        std::vector<std::string> expected;
    };
    const std::vector<Case> cases = {
        {"ARMO", {{"DATA", Bytes(8, std::byte{1})}, {"FNAM", Bytes(8, std::byte{2})}},
         {"EDID", "DATA", "FNAM", "OBTE", "OBTS", "STOP"}},
        {"FURN", {}, {"EDID", "OBTE", "OBTS", "STOP"}},
        {"NPC_", {{"CNAM", Bytes(4, std::byte{3})}}, {"EDID", "OBTE", "OBTS", "STOP", "CNAM"}},
    };
    for (const auto& item : cases) {
        INFO(item.type);
        Session session(makePlugin(item.type, item.rows));
        const auto result = session.elements().addElement(args("ObjectTemplates"));
        INFO(std::string(result.has_value() ? "" : result.error().type));
        REQUIRE(result.has_value());
        const auto disk = session.saved();
        CHECK(signaturesOf(hostIn(disk)) == item.expected);
        CHECK(templateCount(disk, item.type) == 1);
    }
}

TEST_CASE("Oracle object templates refuse a malformed block and stage nothing") {
    const std::vector<Rows> blocks = {
        {{"OBTE", count(2)}, {"OBTS", obts(0xFFFF, 0)}, {"STOP", {}}},
        {{"OBTE", count(1)}, {"OBTS", obts(0xFFFF, 0)}},
        {{"OBTE", count(1)}, {"KSIZ", count(0)}, {"OBTS", obts(0xFFFF, 0)}, {"STOP", {}}},
        {{"OBTE", count(1)}, {"OBTS", obts(0xFFFF, 0)}, {"FULL", count(1)}, {"STOP", {}}},
        {{"OBTE", count(0)}, {"OBTE", count(0)}, {"STOP", {}}},
        {{"OBTE", count(0)}, {"OBTF", {}}, {"STOP", {}}},
        {{"OBTE", count(0)}, {"FULL", count(1)}, {"STOP", {}}},
        {{"OBTE", count(1)}, {"OBTS", Bytes(5, std::byte{0})}, {"STOP", {}}},
    };
    for (std::size_t index = 0; index < blocks.size(); ++index) {
        INFO(index);
        Session session(makePlugin("WEAP", host(blocks[index])));
        expectRefused(session, args("Authoring\\OBTE"), "ElementTemplateBlockMalformed");
        expectRefused(session, args("ObjectTemplates[0]"), "ElementTemplateBlockMalformed", true);
    }
}

TEST_CASE("Oracle object templates refuse invalid targets and stage nothing") {
    Session weapon(makePlugin("WEAP", host()));
    expectRefused(weapon, args("ObjectTemplates[0]"), "ElementInsertIndexUnsupported");
    expectRefused(weapon, args("ObjectTemplates", "000999:Patch.esp"), "RecordNotFound");
    expectRefused(weapon, args("ObjectTemplates", "not-a-key"), "InvalidFormKey");
    expectRefused(weapon, args("ObjectTemplates"), "ElementIndexRequired", true);
    expectRefused(weapon, args("ObjectTemplates[0]"), "ElementIndexOutOfRange", true);

    Session stray(makePlugin("WEAP", {{"ZZZZ", Bytes(1, std::byte{1})}}));
    expectRefused(stray, args("ObjectTemplates"), "ElementUnknownOrder");

    Session misc(makePlugin("MISC", {}));
    expectRefused(misc, args("Authoring\\OBTE"), "ElementPathUnsupported");
    const auto described = parseStringResult(weapon.elements().describeElement(args("ObjectTemplates[0]")));
    CHECK(described.contains("error"));
}

TEST_CASE("Oracle object templates remove a middle combination with its filter and name rows") {
    Session session(makePlugin("WEAP", host(kThreeFree)));

    const auto described = parseStringResult(session.elements().describeElement(args("ObjectTemplates[1]")));
    CHECK(described.at("canRemove") == true);
    CHECK(described.at("canAdd") == false);
    REQUIRE(session.elements().removeElement(args("Authoring\\OBTS[1]")).has_value());

    const auto disk = session.saved();
    const Rows expected = {{"EDID", text("Host")}, {"OBTE", count(2)}, {"OBTS", obts(0xFFFF, 1)},
                           {"OBTF", {}}, {"OBTS", obts(0xFFFF, 3)}, {"STOP", {}},
                           kFollowers[0], kFollowers[1]};
    CHECK(rowsOf(hostIn(disk)) == expected);
    CHECK(templateCount(disk, "WEAP") == 2);
}

TEST_CASE("Oracle object templates drop the whole block when the last combination is removed") {
    Session session(makePlugin("WEAP", host({{"OBTE", count(1)}, {"OBTF", {}}, {"FULL", count(5)},
                                            {"OBTS", obts(0xFFFF, 1)}, {"STOP", {}}})));
    REQUIRE(session.elements().removeElement(args("ObjectTemplates[0]")).has_value());
    const auto disk = session.saved();
    CHECK(signaturesOf(hostIn(disk)) == std::vector<std::string>{"EDID", "DNAM", "FNAM"});
    CHECK(templateCount(disk, "WEAP") == 0);
}

TEST_CASE("Oracle object templates refuse removal while another combination names the removed index as parent") {
    Session session(makePlugin("WEAP", host(kThreeLinked)));
    expectRefused(session, args("ObjectTemplates[0]"), "ElementTemplateReferenced", true);
    expectRefused(session, args("ObjectTemplates[1]"), "ElementTemplateReferenced", true);

    REQUIRE(session.elements().removeElement(args("ObjectTemplates[2]")).has_value());
    REQUIRE(session.elements().removeElement(args("ObjectTemplates[1]")).has_value());
    const auto disk = session.saved();
    const Rows expected = {{"EDID", text("Host")}, {"OBTE", count(1)}, {"OBTS", obts(0xFFFF, 1)},
                           {"STOP", {}}, kFollowers[0], kFollowers[1]};
    CHECK(rowsOf(hostIn(disk)) == expected);
}

TEST_CASE("Oracle object templates remove the first combination of a block and keep the rest byte for byte") {
    Session session(makePlugin("WEAP", host(kThreeFree)));
    REQUIRE(session.elements().removeElement(args("ObjectTemplates[0]")).has_value());
    const auto disk = session.saved();
    const Rows expected = {{"EDID", text("Host")}, {"OBTE", count(2)}, {"OBTF", {}}, {"FULL", count(0x3A1)},
                           {"OBTS", obts(0xFFFF, 2, 1)}, {"OBTF", {}}, {"OBTS", obts(0xFFFF, 3)}, {"STOP", {}},
                           kFollowers[0], kFollowers[1]};
    CHECK(rowsOf(hostIn(disk)) == expected);
}

TEST_CASE("Oracle object templates add into an empty OBTE block") {
    Session session(makePlugin("WEAP", host({{"OBTE", count(0)}, {"STOP", {}}})));
    const auto described = parseStringResult(session.elements().describeElement(args("ObjectTemplates")));
    CHECK(described.at("count") == 0);
    CHECK(described.at("canAdd") == true);
    REQUIRE(session.elements().addElement(args("ObjectTemplates")).has_value());
    const Rows expected = {{"EDID", text("Host")}, {"OBTE", count(1)}, {"OBTS", obts(0xFFFF, 0)}, {"STOP", {}},
                           kFollowers[0], kFollowers[1]};
    CHECK(rowsOf(hostIn(session.saved())) == expected);
}

TEST_CASE("Oracle object templates describe a referenced combination as not removable and a free one as removable") {
    Session session(makePlugin("WEAP", host(kThreeLinked)));
    const auto referenced = parseStringResult(session.elements().describeElement(args("ObjectTemplates[1]")));
    CHECK(referenced.at("canRemove") == false);
    CHECK(referenced.at("canAdd") == false);
    const auto free = parseStringResult(session.elements().describeElement(args("ObjectTemplates[2]")));
    CHECK(free.at("canRemove") == true);
    const auto list = parseStringResult(session.elements().describeElement(args("ObjectTemplates")));
    CHECK(list.at("canAdd") == true);
    CHECK(list.at("canRemove") == false);
    CHECK_FALSE(pending(session.backend));
}
