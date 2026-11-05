#include <algorithm>
#include "../helpers/TempRoot.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <iterator>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

using namespace std::chrono_literals;

std::filesystem::path tempRoot() {
    const auto id = fo4ide_test::uniqueTempName();
    return std::filesystem::temp_directory_path() /
           ("fo4ide_radicle_concurrency_" + id);
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

void writeHeader(fo4::test::ByteWriter& out, bool withMaster) {
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(2);
    hedr.u32(0x900);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    if (withMaster) {
        fo4::test::write_subrecord(out, "MAST", std::string_view{"Master.esm\0", 11});
        fo4::test::ByteWriter data;
        data.u32(0);
        data.u32(0);
        fo4::test::write_subrecord(out, "DATA", data.bytes);
    }
    fo4::test::end_record(out, tes4);
}

void writeMisc(fo4::test::ByteWriter& out, std::uint32_t formId, std::string_view editorId) {
    const auto record = fo4::test::begin_record(out, "MISC", 0, formId);
    std::string edid(editorId);
    edid.push_back('\0');
    fo4::test::write_subrecord(out, "EDID", std::string_view{edid});
    fo4::test::end_record(out, record);
}

std::vector<std::byte> makeLocalPlugin() {
    fo4::test::ByteWriter out;
    writeHeader(out, false);
    const auto group = fo4::test::begin_group(out, "MISC", 0);
    writeMisc(out, 0x00000800, "FirstLocal");
    writeMisc(out, 0x00000801, "SecondLocal");
    fo4::test::end_group(out, group);
    return out.bytes;
}

std::vector<std::byte> makeOverridePlugin(std::uint32_t formId, std::string_view editorId) {
    fo4::test::ByteWriter out;
    writeHeader(out, true);
    const auto group = fo4::test::begin_group(out, "MISC", 0);
    writeMisc(out, formId, editorId);
    fo4::test::end_group(out, group);
    return out.bytes;
}

nlohmann::json pluginEntry(const std::string& name, const std::filesystem::path& path) {
    return {{"name", name}, {"path", path.string()}, {"source", "test"}, {"resolved", true}};
}

void loadPlugins(fo4ide::plugin::RadiclePluginBackend& backend,
                 const std::vector<std::pair<std::string, std::filesystem::path>>& plugins) {
    auto list = nlohmann::json::array();
    for (const auto& [name, path] : plugins) list.push_back(pluginEntry(name, path));
    REQUIRE(backend.invoke("environment.LoadMo2", nlohmann::json{{"plugins", list}}).has_value());
}

fo4ide::Result<nlohmann::json> setEditorId(fo4ide::plugin::RadiclePluginBackend& backend,
                                           const std::string& plugin,
                                           const std::string& formKey,
                                           const std::string& value) {
    return backend.invokeBackend(
        "SetField",
        nlohmann::json::array({plugin, formKey, "Subrecords\\EDID[0]", value}));
}

std::string recordEditorId(fo4ide::plugin::RadiclePluginBackend& backend,
                           const std::string& plugin,
                           const std::string& formKey) {
    const auto details = backend.invoke(
        "backend.GetConflictMatrix", nlohmann::json::array({formKey}));
    REQUIRE(details.has_value());
    const auto matrix = nlohmann::json::parse(details->get<std::string>());
    const auto& columns = matrix.at("Plugins");
    const auto column = std::find(columns.begin(), columns.end(), plugin);
    REQUIRE(column != columns.end());
    const auto index = static_cast<std::size_t>(std::distance(columns.begin(), column));
    for (const auto& row : matrix.at("Rows")) {
        if (row.value("Field", std::string{}) == "Subrecords\\EDID[0]") {
            return row.at("Values").at(index).get<std::string>();
        }
    }
    FAIL("EDID row missing for " << formKey);
}

struct HookGate {
    std::mutex mutex;
    std::condition_variable changed;
    int entered{0};
    bool release{false};
    bool timedOut{false};
};

HookGate* gate = nullptr;

void blockFirstWriter() {
    std::unique_lock lock(gate->mutex);
    const int order = ++gate->entered;
    gate->changed.notify_all();
    if (order != 1) return;
    if (!gate->changed.wait_for(lock, 5s, [] { return gate->release; })) {
        gate->timedOut = true;
    }
}

void yieldWriter() {
    std::this_thread::yield();
}

struct HookGuard {
    explicit HookGuard(fo4ide::plugin::WriteOpLockedHook hook) {
        fo4ide::plugin::setWriteOpLockedHook(hook);
    }
    ~HookGuard() { fo4ide::plugin::setWriteOpLockedHook(nullptr); }
};

bool waitForEntered(int count) {
    std::unique_lock lock(gate->mutex);
    return gate->changed.wait_for(lock, 5s, [count] { return gate->entered >= count; });
}

void releaseGate() {
    {
        std::scoped_lock lock(gate->mutex);
        gate->release = true;
    }
    gate->changed.notify_all();
}

}

