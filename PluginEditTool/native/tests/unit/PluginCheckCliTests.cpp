
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <fo4/plugin/reader.hpp>
#include <fo4/plugin/subrecord_authoring.hpp>
#include <fo4/plugin/writer.hpp>
#include <fo4/schema/formlink_lists.hpp>
#include <fo4/schema/record_constraints.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "../../src/plugin/RadiclePluginEffectRows.hpp"

#if defined(_WIN32)
#define FO4IDE_POPEN _popen
#define FO4IDE_PCLOSE _pclose
#else
#define FO4IDE_POPEN popen
#define FO4IDE_PCLOSE pclose
#include <sys/wait.h>
#endif

namespace {

struct TempRoot {
    std::filesystem::path root;
    TempRoot() {
        root = std::filesystem::temp_directory_path() /
               ("fo4ide_check_cli_" + std::to_string(
                    std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(root);
    }
    ~TempRoot() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

std::string shellQuote(const std::filesystem::path& path) {
    auto text = path.string();
#if defined(_WIN32)
    std::replace(text.begin(), text.end(), '/', '\\');
#endif
    if (text.find(' ') == std::string::npos) return text;
    return std::string("\"") + text + "\"";
}

struct RunResult {
    int exitCode{-1};
    std::string output;
};

RunResult runCheck(const std::string& arguments) {
    const std::string command =
        shellQuote(FO4IDE_APP_EXE) + " " + arguments + " 2>&1";
    RunResult result;
    auto* pipe = FO4IDE_POPEN(command.c_str(), "r");
    REQUIRE(pipe != nullptr);
    std::array<char, 4096> buffer{};
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        result.output += buffer.data();
    }
    const int status = FO4IDE_PCLOSE(pipe);
#if defined(_WIN32)
    result.exitCode = status;
#else
    result.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
    return result;
}

void appendU32(std::vector<std::byte>& bytes, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        bytes.push_back(static_cast<std::byte>((value >> shift) & 0xFFu));
    }
}

void appendU16(std::vector<std::byte>& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::byte>(value & 0xFFu));
    bytes.push_back(static_cast<std::byte>((value >> 8) & 0xFFu));
}

void appendSignature(std::vector<std::byte>& bytes, std::string_view signature) {
    for (const auto ch : signature) bytes.push_back(static_cast<std::byte>(ch));
}

fo4::Plugin cleanPlugin() {
    std::vector<std::byte> hedr;
    appendU32(hedr, 0x3F800000u);
    appendU32(hedr, 0);
    appendU32(hedr, 0x800u);

    std::vector<std::byte> bytes;
    appendSignature(bytes, "TES4");
    appendU32(bytes, static_cast<std::uint32_t>(hedr.size() + 6));
    appendU32(bytes, 0);
    appendU32(bytes, 0);
    appendU16(bytes, 0);
    appendU16(bytes, 0);
    appendU16(bytes, 0);
    appendU16(bytes, 0);
    appendSignature(bytes, "HEDR");
    appendU16(bytes, static_cast<std::uint16_t>(hedr.size()));
    bytes.insert(bytes.end(), hedr.begin(), hedr.end());

    auto plugin = fo4::read_plugin(bytes, "Check.esp");
    plugin.original_range.clear();
    plugin.tes4.original_range.clear();
    const auto created =
        fo4::create_record(plugin, fo4::Signature::from_string("GLOB"), "CheckGlobal");
    REQUIRE(created.status == fo4::RecordCreateStatus::created);
    return plugin;
}

std::filesystem::path writePlugin(const std::filesystem::path& path, const fo4::Plugin& plugin) {
    auto copy = plugin;
    copy.original_range.clear();
    copy.tes4.original_range.clear();
    const auto bytes = fo4::write_plugin(copy);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    REQUIRE(out.good());
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    REQUIRE(out.good());
    return path;
}

}

