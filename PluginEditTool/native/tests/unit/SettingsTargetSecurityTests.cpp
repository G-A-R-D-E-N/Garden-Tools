#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <fo4ide/app/ServiceTargets.hpp>
#include <fo4ide/rpc/RpcDispatcher.hpp>
#include <fo4ide/tools/ToolLocator.hpp>

#include "../helpers/ServiceTargetFakes.hpp"

using namespace fo4ide::app::test;

namespace {

class CountingRunner final : public fo4ide::process::Runner {
public:
    int calls{0};
    fo4ide::process::Request last;

    fo4ide::Result<fo4ide::process::Output> run(const fo4ide::process::Request& request) override {
        ++calls;
        last = request;
        return fo4ide::process::Output{true, false, 0, "claude 1.0", {}};
    }
};

fo4ide::Result<nlohmann::json> saveSettingsJson(fo4ide::app::SettingsTarget& target,
                                                const nlohmann::json& payload) {
    return target.invoke("SaveSettings", nlohmann::json::array({payload.dump()}));
}

class ScopedPath {
public:
    explicit ScopedPath(const std::string& value) {
        if (const char* raw = std::getenv("PATH")) original_ = raw;
        setPath(value);
    }
    ~ScopedPath() { setPath(original_); }

private:
    static void setPath(const std::string& value) {
#if defined(_WIN32)
        _putenv_s("PATH", value.c_str());
#else
        setenv("PATH", value.c_str(), 1);
#endif
    }

    std::string original_;
};

}  // namespace

TEST_CASE("GetSettings withholds the API key value and reports that it is set") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::settings::Store store(root / "settings.json");
    fo4ide::settings::Settings settings;
    settings.anthropicApiKey = "sk-ant-test-secret";
    settings.model = "test-model";
    fo4ide::settings::SharedSettings shared(settings);
    FakeRunner runner;
    FakePlatform platform;

    fo4ide::app::SettingsTarget target(store, shared, platform, runner);
    auto result = target.invoke("GetSettings", nlohmann::json::array());

    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    const auto text = result->get<std::string>();
    CHECK(text.find("sk-ant-test-secret") == std::string::npos);
    CHECK(text.find("\"AnthropicApiKey\"") == std::string::npos);
    CHECK(text.find("\"GeminiApiKey\"") == std::string::npos);
    const auto json = nlohmann::json::parse(text);
    CHECK(json.at("hasAnthropicApiKey") == true);
    CHECK(json.at("hasGeminiApiKey") == false);
    CHECK(json.at("Model") == "test-model");
}

TEST_CASE("GetSettings through the RPC dispatcher is redacted for the MCP get_settings tool") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::settings::Store store(root / "settings.json");
    fo4ide::settings::Settings settings;
    settings.geminiApiKey = "AIza-test-secret";
    fo4ide::settings::SharedSettings shared(settings);
    FakeRunner runner;
    FakePlatform platform;

    fo4ide::rpc::Dispatcher rpc;
    auto registered = rpc.registerTarget(
        "settings", std::make_shared<fo4ide::app::SettingsTarget>(store, shared, platform, runner));
    REQUIRE(registered.has_value());

    const auto response = rpc.invoke({"settings", "GetSettings", nlohmann::json::array()});
    REQUIRE(response.ok);
    REQUIRE(response.value.is_string());
    const auto text = response.value.get<std::string>();
    CHECK(text.find("AIza-test-secret") == std::string::npos);
    CHECK(text.find("\"GeminiApiKey\"") == std::string::npos);
    CHECK(nlohmann::json::parse(text).at("hasGeminiApiKey") == true);
}

