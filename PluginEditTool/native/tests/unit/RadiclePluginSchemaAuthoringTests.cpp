#include <chrono>
#include <filesystem>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <fo4/plugin/mutation.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/plugin/subrecord_authoring.hpp>
#include <fo4/plugin/writer.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

namespace {

struct Fixture {
    std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("fo4ide_authoring_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::path path = root / "Authoring.esp";
    fo4::FormKey key;
    fo4ide::plugin::RadiclePluginBackend backend;

    explicit Fixture(bool unknown = false, std::string name = "Authoring.esp") {
        path = root / name;
        auto plugin = fo4::make_empty_plugin(name);
        const auto result = fo4::create_record(plugin, fo4::Signature::from_string("ARMO"), "Armor");
        REQUIRE(result.status == fo4::RecordCreateStatus::created);
        key = result.key;
        if (unknown) {
            auto& record = std::get<fo4::Record>(plugin.groups.front().children.front());
            record.subrecords.push_back({fo4::Signature::from_string("ZZZZ"), {std::byte{42}}, false});
        }
        std::filesystem::create_directories(root);
        fo4::write_plugin_file(plugin, path);
        REQUIRE(backend.invoke("environment.LoadMo2", nlohmann::json{
            {"plugins", nlohmann::json::array({{
                {"name", name}, {"path", path.string()}, {"source", "AuthoringTest"}, {"resolved", true}
            }})}
        }).has_value());
    }

    ~Fixture() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    nlohmann::json describe(std::string_view field) {
        const auto result = static_cast<fo4ide::plugin::IPluginBackend&>(backend).describeElement(
            nlohmann::json::array({path.filename().string(), key.to_string(), field}));
        REQUIRE(result.has_value());
        return nlohmann::json::parse(result->get<std::string>());
    }

    auto mutate(std::string_view method, std::string_view field) {
        const auto arguments = nlohmann::json::array({
            path.filename().string(), key.to_string(), field, "ICON"});
        auto& elements = static_cast<fo4ide::plugin::IPluginBackend&>(backend);
        return method == "AddElement" ? elements.addElement(arguments) : elements.removeElement(arguments);
    }
};

}

TEST_CASE("Schema authoring capabilities use native rules and persist exact operations", "[w0][authoring]") {
    Fixture fixture;
    const auto icon = fixture.describe("Authoring\\ICON");
    REQUIRE(icon.contains("canAdd"));
    CHECK(icon.at("canAdd") == true);
    CHECK(icon.at("canCreate") == true);
    CHECK(fixture.describe("Authoring\\DATA[0]").at("canRemove") == false);
    CHECK(fixture.describe("Authoring\\MOD2").at("canAdd") == false);
    CHECK(fixture.mutate("AddElement", "Authoring\\ICON").has_value());
    CHECK(fixture.describe("Authoring\\ICON").at("canAdd") == false);
    CHECK(fixture.describe("Authoring\\ICON[0]").at("canRemove") == true);
    CHECK(fixture.mutate("RemoveElement", "Authoring\\ICON[0]").has_value());
    CHECK(fixture.describe("Authoring\\ICON").at("count") == 0);
    REQUIRE(fixture.backend.invokeBackend("SavePlugin",
        nlohmann::json::array({"Authoring.esp"})).has_value());
    const auto saved = fo4::read_plugin_file(fixture.path);
    const auto* record = fo4::find_record(saved, fixture.key);
    REQUIRE(record != nullptr);
    REQUIRE(record->subrecords.size() == 2);
    CHECK(record->subrecords[0].signature == fo4::sig_edid);
    CHECK(record->subrecords[1].signature == fo4::Signature::from_string("DATA"));
    CHECK(record->subrecords[1].data == std::vector<std::byte>(12, std::byte{0}));
}

TEST_CASE("Schema authoring refuses coupled required and unknown-order operations without staging", "[w0][authoring]") {
    Fixture fixture(true);
    CHECK(fixture.describe("Authoring\\ICON").at("canAdd") == false);
    const auto required = fixture.mutate("RemoveElement", "Authoring\\DATA[0]");
    REQUIRE_FALSE(required.has_value());
    CHECK(required.error().type == "ElementBelowMinimum");
    const auto coupled = fixture.mutate("AddElement", "Authoring\\MOD2");
    REQUIRE_FALSE(coupled.has_value());
    CHECK(coupled.error().type == "ElementNotAuthorable");
    const auto unknown = fixture.mutate("AddElement", "Authoring\\ICON");
    REQUIRE_FALSE(unknown.has_value());
    CHECK(unknown.error().type == "ElementUnknownOrder");
    const auto pending = fixture.backend.invoke("appInterop.GetPlugins", nlohmann::json::array());
    REQUIRE(pending.has_value());
    CHECK(nlohmann::json::parse(pending->get<std::string>())[0].at("Pending") == false);
}

TEST_CASE("Schema creation uses proven defaults and refuses unsupported placement", "[w0][authoring]") {
    Fixture fixture;
    const auto created = fixture.backend.invokeBackend("CreateRecord", nlohmann::json::array({
        "Authoring.esp", "ARMO", {{"schema", true}, {"editorId", "NewArmor"}}}));
    REQUIRE(created.has_value());
    const auto result = nlohmann::json::parse(created->get<std::string>());
    CHECK(result.at("ok") == true);
    const auto refused = fixture.backend.invokeBackend("CreateRecord", nlohmann::json::array({
        "Authoring.esp", "INFO", {{"schema", true}, {"editorId", "Unsafe"}}}));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "RecordNotAuthorable");
}