// Detects: CLI must return clean JSON and exit zero for a clean plugin.
TEST_CASE("The check CLI exits 0 and reports clean JSON for a clean plugin", "[check-cli]") {
    TempRoot temp;
    const auto path = writePlugin(temp.root / "Clean.esp", cleanPlugin());

    const auto result = runCheck("--check " + shellQuote(path) + " --json");
    INFO("command: " << FO4IDE_APP_EXE << " --check " << path.string() << " --json");
    INFO("captured " << result.output.size() << " bytes, exit " << result.exitCode);
    CHECK(result.exitCode == 0);
    REQUIRE_FALSE(result.output.empty());

    const auto json = nlohmann::json::parse(result.output);
    CHECK(json.at("clean").get<bool>());
    CHECK(json.at("errors").get<std::size_t>() == 0);
    CHECK(json.at("findings").is_array());
    CHECK(json.at("plugin").get<std::string>() == "Clean.esp");
}

void bumpHedrInFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    REQUIRE(in.good());
    const auto size = in.tellg();
    REQUIRE(size > 0);
    std::vector<char> bytes(static_cast<std::size_t>(size));
    in.seekg(0);
    in.read(bytes.data(), size);
    in.close();
    REQUIRE(in.gcount() == size);

    const auto tes4Size = static_cast<std::size_t>(
        static_cast<unsigned char>(bytes[4]) | (static_cast<unsigned char>(bytes[5]) << 8) |
        (static_cast<unsigned char>(bytes[6]) << 16) |
        (static_cast<unsigned char>(bytes[7]) << 24));
    bool patched = false;
    for (std::size_t pos = 24; pos + 6 <= 24 + tes4Size;) {
        const auto subSize = static_cast<std::size_t>(
            static_cast<unsigned char>(bytes[pos + 4]) |
            (static_cast<unsigned char>(bytes[pos + 5]) << 8));
        if (std::memcmp(bytes.data() + pos, "HEDR", 4) == 0 && subSize >= 12) {
            auto value = static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[pos + 10])) |
                         (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[pos + 11])) << 8) |
                         (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[pos + 12])) << 16) |
                         (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[pos + 13])) << 24);
            value += 3;
            for (int index = 0; index < 4; ++index) {
                bytes[pos + 10 + index] = static_cast<char>((value >> (index * 8)) & 0xFFu);
            }
            patched = true;
            break;
        }
        pos += 6 + subSize;
    }
    REQUIRE(patched);

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    REQUIRE(out.good());
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(out.good());
}

// Detects: CLI strict mode must fail and report a stale HEDR count.
TEST_CASE("The check CLI with --strict exits 1 and names the warning for a broken HEDR", "[check-cli]") {
    TempRoot temp;
    const auto path = writePlugin(temp.root / "Broken.esp", cleanPlugin());
    bumpHedrInFile(path);

    const auto result = runCheck("--check " + shellQuote(path) + " --json --strict");
    INFO("output: " << result.output);
    CHECK(result.exitCode == 1);

    const auto json = nlohmann::json::parse(result.output);
    REQUIRE(json.at("findings").is_array());
    bool saw = false;
    for (const auto& finding : json.at("findings")) {
        if (finding.at("code").get<std::string>() != "HEDR_RECORD_COUNT_MISMATCH") continue;
        saw = true;
        CHECK(finding.at("severity").get<std::string>() == "warning");
        CHECK(finding.at("field").get<std::string>() == "HEDR");
    }
    CHECK(saw);
}

// Detects: CLI must allow warnings without strict mode.
TEST_CASE("The check CLI keeps exit 0 for a warning-only plugin without strict", "[check-cli]") {
    TempRoot temp;
    const auto path = writePlugin(temp.root / "Warn.esp", cleanPlugin());

    const auto plain = runCheck("--check " + shellQuote(path));
    CHECK(plain.exitCode == 0);
    const auto strict = runCheck("--check " + shellQuote(path) + " --strict");
    CHECK(strict.exitCode == 0);
}

