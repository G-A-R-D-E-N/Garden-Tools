#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/papyrus/decompiler.hpp>
#include <fo4/pex/pex.hpp>
#include <fo4ide/app/ServiceTargets.hpp>
#include <fo4ide/papyrus/AnalysisService.hpp>
#include <fo4ide/papyrus/QuestFragmentSource.hpp>
#include <fo4ide/papyrus/Parser.hpp>
#include <fo4ide/papyrus/SecureSourceSnapshot.hpp>
#include <fo4/papyrus/analysis_service.hpp>

#include "../helpers/GraphTestSupport.hpp"
#include "../helpers/ServiceTargetFakes.hpp"

#ifdef __linux__
#include <fcntl.h>
#include <sched.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
using namespace fo4ide::app::test;
#ifdef __linux__
namespace {
std::filesystem::path snapshotHookFile;
void rewriteSnapshotSource() {
    { std::ofstream out(snapshotHookFile, std::ios::binary | std::ios::trunc); out << "Scriptname Other\n"; }
    const auto now = std::filesystem::last_write_time(snapshotHookFile);
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch());
    std::filesystem::last_write_time(snapshotHookFile,
        std::filesystem::file_time_type(seconds + std::chrono::milliseconds(200)));
}
#ifdef __linux__
void swapSnapshotSourceForFifo() {
    std::filesystem::remove(snapshotHookFile);
    if (::mkfifo(snapshotHookFile.c_str(), 0600) != 0) throw std::runtime_error("cannot make test FIFO");
}
#endif
}
// Detects: an import snapshot must reject symlink escapes and overlarge source files.
TEST_CASE("Secure Papyrus source snapshots are bounded and reject links") {
    const auto root = tempRoot();
    std::filesystem::create_directories(root / "nested");
    { std::ofstream out(root / "nested" / "Quest.psc"); out << "Scriptname Quest\n"; }
    const int fd = ::open(root.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    REQUIRE(fd >= 0);
    const auto files = fo4ide::papyrus::snapshotPapyrusSources(fd, 100, 128, 256);
    REQUIRE(files.size() == 1u);
    CHECK(files[0].relativePath == std::filesystem::path("nested") / "Quest.psc");
    CHECK(files[0].text == "Scriptname Quest\n");
    CHECK_THROWS(fo4ide::papyrus::snapshotPapyrusSources(fd, 100, 4, 256));
    snapshotHookFile = root / "nested" / "Quest.psc";
    const auto now = std::filesystem::last_write_time(snapshotHookFile);
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch());
    std::filesystem::last_write_time(snapshotHookFile,
        std::filesystem::file_time_type(seconds + std::chrono::milliseconds(100)));
    fo4ide::papyrus::setSnapshotReadHook(rewriteSnapshotSource);
    CHECK_THROWS(fo4ide::papyrus::snapshotPapyrusSources(fd, 100, 128, 256));
    fo4ide::papyrus::setSnapshotReadHook(nullptr);
    std::error_code ec;
    std::filesystem::create_directory_symlink(root.parent_path(), root / "nested" / "outside", ec);
    if (!ec) CHECK_THROWS(fo4ide::papyrus::snapshotPapyrusSources(fd, 100, 128, 256));
    ::close(fd);
    std::filesystem::remove_all(root, ec);
}
// Detects: a deeply nested import tree must stop before exhausting the stack or file descriptors.
TEST_CASE("Secure Papyrus source snapshots cap directory depth") {
    const auto root = tempRoot();
    auto nested = root;
    for (int i = 0; i < 70; ++i) { nested /= "d"; std::filesystem::create_directories(nested); }
    const int fd = ::open(root.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    REQUIRE(fd >= 0);
    CHECK_THROWS(fo4ide::papyrus::snapshotPapyrusSources(fd, 1000, 128, 256));
    ::close(fd);
    std::error_code ec; std::filesystem::remove_all(root, ec);
}
#ifdef __linux__
// Detects: an import entry replaced by a special file between inspection and open must be refused.
TEST_CASE("Secure Papyrus source snapshots refuse a swapped special file") {
    const auto root = tempRoot(); std::filesystem::create_directories(root);
    snapshotHookFile = root / "Quest.psc";
    { std::ofstream out(snapshotHookFile); out << "Scriptname Quest\n"; }
    const int fd = ::open(root.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    REQUIRE(fd >= 0);
    fo4ide::papyrus::setSnapshotBeforeOpenHook(swapSnapshotSourceForFifo);
    CHECK_THROWS(fo4ide::papyrus::snapshotPapyrusSources(fd, 100, 128, 256));
    fo4ide::papyrus::setSnapshotBeforeOpenHook(nullptr);
    ::close(fd);
    std::error_code ec; std::filesystem::remove_all(root, ec);
}

// Detects: an attacker-controlled bind mount beneath an import root must not be traversed.
TEST_CASE("Secure Papyrus source snapshots refuse nested bind mounts") {
    const auto root = tempRoot();
    std::filesystem::create_directories(root / "tree" / "mounted");
    std::filesystem::create_directories(root / "outside");
    { std::ofstream out(root / "outside" / "Quest.psc"); out << "Scriptname Quest\n"; }
    const auto child = ::fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        if (::unshare(CLONE_NEWNS) != 0 || ::mount(nullptr, "/", nullptr, MS_REC | MS_PRIVATE, nullptr) != 0 ||
            ::mount((root / "outside").c_str(), (root / "tree" / "mounted").c_str(), nullptr, MS_BIND, nullptr) != 0) _exit(77);
        const int fd = ::open((root / "tree").c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
        if (fd < 0) _exit(2);
        try { (void)fo4ide::papyrus::snapshotPapyrusSources(fd, 100, 128, 256); }
        catch (...) { ::close(fd); _exit(0); }
        ::close(fd); _exit(1);
    }
    int status = 0; REQUIRE(::waitpid(child, &status, 0) == child);
    std::error_code ec; std::filesystem::remove_all(root, ec);
    REQUIRE(WIFEXITED(status));
    if (WEXITSTATUS(status) == 77) SKIP("mount namespace or bind mount is unavailable");
    CHECK(WEXITSTATUS(status) == 0);
}
#endif
#endif

// Detects: an oversized import tree must stop before partially indexing scripts.
TEST_CASE("Papyrus import indexing refuses oversized trees without retaining partial results") {
    fo4ide::papyrus::ScriptIndex index;
    CHECK_THROWS(index.addRoot(fo4::graph::test::baseStubs(), 1));
    CHECK(index.count() == 0u);
    CHECK(index.roots().empty());
    index.addRoot(fo4::graph::test::baseStubs(), 10000);
    CHECK(index.count() > 1u);
}

// Detects: oversized Papyrus imports must be refused before their contents are parsed.
TEST_CASE("Papyrus import indexing enforces source byte limits") {
    const auto root = tempRoot();
    std::filesystem::create_directories(root);
    const auto path = root / "Large.psc";
    { std::ofstream out(path, std::ios::binary); out << std::string(1024, 'x'); }
    fo4ide::papyrus::ScriptIndex index;
    index.addRoot(root, 100);
    index.setReadLimits(64, 128);
    CHECK(index.resolve("Large") == nullptr);
    { std::ofstream out(root / "Small1.psc", std::ios::binary); out << "Scriptname Small1\n"; }
    { std::ofstream out(root / "Small2.psc", std::ios::binary); out << "Scriptname Small2\n"; }
    fo4ide::papyrus::ScriptIndex aggregate;
    aggregate.addRoot(root, 100);
    aggregate.setReadLimits(64, 25);
    REQUIRE(aggregate.resolve("Small1") != nullptr);
    CHECK(aggregate.resolve("Small2") == nullptr);
    CHECK_THROWS(aggregate.setReadLimits(1, 1));
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

// Detects: editing one generated quest fragment must keep the other stage functions intact.
TEST_CASE("Quest fragment source updates one stage without dropping other stages") {
    using namespace fo4ide::papyrus;
    const std::vector<QuestFragmentAction> actions{{QuestFragmentActionKind::Display, 0},
                                                    {QuestFragmentActionKind::Complete, 1},
                                                    {QuestFragmentActionKind::SetStage, 20}};
    const auto first = updateQuestFragmentSource({}, "QF_Test", 10, 0, actions);
    REQUIRE(first.has_value());
    CHECK_FALSE(Parser::parse(*first).hasErrors());
    CHECK(first->find("SetObjectiveDisplayed(0)") != std::string::npos);
    CHECK(first->find("SetObjectiveCompleted(1)") != std::string::npos);
    CHECK(first->find("SetStage(20)") != std::string::npos);
    const auto second = updateQuestFragmentSource(*first, "QF_Test", 20, 0, actions);
    REQUIRE(second.has_value());
    const auto changed = updateQuestFragmentSource(*second, "QF_Test", 10, 0,
                                                   {{QuestFragmentActionKind::SetStage, 30}});
    REQUIRE(changed.has_value());
    CHECK(changed->find("Fragment_Stage_0020_Item_00") != std::string::npos);
    CHECK(changed->find("SetStage(20)") != std::string::npos);
    CHECK(changed->find("SetStage(30)") != std::string::npos);
}

// Detects: a third-party or malformed script must not be edited as managed fragment source.
TEST_CASE("Quest fragment source refuses unowned or ambiguous scripts") {
    using namespace fo4ide::papyrus;
    const std::vector<QuestFragmentAction> actions{{QuestFragmentActionKind::SetStage, 20}};
    CHECK_FALSE(updateQuestFragmentSource("Scriptname QF_Test Extends Quest\n", "QF_Test", 10, 0, actions));
    CHECK_FALSE(updateQuestFragmentSource({}, "QF_Test\nFunction Evil()", 10, 0, actions));
    CHECK_FALSE(updateQuestFragmentSource({}, "Function", 10, 0, actions));
    const auto source = updateQuestFragmentSource({}, "QF_Test", 10, 0, actions);
    REQUIRE(source.has_value());
    CHECK_FALSE(updateQuestFragmentSource(*source, "QF_Other", 10, 0, actions));
    CHECK_FALSE(updateQuestFragmentSource(*source + "Function Broken(\n", "QF_Test", 10, 0, actions));
    const auto typed = std::string("; GardenTools managed quest fragments\n") +
        "Scriptname QF_Test Extends Quest Hidden Const\nInt Function Fragment_Stage_0010_Item_00()\n"
        "Return 7\nEndFunction\n";
    CHECK_FALSE(updateQuestFragmentSource(typed, "QF_Test", 10, 0, actions));
}

TEST_CASE("Quest fragment source emits objective flags only when given") {
    using namespace fo4ide::papyrus;
    const auto plain = updateQuestFragmentSource({}, "QF_Flags", 10, 0,
        {{QuestFragmentActionKind::Display, 3}, {QuestFragmentActionKind::Complete, 4}});
    REQUIRE(plain.has_value());
    CHECK(plain->find("SetObjectiveDisplayed(3)\n") != std::string::npos);
    CHECK(plain->find("SetObjectiveCompleted(4)\n") != std::string::npos);
    QuestFragmentAction hide{QuestFragmentActionKind::Display, 5};
    hide.visible = false;
    const auto hidden = updateQuestFragmentSource({}, "QF_Flags", 10, 0, {hide});
    REQUIRE(hidden.has_value());
    CHECK(hidden->find("SetObjectiveDisplayed(5, false, false)\n") != std::string::npos);
    QuestFragmentAction force{QuestFragmentActionKind::Display, 6};
    force.force = true;
    const auto forced = updateQuestFragmentSource({}, "QF_Flags", 10, 0, {force});
    REQUIRE(forced.has_value());
    CHECK(forced->find("SetObjectiveDisplayed(6, true, true)\n") != std::string::npos);
    QuestFragmentAction uncomplete{QuestFragmentActionKind::Complete, 7};
    uncomplete.completed = false;
    const auto reopened = updateQuestFragmentSource({}, "QF_Flags", 10, 0, {uncomplete});
    REQUIRE(reopened.has_value());
    CHECK(reopened->find("SetObjectiveCompleted(7, false)\n") != std::string::npos);
    QuestFragmentAction done{QuestFragmentActionKind::Complete, 8};
    done.completed = true;
    const auto completed = updateQuestFragmentSource({}, "QF_Flags", 10, 0, {done});
    REQUIRE(completed.has_value());
    CHECK(completed->find("SetObjectiveCompleted(8, true)\n") != std::string::npos);
}

TEST_CASE("Quest fragment source refuses objective flags on the wrong action kind") {
    using namespace fo4ide::papyrus;
    QuestFragmentAction displayCompleted{QuestFragmentActionKind::Display, 1};
    displayCompleted.completed = true;
    CHECK_FALSE(updateQuestFragmentSource({}, "QF_Flags", 10, 0, {displayCompleted}).has_value());
    QuestFragmentAction completeVisible{QuestFragmentActionKind::Complete, 1};
    completeVisible.visible = true;
    CHECK_FALSE(updateQuestFragmentSource({}, "QF_Flags", 10, 0, {completeVisible}).has_value());
    QuestFragmentAction completeForce{QuestFragmentActionKind::Complete, 1};
    completeForce.force = false;
    CHECK_FALSE(updateQuestFragmentSource({}, "QF_Flags", 10, 0, {completeForce}).has_value());
    QuestFragmentAction stageFlag{QuestFragmentActionKind::SetStage, 20};
    stageFlag.completed = true;
    CHECK_FALSE(updateQuestFragmentSource({}, "QF_Flags", 10, 0, {stageFlag}).has_value());
}

TEST_CASE("Quest fragment source removes one managed function and refuses unmanaged source") {
    using namespace fo4ide::papyrus;
    const std::vector<QuestFragmentAction> actions{{QuestFragmentActionKind::SetStage, 20}};
    const auto first = updateQuestFragmentSource({}, "QF_Test", 10, 0, actions);
    REQUIRE(first.has_value());
    const auto both = updateQuestFragmentSource(*first, "QF_Test", 20, 0, actions);
    REQUIRE(both.has_value());
    REQUIRE(isManagedQuestFragmentSource(*both));
    const auto removed = removeQuestFragmentSource(*both, "QF_Test", 10, 0);
    REQUIRE(removed.has_value());
    CHECK(removed->find("Fragment_Stage_0010_Item_00") == std::string::npos);
    CHECK(removed->find("SetStage(20)") != std::string::npos);
    CHECK_FALSE(Parser::parse(*removed).hasErrors());
    CHECK_FALSE(removeQuestFragmentSource(*removed, "QF_Test", 10, 0).has_value());
    CHECK_FALSE(removeQuestFragmentSource("Scriptname QF_Test Extends Quest\n", "QF_Test", 10, 0).has_value());
    CHECK_FALSE(isManagedQuestFragmentSource("Scriptname QF_Test Extends Quest\n"));
}

namespace {

struct PapyrusTargetFixture {
    fo4ide::settings::SharedSettings shared;
    fo4ide::tools::Locator locator;
    fo4ide::app::PapyrusTarget target;

    PapyrusTargetFixture(fo4ide::process::Runner& runner,
                         fo4ide::platform::Services& platform,
                         const fo4ide::settings::Settings& settings,
                         const std::filesystem::path& executablePath)
        : shared(settings), locator(shared, executablePath), target(runner, locator, platform) {}
};

std::filesystem::path buildPex(const std::filesystem::path& sourceFile,
                               const std::filesystem::path& outDir) {
    const auto out = outDir.string();
    const auto result = fo4ide::papyrus::AnalysisService::compile(sourceFile.string(), &out);
    REQUIRE(result.find("RESULT: 1 succeeded") != std::string::npos);
    return outDir / "Greeter.pex";
}

std::string readBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

}

TEST_CASE("PapyrusTarget Compile uses the built-in engine with the Auto note") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto script = root / "Greeter.psc";
    touch(script, "Scriptname Greeter\nFunction Ping()\nEndFunction\n");

    FakeRunner runner;
    FakePlatform platform;
    fo4ide::settings::Settings settings;
    settings.fallout4Path = root;
    PapyrusTargetFixture fixture(runner, platform, settings, root / "host");
    if (!fixture.locator.resolve(fo4ide::tools::Tool::PapyrusCompiler).empty()) {
        SKIP("PAPYRUS_COMPILER_PATH or PATH provides PapyrusCompiler.exe");
    }
    auto& target = fixture.target;

    auto result = target.invoke("Compile", nlohmann::json::array({
        script.string(), "", "", "", false, false, false, "", ""}));
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    CHECK(result->get<std::string>().find("RESULT: 1 succeeded") != std::string::npos);
    CHECK(result->get<std::string>().find("No PapyrusCompiler.exe was found") != std::string::npos);
}

TEST_CASE("PapyrusTarget Compile rejects a missing source") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    FakeRunner runner;
    FakePlatform platform;
    fo4ide::settings::Settings settings;
    PapyrusTargetFixture fixture(runner, platform, settings, root / "host");
    auto& target = fixture.target;

    auto result = target.invoke("Compile", nlohmann::json::array({
        (root / "nope.psc").string(), "", "", "", false, false, false, "", ""}));
    REQUIRE(result.has_value());
    CHECK(*result == "Source not found: " + (root / "nope.psc").string());
}

TEST_CASE("PapyrusTarget Compile with engine=creationkit and no exe explains itself") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto script = root / "Greeter.psc";
    touch(script, "Scriptname Greeter\nFunction Ping()\nEndFunction\n");

    FakeRunner runner;
    FakePlatform platform;
    fo4ide::settings::Settings settings;
    settings.fallout4Path = root;
    PapyrusTargetFixture fixture(runner, platform, settings, root / "host");
    if (!fixture.locator.resolve(fo4ide::tools::Tool::PapyrusCompiler).empty()) {
        SKIP("PAPYRUS_COMPILER_PATH or PATH provides PapyrusCompiler.exe");
    }
    auto& target = fixture.target;

    auto result = target.invoke("Compile", nlohmann::json::array({
        script.string(), "", "", "", false, false, false, "", "creationkit"}));
    REQUIRE(result.has_value());
    const auto text = result->get<std::string>();
    CHECK(text.find("PapyrusCompiler.exe not found") != std::string::npos);
    CHECK(text.find("engine='builtin'") != std::string::npos);
}

