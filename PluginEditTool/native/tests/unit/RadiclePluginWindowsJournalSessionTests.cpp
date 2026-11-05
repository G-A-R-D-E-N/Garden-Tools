#include <catch2/matchers/catch_matchers_string.hpp>
#include "RadiclePluginWindowsJournalTestSupport.hpp"
#include "../../src/plugin/RadiclePluginJournalSession.hpp"

#if defined(_WIN32) && defined(FO4IDE_TEST_HOOKS)
namespace {
using SessionInventory = std::vector<std::tuple<std::string, WindowsJournalFileIdentity, std::vector<std::byte>, std::wstring, DWORD, std::int64_t>>;
SessionInventory sessionInventory(const std::filesystem::path& root) {
    SessionInventory result;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        const bool directory = entry.is_directory();
        result.emplace_back(entry.path().lexically_relative(root).generic_string(), splitIdentity(entry.path(), directory),
            directory ? std::vector<std::byte>{} : readSplitFile(entry.path()), splitSecurity(entry.path()),
            ::GetFileAttributesW(entry.path().c_str()), splitCreation(entry.path(), directory));
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return std::get<0>(a) < std::get<0>(b); });
    return result;
}
JournalRoots sessionRoots(const std::filesystem::path& root) {
    JournalRoots roots;
    roots.plugin = root / "data" / "Patch.esp";
    roots.pluginDirectoryIdentity = JournalDestination::captureRoot(root / "data");
    roots.stringsDirectory = root / "data";
    roots.stringsDirectoryIdentity = roots.pluginDirectoryIdentity;
    roots.approvedRoots.push_back({root / "data", *roots.pluginDirectoryIdentity});
    return roots;
}
std::vector<JournalInput> sessionInputs() {
    const auto make = [](std::string kind, std::string name, std::string_view old, std::string_view final) {
        const auto before = splitBytes(old), after = splitBytes(final);
        const std::vector<std::byte> original(before.begin(), before.end()), replacement(after.begin(), after.end());
        return JournalInput{std::move(kind), std::move(name), !old.empty(),
            old.empty() ? "" : fo4::io::sha256_hex(original), original.size(), replacement};
    };
    return {make("plugin", "", "old-plugin", "new-plugin"), make("strings", "en", "old-table", "new-table"),
        make("dlstrings", "en", "", "new-dialogue")};
}
void prepareSessionFiles(const std::filesystem::path& root) {
    makeSplitData(root / "data");
    writeSplitFile(root / "data" / "Patch.esp", "old-plugin");
    writeSplitFile(root / "data" / "Patch_en.STRINGS", "old-table");
    writeSplitFile(root / "data" / "expected-inheritance", "ordinary");
}
std::vector<JournalSourceSet> sessionSources(const std::filesystem::path& root) {
    return {{"strings", root / "data", {}, {"Patch_en.STRINGS"}, true}};
}
bool sessionPreparedReached = false;
void stopSessionPrepared(WindowsJournalSessionStep step, std::size_t, const std::filesystem::path&, const std::filesystem::path&) {
    if (step == WindowsJournalSessionStep::prepared) {
        sessionPreparedReached = true;
        throw std::runtime_error("stop before any destination publication");
    }
}
std::filesystem::path sessionWorkerRoot;
void crashSessionStage(WindowsJournalSessionStep step, std::size_t index, const std::filesystem::path& stage, const std::filesystem::path& receipt) {
    const int phase = step == WindowsJournalSessionStep::stageCreated ? 10 :
        step == WindowsJournalSessionStep::stageReceipted ? 11 : step == WindowsJournalSessionStep::stageExposed ? 12 :
        step == WindowsJournalSessionStep::completionPublished ? 14 : step == WindowsJournalSessionStep::backupRemoved ? 15 :
        step == WindowsJournalSessionStep::stagingDirectoryRemoved ? 16 : step == WindowsJournalSessionStep::manifestRemoved ? 17 :
        step == WindowsJournalSessionStep::privateRecordRemoved ? 18 : -2;
    if (phase == splitCrashPhase) {
        writeSplitFile(sessionWorkerRoot / "session-phase", nlohmann::json{{"path", stage.string()}, {"receipt", receipt.string()}, {"index", index}}.dump());
        ::TerminateProcess(::GetCurrentProcess(), 77);
    }
}
void crashSessionCommit(std::size_t count) {
    if (count && count == static_cast<std::size_t>(splitCrashPhase)) ::TerminateProcess(::GetCurrentProcess(), 77);
}
std::optional<SessionInventory> sessionInvalidated;
void invalidateSessionPreflight(WindowsJournalSessionStep step, std::size_t, const std::filesystem::path&, const std::filesystem::path&) {
    if (step != WindowsJournalSessionStep::preflightComplete) return;
    writeSplitFile(sessionWorkerRoot / "data" / "Patch_en.DLSTRINGS", "newcomer");
    sessionInvalidated = sessionInventory(sessionWorkerRoot / "data");
}
struct SessionHookReset {
    ~SessionHookReset() { setWindowsJournalSessionHook(nullptr); setJournalCommitHook(nullptr); setWindowsJournalIntentHook(nullptr); }
};
}
#endif