TEST_CASE("SaveSettings keeps a stored API key when the payload omits or blanks it") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::settings::Store store(root / "settings.json");
    fo4ide::settings::Settings settings;
    settings.anthropicApiKey = "sk-ant-stored";
    settings.geminiApiKey = "AIza-stored";
    fo4ide::settings::SharedSettings shared(settings);
    FakeRunner runner;
    FakePlatform platform;

    fo4ide::app::SettingsTarget target(store, shared, platform, runner);
    auto omitted = saveSettingsJson(target, nlohmann::json{{"Model", "after"}});
    REQUIRE(omitted.has_value());
    CHECK(*omitted == "Settings saved.");
    CHECK(shared.snapshot().anthropicApiKey == "sk-ant-stored");
    CHECK(shared.snapshot().geminiApiKey == "AIza-stored");

    auto blanked = saveSettingsJson(
        target, nlohmann::json{{"AnthropicApiKey", "   "}, {"GeminiApiKey", ""}});
    REQUIRE(blanked.has_value());
    CHECK(*blanked == "Settings saved.");
    CHECK(shared.snapshot().anthropicApiKey == "sk-ant-stored");
    CHECK(shared.snapshot().geminiApiKey == "AIza-stored");
}

TEST_CASE("SaveSettings replaces a stored API key with the trimmed non-blank value") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::settings::Store store(root / "settings.json");
    fo4ide::settings::Settings settings;
    settings.anthropicApiKey = "sk-ant-old";
    fo4ide::settings::SharedSettings shared(settings);
    FakeRunner runner;
    FakePlatform platform;

    fo4ide::app::SettingsTarget target(store, shared, platform, runner);
    auto result = saveSettingsJson(target, nlohmann::json{{"AnthropicApiKey", "  sk-ant-new  "}});

    REQUIRE(result.has_value());
    CHECK(*result == "Settings saved.");
    CHECK(shared.snapshot().anthropicApiKey == "sk-ant-new");
}

#if defined(_WIN32)
TEST_CASE("TestClaude refuses a cmd or bat path that cmd.exe would interpret") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::settings::Store store(root / "settings.json");
    fo4ide::settings::SharedSettings shared;
    CountingRunner runner;
    FakePlatform platform;

    fo4ide::app::SettingsTarget target(store, shared, platform, runner);
    for (const auto* path : {R"(x" & echo pwned & "y.cmd)", "a|b.bat", "a^b.cmd", "%PATH%.cmd"}) {
        auto result = target.invoke("TestClaude", nlohmann::json::array({path}));
        REQUIRE(result.has_value());
        REQUIRE(result->is_string());
        CHECK(result->get<std::string>().rfind("Refused:", 0) == 0);
    }
    CHECK(runner.calls == 0);
}

TEST_CASE("TestClaude runs a cmd or bat path through cmd.exe with a quoted path") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto script = root / "tools" / "claude.cmd";
    touch(script);
    fo4ide::settings::Store store(root / "settings.json");
    fo4ide::settings::SharedSettings shared;
    CountingRunner runner;
    FakePlatform platform;

    fo4ide::app::SettingsTarget target(store, shared, platform, runner);
    auto result = target.invoke("TestClaude", nlohmann::json::array({script.string()}));

    REQUIRE(result.has_value());
    CHECK(result->get<std::string>().rfind("Found:", 0) == 0);
    CHECK(runner.last.executable.string() == "cmd.exe");
    REQUIRE(runner.last.arguments.size() == 4);
    CHECK(runner.last.arguments.back() == "\"" + script.string() + "\" --version");
}

TEST_CASE("TestClaude resolves a bare name through PATH and PATHEXT before choosing cmd.exe") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    touch(root / "bin" / "fakeclaude.cmd");
    ScopedPath path((root / "bin").string());
    fo4ide::settings::Store store(root / "settings.json");
    fo4ide::settings::SharedSettings shared;
    CountingRunner runner;
    FakePlatform platform;

    fo4ide::app::SettingsTarget target(store, shared, platform, runner);
    auto result = target.invoke("TestClaude", nlohmann::json::array({"fakeclaude"}));

    REQUIRE(result.has_value());
    CHECK(runner.last.executable.string() == "cmd.exe");
    REQUIRE(runner.last.arguments.size() == 4);
    const auto prefix = "\"" + (root / "bin" / "fakeclaude").string() + ".";
    CHECK(runner.last.arguments.back().rfind(prefix, 0) == 0);
}
#endif