TEST_CASE("PapyrusTarget Compile runs the Creation Kit process path when exe exists") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto exe = root / "PapyrusCompiler.exe";
    touch(exe);
    const auto script = root / "Greeter.psc";
    touch(script, "Scriptname Greeter\nFunction Ping()\nEndFunction\n");

    FakeRunner runner;
    runner.output.stdOut =
        "Batch compile: 2 succeeded, 1 failed\nCompilation succeeded.\n";
    FakePlatform platform;
    fo4ide::settings::Settings settings;
    PapyrusTargetFixture fixture(runner, platform, settings, root / "host");
    auto& target = fixture.target;

    auto result = target.invoke("Compile", nlohmann::json::array({
        script.string(), "", "Data\\Scripts", "", false, true, false, exe.string(), "creationkit"}));
    REQUIRE(result.has_value());
    const auto text = result->get<std::string>();
    CHECK(text.find("RESULT: 2 succeeded, 1 failed") != std::string::npos);
    CHECK(text.find("engine") == std::string::npos);
    CHECK(runner.last.executable == exe);
    CHECK(runner.last.timeout == std::chrono::seconds(300));
    CHECK(runner.last.workingDirectory == root);
    REQUIRE(runner.last.arguments.size() == 5);
    CHECK(runner.last.arguments[0] == "Greeter.psc");
    CHECK(runner.last.arguments[1] == "-f=\"Institute_Papyrus_Flags.flg\"");
    CHECK(runner.last.arguments[2].find("-i=\"Data\\Scripts") != std::string::npos);
    CHECK(runner.last.arguments[3] == "-o=\"" + root.string() + "\"");
    CHECK(runner.last.arguments[4] == "-op");
}