TEST_CASE("Windows JournalSession preflights later conflicts before changing any earlier participant") {
#if defined(_WIN32) && defined(FO4IDE_TEST_HOOKS)
    const auto root = makeSplitRoot(); Cleanup cleanup{root}; prepareSessionFiles(root);
    const auto roots = sessionRoots(root);
    SessionHookReset hooks;
    sessionPreparedReached = false;
    {
        JournalSession session(root / "records");
        setWindowsJournalSessionHook(stopSessionPrepared);
        CHECK_THROWS_AS(session.publish(roots, sessionInputs(), sessionSources(root)), JournalRecoveryRequired);
        setWindowsJournalSessionHook(nullptr);
    }
    REQUIRE(sessionPreparedReached);
    REQUIRE(readSplitFile(root / "data" / "Patch.esp") == std::vector<std::byte>(splitBytes("old-plugin").begin(), splitBytes("old-plugin").end()));
    REQUIRE(readSplitFile(root / "data" / "Patch_en.STRINGS") == std::vector<std::byte>(splitBytes("old-table").begin(), splitBytes("old-table").end()));
    REQUIRE_FALSE(std::filesystem::exists(root / "data" / "Patch_en.DLSTRINGS"));
    std::vector<std::filesystem::path> manifests;
    for (const auto& entry : std::filesystem::directory_iterator(root / "records"))
        if (entry.path().extension() == ".manifest") manifests.push_back(entry.path());
    REQUIRE(manifests.size() == 1);
    const auto manifest = nlohmann::json::parse(readSplitFile(manifests.front()));
    SECTION("later newcomer") { writeSplitFile(root / "data" / "Patch_en.DLSTRINGS", "newcomer"); }
    SECTION("later final bytes without recorded identity") { writeSplitFile(root / "data" / "Patch_en.DLSTRINGS", "new-dialogue"); }
    SECTION("later payload corruption") { writeSplitFile(root / "records" / manifest.at("files").at(2).at("payload").get<std::string>(), "corrupt"); }
    SECTION("later intent corruption") {
        writeSplitFile(root / "records" / ("wr-" + manifest.at("transaction").get<std::string>() + "-2.intent"), "{");
    }
    SECTION("source inventory changed") { writeSplitFile(root / "data" / "Patch_fr.STRINGS", "new source"); }
    SECTION("manifest extension casing is not ignored") {
        std::filesystem::rename(manifests.front(), manifests.front().parent_path() / (manifests.front().stem().string() + ".MANIFEST"));
    }
    const auto before = sessionInventory(root);
    { JournalSession recovered(root / "records"); CHECK_THROWS_AS(recovered.recover({roots}), JournalRecoveryRequired); }
    CHECK(sessionInventory(root) == before);
#endif
}