TEST_CASE("Writers to the same plugin enter their write ops one at a time and keep both edits") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Shared.esp";
    writeBytes(path, makeLocalPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadPlugins(backend, {{"Shared.esp", path}});

    HookGate state;
    gate = &state;
    HookGuard hook(&blockFirstWriter);

    auto first = std::async(std::launch::async, [&] {
        return setEditorId(backend, "Shared.esp", "000800:Shared.esp", "FirstEdited");
    });
    REQUIRE(waitForEntered(1));

    auto second = std::async(std::launch::async, [&] {
        return setEditorId(backend, "Shared.esp", "000801:Shared.esp", "SecondEdited");
    });
    std::this_thread::sleep_for(200ms);
    {
        std::scoped_lock lock(state.mutex);
        CHECK(state.entered == 1);
    }

    releaseGate();
    REQUIRE(first.wait_for(5s) == std::future_status::ready);
    REQUIRE(second.wait_for(5s) == std::future_status::ready);
    CHECK(first.get().has_value());
    CHECK(second.get().has_value());
    CHECK_FALSE(state.timedOut);
    CHECK(state.entered == 2);

    CHECK(recordEditorId(backend, "Shared.esp", "000800:Shared.esp") == "FirstEdited");
    CHECK(recordEditorId(backend, "Shared.esp", "000801:Shared.esp") == "SecondEdited");
    gate = nullptr;
}

TEST_CASE("Writers to unrelated plugins run concurrently") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto alphaPath = root / "Alpha.esp";
    const auto betaPath = root / "Beta.esp";
    writeBytes(alphaPath, makeLocalPlugin());
    writeBytes(betaPath, makeLocalPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadPlugins(backend, {{"Alpha.esp", alphaPath}, {"Beta.esp", betaPath}});

    HookGate state;
    gate = &state;
    HookGuard hook(&blockFirstWriter);

    auto alpha = std::async(std::launch::async, [&] {
        return setEditorId(backend, "Alpha.esp", "000800:Alpha.esp", "AlphaEdited");
    });
    REQUIRE(waitForEntered(1));

    auto beta = std::async(std::launch::async, [&] {
        return setEditorId(backend, "Beta.esp", "000800:Beta.esp", "BetaEdited");
    });
    CHECK(waitForEntered(2));
    {
        std::scoped_lock lock(state.mutex);
        CHECK_FALSE(state.release);
    }

    releaseGate();
    REQUIRE(alpha.wait_for(5s) == std::future_status::ready);
    REQUIRE(beta.wait_for(5s) == std::future_status::ready);
    CHECK(alpha.get().has_value());
    CHECK(beta.get().has_value());
    CHECK_FALSE(state.timedOut);

    CHECK(recordEditorId(backend, "Alpha.esp", "000800:Alpha.esp") == "AlphaEdited");
    CHECK(recordEditorId(backend, "Beta.esp", "000800:Beta.esp") == "BetaEdited");
    gate = nullptr;
}

TEST_CASE("Reversed two-plugin CopyAsOverride does not deadlock") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto leftPath = root / "Left.esp";
    const auto rightPath = root / "Right.esp";
    writeBytes(leftPath, makeOverridePlugin(0x00000800, "LeftOverride"));
    writeBytes(rightPath, makeOverridePlugin(0x00000900, "RightOverride"));

    fo4ide::plugin::RadiclePluginBackend backend;
    loadPlugins(backend, {{"Left.esp", leftPath}, {"Right.esp", rightPath}});

    HookGuard hook(&yieldWriter);
    constexpr int k_iterations = 200;
    std::atomic<int> failures{0};
    const auto copyLoop = [&](const char* source, const char* formKey, const char* target) {
        for (int i = 0; i < k_iterations; ++i) {
            const auto copied = backend.invokeBackend(
                "CopyAsOverride", nlohmann::json::array({source, formKey, target, true, true}));
            if (!copied.has_value()) ++failures;
        }
    };

    auto leftToRight = std::async(std::launch::async, copyLoop,
                                  "Left.esp", "000800:Master.esm", "Right.esp");
    auto rightToLeft = std::async(std::launch::async, copyLoop,
                                  "Right.esp", "000900:Master.esm", "Left.esp");
    REQUIRE(leftToRight.wait_for(8s) == std::future_status::ready);
    REQUIRE(rightToLeft.wait_for(8s) == std::future_status::ready);
    CHECK(failures == 0);

    CHECK(recordEditorId(backend, "Right.esp", "000800:Master.esm") == "LeftOverride");
    CHECK(recordEditorId(backend, "Left.esp", "000900:Master.esm") == "RightOverride");
}