TEST_CASE("PapyrusTarget Compile refuses a folder whose ScriptName escapes and runs no compiler") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto exe = root / "PapyrusCompiler.exe";
    touch(exe);
    const auto folder = root / "scripts";
    touch(folder / "Evil.psc", "ScriptName A:..:B\nFunction Ping()\nEndFunction\n");

    FakeRunner runner;
    FakePlatform platform;
    fo4ide::settings::Settings settings;
    PapyrusTargetFixture fixture(runner, platform, settings, root / "host");
    auto& target = fixture.target;

    auto result = target.invoke("Compile", nlohmann::json::array({
        folder.string(), "", "", "", false, false, false, exe.string(), "creationkit"}));
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    CHECK(result->get<std::string>().find("Refused to compile") != std::string::npos);
    CHECK(runner.last.executable.empty());
}

TEST_CASE("PapyrusTarget Compile refuses a ScriptName part with a character that is invalid in file names and runs no compiler") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto exe = root / "PapyrusCompiler.exe";
    touch(exe);
    const auto folder = root / "scripts";
    touch(folder / "Star.psc", "ScriptName Bad*:Foo\nFunction Ping()\nEndFunction\n");

    FakeRunner runner;
    FakePlatform platform;
    fo4ide::settings::Settings settings;
    PapyrusTargetFixture fixture(runner, platform, settings, root / "host");
    auto& target = fixture.target;

    auto result = target.invoke("Compile", nlohmann::json::array({
        folder.string(), "", "", "", false, false, false, exe.string(), "creationkit"}));
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    CHECK(result->get<std::string>().find("Refused to compile") != std::string::npos);
    CHECK(runner.last.executable.empty());
}