TEST_CASE("Windows JournalSession creates a missing Strings directory for a pending table and rolls it forward") {
#if defined(_WIN32) && defined(FO4IDE_TEST_HOOKS)
    namespace fs = std::filesystem;
    const auto root = makeSplitRoot(); Cleanup cleanup{root}; makeSplitData(root / "data");
    writeSplitFile(root / "data" / "Patch.esp", "old-plugin");
    fs::create_directory(root / "data" / "reference");
    const auto roots = sessionRoots(root);
    const auto table = splitBytes("new-dialogue");
    JournalInput dialogue{"dlstrings", "en", false, "", 0, std::vector<std::byte>(table.begin(), table.end())};
    dialogue.rootSelector = root / "data"; dialogue.directories = {"Strings"};
    const std::vector<JournalSourceSet> sources{{"strings", root / "data", {"Strings"}, {}, false}};
    SessionHookReset hooks;
    sessionPreparedReached = false;
    {
        JournalSession session(root / "records");
        setWindowsJournalSessionHook(stopSessionPrepared);
        CHECK_THROWS_AS(session.publish(roots, {sessionInputs().front(), dialogue}, sources), JournalRecoveryRequired);
        setWindowsJournalSessionHook(nullptr);
    }
    REQUIRE(sessionPreparedReached);
    REQUIRE(fs::is_directory(root / "data" / "Strings"));
    CHECK(splitSecurity(root / "data" / "Strings") == splitSecurity(root / "data" / "reference"));
    { JournalSession recovered(root / "records"); CHECK_NOTHROW(recovered.recover({roots})); }
    CHECK(readSplitFile(root / "data" / "Strings" / "Patch_en.DLSTRINGS") == std::vector<std::byte>(table.begin(), table.end()));
    const auto newPluginBytes = splitBytes("new-plugin");
    CHECK(readSplitFile(root / "data" / "Patch.esp") == std::vector<std::byte>(newPluginBytes.begin(), newPluginBytes.end()));
#endif
}

TEST_CASE("Windows multi-file recovery worker", "[.windows-session-worker]") {
#if defined(_WIN32) && defined(FO4IDE_TEST_HOOKS)
    const auto root = std::filesystem::path(splitEnvironment(L"FO4IDE_SPLIT_ROOT"));
    splitCrashPhase = std::stoi(splitEnvironment(L"FO4IDE_SPLIT_PHASE"));
    const auto roots = sessionRoots(root);
    sessionWorkerRoot = root;
    JournalSession session(root / "records");
    if (splitCrashPhase == -1) { try { session.recover({roots}); } catch (const std::exception& error) { writeSplitFile(root / "session-error", error.what()); throw; } }
    else {
        setWindowsJournalSessionHook(crashSessionStage); setJournalCommitHook(crashSessionCommit);
        setWindowsJournalIntentHook([](WindowsJournalIntentStep step) { if (splitCrashPhase == 13 && step == WindowsJournalIntentStep::originalBackedUp &&
            !std::filesystem::exists(sessionWorkerRoot / "data" / "Patch.esp")) ::TerminateProcess(::GetCurrentProcess(), 77); });
        try { session.publish(roots, sessionInputs(), sessionSources(root)); }
        catch (const std::exception& error) { writeSplitFile(root / "session-error", error.what()); throw; }
        FAIL("session worker missed its crash boundary");
    }
#endif
}

