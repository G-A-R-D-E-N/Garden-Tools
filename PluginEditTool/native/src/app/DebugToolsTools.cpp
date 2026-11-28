#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <fstream>
#include <iterator>
#include <set>
#include <system_error>
#include <unordered_map>

#include <fo4/archive/assets.hpp>
#include <fo4/io/atomic_write.hpp>
#include <fo4/nif/nif.hpp>
#include <fo4/nif/nif_write.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/plugin/writer.hpp>

#include "DebugToolsInternal.hpp"

namespace fo4ide::app::debugtools {

namespace fs = std::filesystem;

namespace {

constexpr std::size_t kMaxDiffRows = 50;
constexpr std::size_t kHedrCountOffset = 34;

long long countOf(const json& value) {
    if (value.is_number()) return static_cast<long long>(value.get<double>());
    if (value.is_array() || value.is_object()) return static_cast<long long>(value.size());
    if (value.is_boolean()) return value.get<bool>() ? 1 : 0;
    return 0;
}

bool truthy(const json& value) {
    if (value.is_boolean()) return value.get<bool>();
    if (value.is_number()) return value.get<double>() != 0.0;
    return false;
}

}  // namespace

std::string lowered(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string textField(const json& object, const char* key) {
    if (!object.is_object()) return {};
    const auto it = object.find(key);
    return it != object.end() && it->is_string() ? it->get<std::string>() : std::string{};
}

long long countField(const json& object, const char* key) {
    if (!object.is_object()) return 0;
    const auto it = object.find(key);
    return it == object.end() ? 0 : countOf(*it);
}

bool hasField(const json& object, const char* key) {
    return object.is_object() && object.contains(key);
}

json fieldOr(const json& object, const char* key, json fallback) {
    if (!object.is_object()) return fallback;
    const auto it = object.find(key);
    return it == object.end() ? fallback : *it;
}

Diag begin(const char* id, const char* title, std::vector<std::string> evidence) {
    Diag diag;
    diag.id = id;
    diag.title = title;
    diag.evidence = std::move(evidence);
    return diag;
}

void setStatus(Diag& diag, const char* status, std::string detail) {
    diag.status = status;
    diag.detail = std::move(detail);
}

bool stoppedForShutdown(Ctx& c, Diag& diag) {
    if (!c.cancelled()) return false;
    c.cancelledRun = true;
    setStatus(diag, "skip", "Stopped between steps because FO4IDE is closing; the run is partial and nothing after this step ran.");
    return true;
}

void fromError(Diag& diag, const Error& error) {
    static const std::set<std::string> skips{"NoEnvironment", "PrevisNoEnvironment", "PrevisDataDir",
                                             "PrevisEnvironment", "NoDataRoots", "PluginNotLoaded"};
    diag.status = skips.contains(error.type) ? "skip" : "error";
    diag.detail = error.type + ": " + error.message;
}

void addDiff(Diag& diag, std::string row) {
    if (diag.diffs.size() < kMaxDiffRows) diag.diffs.push_back(std::move(row));
}

std::string needCall(const DebugToolsEnv& env) {
    return env.call ? std::string{} : "the host call bridge is not wired into the debug tools";
}


std::string needAssets(const DebugToolsEnv& env) {
    return env.assets ? std::string{} : "the shared asset resolver is not available to the debug tools";
}

std::string noRequirement(const DebugToolsEnv&) {
    return {};
}

namespace {

std::string needProfile(const DebugToolsEnv& env) {
    if (auto reason = needCall(env); !reason.empty()) return reason;
    return env.snapshot ? std::string{} : "the MO2 profile is not available to the debug tools";
}

bool dataDirReady(const Ctx& c, Diag& diag, fs::path& dataDir) {
    dataDir = dataDirOf(c.env);
    std::error_code ec;
    if (dataDir.empty() || !fs::is_directory(dataDir, ec)) {
        setStatus(diag, "skip", "No Fallout 4 Data folder is configured or found, so there is no game data to check.");
        return false;
    }
    return true;
}

struct LodInput {
    std::string plugins;
    std::string dataDir;
    std::string archives;
};

std::optional<LodInput> lodInput(const Ctx& c, Diag& diag) {
    fs::path dataDir;
    if (!dataDirReady(c, diag, dataDir)) return std::nullopt;
    const auto snapshot = c.env.snapshot ? c.env.snapshot() : std::nullopt;
    LodInput input;
    input.dataDir = dataDir.string();
    if (snapshot) {
        std::unordered_map<std::string, fs::path> resolved;
        for (const auto& plugin : snapshot->plugins) {
            if (plugin.resolved) resolved[lowered(plugin.name)] = plugin.path;
        }
        for (const auto& name : snapshot->loadOrder) {
            if (const auto it = resolved.find(lowered(name)); it != resolved.end()) {
                input.plugins += it->second.string() + "\n";
            }
        }
    }
    if (input.plugins.empty()) {
        setStatus(diag, "skip",
                  "The MO2 profile has no load order with resolved plugin files, so there is nothing to open.");
        return std::nullopt;
    }
    if (c.env.assets) {
        if (const auto resolver = c.env.assets()) {
            for (const auto& archive : resolver->archives()) input.archives += archive.path().string() + "\n";
        }
    }
    return input;
}

json lodArgs(const LodInput& input, std::initializer_list<json> rest) {
    json args = json::array({input.plugins, input.dataDir, input.archives});
    for (const auto& value : rest) args.push_back(value);
    return args;
}

void envContext(Ctx& c, std::vector<Diag>& out) {
    Diag d = begin("env.context", "Environment, versions and load order", {});
    const auto context = debugToolsContext(c.env);
    const auto dataDir = textField(context, "dataDir");
    const auto build = textField(context, "gameBuild");
    d.left = Side{"Fallout 4 Data folder", "settings, MO2 profile or game root", dataDir};
    d.right = Side{"game build", "Fallout4.exe file version", build.empty() ? "not readable" : build};
    std::error_code ec;
    if (dataDir.empty() || !fs::is_directory(fs::path(dataDir), ec)) {
        setStatus(d, "skip", "No Fallout 4 Data folder is configured or found.");
    } else {
        d.evidence = {build.empty() ? "structural" : "real-game-data"};
        setStatus(d, "pass",
                  "FO4IDE " + context["versions"].value("fo4ide", std::string{}) + " (" +
                      context["versions"].value("commit", std::string{}) + "), Radicle ABI " +
                      context["versions"].value("radicleAbi", std::string{}) + ", " +
                      std::to_string(context["loadOrder"].size()) + " plugin(s) in the load order.");
    }
    out.push_back(std::move(d));
}

void assetPrecedence(Ctx& c, std::vector<Diag>& out) {
    Diag d = begin("env.assetPrecedence", "Which provider wins an asset path", {"real-game-data"});
    const auto asset = c.param("asset");
    auto reply = c.call("nif", "ResolveAsset", json::array({asset, false, 25}));
    if (!reply) {
        fromError(d, reply.error());
        out.push_back(std::move(d));
        return;
    }
    const auto& row = *reply;
    if (!row.is_object() || !row.value("Found", false)) {
        setStatus(d, "skip", "No asset root or archive provides this path, so there is no precedence to show.");
        out.push_back(std::move(d));
        return;
    }
    const auto winner = row.value("Winner", json::object());
    d.left = Side{"winner", textField(winner, "Provider"), textField(winner, "Container")};
    std::string others;
    const auto rest = row.value("Others", json::array());
    for (const auto& other : rest) {
        others += (others.empty() ? "" : "; ") + textField(other, "Provider");
        addDiff(d, "overridden: " + textField(other, "Provider") + " (" + textField(other, "Kind") + ") " +
                       textField(other, "Container"));
    }
    d.right = Side{"other providers", "FO4IDE ordering", others.empty() ? "none" : others};
    const auto providers = countField(row, "Providers");
    if (providers <= 1) {
        setStatus(d, "pass", "Exactly one provider supplies " + textField(row, "Path") + ".");
    } else {
        setStatus(d, "unverified",
                  std::to_string(providers) + " providers supply " + textField(row, "Path") +
                      ". The winner shown is FO4IDE ordering, not proven engine order.");
    }
    out.push_back(std::move(d));
}

void pluginValidate(Ctx& c, std::vector<Diag>& out) {
    Diag d = begin("plugin.validate", "Plugin structural validation", {"structural"});
    const auto plugin = c.param("plugin");
    const auto source = resolvePluginPath(c.env, plugin);
    const auto dataDir = dataDirOf(c.env);
    if (!source.empty() && !dataDir.empty() && isInside(source, dataDir)) d.evidence.push_back("real-game-data");
    auto reply = c.call("backend", "ValidatePlugin", json::array({plugin}));
    if (!reply) {
        fromError(d, reply.error());
        out.push_back(std::move(d));
        return;
    }
    const auto& report = *reply;
    if (!hasField(report, "errors") || !report["errors"].is_number()) {
        setStatus(d, "error", "The validator reply had no error count, so nothing can be said about this plugin.");
        out.push_back(std::move(d));
        return;
    }
    const auto errors = countField(report, "errors");
    d.left = Side{"errors", plugin, std::to_string(errors)};
    d.right = Side{"warnings", plugin, std::to_string(countField(report, "warnings"))};
    for (const auto& finding : report.value("findings", json::array())) {
        addDiff(d, textField(finding, "severity") + " " + textField(finding, "code") + " " +
                       textField(finding, "formKey") + ": " + textField(finding, "message"));
    }
    setStatus(d, errors > 0 ? "fail" : "pass",
              std::to_string(errors) + " error(s), " + std::to_string(countField(report, "warnings")) +
                  " warning(s), " + std::to_string(countField(report, "infos")) +
                  " info(s). A structural check does not prove the game accepts the plugin.");
    out.push_back(std::move(d));
}

void pluginCheckErrors(Ctx& c, std::vector<Diag>& out) {
    Diag d = begin("plugin.checkErrors", "Plugin cross-record error check", {"structural"});
    const auto plugin = c.param("plugin");
    auto reply = c.call("backend", "CheckForErrors", json::array({plugin, 200}));
    if (!reply) {
        fromError(d, reply.error());
        out.push_back(std::move(d));
        return;
    }
    if (!reply->is_array()) {
        setStatus(d, "error", "The check reply was not a list of findings.");
        out.push_back(std::move(d));
        return;
    }
    long long errors = 0;
    long long warnings = 0;
    for (const auto& row : *reply) {
        const auto severity = textField(row, "Severity");
        (severity == "error" ? errors : warnings) += 1;
        addDiff(d, severity + " " + textField(row, "Code") + " " + textField(row, "FormKey") + ": " +
                       textField(row, "Message"));
    }
    d.left = Side{"errors", plugin, std::to_string(errors)};
    d.right = Side{"warnings", plugin, std::to_string(warnings)};
    setStatus(d, errors > 0 ? "fail" : "pass",
              std::to_string(errors) + " error(s), " + std::to_string(warnings) +
                  " warning(s) (list capped at 200 findings by the check).");
    out.push_back(std::move(d));
}

std::vector<std::byte> readFileBytes(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("cannot open " + path.string());
    file.seekg(0, std::ios::end);
    const auto size = static_cast<std::size_t>(file.tellg());
    file.seekg(0);
    std::vector<std::byte> bytes(size);
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    if (static_cast<std::size_t>(file.gcount()) != size) throw std::runtime_error("short read of " + path.string());
    return bytes;
}

void pluginRoundtrip(Ctx& c, std::vector<Diag>& out) {
    Diag d = begin("plugin.roundtrip", "Plugin write round trip", {"structural"});
    const auto source = resolvePluginPath(c.env, c.param("plugin"));
    if (source.empty()) {
        setStatus(d, "skip", "The plugin file was not found in the load order or the Data folder.");
        out.push_back(std::move(d));
        return;
    }
    const auto dataDir = dataDirOf(c.env);
    if (!dataDir.empty() && isInside(source, dataDir)) d.evidence.push_back("real-game-data");
    if (stoppedForShutdown(c, d)) {
        out.push_back(std::move(d));
        return;
    }
    std::error_code ec;
    if (c.scratch.empty() || !fs::create_directory(c.scratch, ec) || ec) {
        setStatus(d, "error", ec ? "The fresh scratch folder could not be created: " + ec.message()
                                 : "The fresh scratch folder could not be created (it may already exist), so nothing was written.");
        out.push_back(std::move(d));
        return;
    }
    [[maybe_unused]] const EmptyScratchCleanup cleanup{c.scratch};
    c.progress(0, 3, "reading " + source.filename().string());
    const auto original = readFileBytes(source);
    auto plugin = fo4::read_plugin_file(source);
    const auto records = fo4::collect_records(plugin).size();
    const auto groups = fo4::collect_groups(plugin).size();
    const auto masters = plugin.header.masters.size();
    plugin.original_range.clear();
    fo4::repair_hedr_record_count(plugin);
    if (stoppedForShutdown(c, d)) {
        out.push_back(std::move(d));
        return;
    }
    c.progress(1, 3, "rebuilding from the parsed structure");
    const auto written = fo4::write_plugin(plugin, source);
    const auto target = c.scratch / source.filename();
    auto staged = fo4::io::StagedFile::create(target);
    staged.write(written);
    staged.finish();
    if (!std::move(staged).commit_if_absent()) {
        setStatus(d, "error", "The scratch file already existed and was not replaced.");
        out.push_back(std::move(d));
        return;
    }
    d.outputs.push_back(target.string());
    c.progress(2, 3, "comparing bytes");

    d.left = Side{"original", source.string(), std::to_string(original.size()) + " bytes"};
    d.right = Side{"rebuilt", target.string(), std::to_string(written.size()) + " bytes"};
    const std::string caveat =
        " This proves the header, master list, group and record structure rebuild; it does not prove the game accepts the file, and records keep their own original bytes.";
    if (written == original) {
        setStatus(d, "pass", "Byte-identical after a rebuild from the parsed structure." + caveat);
        out.push_back(std::move(d));
        return;
    }
    const auto mismatch = std::mismatch(written.begin(), written.end(), original.begin(), original.end());
    const auto firstDiff = static_cast<std::size_t>(std::distance(written.begin(), mismatch.first));
    bool hedrOnly = written.size() == original.size() && written.size() >= kHedrCountOffset + 4;
    if (hedrOnly) {
        for (std::size_t i = 0; i < written.size(); ++i) {
            if (written[i] != original[i] && (i < kHedrCountOffset || i >= kHedrCountOffset + 4)) {
                hedrOnly = false;
                break;
            }
        }
    }
    if (hedrOnly) {
        std::uint32_t stored = 0;
        std::memcpy(&stored, written.data() + kHedrCountOffset, sizeof(stored));
        if (stored == fo4::expected_hedr_record_count(plugin)) {
            std::uint32_t before = 0;
            std::memcpy(&before, original.data() + kHedrCountOffset, sizeof(before));
            addDiff(d, "HEDR record count at offset 34: shipped " + std::to_string(before) + ", rebuilt " +
                           std::to_string(stored));
            setStatus(d, "pass", "Identical except the HEDR record count, which is rebuilt from the structure." + caveat);
            out.push_back(std::move(d));
            return;
        }
    }
    addDiff(d, "first differing byte at offset " + std::to_string(firstDiff));
    try {
        const auto again = fo4::read_plugin(written, source);
        addDiff(d, "masters: " + std::to_string(masters) + " -> " + std::to_string(again.header.masters.size()));
        addDiff(d, "records: " + std::to_string(records) + " -> " + std::to_string(fo4::collect_records(again).size()));
        addDiff(d, "groups: " + std::to_string(groups) + " -> " + std::to_string(fo4::collect_groups(again).size()));
    } catch (const std::exception& error) {
        addDiff(d, std::string("the rebuilt file does not parse: ") + error.what());
    }
    setStatus(d, "fail", "The rebuilt plugin differs from the original beyond the HEDR count.");
    out.push_back(std::move(d));
}

void assetsAudit(Ctx& c, std::vector<Diag>& out) {
    Diag d = begin("assets.audit", "Asset references of a plugin", {"real-game-data"});
    const auto plugin = c.param("plugin");
    auto reply = c.call("nif", "AuditAssetUsage", json::array({plugin, 200}));
    if (!reply) {
        fromError(d, reply.error());
        out.push_back(std::move(d));
        return;
    }
    const auto& report = *reply;
    if (!hasField(report, "missing") || !hasField(report, "referenced")) {
        setStatus(d, "error", "The audit reply had no counts.");
        out.push_back(std::move(d));
        return;
    }
    if (hasField(report, "note")) {
        setStatus(d, "skip", textField(report, "note"));
        out.push_back(std::move(d));
        return;
    }
    const auto missing = countField(report, "missing");
    const auto unusable = countField(report, "unusable");
    d.left = Side{"referenced", plugin, std::to_string(countField(report, "referenced"))};
    d.right = Side{"resolved", "asset roots and archives", std::to_string(countField(report, "resolved"))};
    for (const auto& row : report.value("missingAssets", json::array())) {
        addDiff(d, "missing: " + textField(row, "path") + " (" + std::to_string(countField(row, "recordCount")) +
                       " record(s))");
    }
    for (const auto& path : report.value("unusablePaths", json::array())) {
        if (path.is_string()) addDiff(d, "unusable path: " + path.get<std::string>());
    }
    setStatus(d, missing > 0 || unusable > 0 ? "fail" : countField(report, "skippedArchives") > 0 ? "unverified" : "pass",
              std::to_string(missing) + " missing, " + std::to_string(unusable) + " unusable path(s); " +
                  std::to_string(countField(report, "skippedArchives")) + " archive(s) could not be read.");
    out.push_back(std::move(d));
}

void nifVerify(Ctx& c, std::vector<Diag>& out) {
    Diag d = begin("nif.verify", "NIF verify checks", {"structural", "real-game-data"});
    auto resolved = c.call("nif", "ResolveAsset", json::array({c.param("asset"), false, 25}));
    if (!resolved) {
        fromError(d, resolved.error());
        out.push_back(std::move(d));
        return;
    }
    if (!resolved->is_object() || !resolved->value("Found", false)) {
        setStatus(d, "skip", "No asset root or archive provides this path.");
        out.push_back(std::move(d));
        return;
    }
    const auto winner = resolved->value("Winner", json::object());
    if (textField(winner, "Kind") != "loose") {
        d.evidence.clear();
        setStatus(d, "unsupported",
                  "The winning copy is inside a BA2. nif.Verify takes a file path and extracting would write the asset cache, so only loose files are verified.");
        out.push_back(std::move(d));
        return;
    }
    const auto file = textField(winner, "Container");
    auto report = c.call("nif", "Verify", json::array({file}));
    if (!report) {
        fromError(d, report.error());
        out.push_back(std::move(d));
        return;
    }
    if (!hasField(*report, "checks") || !(*report)["checks"].is_array()) {
        setStatus(d, "error", "The verify reply had no checks.");
        out.push_back(std::move(d));
        return;
    }
    long long failed = 0;
    for (const auto& check : (*report)["checks"]) {
        const auto failure = textField(check, "failure");
        if (failure.empty()) continue;
        ++failed;
        addDiff(d, textField(check, "name") + ": " + failure);
    }
    d.left = Side{"file", textField(winner, "Provider"), file};
    d.right = Side{"failed checks", "nif.Verify", std::to_string(failed)};
    setStatus(d, failed > 0 ? "fail" : "pass",
              std::to_string(failed) + " of " + std::to_string((*report)["checks"].size()) + " check(s) failed.");
    out.push_back(std::move(d));
}

void nifRoundtrip(Ctx& c, std::vector<Diag>& out) {
    Diag d = begin("nif.roundtrip", "NIF parse and write round trip", {"real-game-data", "structural"});
    const auto resolver = c.env.assets ? c.env.assets() : nullptr;
    if (!resolver) {
        d.evidence.clear();
        setStatus(d, "skip", "No asset resolver is available.");
        out.push_back(std::move(d));
        return;
    }
    const auto key = fo4::archive::normalize_asset_path(c.param("asset"));
    const auto location = resolver->locate(key);
    if (!location) {
        d.evidence.clear();
        setStatus(d, "skip", "No asset root or archive provides " + key + ".");
        out.push_back(std::move(d));
        return;
    }
    if (stoppedForShutdown(c, d)) {
        out.push_back(std::move(d));
        return;
    }
    const auto shipped = resolver->read(*location);
    const auto nif = fo4::nif::read_nif(shipped);
    const auto rebuilt = fo4::nif::write_nif(nif);
    d.left = Side{"shipped", key, std::to_string(shipped.size()) + " bytes"};
    d.right = Side{"rewritten in memory", "read_nif then write_nif", std::to_string(rebuilt.size()) + " bytes"};
    if (rebuilt == shipped) {
        setStatus(d, "pass", "Byte-identical after read_nif and write_nif. Nothing was written to disk, and this is not a game load.");
    } else {
        const auto mismatch = std::mismatch(rebuilt.begin(), rebuilt.end(), shipped.begin(), shipped.end());
        addDiff(d, "first differing byte at offset " +
                       std::to_string(static_cast<std::size_t>(std::distance(rebuilt.begin(), mismatch.first))));
        setStatus(d, "fail", "The rewritten NIF differs from the shipped bytes.");
    }
    out.push_back(std::move(d));
}

void previsScan(Ctx& c, std::vector<Diag>& out) {
    Diag d = begin("previs.scan", "Previs scan of the load order", {"structural", "real-game-data"});
    fs::path dataDir;
    if (!dataDirReady(c, d, dataDir)) {
        out.push_back(std::move(d));
        return;
    }
    auto reply = c.call("previs", "Scan", json::array({dataDir.string()}));
    if (!reply) {
        fromError(d, reply.error());
    } else {
        const auto& report = *reply;
        const auto summary = fieldOr(report, "summary", json::object());
        const auto validCount = [](const json& value) {
            return value.is_number_unsigned() ? value.get<std::uint64_t>() <= 9007199254740991ULL
                : value.is_number_integer() && value.get<std::int64_t>() >= 0 && value.get<std::int64_t>() <= 9007199254740991LL;
        };
        for (const char* key : {"broken", "outdated", "problems", "skipped"}) {
            const auto value = fieldOr(summary, key, fieldOr(report, key, json()));
            if (((hasField(summary, key) || hasField(report, key)) && !validCount(value)) || (value.is_null() && (std::string_view(key) == "broken" || std::string_view(key) == "outdated"))) {
                setStatus(d, "error", "The scan reply had missing or invalid counts.");
                out.push_back(std::move(d));
                return;
            }
        }
        const auto broken = hasField(summary, "broken") ? countField(summary, "broken") : countField(report, "broken");
        const auto outdated =
            hasField(summary, "outdated") ? countField(summary, "outdated") : countField(report, "outdated");
        d.left = Side{"broken cells", dataDir.string(), std::to_string(broken)};
        d.right = Side{"outdated cells", dataDir.string(), std::to_string(outdated)};
        for (const char* key : {"broken", "outdated"}) {
            const auto rows = fieldOr(report, key, json::array());
            if (!rows.is_array()) continue;
            for (const auto& row : rows) {
                std::string codes;
                const auto issues = fieldOr(row, "issues", json::array());
                if (!issues.is_array()) continue;
                for (const auto& issue : issues) {
                    codes += (codes.empty() ? "" : ",") + textField(issue, "code");
                }
                addDiff(d, std::string(key) + ": " + textField(row, "cell") + " " + textField(row, "plugin") + " " + codes);
            }
        }
        const auto problems = hasField(summary, "problems") ? countField(summary, "problems") : countField(report, "problems");
        const bool truncated = truthy(fieldOr(report, "truncated", json(false)));
        const auto pending = fieldOr(report, "pendingPlugins", json());
        std::string detail = std::to_string(broken) + " broken and " + std::to_string(outdated) +
                             " outdated cell(s) found by the scan. Whether the game accepts previs is a separate, unverified question.";
        const auto skipped = hasField(summary, "skipped") ? countField(summary, "skipped") : countField(report, "skipped");
        const bool hasPending = pending.is_array() && !pending.empty();
        if (hasPending) {
            detail += " The scan reads on-disk plugins only, so unsaved edits were not seen, and " +
                      std::to_string(pending.size()) + " plugin(s) in the profile were not on disk.";
        }
        if (broken > 0 || outdated > 0) {
            setStatus(d, "fail", detail);
        } else if (problems > 0 || truncated || skipped > 0 || hasPending || (!hasField(summary, "problems") && !hasField(report, "problems")) || (!hasField(summary, "skipped") && !hasField(report, "skipped")) || !fieldOr(report, "truncated", json()).is_boolean() || !pending.is_array()) {
            setStatus(d, "unverified",
                      std::string(problems > 0 ? std::to_string(problems) + " previs data problem(s) were reported. " : "") +
                          (((!hasField(summary, "problems") && !hasField(report, "problems")) || (!hasField(summary, "skipped") && !hasField(report, "skipped")) || !fieldOr(report, "truncated", json()).is_boolean() || !pending.is_array()) ? "The scan reply omitted coverage information. " : "") +
                          (truncated ? "The cell lists were truncated. " : "") +
                          (skipped > 0 ? std::to_string(skipped) + " cell(s) were skipped as unsupported and not diagnosed. " : "") +
                          detail);
        } else {
            setStatus(d, "pass", detail);
        }
    }
    out.push_back(std::move(d));
}

void previsPlan(Ctx& c, std::vector<Diag>& out) {
    Diag d = begin("previs.plan", "Previs precombine plan", {"structural", "real-game-data"});
    auto reply = c.call("previs", "PatchPlan", json::array({c.param("cell"), c.param("plugin"), 2u, json::object(), 25u, false}));
    if (!reply) {
        fromError(d, reply.error());
        out.push_back(std::move(d));
        return;
    }
    const auto& plan = *reply;
    if (!hasField(plan, "groupCount")) {
        setStatus(d, "error", "The plan reply had no group count.");
        out.push_back(std::move(d));
        return;
    }
    d.left = Side{"planned groups", c.param("plugin"), std::to_string(countField(plan, "groupCount"))};
    d.right = Side{"eligible references", c.param("cell"), std::to_string(countField(plan, "eligibleReferences"))};
    for (const auto& group : plan.value("groups", json::array())) {
        addDiff(d, textField(group, "modelPath") + ": " + std::to_string(countField(group, "instanceCount")) + " instance(s)");
    }
    long long skippedTotal = 0;
    std::string skippedDetail;
    const auto skipped = plan.value("skipped", json::object());
    if (skipped.is_object()) {
        for (const auto& item : skipped.items()) {
            const std::string key = item.key();
            const auto count = countField(skipped, key.c_str());
            if (count <= 0) continue;
            skippedTotal += count;
            skippedDetail += (skippedDetail.empty() ? "" : ", ") + key + " " + std::to_string(count);
            addDiff(d, "skipped " + key + ": " + std::to_string(count));
        }
    }
    const auto omitted = countField(plan, "groupsOmitted");
    if (omitted > 0) addDiff(d, "groups not listed: " + std::to_string(omitted));
    const std::string omittedNote =
        omitted > 0 ? " " + std::to_string(omitted) + " group(s) are counted but not listed." : std::string{};
    if (skippedTotal > 0) {
        setStatus(d, "unverified",
                  std::to_string(skippedTotal) + " reference(s) were skipped and are not in the plan (" + skippedDetail +
                      "), so the plan is incomplete. Nothing was written." + omittedNote);
    } else {
        setStatus(d, "pass", "The plan was computed. Nothing was written." + omittedNote);
    }
    out.push_back(std::move(d));
}

void previsDryRun(Ctx& c, std::vector<Diag>& out) {
    Diag d = begin("previs.dryRunGenerate", "Previs generate dry run", {"structural", "real-game-data"});
    json args = json::array({c.param("plugin"), c.param("cell"), "", "", 1, false, false, json::object(), false});
    auto reply = c.call("previs", "Generate", args);
    if (!reply) {
        fromError(d, reply.error());
        out.push_back(std::move(d));
        return;
    }
    const auto& result = *reply;
    if (!result.is_object() || !result.value("dryRun", false)) {
        setStatus(d, "fail", "The host did not report a dry run, so the result cannot be trusted as write-free.");
        out.push_back(std::move(d));
        return;
    }
    d.left = Side{"references", c.param("cell"), std::to_string(countField(result, "references"))};
    d.right = Side{"mesh ids", c.param("plugin"), std::to_string(countField(result, "meshIds"))};
    for (const auto& group : result.value("groups", json::array())) {
        addDiff(d, "would write " + textField(group, "path"));
    }
    static const std::set<std::string> expectedInDryRun{"dry_run_only", "tome_not_written", "cdx_row_missing"};
    std::string limitationText;
    for (const auto& item : result.value("limitations", json::array())) {
        const auto text = item.is_string() ? item.get<std::string>() : item.dump();
        addDiff(d, "limitation: " + text);
        if (expectedInDryRun.contains(text)) continue;
        limitationText += (limitationText.empty() ? "" : ", ") + text;
    }
    const std::string planned =
        "The dry run planned " + std::to_string(countField(result, "groups")) + " group(s); nothing was written.";
    if (limitationText.empty()) {
        setStatus(d, "pass", planned);
    } else {
        setStatus(d, "unverified", planned + " The result is incomplete: " + limitationText + ".");
    }
    out.push_back(std::move(d));
}

void lodWorlds(Ctx& c, std::vector<Diag>& out) {
    Diag d = begin("lod.worlds", "LOD worldspaces", {"structural", "real-game-data"});
    const auto input = lodInput(c, d);
    if (!input) {
        out.push_back(std::move(d));
        return;
    }
    auto reply = c.call("lod", "Worlds", lodArgs(*input, {}));
    if (!reply) {
        fromError(d, reply.error());
    } else if (!reply->is_array()) {
        setStatus(d, "error", "The worlds reply was not a list.");
    } else if (reply->empty()) {
        setStatus(d, "skip", "The load order has no worldspace the LOD generator can use.");
    } else {
        d.left = Side{"worlds", input->dataDir, std::to_string(reply->size())};
        for (const auto& world : *reply) {
            addDiff(d, textField(world, "name") + " stride " + std::to_string(countField(world, "stride")));
        }
        setStatus(d, "pass", std::to_string(reply->size()) + " world(s) listed.");
    }
    out.push_back(std::move(d));
}

void lodPlan(Ctx& c, std::vector<Diag>& out) {
    Diag d = begin("lod.plan", "LOD rebake plan", {"structural", "real-game-data"});
    const auto input = lodInput(c, d);
    if (!input) {
        out.push_back(std::move(d));
        return;
    }
    const auto ledger = c.param("ledger");
    auto reply = c.call("lod", "Plan", lodArgs(*input, {ledger}));
    if (!reply) {
        fromError(d, reply.error());
    } else if (!hasField(*reply, "plan") || !(*reply)["plan"].is_array()) {
        setStatus(d, "error", "The plan reply had no tile list.");
    } else {
        long long rebuild = 0;
        for (const auto& tile : (*reply)["plan"]) rebuild += truthy(fieldOr(tile, "rebuild", json(false))) ? 1 : 0;
        d.left = Side{"tiles", input->dataDir, std::to_string((*reply)["plan"].size())};
        d.right = Side{"to rebuild", ledger.empty() ? "no ledger given" : "given ledger", std::to_string(rebuild)};
        setStatus(d, "pass",
                  ledger.empty() ? "No ledger was given, so every tile counts as needing a rebuild."
                                 : "The plan was computed against the given ledger.");
    }
    out.push_back(std::move(d));
}

void lodDryRun(Ctx& c, std::vector<Diag>& out) {
    Diag d = begin("lod.dryRunGenerate", "LOD generate dry run for one world", {"structural", "real-game-data"});
    const auto input = lodInput(c, d);
    if (!input) {
        out.push_back(std::move(d));
        return;
    }
    const auto world = c.param("world");
    if (c.cancelled()) {
        c.cancelledRun = true;
        setStatus(d, "skip", "cancelled before start");
        out.push_back(std::move(d));
        return;
    }
    auto reply = c.call("lod", "GenerateScoped",lodArgs(*input, {world, c.param("ledger"), false, 0, 0}));
    if (!reply) {
        fromError(d, reply.error());
        out.push_back(std::move(d));
        return;
    }
    const auto report = reply->is_object() ? reply->value("report", json::object()) : json::object();
    if (!report.is_object() || report.empty()) {
        setStatus(d, "error", "The generate reply had no report.");
        out.push_back(std::move(d));
        return;
    }
    const auto written = countField(report, "written");
    const auto failed = countField(report, "failed");
    d.left = Side{"would write", world, std::to_string(countField(report, "wouldWrite"))};
    d.right = Side{"would overwrite", world, std::to_string(countField(report, "wouldOverwrite"))};
    addDiff(d, "would delete: " + std::to_string(countField(report, "wouldDelete")));
    addDiff(d, "unsupported candidates: " + std::to_string(countField(report, "unsupportedCandidates")));
    addDiff(d, "objects missing meshes: " + std::to_string(countField(report, "objectMissingMeshes")));
    if (truthy(report.value("cancelled", json(false)))) {
        c.cancelledRun = true;
        setStatus(d, "skip", "The run was cancelled before it finished, so the report is partial.");
    } else if (truthy(report.value("apply", json(false))) || written > 0) {
        setStatus(d, "fail", "The host reported writes during a dry run.");
    } else if (failed > 0) {
        setStatus(d, "fail", std::to_string(failed) + " tile(s) failed to plan.");
    } else {
        setStatus(d, "pass", "The dry run planned the tiles of " + world + "; nothing was written.");
    }
    out.push_back(std::move(d));
}

void lodVerify(Ctx& c, std::vector<Diag>& out) {
    Diag d = begin("lod.verifyWorld", "LOD tile verification for one world", {"structural", "real-game-data"});
    const auto input = lodInput(c, d);
    if (!input) {
        out.push_back(std::move(d));
        return;
    }
    const auto world = c.param("world");
    const auto ledger = c.param("ledger");
    auto reply = c.call("lod", "VerifyWorld", lodArgs(*input, {world, ledger}));
    if (!reply) {
        fromError(d, reply.error());
        out.push_back(std::move(d));
        return;
    }
    const auto& report = *reply;
    if (!hasField(report, "missing") || !hasField(report, "ok")) {
        setStatus(d, "error", "The verify reply had no counts.");
        out.push_back(std::move(d));
        return;
    }
    const auto missing = countField(report, "missing");
    const auto partial = countField(report, "partial");
    const auto stale = countField(report, "stale");
    const auto unverified = countField(report, "unverified");
    d.left = Side{"ok", world, std::to_string(countField(report, "ok"))};
    d.right = Side{"missing or partial", world, std::to_string(missing + partial)};
    addDiff(d, "stale: " + std::to_string(stale));
    addDiff(d, "unverified: " + std::to_string(unverified));
    const auto texturesMissing = countField(report, "texturesMissing");
    const auto texturesStale = countField(report, "texturesStale");
    const auto texturesUnverified = countField(report, "texturesUnverified");
    const auto texturesLost = ledger.empty() ? 0 : std::min(countField(report, "texturesMissingRecorded"), texturesMissing);
    const auto texturesNeverGenerated = texturesMissing - texturesLost;
    addDiff(d, "texture maps missing: " + std::to_string(texturesMissing) +
                   " (recorded in ledger: " + std::to_string(texturesLost) + "), stale: " + std::to_string(texturesStale) +
                   ", unverified: " + std::to_string(texturesUnverified));
    const bool staleAgainstLedger = !ledger.empty() && stale > 0;
    const bool texturesBroken = !ledger.empty() && texturesStale > 0;
    const bool texturesDeleted = texturesLost > 0;
    if (missing > 0 || partial > 0 || staleAgainstLedger || texturesBroken || texturesDeleted) {
        auto text = ledger.empty() ? std::to_string(missing) + " tile(s) missing and " + std::to_string(partial) + " partial."
                                   : std::to_string(missing) + " tile(s) missing, " + std::to_string(partial) +
                                         " partial and " + std::to_string(stale) + " stale against the ledger.";
        if (texturesBroken) text += " " + std::to_string(texturesStale) + " texture map(s) stale against the ledger.";
        if (texturesDeleted) text += " " + std::to_string(texturesLost) + " recorded texture map(s) are missing.";
        setStatus(d, "fail", text);
    } else if (unverified > 0 || texturesUnverified > 0 || texturesNeverGenerated > 0 || texturesStale > 0 ||
               (ledger.empty() && stale > 0)) {
        setStatus(d, "unverified",
                  ledger.empty() ? "No ledger was given, so every tile reads as stale and nothing can be called verified."
                                 : std::to_string(unverified) + " tile(s) and " +
                                       std::to_string(texturesUnverified + texturesNeverGenerated) +
                                       " texture map(s) could not be verified; " + std::to_string(texturesNeverGenerated) +
                                       " map(s) were never generated.");
    } else {
        setStatus(d, "pass", "Every tile of " + world + " is present and matches the ledger.");
    }
    out.push_back(std::move(d));
}

std::vector<ToolSpec> buildTable() {
    const std::vector<std::string> data{"structural", "real-game-data"};
    const auto tool = makeTool;
    const ParamSpec plugin{"plugin", "Plugin file name", "plugin", true};
    const ParamSpec cell{"cell", "Cell form key", "text", true};
    const ParamSpec world{"world", "Worldspace name", "text", true};
    const ParamSpec ledger{"ledger", "Ledger text (optional)", "text", false};

    std::vector<ToolSpec> table;
    table.push_back(tool("env.context", "environment", "Environment, versions and load order",
                         "Shows the versions, Data folder, MO2 profile and load order the tools will use. No scan.",
                         {"structural", "real-game-data"}, {}, noRequirement, envContext));
    table.push_back(tool("env.assetPrecedence", "environment", "Which provider wins an asset path",
                         "Lists every loose root and archive that supplies a path and which one FO4IDE puts first.",
                         {"real-game-data"}, {{"asset", "Asset path (Data relative)", "asset", true}}, needCall,
                         assetPrecedence));
    table.push_back(tool("plugin.validate", "plugins", "Plugin structural validation",
                         "Runs the structural validator on a loaded plugin.", {"structural", "real-game-data"},
                         {plugin}, needCall, pluginValidate));
    table.push_back(tool("plugin.checkErrors", "plugins", "Plugin cross-record error check",
                         "Runs the link and record error check on a loaded plugin.", {"structural"}, {plugin},
                         needCall, pluginCheckErrors));
    auto roundtrip = tool("plugin.roundtrip", "plugins", "Plugin write round trip",
                          "Rebuilds a plugin from its parsed structure and compares the bytes. Writes only a fresh folder inside the output folder you choose.",
                          {"structural", "real-game-data"}, {plugin}, noRequirement, pluginRoundtrip);
    roundtrip.readOnly = false;
    roundtrip.writes = true;
    roundtrip.needsOutputDir = true;
    roundtrip.expensive = true;
    table.push_back(std::move(roundtrip));
    auto audit = tool("assets.audit", "assets", "Asset references of a plugin",
                      "Counts the assets a plugin references and lists the first missing ones.", {"real-game-data"},
                      {plugin}, needCall, assetsAudit);
    audit.expensive = true;
    table.push_back(std::move(audit));
    table.push_back(tool("nif.verify", "assets", "NIF verify checks",
                         "Runs the NIF verify checks on the winning loose copy of an asset.", data,
                         {{"asset", "NIF path (Data relative)", "asset", true}}, needCall, nifVerify));
    table.push_back(tool("nif.roundtrip", "assets", "NIF parse and write round trip",
                         "Reads a shipped NIF, rewrites it in memory and compares the bytes. Writes nothing.",
                         {"real-game-data", "structural"}, {{"asset", "NIF path (Data relative)", "asset", true}},
                         needAssets, nifRoundtrip));
    auto scan = tool("previs.scan", "previs", "Previs scan of the load order",
                     "Finds broken and outdated previs cells in the saved load order.", data, {}, needCall, previsScan);
    scan.expensive = true;
    scan.inGame = true;
    table.push_back(std::move(scan));
    auto plan = tool("previs.plan", "previs", "Previs precombine plan",
                     "Computes the precombine plan for one cell without writing.", data, {plugin, cell}, needCall,
                     previsPlan);
    plan.inGame = true;
    table.push_back(std::move(plan));
    auto dry = tool("previs.dryRunGenerate", "previs", "Previs generate dry run",
                    "Runs the previs generator with apply off and reports what it would write.", data,
                    {plugin, cell}, needCall, previsDryRun);
    dry.expensive = true;
    dry.inGame = true;
    table.push_back(std::move(dry));
    auto worlds = tool("lod.worlds", "lod", "LOD worldspaces", "Lists the worldspaces the LOD generator can use.",
                       data, {}, needProfile, lodWorlds);
    worlds.inGame = true;
    table.push_back(std::move(worlds));
    auto lodplan = tool("lod.plan", "lod", "LOD rebake plan", "Plans which LOD tiles need a rebake. Read only.", data,
                        {ledger}, needProfile, lodPlan);
    lodplan.inGame = true;
    table.push_back(std::move(lodplan));
    auto lodDry = tool("lod.dryRunGenerate", "lod", "LOD generate dry run for one world",
                       "Plans one world's LOD tiles with apply off. Can be cancelled.", data, {world, ledger},
                       needProfile, lodDryRun);
    lodDry.expensive = true;
    lodDry.cancellable = true;
    lodDry.cancelTarget = "lod";
    lodDry.cancelMethod = "CancelScoped";
    lodDry.inGame = true;
    table.push_back(std::move(lodDry));
    auto lodVer = tool("lod.verifyWorld", "lod", "LOD tile verification for one world",
                       "Checks one world's LOD tiles against a ledger.", data, {world, ledger}, needProfile, lodVerify);
    lodVer.expensive = true;
    lodVer.inGame = true;
    table.push_back(std::move(lodVer));
    appendAssetTools(table);
    appendReferenceTools(table);
    return table;
}

}  // namespace

ToolSpec makeTool(const char* id, const char* group, const char* title, const char* description,
                  std::vector<std::string> evidence, std::vector<ParamSpec> params,
                  std::function<std::string(const DebugToolsEnv&)> unavailable,
                  std::function<void(Ctx&, std::vector<Diag>&)> run) {
    ToolSpec spec;
    spec.id = id;
    spec.group = group;
    spec.title = title;
    spec.description = description;
    spec.evidence = std::move(evidence);
    spec.params = std::move(params);
    spec.unavailable = std::move(unavailable);
    spec.run = std::move(run);
    return spec;
}

std::string Ctx::param(const std::string& name) const {
    if (!params.is_object()) return {};
    const auto it = params.find(name);
    return it != params.end() && it->is_string() ? it->get<std::string>() : std::string{};
}

Result<json> Ctx::call(const std::string& target, const std::string& method, const json& args) const {
    if (!env.call) return std::unexpected(Error{"NoHost", "the host call bridge is not wired"});
    try {
        auto reply = env.call(target, method, args);
        if (!reply) return reply;
        if (!reply->is_string()) return reply;
        auto parsed = json::parse(reply->get<std::string>(), nullptr, false);
        if (parsed.is_discarded()) {
            return std::unexpected(Error{"BadReply", target + "." + method + " returned text that is not JSON"});
        }
        return parsed;
    } catch (const std::exception& error) {
        return std::unexpected(Error{"Exception", target + "." + method + ": " + error.what()});
    }
}

json Diag::toJson() const {
    static const std::set<std::string> known{"pass", "fail", "skip", "unsupported", "unverified", "error"};
    const auto side = [](const std::optional<Side>& value) -> json {
        if (!value) return nullptr;
        return {{"label", value->label}, {"source", value->source}, {"value", value->value}};
    };
    const auto emitted = known.contains(status) ? status : std::string("error");
    const bool ranNothing = emitted == "skip" || emitted == "error" || emitted == "unsupported";
    return {{"id", id},
            {"title", title},
            {"status", emitted},
            {"evidence", ranNothing ? std::vector<std::string>{} : evidence},
            {"left", side(left)},
            {"right", side(right)},
            {"diffs", diffs},
            {"detail", detail},
            {"durationMs", durationMs},
            {"outputs", outputs}};
}

Diag inGameRow(const std::string& toolId) {
    Diag row;
    row.id = toolId + ".inGameAcceptance";
    row.title = "In-game acceptance and RVIS/visibility behaviour";
    row.status = "unverified";
    row.detail =
        "Nothing here was observed in the game. Radicle/docs/PREVIS.md:1013,1051 says in-game previs and RVIS behaviour are not proven, and docs/lodgen-plan.md:44-49 says in-game load stays the only real acceptance gate.";
    return row;
}

const std::vector<ToolSpec>& toolTable() {
    static const std::vector<ToolSpec> table = buildTable();
    return table;
}

const ToolSpec* findTool(std::string_view id) {
    for (const auto& spec : toolTable()) {
        if (spec.id == id) return &spec;
    }
    return nullptr;
}

}  // namespace fo4ide::app::debugtools