TEST_CASE("PapyrusTarget Decompile shows the NOT SAVED trailer without write") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto script = root / "Greeter.psc";
    touch(script, "Scriptname Greeter\nFunction Ping()\nEndFunction\n");
    const auto outDir = root / "out";
    const auto pex = buildPex(script, outDir);

    FakeRunner runner;
    FakePlatform platform;
    fo4ide::settings::Settings settings;
    PapyrusTargetFixture fixture(runner, platform, settings, root / "host");
    auto& target = fixture.target;

    auto result = target.invoke("Decompile", nlohmann::json::array({pex.string(), "", false, false}));
    REQUIRE(result.has_value());
    const auto text = result->get<std::string>();
    CHECK(text.find("ScriptName Greeter") != std::string::npos);
    CHECK(text.find("NOT SAVED") != std::string::npos);

    const auto bytes = readBytes(pex);
    const auto file = fo4::pex::PexFile::fromBytes(
        std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
    for (const bool assembly : {false, true}) {
        auto shown = target.invoke(
            "Decompile", nlohmann::json::array({pex.string(), "", assembly, false}));
        REQUIRE(shown.has_value());
        const auto expected = assembly ? fo4::papyrus::Decompiler::disassemble(file)
                                       : fo4::papyrus::Decompiler::toSource(file);
        CHECK(shown->get<std::string>().rfind(expected, 0) == 0);
    }
}