TEST_CASE("Windows JournalSession recovers staged receipts exposure and plugin-last publication in fresh processes", "[multi-process]") {
#if defined(_WIN32) && defined(FO4IDE_TEST_HOOKS)
    for (const int phase : {10, 11, 12, 13, 14, 15, 16, 17, 18, 1, 2, 3}) { DYNAMIC_SECTION("crash phase " << phase) {
        CAPTURE(phase);
        const auto root = makeSplitRoot(); Cleanup cleanup{root}; prepareSessionFiles(root);
        const auto inherited = splitSecurity(root / "data" / "expected-inheritance");
        const auto initial = sessionInventory(root / "data");
        const auto original = std::find_if(initial.begin(), initial.end(), [](const auto& row) { return std::get<0>(row) == "Patch.esp"; });
        REQUIRE(original != initial.end());
        const auto exit = runSplitWorker(root, true, phase, L"Windows multi-file recovery worker");
        if (exit != 77 && std::filesystem::exists(root / "session-error")) { const auto error = readSplitFile(root / "session-error"); INFO(std::string(reinterpret_cast<const char*>(error.data()), error.size())); REQUIRE(exit == 77); }
        REQUIRE(exit == 77);
        SessionInventory unreceipted;
        WindowsJournalFileIdentity stagedIdentity; std::size_t stagedIndex = 0;
        if (phase >= 10 && phase <= 12) {
            const auto trace = nlohmann::json::parse(readSplitFile(root / "session-phase"));
            const auto stage = std::filesystem::path(trace.at("path").get<std::string>());
            const auto receipt = std::filesystem::path(trace.at("receipt").get<std::string>());
            stagedIndex = trace.at("index").get<std::size_t>(); stagedIdentity = splitIdentity(stage);
            if (phase == 10) {
                REQUIRE_FALSE(std::filesystem::exists(receipt));
                for (const auto& entry : sessionInventory(root / "data"))
                    if (root / "data" / std::get<0>(entry) == stage) unreceipted.push_back(entry);
                REQUIRE(unreceipted.size() == 1);
            } else {
                const auto record = nlohmann::json::parse(readSplitFile(receipt));
                std::string id;
                for (const auto byte : stagedIdentity.file) { const auto value = std::to_integer<unsigned>(byte); id += "0123456789abcdef"[value >> 4]; id += "0123456789abcdef"[value & 15]; }
                CHECK(record.at("stage").at("identity") == nlohmann::json{{"volume", stagedIdentity.volume}, {"file", id}});
                CHECK((stage.parent_path() == root / "data") == (phase == 12));
            }
        }
        if (phase == 13) {
            CHECK_FALSE(std::filesystem::exists(root / "data" / "Patch.esp"));
            std::vector<std::filesystem::path> authorities;
            for (const auto& entry : std::filesystem::directory_iterator(root / "records"))
                if (entry.path().extension() == ".authority") authorities.push_back(entry.path());
            REQUIRE(authorities.size() == 1);
            const auto authority = nlohmann::json::parse(readSplitFile(authorities.front()));
            const auto backupName = authority.at("entries").at(0).at("backup").get<std::string>();
            const auto current = sessionInventory(root / "data");
            const auto backup = std::find_if(current.begin(), current.end(), [&](const auto& row) { return std::get<0>(row) == backupName; });
            REQUIRE(backup != current.end());
            auto restoredName = *backup; std::get<0>(restoredName) = "Patch.esp"; CHECK(restoredName == *original);
            for (const auto& row : current) if (std::get<0>(row) == "Patch_en.STRINGS" || std::get<0>(row) == "Patch_en.DLSTRINGS")
                unreceipted.push_back(row);
            REQUIRE(unreceipted.size() == 2);
            CHECK(readSplitFile(root / "data" / "Patch_en.STRINGS") == std::vector<std::byte>(splitBytes("new-table").begin(), splitBytes("new-table").end()));
            CHECK(readSplitFile(root / "data" / "Patch_en.DLSTRINGS") == std::vector<std::byte>(splitBytes("new-dialogue").begin(), splitBytes("new-dialogue").end()));
        }
        else if (phase == 10 || phase == 11 || phase == 12 || phase == 1 || phase == 2) CHECK(readSplitFile(root / "data" / "Patch.esp") ==
            std::vector<std::byte>(splitBytes("old-plugin").begin(), splitBytes("old-plugin").end()));
        if (phase >= 14) {
            const auto trace = nlohmann::json::parse(readSplitFile(root / "session-phase"));
            const auto boundaryPath = std::filesystem::path(trace.at("path").get<std::string>());
            const auto completionPath = std::filesystem::path(trace.at("receipt").get<std::string>());
            REQUIRE(std::filesystem::exists(completionPath));
            const auto completion = nlohmann::json::parse(readSplitFile(completionPath));
            const auto transaction = completion.at("transaction").get<std::string>();
            const auto authorityPath = root / "records" / (transaction + ".authority");
            nlohmann::json authority;
            if (phase == 18) {
                REQUIRE_FALSE(std::filesystem::exists(authorityPath));
                const auto& records = completion.at("records");
                const auto row = std::find_if(records.begin(), records.end(), [&](const auto& item) { return item.at("name") == transaction + ".authority"; });
                REQUIRE(row != records.end()); authority = nlohmann::json::parse(row->at("content").get<std::string>());
            } else { REQUIRE(std::filesystem::exists(authorityPath)); authority = nlohmann::json::parse(readSplitFile(authorityPath)); }
            if (phase == 14) {
                CHECK(boundaryPath == completionPath);
                CHECK(std::filesystem::exists(root / "records" / (transaction + ".manifest")));
                const auto backup = root / "data" / authority.at("entries").at(0).at("backup").get<std::string>();
                CHECK(splitIdentity(backup) == std::get<1>(*original));
                CHECK(readSplitFile(backup) == std::get<2>(*original));
            } else {
                CHECK_FALSE(std::filesystem::exists(boundaryPath));
                const auto index = trace.at("index").get<std::size_t>();
                if (phase == 15) CHECK(boundaryPath == root / "data" / authority.at("entries").at(index).at("backup").get<std::string>());
                if (phase == 16) {
                    const auto& directories = completion.at("directories");
                    CHECK(std::any_of(directories.begin(), directories.end(), [&](const auto& row) {
                        return row.at("index") == index && row.at("remove") == true && root / "data" / row.at("name").template get<std::string>() == boundaryPath;
                    }));
                }
                if (phase == 17) CHECK(boundaryPath == root / "records" / (transaction + ".manifest"));
                if (phase == 18) CHECK(boundaryPath == authorityPath);
            }
            for (const auto& row : sessionInventory(root / "data"))
                if (std::get<0>(row) == "Patch.esp" || std::get<0>(row) == "Patch_en.STRINGS" || std::get<0>(row) == "Patch_en.DLSTRINGS") unreceipted.push_back(row);
            REQUIRE(unreceipted.size() == 3);
        }
        const auto recovered = runSplitWorker(root, true, -1, L"Windows multi-file recovery worker");
        if (recovered && std::filesystem::exists(root / "session-error")) { const auto error = readSplitFile(root / "session-error"); INFO(std::string(reinterpret_cast<const char*>(error.data()), error.size())); REQUIRE(recovered == 0); }
        REQUIRE(recovered == 0);
        for (const auto& entry : std::filesystem::directory_iterator(root / "records")) {
            const auto extension = entry.path().extension().string();
            CHECK((extension == ".lock" || (phase == 10 && extension == ".retained")));
        }
        CHECK(readSplitFile(root / "data" / "Patch.esp") == std::vector<std::byte>(splitBytes("new-plugin").begin(), splitBytes("new-plugin").end()));
    CHECK(readSplitFile(root / "data" / "Patch_en.STRINGS") == std::vector<std::byte>(splitBytes("new-table").begin(), splitBytes("new-table").end()));
        CHECK(readSplitFile(root / "data" / "Patch_en.DLSTRINGS") == std::vector<std::byte>(splitBytes("new-dialogue").begin(), splitBytes("new-dialogue").end()));
        const auto dlstrings = splitSecurity(root / "data" / "Patch_en.DLSTRINGS");
        const auto withoutInheritedFlag = [](std::wstring text) {
            for (auto at = text.find(L"ID;"); at != std::wstring::npos; at = text.find(L"ID;", at)) text.replace(at, 3, L";");
            return text;
        };
        CHECK(dlstrings.find(L"(A;;") == std::wstring::npos);
        CHECK(withoutInheritedFlag(dlstrings) == inherited);
        if (phase >= 10 && phase <= 12) {
            const std::array<const char*, 3> names{"Patch.esp", "Patch_en.STRINGS", "Patch_en.DLSTRINGS"};
            REQUIRE(stagedIndex < names.size());
            CHECK((splitIdentity(root / "data" / names[stagedIndex]) == stagedIdentity) == (phase != 10));
        }
        const auto after = sessionInventory(root / "data");
        for (const auto& orphan : unreceipted) {
            const auto found = std::find_if(after.begin(), after.end(), [&](const auto& entry) { return std::get<0>(entry) == std::get<0>(orphan); });
            REQUIRE(found != after.end()); CHECK(*found == orphan);
        }
        const auto completed = sessionInventory(root);
        REQUIRE(runSplitWorker(root, true, -1, L"Windows multi-file recovery worker") == 0);
        CHECK(sessionInventory(root) == completed);
    } }
#endif
}