TEST_CASE("A losing-override check sees no later-plugin commit during its compute phase") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto srcPath = root / "Src.esp";
    const auto donorPath = root / "Donor.esp";
    const auto leftPath = root / "Left.esp";
    const auto latePath = root / "Late.esp";
    writeBytes(srcPath, makeOverridePlugin(0x00000800, "SrcOverride"));
    writeBytes(donorPath, makeOverridePlugin(0x00000800, "DonorOverride"));
    writeBytes(leftPath, makeOverridePlugin(0x00000900, "LeftOther"));
    writeBytes(latePath, makeOverridePlugin(0x00000901, "LateOther"));

    fo4ide::plugin::RadiclePluginBackend backend;
    loadPlugins(backend, {{"Src.esp", srcPath},
                          {"Donor.esp", donorPath},
                          {"Left.esp", leftPath},
                          {"Late.esp", latePath}});

    HookGate state;
    gate = &state;
    HookGuard hook(&blockFirstWriter);

    auto left = std::async(std::launch::async, [&] {
        return backend.invokeBackend(
            "CopyAsOverride",
            nlohmann::json::array({"Src.esp", "000800:Master.esm", "Left.esp", false}));
    });
    REQUIRE(waitForEntered(1));

    auto late = std::async(std::launch::async, [&] {
        return backend.invokeBackend(
            "CopyAsOverride",
            nlohmann::json::array({"Donor.esp", "000800:Master.esm", "Late.esp", false}));
    });
    CHECK(waitForEntered(2));
    CHECK(late.wait_for(200ms) == std::future_status::timeout);

    releaseGate();
    REQUIRE(left.wait_for(5s) == std::future_status::ready);
    REQUIRE(late.wait_for(5s) == std::future_status::ready);
    const auto leftResult = left.get();
    const auto lateResult = late.get();
    REQUIRE(leftResult.has_value());
    REQUIRE(lateResult.has_value());
    CHECK_FALSE(state.timedOut);

    CHECK(leftResult->get<std::string>() ==
          "Copied 000800:Master.esm as override in Left.esp.");
    CHECK(recordEditorId(backend, "Left.esp", "000800:Master.esm") == "SrcOverride");
    CHECK(recordEditorId(backend, "Late.esp", "000800:Master.esm") == "DonorOverride");
    gate = nullptr;
}

TEST_CASE("Saves that resolve to one file serialize their file IO") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto alphaPath = root / "Alpha.esp";
    const auto betaPath = root / "Beta.esp";
    writeBytes(alphaPath, makeOverridePlugin(0x00000800, "AlphaSaved"));
    writeBytes(betaPath, makeOverridePlugin(0x00000801, "BetaSaved"));

    fo4ide::plugin::RadiclePluginBackend backend;
    loadPlugins(backend, {{"Alpha.esp", alphaPath}, {"Beta.esp", betaPath}});

    HookGate state;
    gate = &state;
    fo4ide::plugin::setSaveIoLockedHook(&blockFirstWriter);
    struct SaveHookReset {
        ~SaveHookReset() { fo4ide::plugin::setSaveIoLockedHook(nullptr); }
    } saveHookReset;

    auto inPlace = std::async(std::launch::async, [&] {
        return backend.invoke("backend.SavePlugin", nlohmann::json::array({"Beta.esp"}));
    });
    REQUIRE(waitForEntered(1));

    auto saveAs = std::async(std::launch::async, [&] {
        return backend.invoke("backend.SavePlugin",
                              nlohmann::json::array({"Alpha.esp", betaPath.string(), true}));
    });
    CHECK(saveAs.wait_for(200ms) == std::future_status::timeout);
    {
        std::scoped_lock lock(state.mutex);
        CHECK(state.entered == 1);
    }

    releaseGate();
    REQUIRE(inPlace.wait_for(5s) == std::future_status::ready);
    REQUIRE(saveAs.wait_for(5s) == std::future_status::ready);
    CHECK(inPlace.get().has_value());
    CHECK(saveAs.get().has_value());
    CHECK_FALSE(state.timedOut);
    CHECK(state.entered == 2);

    std::ifstream saved(betaPath, std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(saved)), std::istreambuf_iterator<char>());
    CHECK(bytes.find("AlphaSaved") != std::string::npos);
    CHECK(bytes.find("BetaSaved") == std::string::npos);
    gate = nullptr;
}