TEST_CASE("PapyrusTarget Decompile reports a .pex it cannot read as a failure, not a throw") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto junk = root / "x.pex";
    touch(junk, "junk");

    FakeRunner runner;
    FakePlatform platform;
    fo4ide::settings::Settings settings;
    PapyrusTargetFixture fixture(runner, platform, settings, root / "host");
    auto& target = fixture.target;

    auto result =
        target.invoke("Decompile", nlohmann::json::array({junk.string(), "", false, false}));
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    CHECK(result->get<std::string>().rfind("Decompile failed for x.pex", 0) == 0);
}

TEST_CASE("PapyrusTarget Decompile writes a namespaced .psc when asked") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto script = root / "Greeter.psc";
    touch(script, "Scriptname Greeter\nFunction Ping()\nEndFunction\n");
    const auto outDir = root / "out";
    const auto pex = buildPex(script, outDir);

    FakeRunner runner;
    FakePlatform platform;
    fo4ide::settings::Settings settings;
    PapyrusTargetFixture fixture(runner, platform, settings, root / "host");
    auto& target = fixture.target;

    auto result = target.invoke("Decompile", nlohmann::json::array({pex.string(), "", false, true}));
    REQUIRE(result.has_value());
    const auto text = result->get<std::string>();
    CHECK(text.find("SAVED ->") != std::string::npos);
    CHECK(text.find("Greeter.psc") != std::string::npos);
    REQUIRE(std::filesystem::is_regular_file(outDir / "Greeter.psc"));
    std::ifstream stream(outDir / "Greeter.psc");
    const std::string contents((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    CHECK(contents.find("ScriptName Greeter") != std::string::npos);
}

TEST_CASE("PapyrusTarget Decompile handles a folder of pex files") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto script = root / "Greeter.psc";
    touch(script, "Scriptname Greeter\nFunction Ping()\nEndFunction\n");
    const auto outDir = root / "out";
    const auto pex = buildPex(script, outDir);
    const auto folderOut = root / "decompiled";

    FakeRunner runner;
    FakePlatform platform;
    fo4ide::settings::Settings settings;
    PapyrusTargetFixture fixture(runner, platform, settings, root / "host");
    auto& target = fixture.target;

    auto result = target.invoke("Decompile",
                                nlohmann::json::array({outDir.string(), folderOut.string(), false, true}));
    REQUIRE(result.has_value());
    const auto text = result->get<std::string>();
    CHECK(text.find("RESULT: 1/1 decompiled -> .psc") != std::string::npos);
    CHECK(text.find("OUTPUT: " + folderOut.string()) != std::string::npos);
    REQUIRE(std::filesystem::is_regular_file(folderOut / "Greeter.psc"));
}

TEST_CASE("PapyrusTarget Analyze returns the analyze JSON contract") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    FakeRunner runner;
    FakePlatform platform;
    fo4ide::settings::Settings settings;
    PapyrusTargetFixture fixture(runner, platform, settings, root / "host");
    auto& target = fixture.target;

    auto result = target.invoke("Analyze", nlohmann::json::array({
        "Scriptname Greeter\nFunction Ping()\nEndFunction\n", ""}));
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    const auto json = nlohmann::json::parse(result->get<std::string>());
    CHECK(json.at("script") == "Greeter");
}

TEST_CASE("PapyrusTarget Check reports clean and broken scripts") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto good = root / "Good.psc";
    touch(good, "Scriptname Good\nFunction Ping()\nEndFunction\n");
    const auto bad = root / "Bad.psc";
    touch(bad, "Scriptname Bad\nFunction Broken(\nEndFunction\n");

    FakeRunner runner;
    FakePlatform platform;
    fo4ide::settings::Settings settings;
    PapyrusTargetFixture fixture(runner, platform, settings, root / "host");
    auto& target = fixture.target;

    auto clean = target.invoke("Check", nlohmann::json::array({good.string(), true, ""}));
    REQUIRE(clean.has_value());
    CHECK(clean->get<std::string>().find("RESULT: 1 clean") != std::string::npos);

    auto broken = target.invoke("Check", nlohmann::json::array({bad.string(), true, ""}));
    REQUIRE(broken.has_value());
    const auto brokenText = broken->get<std::string>();
    CHECK(brokenText.find("syntax errors") != std::string::npos);
    CHECK(brokenText.find("Bad.psc") != std::string::npos);

    auto syntaxOnly = target.invoke("Check", nlohmann::json::array({bad.string(), false, ""}));
    REQUIRE(syntaxOnly.has_value());
    CHECK(syntaxOnly->get<std::string>().find("Syntax only") != std::string::npos);
}

