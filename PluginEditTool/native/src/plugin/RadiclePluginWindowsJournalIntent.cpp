#include "RadiclePluginWindowsJournalIntent.hpp"
#include "RadiclePluginWindowsJournalDirectory.hpp"
#include "RadiclePluginJournalDestination.hpp"

#ifdef _WIN32
#include <fo4/io/sha256.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <stdexcept>

namespace fo4ide::plugin::detail {
namespace {
using Json = nlohmann::json;
constexpr std::size_t maxIntentBytes = 16384;
#ifdef FO4IDE_TEST_HOOKS
WindowsJournalIntentHook intentHook = nullptr;
#endif

bool hex(std::string_view value, std::size_t size) {
    return value.size() == size && std::all_of(value.begin(), value.end(), [](char ch) {
        return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
    });
}

std::string fold(std::string name) {
    for (auto& ch : name) if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch + ('a' - 'A'));
    return name;
}

std::string recordBase(const WindowsJournalReplacementRequest& request) {
    if (!hex(request.transaction, 32) || request.entry >= 128) throw std::runtime_error("invalid replacement transaction or entry");
    for (const auto* name : {&request.destination, &request.staged, &request.backup}) {
        validateWindowsJournalFileName(*name);
        const auto lower = fold(*name);
        if (lower.starts_with("wr-") || lower == ".windows-replacement.lock") throw std::runtime_error("reserved replacement artifact name");
    }
    if (fold(request.destination) == fold(request.staged) || fold(request.destination) == fold(request.backup) ||
        fold(request.staged) == fold(request.backup)) throw std::runtime_error("replacement artifact names must differ");
    return "wr-" + request.transaction + "-" + std::to_string(request.entry);
}

Json identityJson(const WindowsJournalFileIdentity& identity) {
    std::string id;
    for (const auto value : identity.file) {
        const auto byte = std::to_integer<unsigned>(value);
        id.push_back("0123456789abcdef"[byte >> 4]);
        id.push_back("0123456789abcdef"[byte & 15]);
    }
    return {{"volume", identity.volume}, {"file", id}};
}

WindowsJournalFileIdentity decodeIdentity(const Json& value) {
    if (!value.is_object() || value.size() != 2 || !value.contains("volume") || !value["volume"].is_number_unsigned() ||
        !value.contains("file") || !value["file"].is_string()) throw std::runtime_error("invalid replacement identity");
    const auto id = value["file"].get<std::string>();
    if (!hex(id, 32)) throw std::runtime_error("invalid replacement file ID");
    WindowsJournalFileIdentity identity;
    identity.volume = value["volume"].get<std::uint64_t>();
    const auto nibble = [](char ch) { return ch <= '9' ? ch - '0' : ch - 'a' + 10; };
    for (std::size_t i = 0; i < identity.file.size(); ++i) identity.file[i] = static_cast<std::byte>((nibble(id[i * 2]) << 4) | nibble(id[i * 2 + 1]));
    return identity;
}

struct Evidence {
    WindowsJournalFileIdentity identity{};
    std::uint64_t size{};
    std::string hash{};
    std::optional<WindowsJournalFileMetadata> metadata{};
};

Json evidenceJson(const WindowsJournalFileSnapshot& file) {
    if (file.bytes.size() > kWindowsReplacementMaxFileBytes) throw std::runtime_error("replacement exceeds file bound");
    Json value{{"identity", identityJson(file.identity)}, {"size", file.bytes.size()}, {"sha256", fo4::io::sha256_hex(file.bytes)}};
    if (file.metadata) value["metadata"] = {{"security", file.metadata->security}, {"attributes", file.metadata->attributes}, {"creationTime", file.metadata->creationTime}};
    return value;
}

Evidence decodeEvidence(const Json& value, bool withMetadata) {
    if (!value.is_object() || value.size() != (withMetadata ? 4u : 3u) || !value.contains("identity") || !value.contains("size") ||
        !value["size"].is_number_unsigned() || !value.contains("sha256") || !value["sha256"].is_string()) {
        throw std::runtime_error("invalid replacement file evidence");
    }
    Evidence result{decodeIdentity(value["identity"]), value["size"].get<std::uint64_t>(), value["sha256"].get<std::string>()};
    if (withMetadata) {
        const auto& metadata = value.at("metadata");
        if (!metadata.is_object() || metadata.size() != 3 || !metadata.at("security").is_string() ||
            metadata.at("security").get_ref<const std::string&>().empty() ||
            metadata.at("security").get_ref<const std::string&>().size() > 4096 ||
            !metadata.at("attributes").is_number_unsigned() || metadata.at("attributes").get<std::uint64_t>() > UINT32_MAX ||
            !metadata.at("creationTime").is_number_integer() || metadata.at("creationTime").get<std::int64_t>() < 0)
            throw std::runtime_error("invalid replacement metadata");
        result.metadata = WindowsJournalFileMetadata{metadata.at("security").get<std::string>(), metadata.at("attributes").get<std::uint32_t>(),
            metadata.at("creationTime").get<std::int64_t>()};
    }
    if (result.size > kWindowsReplacementMaxFileBytes || !hex(result.hash, 64)) throw std::runtime_error("invalid replacement size or hash");
    return result;
}

struct Intent {
    WindowsJournalFileIdentity directory{};
    std::optional<Evidence> original{};
    Evidence replacement{};
    std::optional<WindowsJournalFileIdentity> recordDirectory{};
};

std::string text(const WindowsJournalFileSnapshot& file) {
    return {reinterpret_cast<const char*>(file.bytes.data()), file.bytes.size()};
}

Intent decodeIntent(std::string_view encoded, const WindowsJournalReplacementRequest& request) {
    if (encoded.empty() || encoded.size() > maxIntentBytes) throw std::runtime_error("replacement intent exceeds bound");
    const auto value = Json::parse(encoded);
    if (!value.is_object() || value.dump() != encoded || (value.at("version") != 1 && value.at("version") != 2) ||
        !value.at("version").is_number_unsigned() || value.at("transaction") != request.transaction ||
        !value.at("entry").is_number_unsigned() || value.at("entry") != request.entry ||
        value.at("destination") != request.destination || value.at("staged") != request.staged || value.at("backup") != request.backup) {
        throw std::runtime_error("replacement intent shape or independent request mismatch");
    }
    const bool splitStore = value.at("version") == 2;
    const bool hasOriginalExists = value.contains("originalExists");
    if (splitStore && !hasOriginalExists) throw std::runtime_error("replacement intent missing original flag");
    if (hasOriginalExists && !value.at("originalExists").is_boolean()) throw std::runtime_error("invalid replacement original flag");
    const bool hasOriginal = value.contains("original");
    if (!hasOriginalExists && !hasOriginal) throw std::runtime_error("replacement intent is missing original state");
    const bool originalExists = hasOriginalExists ? value.at("originalExists").get<bool>() : hasOriginal;
    if (value.size() != (hasOriginalExists && originalExists ? 10u : 9u) + (splitStore ? 1u : 0u) || hasOriginal != originalExists) {
        throw std::runtime_error("replacement intent original evidence mismatch");
    }
    Intent intent{decodeIdentity(value.at("directory")), std::nullopt, decodeEvidence(value.at("replacement"), splitStore)};
    if (splitStore) intent.recordDirectory = decodeIdentity(value.at("recordDirectory"));
    if (originalExists) intent.original = decodeEvidence(value.at("original"), splitStore);
    if ((intent.original && (intent.original->identity.volume != intent.directory.volume ||
            intent.original->identity == intent.replacement.identity || intent.original->metadata != intent.replacement.metadata)) ||
        intent.replacement.identity.volume != intent.directory.volume) throw std::runtime_error("invalid replacement identity relationship");
    return intent;
}

void requireExpected(const Intent& intent, const WindowsJournalReplacementExpectation& expected) {
    if (expected.directory != intent.directory || expected.originalExists != intent.original.has_value() ||
        expected.originalSize != (intent.original ? intent.original->size : 0) ||
        expected.originalHash != (intent.original ? intent.original->hash : "") ||
        expected.finalSize != intent.replacement.size || expected.finalHash != intent.replacement.hash ||
        expected.metadata != intent.replacement.metadata) {
        throw std::runtime_error("replacement intent does not match caller manifest evidence");
    }
}

bool matches(const std::optional<WindowsJournalFileSnapshot>& actual, const Evidence& expected) {
    return actual && actual->identity == expected.identity && actual->bytes.size() == expected.size &&
        fo4::io::sha256_hex(actual->bytes) == expected.hash && actual->metadata == expected.metadata;
}

void requireExact(const WindowsJournalFileSnapshot& actual, const WindowsJournalFileSnapshot& expected) {
    if (actual.identity != expected.identity || actual.bytes != expected.bytes) throw std::runtime_error("replacement snapshot changed");
}

WindowsJournalFileSnapshot recordSnapshot(const WindowsJournalDirectory& root, std::string_view name, std::string_view expected) {
    auto file = root.snapshotFile(name, maxIntentBytes);
    if (text(file) != expected) throw std::runtime_error("replacement record changed");
    return file;
}

std::span<const std::byte> bytes(std::string_view value) { return std::as_bytes(std::span(value.data(), value.size())); }
}

