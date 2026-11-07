#include <chrono>
#include "../helpers/TempRoot.hpp"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/plugin/reader.hpp>
#include <fo4/plugin/subrecord_authoring.hpp>
#include <fo4/plugin/writer.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = fo4ide_test::uniqueTempName();
    return std::filesystem::temp_directory_path() / ("fo4ide_radicle_batch_" + id);
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

std::vector<std::byte> makeSource(std::uint32_t formId, std::string_view editorId) {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(1);
    hedr.u32(0x900);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::write_subrecord(out, "MAST", std::string_view{"Master.esm\0", 11});
    fo4::test::ByteWriter data;
    data.u32(0);
    data.u32(0);
    fo4::test::write_subrecord(out, "DATA", data.bytes);
    fo4::test::end_record(out, tes4);

    const auto group = fo4::test::begin_group(out, "WEAP", 0);
    const auto record = fo4::test::begin_record(out, "WEAP", 0, formId);
    std::vector<std::byte> edid;
    for (const char ch : editorId) edid.push_back(std::byte{static_cast<std::uint8_t>(ch)});
    edid.push_back(std::byte{0});
    fo4::test::write_subrecord(out, "EDID", edid);
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    return out.bytes;
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

nlohmann::json parseResult(const fo4ide::Result<nlohmann::json>& result) {
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    return nlohmann::json::parse(result->get<std::string>());
}

}

TEST_CASE("Native CopyAsOverrideMany copies selected conflicts in one transaction") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto sourceA = root / "SourceA.esp";
    const auto sourceB = root / "SourceB.esp";
    const auto target = root / "Patch.esp";
    writeBytes(sourceA, makeSource(0x00000800, "WeaponA"));
    writeBytes(sourceB, makeSource(0x00000802, "WeaponB"));
    writeBytes(target, makeTarget());

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("SourceA.esp", sourceA),
            pluginEntry("SourceB.esp", sourceB),
            pluginEntry("Patch.esp", target)
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    const nlohmann::json items = nlohmann::json::array({
        {{"formKey", "000800:Master.esm"}, {"source", "SourceA.esp"}},
        {{"formKey", "000802:Master.esm"}, {"source", "SourceB.esp"}}
    });

    const auto result = parseResult(backend.invokeBackend(
        "CopyAsOverrideMany",
        nlohmann::json::array({items.dump(), "Patch.esp", false})));
    CHECK(result.at("total") == 2);
    CHECK(result.at("ok") == 2);
    CHECK(result.at("failed") == 0);
    CHECK(result.at("requiresOverwrite") == false);

    const auto beforeSave = fo4::read_plugin_file(target);
    CHECK(beforeSave.header.masters.empty());
    CHECK(fo4::find_record(
        beforeSave, fo4::FormKey::parse("000800:Master.esm")) == nullptr);
    CHECK(fo4::find_record(
        beforeSave, fo4::FormKey::parse("000802:Master.esm")) == nullptr);

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto reloaded = fo4::read_plugin_file(target);
    REQUIRE(reloaded.header.masters.size() == 1);
    CHECK(reloaded.header.masters[0].key.filename() == "Master.esm");
    CHECK(fo4::find_record(reloaded, fo4::FormKey::parse("000800:Master.esm")) != nullptr);
    CHECK(fo4::find_record(reloaded, fo4::FormKey::parse("000802:Master.esm")) != nullptr);
    const auto* local = fo4::find_record(reloaded, fo4::FormKey::parse("000810:Patch.esp"));
    REQUIRE(local != nullptr);
    CHECK(local->header.form_id == fo4::RawFormId{0x01000810});
}