TEST_CASE("PapyrusTarget Check resolves Extends from Settings base imports without the env var") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto script = root / "Greeter.psc";
    touch(script, "Scriptname Greeter extends ObjectReference\n");

    FakeRunner runner;
    FakePlatform platform;
    fo4ide::settings::Settings settings;
    settings.fallout4Path = root / "Fallout 4";
    settings.papyrusBaseImports = fo4::graph::test::baseStubs();
    PapyrusTargetFixture fixture(runner, platform, settings, root / "host");
    auto& target = fixture.target;

    auto result = target.invoke("Check", nlohmann::json::array({script.string(), true, ""}));
    REQUIRE(result.has_value());
    CHECK(result->get<std::string>().find("not on the roots") == std::string::npos);
}

TEST_CASE("PapyrusTarget SymbolAt returns the symbol JSON contract") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    FakeRunner runner;
    FakePlatform platform;
    fo4ide::settings::Settings settings;
    PapyrusTargetFixture fixture(runner, platform, settings, root / "host");
    auto& target = fixture.target;

    const std::string text = "Scriptname Greeter\nFunction Ping()\nEndFunction\n";
    const auto offset = static_cast<int>(text.find("Ping"));
    auto result = target.invoke("SymbolAt",
                                nlohmann::json::array({text, "", offset, ""}));
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    const auto json = nlohmann::json::parse(result->get<std::string>());
    CHECK(json.contains("resolved"));
    CHECK(json.at("name") == "Ping");
}

TEST_CASE("PapyrusTarget Complete, SignatureHelp and CheckText answer an unsaved buffer from base roots") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto base = root / "base";
    touch(base / "Parent.psc",
          "ScriptName Parent\nInt Property Health Auto\nFunction DoStuff()\nEndFunction\n"
          "Function Pick(Int first, Int second, Int third)\nEndFunction\n");
    FakeRunner runner;
    FakePlatform platform;
    fo4ide::settings::Settings settings;
    PapyrusTargetFixture fixture(runner, platform, settings, root / "host");
    auto& target = fixture.target;
    const auto roots = fixture.locator.papyrusBaseImports();
    const auto imports = base.string();
    const auto hasLabel = [](const nlohmann::json& reply, const std::string& label) {
        for (const auto& item : reply.at("items")) {
            if (item.at("label").get<std::string>() == label) return true;
        }
        return false;
    };
    const auto hasCode = [](const nlohmann::json& reply, const std::string& code) {
        for (const auto& diagnostic : reply.at("diagnostics")) {
            if (diagnostic.at("code").get<std::string>() == code) return true;
        }
        return false;
    };

    const std::string completeText =
        "ScriptName Child extends Parent\nParent Property Link Auto\nFunction F()\n  Link.\nEndFunction\n";
    const auto completeOffset = static_cast<int>(completeText.find("Link.") + 5);
    auto complete = target.invoke("Complete",
                                  nlohmann::json::array({completeText, "", completeOffset, imports}));
    REQUIRE(complete.has_value());
    REQUIRE(complete->is_string());
    CHECK(complete->get<std::string>() ==
          fo4::papyrus::AnalysisService::completionJson(completeText, nullptr, completeOffset,
                                                        &imports, roots));
    const auto completed = nlohmann::json::parse(complete->get<std::string>());
    CHECK(hasLabel(completed, "DoStuff"));
    CHECK(hasLabel(completed, "Health"));

    const std::string signatureText =
        "ScriptName Child extends Parent\nFunction F()\n  Pick(1, 2, \nEndFunction\n";
    const auto signatureOffset = static_cast<int>(signatureText.find("Pick(1, 2, ") + 11);
    auto signature = target.invoke("SignatureHelp",
                                   nlohmann::json::array({signatureText, "", signatureOffset, imports}));
    REQUIRE(signature.has_value());
    REQUIRE(signature->is_string());
    CHECK(signature->get<std::string>() ==
          fo4::papyrus::AnalysisService::signatureHelpJson(signatureText, nullptr, signatureOffset,
                                                           &imports, roots));
    const auto help = nlohmann::json::parse(signature->get<std::string>());
    CHECK(help.at("active").get<bool>());
    CHECK(help.at("activeParameter").get<int>() == 2);

    const std::string checkBuffer =
        "ScriptName Child extends Parent\nFunction F()\n    int n = nope\nEndFunction\n";
    auto check = target.invoke("CheckText", nlohmann::json::array({checkBuffer, "", 7, imports}));
    REQUIRE(check.has_value());
    REQUIRE(check->is_string());
    CHECK(check->get<std::string>() ==
          fo4::papyrus::AnalysisService::checkTextJson(checkBuffer, nullptr, 7, &imports, roots));
    const auto checked = nlohmann::json::parse(check->get<std::string>());
    CHECK(checked.at("revision").get<int>() == 7);
    CHECK(checked.at("complete").get<bool>());
    CHECK(hasCode(checked, "PAP0030"));
}