#ifdef FO4IDE_TEST_HOOKS
void setWindowsJournalIntentHook(WindowsJournalIntentHook hook) { intentHook = hook; }
#endif

void WindowsJournalDirectory::persistReplacementIntent(const WindowsJournalReplacementRequest& request,
    const WindowsJournalFileSnapshot& original, const WindowsJournalFileSnapshot& replacement) const {
    const auto base = recordBase(request);
    const auto lock = lockReplacement();
    const Json value{{"version", 1}, {"transaction", request.transaction}, {"entry", request.entry}, {"originalExists", true},
        {"directory", identityJson(directoryIdentity())}, {"destination", request.destination}, {"staged", request.staged},
        {"backup", request.backup}, {"original", evidenceJson(original)}, {"replacement", evidenceJson(replacement)}};
    const auto encoded = value.dump();
    static_cast<void>(decodeIntent(encoded, request));
    requireExact(snapshotFile(request.destination, original.bytes.size()), original);
    requireExact(snapshotFile(request.staged, replacement.bytes.size()), replacement);
    for (const auto& name : {base + ".intent", base + ".intent.stage", base + ".done", base + ".done.stage", request.backup}) {
        if (trySnapshotFile(name, maxIntentBytes)) throw std::runtime_error("replacement artifact already exists");
    }
    writeFile(base + ".intent.stage", bytes(encoded));
#ifdef FO4IDE_TEST_HOOKS
    if (intentHook) intentHook(WindowsJournalIntentStep::intentWritten);
#endif
    publishSnapshot(base + ".intent.stage", recordSnapshot(*this, base + ".intent.stage", encoded), base + ".intent");
#ifdef FO4IDE_TEST_HOOKS
    if (intentHook) intentHook(WindowsJournalIntentStep::intentPublished);
#endif
}