TEST_CASE("Windows JournalSession rechecks all targets after publish preflight before replay preparation") {
#if defined(_WIN32) && defined(FO4IDE_TEST_HOOKS)
    const auto root = makeSplitRoot(); Cleanup cleanup{root}; prepareSessionFiles(root);
    const auto roots = sessionRoots(root); SessionHookReset hooks;
    SECTION("unsupported automatic inheritance or parent mandatory label") {
        bool label = false; SECTION("automatic inheritance") {} SECTION("inherited label") { label = true; }
        auto path = label ? root / "data" : root / "data" / "Patch_en.STRINGS";
        const auto firstSecurity = splitSecurity(root / "data" / "Patch.esp");
        const auto sddl = label ? std::wstring(L"S:(ML;OICI;NW;;;LW)") : splitSecurity(path);
        PSECURITY_DESCRIPTOR sd = nullptr; REQUIRE(::ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), 1, &sd, nullptr));
        PACL acl = nullptr; BOOL present = FALSE, defaulted = FALSE;
        REQUIRE((label ? ::GetSecurityDescriptorSacl(sd, &present, &acl, &defaulted) : ::GetSecurityDescriptorDacl(sd, &present, &acl, &defaulted)));
        auto name = path.native(); const auto error = ::SetNamedSecurityInfoW(name.data(), SE_FILE_OBJECT, label ? LABEL_SECURITY_INFORMATION : DACL_SECURITY_INFORMATION | UNPROTECTED_DACL_SECURITY_INFORMATION, nullptr, nullptr, label ? nullptr : acl, label ? acl : nullptr); ::LocalFree(sd);
        if (error == ERROR_ACCESS_DENIED || error == ERROR_PRIVILEGE_NOT_HELD) SKIP("Token cannot configure unsupported security fixture");
        REQUIRE(error == ERROR_SUCCESS);
        if (!label) { REQUIRE(splitSecurity(path).find(L"D:AI") != std::wstring::npos); REQUIRE(splitSecurity(root / "data" / "Patch.esp") == firstSecurity); }
        else REQUIRE(splitSecurity(path).find(L"(ML;OICI;NW;;;LW)") != std::wstring::npos);
        const auto before = sessionInventory(root / "data"); JournalSession session(root / "records");
        CHECK_THROWS_WITH(session.publish(roots, sessionInputs(), sessionSources(root)), "unsupported Windows staging metadata");
        CHECK(sessionInventory(root / "data") == before); return;
    }
    SECTION("later newcomer after publish preflight") {
    sessionWorkerRoot = root; sessionInvalidated.reset();
    setWindowsJournalSessionHook(invalidateSessionPreflight);
    { JournalSession session(root / "records"); CHECK_THROWS_AS(session.publish(roots, sessionInputs(), sessionSources(root)), JournalRecoveryRequired); }
    REQUIRE(sessionInvalidated.has_value());
    CHECK(sessionInventory(root / "data") == *sessionInvalidated);
    }