TEST_CASE("PapyrusTarget References, Rename and WorkspaceSymbols answer a script tree and a rename writes no file") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto scripts = root / "scripts";
    touch(scripts / "Helper.psc", "ScriptName Helper\nFunction Help()\nEndFunction\n");
    touch(scripts / "User.psc", "ScriptName User\nFunction F()\n  Helper h = None\nEndFunction\n");
    FakeRunner runner;
    FakePlatform platform;
    fo4ide::settings::Settings settings;
    PapyrusTargetFixture fixture(runner, platform, settings, root / "host");
    auto& target = fixture.target;
    const auto imports = scripts.string();
    const auto helperPath = (scripts / "Helper.psc").string();
    const std::string helperText = "ScriptName Helper\nFunction Help()\nEndFunction\n";
    const auto helperOffset = static_cast<int>(helperText.find("Helper") + 1);
    const auto userBefore = readBytes(scripts / "User.psc");
    const auto entriesBefore = std::distance(std::filesystem::directory_iterator(scripts),
                                             std::filesystem::directory_iterator{});

    auto references = target.invoke("References",
                                    nlohmann::json::array({helperText, helperPath, helperOffset, imports}));
    REQUIRE(references.has_value());
    REQUIRE(references->is_string());
    const auto found = nlohmann::json::parse(references->get<std::string>());
    CHECK(found.at("declaration").at("kind").get<std::string>() == "Script");
    bool inUser = false;
    for (const auto& hit : found.at("references")) {
        if (hit.at("file").is_string() && hit.at("file").get<std::string>().ends_with("User.psc")) inUser = true;
    }
    CHECK(inUser);

    auto renamed = target.invoke("Rename",
                                 nlohmann::json::array({helperText, helperPath, helperOffset, "Helper2", imports}));
    REQUIRE(renamed.has_value());
    REQUIRE(renamed->is_string());
    const auto renamePlan = nlohmann::json::parse(renamed->get<std::string>());
    CHECK(renamePlan.at("ok").get<bool>());
    CHECK(renamePlan.at("fileRename").at("to").get<std::string>().ends_with("Helper2.psc"));
    CHECK(std::filesystem::is_regular_file(scripts / "Helper.psc"));
    CHECK(!std::filesystem::exists(scripts / "Helper2.psc"));
    CHECK(readBytes(scripts / "User.psc") == userBefore);
    CHECK(std::distance(std::filesystem::directory_iterator(scripts), std::filesystem::directory_iterator{}) ==
          entriesBefore);

    const std::string keywordRename = "Int";
    auto refused = target.invoke("Rename",
                                 nlohmann::json::array({helperText, helperPath, helperOffset, keywordRename, imports}));
    REQUIRE(refused.has_value());
    CHECK(nlohmann::json::parse(refused->get<std::string>()).at("ok") == false);

    const std::string localText = "ScriptName Counter\nFunction F()\n  Int n = 1\n  Int m = n\nEndFunction\n";
    const auto localOffset = static_cast<int>(localText.find("Int n = 1") + 4);
    auto localRefs = target.invoke("References", nlohmann::json::array({localText, "", localOffset, ""}));
    REQUIRE(localRefs.has_value());
    REQUIRE(localRefs->is_string());
    CHECK(nlohmann::json::parse(localRefs->get<std::string>()).at("references").size() == 2);
    auto localRename = target.invoke("Rename", nlohmann::json::array({localText, "", localOffset, "total", ""}));
    REQUIRE(localRename.has_value());
    REQUIRE(localRename->is_string());
    const auto edited = nlohmann::json::parse(localRename->get<std::string>());
    CHECK(edited.at("edits").size() == 2);
    CHECK(edited.at("fileRename").is_null());

    auto symbols = target.invoke("WorkspaceSymbols", nlohmann::json::array({"help", 0, imports}));
    REQUIRE(symbols.has_value());
    REQUIRE(symbols->is_string());
    const auto listing = nlohmann::json::parse(symbols->get<std::string>());
    bool helpListed = false;
    for (const auto& symbol : listing.at("results")) {
        if (symbol.at("name").get<std::string>() == "Help" && symbol.at("file").is_string() &&
            symbol.at("file").get<std::string>().ends_with("Helper.psc")) {
            helpListed = true;
        }
    }
    CHECK(helpListed);
}

TEST_CASE("PapyrusTarget ReadScript and WriteScript round-trip with guards") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    FakeRunner runner;
    FakePlatform platform;
    fo4ide::settings::Settings settings;
    PapyrusTargetFixture fixture(runner, platform, settings, root / "host");
    auto& target = fixture.target;

    auto missing = target.invoke("ReadScript", nlohmann::json::array({(root / "nope.psc").string()}));
    REQUIRE(missing.has_value());
    CHECK(missing->get<std::string>().find("ERR:Not found:") == 0);

    auto bad = target.invoke("WriteScript", nlohmann::json::array({(root / "x.txt").string(), "x"}));
    REQUIRE(bad.has_value());
    CHECK(bad->get<std::string>().find("ERR:Refusing to write") == 0);

    const auto written = root / "sub" / "Greeter.psc";
    auto ok = target.invoke("WriteScript", nlohmann::json::array({written.string(), "Scriptname Greeter\n"}));
    REQUIRE(ok.has_value());
    CHECK(ok->get<std::string>().empty());
    REQUIRE(std::filesystem::is_regular_file(written));

    auto read = target.invoke("ReadScript", nlohmann::json::array({written.string()}));
    REQUIRE(read.has_value());
    CHECK(read->get<std::string>() == "Scriptname Greeter\n");
}

TEST_CASE("PapyrusTarget OpenFolder routes through platform services") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    std::filesystem::create_directories(root / "scripts");
    FakeRunner runner;
    FakePlatform platform;
    fo4ide::settings::Settings settings;
    PapyrusTargetFixture fixture(runner, platform, settings, root / "host");
    auto& target = fixture.target;

    auto result = target.invoke("OpenFolder", nlohmann::json::array({(root / "scripts").string()}));
    REQUIRE(result.has_value());
    CHECK(result->get<std::string>().empty());
    CHECK(platform.openedFolder == root / "scripts");

    auto missing = target.invoke("OpenFolder", nlohmann::json::array({(root / "void").string()}));
    REQUIRE(missing.has_value());
    CHECK(missing->get<std::string>() == "Folder not found: " + (root / "void").string());
}