void WindowsJournalDirectory::persistReplacementIntent(const WindowsJournalReplacementRequest& request, std::nullopt_t,
    const WindowsJournalFileSnapshot& replacement) const {
    const auto base = recordBase(request);
    const auto lock = lockReplacement();
    const Json value{{"version", 1}, {"transaction", request.transaction}, {"entry", request.entry}, {"originalExists", false},
        {"directory", identityJson(directoryIdentity())}, {"destination", request.destination}, {"staged", request.staged},
        {"backup", request.backup}, {"replacement", evidenceJson(replacement)}};
    const auto encoded = value.dump();
    static_cast<void>(decodeIntent(encoded, request));
    if (trySnapshotFile(request.destination, 0)) throw std::runtime_error("replacement destination already exists");
    requireExact(snapshotFile(request.staged, replacement.bytes.size()), replacement);
    for (const auto& name : {base + ".intent", base + ".intent.stage", base + ".done", base + ".done.stage", request.backup}) {
        if (trySnapshotFile(name, maxIntentBytes)) throw std::runtime_error("replacement artifact already exists");
    }
    writeFile(base + ".intent.stage", bytes(encoded));
#ifdef FO4IDE_TEST_HOOKS
    if (intentHook) intentHook(WindowsJournalIntentStep::intentWritten);
#endif
    publishSnapshot(base + ".intent.stage", recordSnapshot(*this, base + ".intent.stage", encoded), base + ".intent");
#ifdef FO4IDE_TEST_HOOKS
    if (intentHook) intentHook(WindowsJournalIntentStep::intentPublished);
#endif
}