// Detects: CLI must accept schema-authored keywords, form lists and cures.
TEST_CASE("The check CLI accepts a keyword form list and cure authored from the schema", "[check-cli]") {
    TempRoot temp;
    auto plugin = cleanPlugin();
    const auto keywordSig = fo4::Signature::from_string("KYWD");
    const auto constraint = fo4::schema::record_authoring_constraint(keywordSig);
    REQUIRE(constraint.has_value());
    const auto generated = fo4::schema::authoring_subrecords(*constraint);
    std::vector<fo4::schema::SubrecordAuthoringConstraint> rules(generated.begin(),
                                                              generated.end());
    static const std::uint8_t color[4] = {0x40, 0x80, 0x30, 0xFF};
    static const std::uint8_t type[4] = {0, 0, 0, 0};
    for (auto& rule : rules) {
        if (rule.signature == fo4::Signature::from_string("CNAM")) {
            rule.minimum = 1;
            rule.maximum = 1;
            rule.default_data = color;
            rule.default_size = 4;
        }
        if (rule.signature == fo4::Signature::from_string("TNAM")) {
            rule.minimum = 1;
            rule.maximum = 1;
            rule.default_data = type;
            rule.default_size = 4;
        }
    }
    const auto keyword = fo4::create_record(plugin, keywordSig, "RadDiseaseKeyword", rules);
    REQUIRE(keyword.status == fo4::RecordCreateStatus::created);
    const auto list = fo4::create_record(plugin, fo4::Signature::from_string("FLST"),
                                        "RadDiseaseForms");
    REQUIRE(list.status == fo4::RecordCreateStatus::created);
    const auto magic = fo4::create_record(plugin, fo4::Signature::from_string("MGEF"),
                                         "RadCureMagic");
    REQUIRE(magic.status == fo4::RecordCreateStatus::created);
    const auto cure = fo4::create_record(plugin, fo4::Signature::from_string("ALCH"), "RadCure");
    REQUIRE(cure.status == fo4::RecordCreateStatus::created);

    const auto descriptor = fo4::schema::formlink_list_descriptor(
        fo4::Signature::from_string("FLST"), "FLST\\Items");
    REQUIRE(descriptor.has_value());
    REQUIRE(fo4::schema::insert_record_formlink_list_element(plugin, list.key, *descriptor, 0,
                                                             keyword.key) ==
            fo4::schema::FormLinkListMutationStatus::updated);
    REQUIRE(fo4::schema::insert_record_formlink_list_element(plugin, list.key, *descriptor, 1,
                                                             magic.key) ==
            fo4::schema::FormLinkListMutationStatus::updated);

    fo4ide::plugin::effects::Entry entry;
    entry.effect = magic.key;
    entry.magnitude = -10.0f;
    std::size_t index = 0;
    REQUIRE(fo4ide::plugin::effects::add(plugin, cure.key, entry, &index) ==
            fo4ide::plugin::effects::Write::written);

    const auto path = writePlugin(temp.root / "RadCure.esp", plugin);
    const auto result = runCheck("--check " + shellQuote(path) + " --json --strict");
    INFO("output: " << result.output);
    CHECK(result.exitCode == 0);
    if (!result.output.empty()) {
        const auto json = nlohmann::json::parse(result.output);
        CHECK(json.at("clean").get<bool>());
        CHECK(json.at("errors").get<std::size_t>() == 0);
    }
}

// Detects: CLI must exit 2 when plugin or contract input cannot be read.
TEST_CASE("The check CLI exits 2 when it cannot run", "[check-cli]") {
    TempRoot temp;
    const auto missing = runCheck("--check " + shellQuote(temp.root / "Absent.esp"));
    CHECK(missing.exitCode == 2);
    const bool mentionsMissing = !missing.output.empty();
    CHECK(mentionsMissing);

    const auto path = writePlugin(temp.root / "Clean.esp", cleanPlugin());
    std::ofstream corrupt(temp.root / "Corrupt.esp", std::ios::binary | std::ios::trunc);
    corrupt << "not a plugin";
    corrupt.close();
    const auto unreadable = runCheck("--check " + shellQuote(temp.root / "Corrupt.esp"));
    CHECK(unreadable.exitCode == 2);

    const auto badContract =
        runCheck("--check " + shellQuote(path) + " --contract " + shellQuote(temp.root / "no.json"));
    CHECK(badContract.exitCode == 2);
    // Detects: an explicit empty contract must fail instead of silently running without assertions.
    CHECK(runCheck("--check " + shellQuote(path) + " --contract \"\"").exitCode == 2);
}