TEST_CASE("PapyrusTarget wiki lookups report no mirror until configured") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    FakeRunner runner;
    FakePlatform platform;
    fo4ide::settings::Settings settings;
    PapyrusTargetFixture fixture(runner, platform, settings, root / "host");
    auto& target = fixture.target;

    auto result = target.invoke("LookupFunction", nlohmann::json::array({"Form", "GetBaseObject"}));
    REQUIRE(result.has_value());
    CHECK(result->get<std::string>().find("No CK wiki mirror configured") != std::string::npos);
}

TEST_CASE("PapyrusTarget wiki lookups resolve the configured mirror") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto wiki = root / "ckwiki" / "fallout4";
    touch(wiki / "GetBaseObject_-_Form.html",
          "<html><h1>GetBaseObject - Form</h1>"
          "<h2><span id=\"Syntax\">Syntax</span></h2><p><b>Form</b> Function GetBaseObject()</p>"
          "<div id=\"catlinks\"></div></html>");

    FakeRunner runner;
    FakePlatform platform;
    fo4ide::settings::Settings settings;
    settings.ckWikiPath = wiki;
    PapyrusTargetFixture fixture(runner, platform, settings, root / "host");
    auto& target = fixture.target;

    auto result = target.invoke("LookupFunction",
                                nlohmann::json::array({"Form", "GetBaseObject"}));
    REQUIRE(result.has_value());
    const auto text = result->get<std::string>();
    CHECK(text.find("GetBaseObject - Form") != std::string::npos);
    CHECK(text.find("Syntax: Form Function GetBaseObject()") != std::string::npos);
}

TEST_CASE("PapyrusTarget StageDroppedFile mirrors the NIF staging contract") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    FakeRunner runner;
    FakePlatform platform;
    fo4ide::settings::Settings settings;
    PapyrusTargetFixture fixture(runner, platform, settings, root / "host");
    auto& target = fixture.target;

    auto result = target.invoke("StageDroppedFile", nlohmann::json::array({"script.psc", "U2NyaXB0bmFtZSBYLg=="}));
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    const auto path = std::filesystem::path(result->get<std::string>());
    REQUIRE(std::filesystem::is_regular_file(path));
    std::ifstream stream(path, std::ios::binary);
    const std::string contents((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    CHECK(contents == "Scriptname X.");
    std::error_code ec;
    std::filesystem::remove_all(path.parent_path(), ec);
}

TEST_CASE("PapyrusTarget analysis RPCs match the PET oracle on real sources") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    FakeRunner runner;
    FakePlatform platform;
    fo4ide::settings::Settings settings;
    settings.papyrusBaseImports = fo4::graph::test::baseStubs();
    PapyrusTargetFixture fixture(runner, platform, settings, root / "host");
    auto& target = fixture.target;
    const auto imports = fixture.locator.papyrusBaseImports();

    const auto event = fo4::graph::test::graphScripts() / "FixtureEventSource.psc";
    const auto actor = fo4::graph::test::baseStubs() / "Actor.psc";
    const auto bad = root / "Bad.psc";
    touch(bad, "Scriptname Bad\nFunction Broken(\nEndFunction\n");

    SECTION("Check") {
        for (const auto& file : {event, actor, bad}) {
            const auto path = file.string();
            const auto oracle =
                fo4ide::papyrus::AnalysisService::check(path, true, true, nullptr, imports);
            auto result = target.invoke("Check", nlohmann::json::array({path, true, ""}));
            REQUIRE(result.has_value());
            CHECK(result->get<std::string>() == oracle);
        }
    }

    SECTION("Analyze") {
        for (const auto& file : {event, actor, bad}) {
            const auto path = file.string();
            const auto text = readBytes(file);
            const auto oracle = fo4ide::papyrus::AnalysisService::analyzeJson(text, &path);
            auto result = target.invoke("Analyze", nlohmann::json::array({text, path}));
            REQUIRE(result.has_value());
            CHECK(result->get<std::string>() == oracle);
        }
    }

    SECTION("SymbolAt") {
        const auto path = event.string();
        const auto text = readBytes(event);
        const auto offset = static_cast<int>(text.find("RaiseIt"));
        const auto oracle =
            fo4ide::papyrus::AnalysisService::symbolAtJson(text, &path, offset, nullptr, imports);
        auto result = target.invoke("SymbolAt", nlohmann::json::array({text, path, offset, ""}));
        REQUIRE(result.has_value());
        CHECK(result->get<std::string>() == oracle);
    }

    SECTION("Compile") {
        const auto path = event.string();
        const auto out = (root / "compiled").string();
        const auto pex = std::filesystem::path(out) / "FixtureEventSource.pex";
        const auto oracle =
            fo4ide::papyrus::AnalysisService::compile(path, &out, nullptr, false, true, nullptr, imports);
        REQUIRE(oracle.find("RESULT: 1 succeeded") != std::string::npos);
        const auto oracleBytes = readBytes(pex);
        REQUIRE(!oracleBytes.empty());
        std::filesystem::remove(pex);

        auto result = target.invoke(
            "Compile", nlohmann::json::array({path, out, "", "", false, false, false, "", "builtin"}));
        REQUIRE(result.has_value());
        CHECK(result->get<std::string>() == oracle);
        CHECK(readBytes(pex) == oracleBytes);
    }
}