#endif
}

TEST_CASE("Windows Session refuses altered private staging ACLs without changing participants", "[multi-process]") {
#if defined(_WIN32) && defined(FO4IDE_TEST_HOOKS)
    const auto root = makeSplitRoot(); Cleanup cleanup{root}; prepareSessionFiles(root);
    REQUIRE(runSplitWorker(root, true, 11, L"Windows multi-file recovery worker") == 77);
    const auto trace = nlohmann::json::parse(readSplitFile(root / "session-phase"));
    const auto stage = std::filesystem::path(trace.at("path").get<std::string>());
    const auto stageBefore = sessionInventory(stage.parent_path());
    const auto descriptor = splitSecurity(stage.parent_path());
    const auto end = descriptor.find(L"G:"); REQUIRE(end != descriptor.npos);
    const auto owner = descriptor.substr(2, end - 2);
    std::wstring changed;
    SECTION("extra deny ACE") { changed = L"D:P(D;;0x10;;;WD)(A;;FA;;;" + owner + L")"; }
    SECTION("inheritable ACE") { changed = L"D:P(A;OICI;FA;;;" + owner + L")"; }
    PSECURITY_DESCRIPTOR security = nullptr;
    REQUIRE(::ConvertStringSecurityDescriptorToSecurityDescriptorW(changed.c_str(), SDDL_REVISION_1, &security, nullptr));
    PACL dacl = nullptr; BOOL present = FALSE, defaulted = FALSE;
    REQUIRE(::GetSecurityDescriptorDacl(security, &present, &dacl, &defaulted));
    {
        Handle directory{::CreateFileW(stage.parent_path().c_str(), MAXIMUM_ALLOWED, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
        REQUIRE(directory.value != INVALID_HANDLE_VALUE);
        const auto error = ::SetSecurityInfo(directory.value, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
            nullptr, nullptr, dacl, nullptr);
        ::LocalFree(security); REQUIRE(error == ERROR_SUCCESS);
    }
    REQUIRE(splitSecurity(stage.parent_path()) != descriptor);
    REQUIRE(sessionInventory(stage.parent_path()) == stageBefore);
    const auto before = sessionInventory(root);
    { JournalSession session(root / "records"); CHECK_THROWS_AS(session.recover({sessionRoots(root)}), JournalRecoveryRequired); }
    CHECK(sessionInventory(root) == before);
#endif
}

TEST_CASE("Windows completion cleanup refuses a same-content later backup newcomer before deleting earlier evidence", "[multi-process]") {
#if defined(_WIN32) && defined(FO4IDE_TEST_HOOKS)
    const auto root = makeSplitRoot(); Cleanup cleanup{root}; prepareSessionFiles(root);
    REQUIRE(runSplitWorker(root, true, 14, L"Windows multi-file recovery worker") == 77);
    const auto trace = nlohmann::json::parse(readSplitFile(root / "session-phase"));
    const auto completion = nlohmann::json::parse(readSplitFile(std::filesystem::path(trace.at("receipt").get<std::string>())));
    const auto transaction = completion.at("transaction").get<std::string>();
    const auto authority = nlohmann::json::parse(readSplitFile(root / "records" / (transaction + ".authority")));
    const auto backup = root / "data" / authority.at("entries").at(1).at("backup").get<std::string>();
    const auto displaced = root / "data" / "displaced-backup";
    const auto securityBefore = splitSecurity(backup); const auto creationBefore = splitCreation(backup);
    std::filesystem::rename(backup, displaced);
    PSECURITY_DESCRIPTOR security = nullptr;
    REQUIRE(::ConvertStringSecurityDescriptorToSecurityDescriptorW(securityBefore.c_str(), SDDL_REVISION_1, &security, nullptr));
    {
        SECURITY_ATTRIBUTES attributes{sizeof(attributes), security, FALSE};
        Handle file{::CreateFileW(backup.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &attributes, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr)};
        ::LocalFree(security); REQUIRE(file.value != INVALID_HANDLE_VALUE);
        DWORD written = 0; REQUIRE(::WriteFile(file.value, "old-table", 9, &written, nullptr)); REQUIRE(written == 9);
    }
    setSplitCreation(backup, creationBefore);
    REQUIRE(splitSecurity(backup) == securityBefore);
    REQUIRE(readSplitFile(backup) == readSplitFile(displaced));
    REQUIRE(splitIdentity(backup) != splitIdentity(displaced));
    const auto before = sessionInventory(root);
    { JournalSession session(root / "records"); CHECK_THROWS_AS(session.recover({sessionRoots(root)}), JournalRecoveryRequired); }
    CHECK(sessionInventory(root) == before);
#endif
}