TEST_CASE("Native CopyAsOverrideMany preflights overwrite conflicts without writing") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto sourceA = root / "SourceA.esp";
    const auto sourceB = root / "SourceB.esp";
    const auto target = root / "Patch.esp";
    writeBytes(sourceA, makeSource(0x00000800, "WeaponA"));
    writeBytes(sourceB, makeSource(0x00000802, "WeaponB"));
    writeBytes(target, makeTarget());

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("SourceA.esp", sourceA),
            pluginEntry("SourceB.esp", sourceB),
            pluginEntry("Patch.esp", target)
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    const nlohmann::json items = nlohmann::json::array({
        {{"formKey", "000800:Master.esm"}, {"source", "SourceA.esp"}},
        {{"formKey", "000802:Master.esm"}, {"source", "SourceB.esp"}}
    });
    REQUIRE(parseResult(backend.invokeBackend(
        "CopyAsOverrideMany",
        nlohmann::json::array({items.dump(), "Patch.esp", false}))).at("ok") == 2);

    const auto beforePlugin = fo4::read_plugin_file(target);
    CHECK(beforePlugin.header.masters.empty());
    CHECK(fo4::find_record(
        beforePlugin, fo4::FormKey::parse("000800:Master.esm")) == nullptr);
    const auto before = fo4::test::original_bytes(beforePlugin);
    const auto preflight = parseResult(backend.invokeBackend(
        "CopyAsOverrideMany",
        nlohmann::json::array({items.dump(), "Patch.esp", false})));
    CHECK(preflight.at("requiresOverwrite") == true);
    CHECK(preflight.at("existing").size() == 2);
    CHECK(preflight.at("ok") == 0);
    CHECK(fo4::test::original_bytes(fo4::read_plugin_file(target)) == before);

    const auto overwrite = parseResult(backend.invokeBackend(
        "CopyAsOverrideMany",
        nlohmann::json::array({items.dump(), "Patch.esp", true})));
    CHECK(overwrite.at("requiresOverwrite") == false);
    CHECK(overwrite.at("ok") == 2);
    CHECK(overwrite.at("failed") == 0);
    CHECK(fo4::test::original_bytes(fo4::read_plugin_file(target)) == before);

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto saved = fo4::read_plugin_file(target);
    CHECK(fo4::find_record(
        saved, fo4::FormKey::parse("000800:Master.esm")) != nullptr);
    CHECK(fo4::find_record(
        saved, fo4::FormKey::parse("000802:Master.esm")) != nullptr);
}

TEST_CASE("Native CopyAsOverrideMany reports per-item failures and commits successes") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto sourceA = root / "SourceA.esp";
    const auto target = root / "Patch.esp";
    writeBytes(sourceA, makeSource(0x00000800, "WeaponA"));
    writeBytes(target, makeTarget());

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("SourceA.esp", sourceA),
            pluginEntry("Patch.esp", target)
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    const nlohmann::json items = nlohmann::json::array({
        {{"formKey", "000800:Master.esm"}, {"source", "SourceA.esp"}},
        {{"formKey", "000802:Master.esm"}, {"source", "Missing.esp"}}
    });

    const auto result = parseResult(backend.invokeBackend(
        "CopyAsOverrideMany",
        nlohmann::json::array({items.dump(), "Patch.esp", false})));
    CHECK(result.at("total") == 2);
    CHECK(result.at("ok") == 1);
    CHECK(result.at("failed") == 1);
    REQUIRE(result.at("failures").size() == 1);
    CHECK(result.at("failures")[0].at("source") == "Missing.esp");

    const auto beforeSave = fo4::read_plugin_file(target);
    CHECK(fo4::find_record(beforeSave, fo4::FormKey::parse("000800:Master.esm")) == nullptr);
    CHECK(fo4::find_record(beforeSave, fo4::FormKey::parse("000802:Master.esm")) == nullptr);

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto reloaded = fo4::read_plugin_file(target);
    CHECK(fo4::find_record(reloaded, fo4::FormKey::parse("000800:Master.esm")) != nullptr);
    CHECK(fo4::find_record(reloaded, fo4::FormKey::parse("000802:Master.esm")) == nullptr);
}

TEST_CASE("Native CopyAsOverrideMany asks before writing overrides that would lose") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto source = root / "SourceA.esp";
    const auto target = root / "Patch.esp";
    writeBytes(source, makeSource(0x00000800, "WeaponA"));
    writeBytes(target, makeTarget());

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("Patch.esp", target),
            pluginEntry("SourceA.esp", source)
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    const nlohmann::json items = nlohmann::json::array({
        {{"formKey", "000800:Master.esm"}, {"source", "SourceA.esp"}}
    });
    const auto pending = [&] {
        const auto active = parseResult(
            backend.invoke("backend.GetActivePlugins", nlohmann::json::array()));
        return active.at(0).at("Pending").get<bool>();
    };

    const auto asked = parseResult(backend.invokeBackend(
        "CopyAsOverrideMany", nlohmann::json::array({items.dump(), "Patch.esp", false})));
    CHECK(asked.at("ok") == 0);
    CHECK(asked.at("requiresOverwrite") == false);
    CHECK(asked.at("requiresLoadOrderConfirmation") == true);
    REQUIRE(asked.at("losing").size() == 1);
    CHECK(asked.at("losing")[0].at("formKey") == "000800:Master.esm");
    CHECK(asked.at("losing")[0].at("loadsAfter") == nlohmann::json::array({"SourceA.esp"}));
    CHECK_FALSE(pending());

    const auto invalid = backend.invokeBackend(
        "CopyAsOverrideMany", nlohmann::json::array({items.dump(), "Patch.esp", false, "yes"}));
    REQUIRE_FALSE(invalid.has_value());
    CHECK(invalid.error().type == "InvalidAllowLosingFlag");
    CHECK_FALSE(pending());

    const auto copied = parseResult(backend.invokeBackend(
        "CopyAsOverrideMany", nlohmann::json::array({items.dump(), "Patch.esp", false, true})));
    CHECK(copied.at("ok") == 1);
    CHECK(copied.at("requiresLoadOrderConfirmation") == false);
    CHECK(copied.at("losing").empty());
    CHECK(pending());
}