WindowsJournalReplacementState WindowsJournalDirectory::processReplacement(const WindowsJournalReplacementRequest& request,
    const WindowsJournalReplacementExpectation* expected, bool apply, const WindowsJournalReplacementArtifacts* artifacts) const {
    if (apply && artifacts) throw std::runtime_error("external destination inspection cannot authorize replacement");
    const auto base = recordBase(request);
    const auto published = trySnapshotFile(base + ".intent", maxIntentBytes);
    const auto stagedIntent = trySnapshotFile(base + ".intent.stage", maxIntentBytes);
    if (published.has_value() == stagedIntent.has_value()) throw std::runtime_error("missing or conflicting replacement intent");
    const auto& record = published ? *published : *stagedIntent;
    const auto encoded = text(record);
    const auto intent = decodeIntent(encoded, request);
    if (apply && intent.recordDirectory) throw std::runtime_error("split-store replacement is not implemented");
    if ((artifacts && !intent.recordDirectory) || (intent.recordDirectory && *intent.recordDirectory != directoryIdentity()))
        throw std::runtime_error("replacement record store identity changed");
    if ((artifacts ? artifacts->directory : directoryIdentity()) != intent.directory) throw std::runtime_error("replacement directory identity changed");
    if (expected) requireExpected(intent, *expected);
    const auto destination = artifacts ? artifacts->destination : trySnapshotFile(request.destination, static_cast<std::size_t>(
        std::max(intent.original ? intent.original->size : 0, intent.replacement.size)));
    const auto staged = artifacts ? artifacts->staged : trySnapshotFile(request.staged, static_cast<std::size_t>(intent.replacement.size));
    const auto backup = artifacts ? artifacts->backup : trySnapshotFile(request.backup, static_cast<std::size_t>(intent.original ? intent.original->size : 0));
    const bool prepared = intent.original ? matches(destination, *intent.original) && matches(staged, intent.replacement) && !backup
                                          : !destination && matches(staged, intent.replacement) && !backup;
    const bool backedUp = intent.original && !destination && matches(staged, intent.replacement) && matches(backup, *intent.original);
    const bool applied = matches(destination, intent.replacement) && !staged &&
        (intent.original ? matches(backup, *intent.original) : !backup);
    if (!prepared && !backedUp && !applied) throw std::runtime_error("replacement artifacts conflict with intent");
    const auto done = trySnapshotFile(base + ".done", maxIntentBytes);
    const auto stagedDone = trySnapshotFile(base + ".done.stage", maxIntentBytes);
    const auto completion = Json{{"version", 1}, {"transaction", request.transaction}, {"entry", request.entry},
        {"intentSha256", fo4::io::sha256_hex(record.bytes)}, {"state", "completed"}}.dump();
    if ((done && stagedDone) || ((done || stagedDone) && !applied) || (done && text(*done) != completion) ||
        (stagedDone && text(*stagedDone) != completion)) throw std::runtime_error("replacement completion record conflicts");
    if (!published && (!prepared || done || stagedDone)) throw std::runtime_error("unpublished intent has unexpected artifacts");
    if (!apply) return prepared ? WindowsJournalReplacementState::prepared :
        backedUp ? WindowsJournalReplacementState::backedUp : WindowsJournalReplacementState::applied;
    if (!published) {
        publishSnapshot(base + ".intent.stage", record, base + ".intent");
#ifdef FO4IDE_TEST_HOOKS
        if (intentHook) intentHook(WindowsJournalIntentStep::intentPublished);
#endif
    }
    if (prepared && intent.original) replaceFileRecoverably(request.destination, *destination, request.staged, *staged, request.backup);
    else if (prepared) {
        publishSnapshot(request.staged, *staged, request.destination);
#ifdef FO4IDE_TEST_HOOKS
        if (intentHook) intentHook(WindowsJournalIntentStep::replacementPublished);
#endif
    }
    else if (backedUp) publishSnapshot(request.staged, *staged, request.destination);
    if (!matches(trySnapshotFile(request.destination, static_cast<std::size_t>(intent.replacement.size)), intent.replacement) ||
        trySnapshotFile(request.staged, static_cast<std::size_t>(intent.replacement.size)) ||
        (intent.original ? !matches(trySnapshotFile(request.backup, static_cast<std::size_t>(intent.original->size)), *intent.original)
                         : trySnapshotFile(request.backup, 0).has_value())) {
        throw std::runtime_error("replacement changed before completion recording");
    }
    if (done) return WindowsJournalReplacementState::applied;
    if (!stagedDone) {
        writeFile(base + ".done.stage", bytes(completion));
#ifdef FO4IDE_TEST_HOOKS
        if (intentHook) intentHook(WindowsJournalIntentStep::doneWritten);
#endif
    }
    publishSnapshot(base + ".done.stage", recordSnapshot(*this, base + ".done.stage", completion), base + ".done");
#ifdef FO4IDE_TEST_HOOKS
    if (intentHook) intentHook(WindowsJournalIntentStep::donePublished);
#endif
    return WindowsJournalReplacementState::applied;
}