// Detects: CLI and validation tool must reject malformed contract assertions.
TEST_CASE("Malformed contract shapes fail through the CLI and validation tool", "[check-cli]") {
    TempRoot temp;
    const auto path = writePlugin(temp.root / "Clean.esp", cleanPlugin());
    const auto contractPath = temp.root / "contract.json";
    fo4ide::plugin::RadiclePluginBackend backend;
    for (const auto* text : {R"({"record":[]})", R"({"records":"bad"})", R"({"records":[{}]})",
                             R"({"globals":[{"editorId":"CheckGlobal"}]})",
                             R"({"requiredFields":[{"editorId":"CheckGlobal"}]})",
                             R"({"vmad":[{"record":"000800:Clean.esp"}]})",
                             R"({"vmad":[{"record":"000800:Clean.esp","script":"Test","properties":[{}]}]})"}) {
        { std::ofstream out(contractPath); out << text; }
        const auto result = runCheck("--check " + shellQuote(path) + " --contract " + shellQuote(contractPath));
        INFO(text);
        CHECK(result.exitCode == 2);
        CHECK(result.output.find("InvalidContract") != std::string::npos);
        const auto toolResult = backend.invoke("backend.ValidatePlugin", nlohmann::json::array({"Clean.esp", text}));
        REQUIRE_FALSE(toolResult.has_value());
        CHECK(toolResult.error().type == "InvalidContract");
    }
    { std::ofstream out(contractPath); out << R"({"records":[{"editorId":"CheckGlobal","type":"GLOB"}]})"; }
    CHECK(runCheck("--check " + shellQuote(path) + " --contract " + shellQuote(contractPath)).exitCode == 0);
}

// Detects: backend must apply object contracts and reject unsupported argument types.
TEST_CASE("Validation backend accepts an inline contract object", "[check-cli]") {
    TempRoot temp;
    const auto path = writePlugin(temp.root / "Clean.esp", cleanPlugin());
    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", {{"plugins", nlohmann::json::array({
        {{"name", "Clean.esp"}, {"path", path.string()}, {"source", "Test"}, {"resolved", true}}
    })}}).has_value());
    const nlohmann::json contract{{"records", nlohmann::json::array({{{"editorId", "CheckGlobal"}, {"type", "SPEL"}}})}};
    const auto result = backend.invoke("backend.ValidatePlugin", nlohmann::json::array({"Clean.esp", contract}));
    REQUIRE(result.has_value());
    bool found = false;
    for (const auto& row : result->at("findings")) found |= row.at("code") == "CONTRACT_RECORD_TYPE_MISMATCH";
    CHECK(found);
    const auto invalid = backend.invoke("backend.ValidatePlugin", nlohmann::json::array({"Clean.esp", nlohmann::json::array()}));
    REQUIRE_FALSE(invalid.has_value());
    CHECK(invalid.error().type == "InvalidContract");
    const auto empty = backend.invoke("backend.ValidatePlugin", nlohmann::json::array({"Clean.esp", ""}));
    REQUIRE_FALSE(empty.has_value());
    CHECK(empty.error().type == "InvalidContract");
}

// Detects: CLI must exit 2 when a requested scripts directory is missing.
TEST_CASE("The check CLI exits 2 when the scripts folder does not exist", "[check-cli]") {
    TempRoot temp;
    const auto path = writePlugin(temp.root / "Clean.esp", cleanPlugin());
    const auto result =
        runCheck("--check " + shellQuote(path) + " --scripts " + shellQuote(temp.root / "Absent"));
    CHECK(result.exitCode == 2);
    CHECK(result.output.find("ScriptsNotFound") != std::string::npos);
}

// Detects: CLI must treat --check after a missing contract value as an option.
TEST_CASE("A --check spelled as an option value still enters the CLI and exits 2", "[check-cli]") {
    const auto result = runCheck("--contract --check --health");
    CHECK(result.exitCode == 2);
    CHECK(result.output.find("Validates one plugin") != std::string::npos);
}

// Detects: CLI must treat --check after a missing scripts value as an option.
TEST_CASE("A missing --scripts value before --check exits 2 without starting the editor", "[check-cli]") {
    TempRoot temp;
    const auto path = writePlugin(temp.root / "Clean.esp", cleanPlugin());
    const auto result = runCheck("--scripts --check " + shellQuote(path));
    CHECK(result.exitCode == 2);
    CHECK(result.output.find("Validates one plugin") != std::string::npos);
}