TEST_CASE("TestClaude runs an exe path directly with --version") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto exe = root / "tools" / "claude.exe";
    touch(exe);
    fo4ide::settings::Store store(root / "settings.json");
    fo4ide::settings::SharedSettings shared;
    CountingRunner runner;
    FakePlatform platform;

    fo4ide::app::SettingsTarget target(store, shared, platform, runner);
    auto result = target.invoke("TestClaude", nlohmann::json::array({exe.string()}));

    REQUIRE(result.has_value());
    CHECK(*result == "Found: claude 1.0");
    CHECK(runner.calls == 1);
    CHECK(runner.last.executable == exe);
    CHECK(runner.last.arguments == std::vector<std::string>{"--version"});
}

TEST_CASE("SaveSettings refuses a tool path that is missing or has the wrong file name") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    touch(root / "wrong" / "NotTheCompiler.exe");
    fo4ide::settings::Store store(root / "settings.json");
    fo4ide::settings::Settings settings;
    settings.model = "before";
    fo4ide::settings::SharedSettings shared(settings);
    FakeRunner runner;
    FakePlatform platform;

    fo4ide::app::SettingsTarget target(store, shared, platform, runner);
    auto missing = saveSettingsJson(
        target, nlohmann::json{{"Model", "after"},
                               {"PapyrusCompilerPath", (root / "missing" / "PapyrusCompiler.exe").string()}});
    REQUIRE(missing.has_value());
    REQUIRE(missing->is_string());
    CHECK(missing->get<std::string>().find("PapyrusCompilerPath") != std::string::npos);

    auto wrong = saveSettingsJson(
        target, nlohmann::json{{"Model", "after"},
                               {"PapyrusCompilerPath", (root / "wrong" / "NotTheCompiler.exe").string()}});
    REQUIRE(wrong.has_value());
    REQUIRE(wrong->is_string());
    CHECK(wrong->get<std::string>().find("PapyrusCompiler.exe") != std::string::npos);

    CHECK(shared.snapshot().model == "before");
    CHECK(shared.snapshot().papyrusCompilerPath.empty());
    CHECK_FALSE(std::filesystem::exists(root / "settings.json"));
}

TEST_CASE("SaveSettings accepts tool paths with the expected file names and empty values") {
    const auto texconvName = GENERATE(std::string("Texconv.exe"), std::string("texconv"));
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto tools = root / "tools";
    touch(tools / "papyruscompiler.exe");
    touch(tools / texconvName);
    touch(tools / "claude.exe");
    fo4ide::settings::Store store(root / "settings.json");
    fo4ide::settings::Settings settings;
    settings.archive2Path = root / "old" / "Archive2.exe";
    fo4ide::settings::SharedSettings shared(settings);
    FakeRunner runner;
    FakePlatform platform;

    fo4ide::app::SettingsTarget target(store, shared, platform, runner);
    auto result = saveSettingsJson(
        target, nlohmann::json{{"PapyrusCompilerPath", (tools / "papyruscompiler.exe").string()},
                               {"TexconvPath", (tools / texconvName).string()},
                               {"ClaudeCodePath", (tools / "claude.exe").string()},
                               {"Archive2Path", ""}});

    REQUIRE(result.has_value());
    CHECK(*result == "Settings saved.");
    CHECK(shared.snapshot().papyrusCompilerPath == tools / "papyruscompiler.exe");
    CHECK(shared.snapshot().texconvPath == tools / texconvName);
    CHECK(shared.snapshot().claudeCodePath == tools / "claude.exe");
    CHECK(shared.snapshot().archive2Path.empty());
}