TEST_CASE("Native CopyAsOverrideMany does not ask when the target already loads last") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto source = root / "SourceA.esp";
    const auto target = root / "Patch.esp";
    writeBytes(source, makeSource(0x00000800, "WeaponA"));
    writeBytes(target, makeTarget());

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("SourceA.esp", source),
            pluginEntry("Patch.esp", target)
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    const nlohmann::json items = nlohmann::json::array({
        {{"formKey", "000800:Master.esm"}, {"source", "SourceA.esp"}}
    });
    const auto result = parseResult(backend.invokeBackend(
        "CopyAsOverrideMany", nlohmann::json::array({items.dump(), "Patch.esp", false})));
    CHECK(result.at("ok") == 1);
    CHECK(result.at("requiresLoadOrderConfirmation") == false);
}

TEST_CASE("Native CopyAsOverrideMany reports a wrong-typed item and applies the rest") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto sourceA = root / "SourceA.esp";
    const auto target = root / "Patch.esp";
    writeBytes(sourceA, makeSource(0x00000800, "WeaponA"));
    writeBytes(target, makeTarget());

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("SourceA.esp", sourceA),
            pluginEntry("Patch.esp", target)
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    const nlohmann::json items = nlohmann::json::array({
        {{"formKey", "000800:Master.esm"}, {"source", "SourceA.esp"}},
        {{"formKey", "000802:Master.esm"}, {"source", 2}}
    });
    const auto result = parseResult(backend.invokeBackend(
        "CopyAsOverrideMany",
        nlohmann::json::array({items.dump(), "Patch.esp", false})));
    CHECK(result.at("total") == 2);
    CHECK(result.at("ok") == 1);
    CHECK(result.at("failed") == 1);
    REQUIRE(result.at("failures").size() == 1);
    CHECK(result.at("failures")[0].at("reason") == "source and formKey must be strings");

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto reloaded = fo4::read_plugin_file(target);
    CHECK(fo4::find_record(reloaded, fo4::FormKey::parse("000800:Master.esm")) != nullptr);
    CHECK(fo4::find_record(reloaded, fo4::FormKey::parse("000802:Master.esm")) == nullptr);
}

namespace {

fo4::FormKey makeArmorFile(const std::filesystem::path& path, std::string_view name,
                           std::optional<std::uint8_t> icon) {
    auto plugin = fo4::make_empty_plugin(name);
    const auto key = fo4::create_record(plugin, fo4::Signature::from_string("ARMO"), "Armor").key;
    if (icon) {
        std::get<fo4::Record>(plugin.groups.front().children.front()).subrecords.push_back(
            {fo4::Signature::from_string("ICON"), {std::byte{*icon}, std::byte{0}}, false});
    }
    std::filesystem::create_directories(path.parent_path());
    fo4::write_plugin_file(plugin, path);
    return key;
}

std::vector<std::vector<std::byte>> iconsOf(const std::filesystem::path& path, const fo4::FormKey& key) {
    const auto plugin = fo4::read_plugin_file(path);
    const auto* record = fo4::find_record(plugin, key);
    REQUIRE(record != nullptr);
    std::vector<std::vector<std::byte>> out;
    for (const auto& sub : record->subrecords) {
        if (sub.signature == fo4::Signature::from_string("ICON")) out.push_back(sub.data);
    }
    return out;
}

}