WindowsJournalReplacementState WindowsJournalDirectory::inspectReplacement(const WindowsJournalReplacementRequest& request,
    const WindowsJournalReplacementExpectation& expected) const {
    return processReplacement(request, &expected, false);
}

void WindowsJournalDirectory::persistReplacementIntent(const WindowsJournalReplacementRequest& request,
    const WindowsJournalReplacementExpectation& expected, const JournalDestination& destination) const {
    const auto base = recordBase(request);
    if (!expected.metadata) throw std::runtime_error("split-store replacement requires approved metadata");
    const auto prepare = [&] {
        const auto artifacts = destination.replacementArtifacts(request, expected.originalSize, expected.finalSize, !expected.originalExists);
        if (artifacts.directory != expected.directory || artifacts.destination.has_value() != expected.originalExists ||
            !artifacts.staged || artifacts.backup) throw std::runtime_error("replacement destination is not prepared");
        Json value{{"version", 2}, {"transaction", request.transaction}, {"entry", request.entry},
            {"originalExists", expected.originalExists}, {"recordDirectory", identityJson(directoryIdentity())},
            {"directory", identityJson(artifacts.directory)}, {"destination", request.destination},
            {"staged", request.staged}, {"backup", request.backup}, {"replacement", evidenceJson(*artifacts.staged)}};
        if (artifacts.destination) value["original"] = evidenceJson(*artifacts.destination);
        const auto encoded = value.dump();
        requireExpected(decodeIntent(encoded, request), expected);
        for (const auto& name : {base + ".intent", base + ".intent.stage", base + ".done", base + ".done.stage"}) {
            if (trySnapshotFile(name, maxIntentBytes)) throw std::runtime_error("replacement record already exists");
        }
        return encoded;
    };
    const auto preflight = prepare();
    const auto lock = lockReplacement();
    const auto encoded = prepare();
    if (encoded != preflight) throw std::runtime_error("replacement artifacts changed after preflight");
    writeFile(base + ".intent.stage", bytes(encoded));
#ifdef FO4IDE_TEST_HOOKS
    if (intentHook) intentHook(WindowsJournalIntentStep::intentWritten);
#endif
    publishSnapshot(base + ".intent.stage", recordSnapshot(*this, base + ".intent.stage", encoded), base + ".intent");
#ifdef FO4IDE_TEST_HOOKS
    if (intentHook) intentHook(WindowsJournalIntentStep::intentPublished);
#endif
}