TEST_CASE("SaveSettings accepts a bare ClaudeCodePath that resolves on PATH") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
#if defined(_WIN32)
    touch(root / "bin" / "claude.cmd");
#else
    touch(root / "bin" / "claude");
#endif
    ScopedPath path((root / "bin").string());
    fo4ide::settings::Store store(root / "settings.json");
    fo4ide::settings::SharedSettings shared;
    FakeRunner runner;
    FakePlatform platform;

    fo4ide::app::SettingsTarget target(store, shared, platform, runner);
    auto result = saveSettingsJson(target, nlohmann::json{{"ClaudeCodePath", "claude"}});

    REQUIRE(result.has_value());
    CHECK(*result == "Settings saved.");
    CHECK(shared.snapshot().claudeCodePath == std::filesystem::path("claude"));
}

TEST_CASE("SaveSettings refuses a bare ClaudeCodePath that is not on PATH and saves nothing") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    std::filesystem::create_directories(root / "bin");
    ScopedPath path((root / "bin").string());
    fo4ide::settings::Store store(root / "settings.json");
    fo4ide::settings::Settings settings;
    settings.model = "before";
    fo4ide::settings::SharedSettings shared(settings);
    FakeRunner runner;
    FakePlatform platform;

    fo4ide::app::SettingsTarget target(store, shared, platform, runner);
    auto result = saveSettingsJson(target, nlohmann::json{{"Model", "after"}, {"ClaudeCodePath", "claude"}});

    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    CHECK(result->get<std::string>().find("ClaudeCodePath") != std::string::npos);
    CHECK(shared.snapshot().model == "before");
    CHECK(shared.snapshot().claudeCodePath.empty());
    CHECK_FALSE(std::filesystem::exists(root / "settings.json"));
}

TEST_CASE("A Locator on shared settings sees a save made through SettingsTarget") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto texconv = root / "tools" / "Texconv.exe";
    touch(texconv);
    fo4ide::settings::Store store(root / "settings.json");
    fo4ide::settings::SharedSettings shared;
    FakeRunner runner;
    FakePlatform platform;

    fo4ide::app::SettingsTarget target(store, shared, platform, runner);
    fo4ide::tools::Locator locator(shared, root / "FO4IDE-native");
    CHECK(locator.resolve(fo4ide::tools::Tool::Texconv) != texconv);

    auto result = saveSettingsJson(target, nlohmann::json{{"TexconvPath", texconv.string()}});
    REQUIRE(result.has_value());
    CHECK(*result == "Settings saved.");
    CHECK(locator.resolve(fo4ide::tools::Tool::Texconv) == texconv);
}

TEST_CASE("Concurrent saves and locator resolves never expose a torn settings value") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::settings::Store store(root / "settings.json");
    fo4ide::settings::SharedSettings shared;
    FakeRunner runner;
    FakePlatform platform;

    fo4ide::app::SettingsTarget target(store, shared, platform, runner);
    fo4ide::tools::Locator locator(shared, root / "FO4IDE-native");
    const std::string first(64, 'a');
    const std::string second(64, 'b');
    std::atomic<bool> failed{false};
    std::atomic<bool> torn{false};

    std::jthread saver([&] {
        for (int i = 0; i < 100; ++i) {
            const auto& model = i % 2 ? first : second;
            auto result = saveSettingsJson(target, nlohmann::json{{"Model", model}});
            if (!result || *result != "Settings saved.") failed = true;
        }
    });
    std::jthread reader([&] {
        for (int i = 0; i < 200; ++i) {
            const auto model = shared.snapshot().model;
            if (!model.empty() && model != first && model != second) torn = true;
            static_cast<void>(locator.resolve(fo4ide::tools::Tool::Texconv));
        }
    });
    saver.join();
    reader.join();

    CHECK_FALSE(failed.load());
    CHECK_FALSE(torn.load());
}