TEST_CASE("Native CopyElementToSelected makes each selected record's element equal to the source and reports an unloaded target") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto sourcePath = root / "Source.esp";
    const auto targetA = root / "TargetA.esp";
    const auto targetB = root / "TargetB.esp";
    const auto sourceKey = makeArmorFile(sourcePath, "Source.esp", std::uint8_t{7});
    const auto keyA = makeArmorFile(targetA, "TargetA.esp", std::uint8_t{3});
    const auto keyB = makeArmorFile(targetB, "TargetB.esp", std::nullopt);

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("Source.esp", sourcePath),
            pluginEntry("TargetA.esp", targetA),
            pluginEntry("TargetB.esp", targetB)
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    const nlohmann::json targets = nlohmann::json::array({
        {{"plugin", "TargetA.esp"}, {"formKey", keyA.to_string()}},
        {{"plugin", "TargetB.esp"}, {"formKey", keyB.to_string()}},
        {{"plugin", "Missing.esp"}, {"formKey", keyA.to_string()}}
    });
    const auto report = parseResult(backend.invokeBackend("CopyElementToSelected", nlohmann::json::array({
        "Source.esp", sourceKey.to_string(), "Authoring\\ICON[0]", targets.dump()})));
    CHECK(report.at("applied") == 2);
    CHECK(report.at("skipped").empty());
    REQUIRE(report.at("failures").size() == 1);
    CHECK(report.at("failures")[0].at("plugin") == "Missing.esp");

    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"TargetA.esp"})).has_value());
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"TargetB.esp"})).has_value());
    const std::vector<std::byte> expected{std::byte{7}, std::byte{0}};
    CHECK(iconsOf(targetA, keyA) == std::vector<std::vector<std::byte>>{expected});
    CHECK(iconsOf(targetB, keyB) == std::vector<std::vector<std::byte>>{expected});
}

TEST_CASE("Native RemoveElementFromSelected removes where present, skips where absent, and refuses a required element") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto targetA = root / "TargetA.esp";
    const auto targetB = root / "TargetB.esp";
    const auto keyA = makeArmorFile(targetA, "TargetA.esp", std::uint8_t{3});
    const auto keyB = makeArmorFile(targetB, "TargetB.esp", std::nullopt);

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("TargetA.esp", targetA),
            pluginEntry("TargetB.esp", targetB)
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    const nlohmann::json targets = nlohmann::json::array({
        {{"plugin", "TargetA.esp"}, {"formKey", keyA.to_string()}},
        {{"plugin", "TargetB.esp"}, {"formKey", keyB.to_string()}}
    });
    const auto removed = parseResult(backend.invokeBackend("RemoveElementFromSelected", nlohmann::json::array({
        "Authoring\\ICON[0]", targets.dump()})));
    CHECK(removed.at("applied") == 1);
    REQUIRE(removed.at("skipped").size() == 1);
    CHECK(removed.at("skipped")[0].at("plugin") == "TargetB.esp");
    CHECK(removed.at("failures").empty());

    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"TargetA.esp"})).has_value());
    CHECK(iconsOf(targetA, keyA).empty());

    const nlohmann::json requiredTarget = nlohmann::json::array({
        {{"plugin", "TargetB.esp"}, {"formKey", keyB.to_string()}}
    });
    const auto refused = parseResult(backend.invokeBackend("RemoveElementFromSelected", nlohmann::json::array({
        "Authoring\\DATA[0]", requiredTarget.dump()})));
    CHECK(refused.at("applied") == 0);
    REQUIRE(refused.at("failures").size() == 1);
    CHECK(refused.at("failures")[0].at("reason").get<std::string>().starts_with("ElementBelowMinimum"));
}

TEST_CASE("Native CopyElementToSelected accepts Authoring\\ICON without an index (regression: ElementPathUnsupported for the non-indexed path)") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto sourcePath = root / "Source.esp";
    const auto targetPath = root / "Target.esp";
    const auto sourceKey = makeArmorFile(sourcePath, "Source.esp", std::uint8_t{7});
    const auto targetKey = makeArmorFile(targetPath, "Target.esp", std::uint8_t{3});

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("Source.esp", sourcePath),
            pluginEntry("Target.esp", targetPath)
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    const nlohmann::json targets = nlohmann::json::array({
        {{"plugin", "Target.esp"}, {"formKey", targetKey.to_string()}}
    });
    const auto report = parseResult(backend.invokeBackend("CopyElementToSelected", nlohmann::json::array({
        "Source.esp", sourceKey.to_string(), "Authoring\\ICON", targets.dump()})));
    CHECK(report.at("applied") == 1);
    CHECK(report.at("failures").empty());

    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Target.esp"})).has_value());
    const std::vector<std::byte> expected{std::byte{7}, std::byte{0}};
    CHECK(iconsOf(targetPath, targetKey) == std::vector<std::vector<std::byte>>{expected});
}