WindowsJournalReplacementState WindowsJournalDirectory::inspectReplacement(const WindowsJournalReplacementRequest& request,
    const WindowsJournalReplacementExpectation& expected, const JournalDestination& destination) const {
    static_cast<void>(recordBase(request));
    const auto artifacts = destination.replacementArtifacts(request, expected.originalSize, expected.finalSize);
    return processReplacement(request, &expected, false, &artifacts);
}

void WindowsJournalDirectory::resumeReplacement(const WindowsJournalReplacementRequest& request,
    const WindowsJournalReplacementExpectation& expected, JournalDestination& destination) const {
    const auto base = recordBase(request);
    const auto lock = lockReplacement();
    std::optional<WindowsJournalFileSnapshot> boundRecord;
    const auto validate = [&] {
        auto artifacts = destination.replacementArtifacts(request, expected.originalSize, expected.finalSize);
        const auto state = processReplacement(request, &expected, false, &artifacts);
        const auto published = trySnapshotFile(base + ".intent", maxIntentBytes);
        const auto record = published ? *published : snapshotFile(base + ".intent.stage", maxIntentBytes);
        if (boundRecord) requireExact(record, *boundRecord);
        else boundRecord = record;
        return std::pair{state, std::move(artifacts)};
    };
    auto current = validate();
    if (!trySnapshotFile(base + ".intent", maxIntentBytes)) {
        publishSnapshot(base + ".intent.stage", *boundRecord, base + ".intent");
#ifdef FO4IDE_TEST_HOOKS
        if (intentHook) intentHook(WindowsJournalIntentStep::intentPublished);
#endif
        current = validate();
    }
    if (current.first == WindowsJournalReplacementState::prepared && expected.originalExists) {
        destination.renameReplacementArtifact(request, current.second, true);
#ifdef FO4IDE_TEST_HOOKS
        if (intentHook) intentHook(WindowsJournalIntentStep::originalBackedUp);
#endif
        current = validate();
    }
    if (current.first == WindowsJournalReplacementState::prepared || current.first == WindowsJournalReplacementState::backedUp) {
        destination.renameReplacementArtifact(request, current.second, false);
#ifdef FO4IDE_TEST_HOOKS
        if (intentHook) intentHook(WindowsJournalIntentStep::replacementPublished);
#endif
    }
    current = validate();
    if (current.first != WindowsJournalReplacementState::applied) throw std::runtime_error("replacement was not applied");
    const auto completion = Json{{"version", 1}, {"transaction", request.transaction}, {"entry", request.entry},
        {"intentSha256", fo4::io::sha256_hex(boundRecord->bytes)}, {"state", "completed"}}.dump();
    if (trySnapshotFile(base + ".done", maxIntentBytes)) return;
    if (!trySnapshotFile(base + ".done.stage", maxIntentBytes)) {
        writeFile(base + ".done.stage", bytes(completion));
#ifdef FO4IDE_TEST_HOOKS
        if (intentHook) intentHook(WindowsJournalIntentStep::doneWritten);
#endif
    }
    current = validate();
    if (current.first != WindowsJournalReplacementState::applied) throw std::runtime_error("replacement changed before completion");
    publishSnapshot(base + ".done.stage", recordSnapshot(*this, base + ".done.stage", completion), base + ".done");
#ifdef FO4IDE_TEST_HOOKS
    if (intentHook) intentHook(WindowsJournalIntentStep::donePublished);
#endif
}

void WindowsJournalDirectory::resumeReplacement(const WindowsJournalReplacementRequest& request) const {
    const auto lock = lockReplacement();
    processReplacement(request, nullptr, true);
}

void WindowsJournalDirectory::resumeReplacement(const WindowsJournalReplacementRequest& request,
    const WindowsJournalReplacementExpectation& expected) const {
    const auto lock = lockReplacement();
    processReplacement(request, &expected, true);
}

}
#endif
