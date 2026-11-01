#include "RadiclePluginSupport.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fo4/graph/check_errors.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/schema/preflight.hpp>

namespace fo4ide::plugin {

using support::editorId;

namespace {

std::size_t sizeArgument(const nlohmann::json& args,
                         std::size_t index,
                         std::size_t fallback,
                         std::size_t maximum) {
    if (!args.is_array() || index >= args.size() || !args[index].is_number_integer()) {
        return fallback;
    }
    if (!args[index].is_number_unsigned() && args[index].get<std::int64_t>() < 0) return fallback;
    return static_cast<std::size_t>(std::min(args[index].get<std::uint64_t>(),
                                           static_cast<std::uint64_t>(maximum)));
}

enum class WorklistKind { all, added, overrides, winning, losing, conflicted };

std::optional<WorklistKind> worklistKind(std::string_view text) {
    if (text.empty() || text == "all") return WorklistKind::all;
    if (text == "added") return WorklistKind::added;
    if (text == "overrides") return WorklistKind::overrides;
    if (text == "winning") return WorklistKind::winning;
    if (text == "losing") return WorklistKind::losing;
    if (text == "conflicted") return WorklistKind::conflicted;
    return std::nullopt;
}

Result<fo4::FormKey> formKeyArgument(const nlohmann::json& args) {
    const auto text = support::stringArgument(args, 0);
    if (text.empty()) {
        return std::unexpected(Error{"FormKeyMissing", "FormKey is empty"});
    }
    try {
        return fo4::FormKey::parse(text);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"InvalidFormKey", error.what()});
    }
}

using SubrecordMap = std::map<std::pair<std::string, std::size_t>, std::vector<std::byte>>;

SubrecordMap subrecordMap(const fo4::Record* record) {
    SubrecordMap out;
    if (record == nullptr) return out;
    std::map<std::string, std::size_t> ordinals;
    for (const auto& subrecord : record->subrecords) {
        const auto signature = subrecord.signature.to_string();
        out.emplace(std::make_pair(signature, ordinals[signature]++), subrecord.data);
    }
    return out;
}

std::size_t changedSubrecordCount(const fo4::Record* previous, const fo4::Record* current) {
    const auto before = subrecordMap(previous);
    const auto after = subrecordMap(current);
    std::set<std::pair<std::string, std::size_t>> keys;
    for (const auto& [key, value] : before) {
        static_cast<void>(value);
        keys.insert(key);
    }
    for (const auto& [key, value] : after) {
        static_cast<void>(value);
        keys.insert(key);
    }

    std::size_t changed = 0;
    for (const auto& key : keys) {
        const auto lhs = before.find(key);
        const auto rhs = after.find(key);
        if (lhs == before.end() || rhs == after.end() || lhs->second != rhs->second) ++changed;
    }
    return changed;
}

std::string lastModifiedIso(const std::filesystem::path& path) {
    std::error_code ec;
    const auto fileTime = std::filesystem::last_write_time(path, ec);
    if (ec) return {};
    const auto sysTime = std::chrono::clock_cast<std::chrono::system_clock>(fileTime);
    const auto seconds = std::chrono::time_point_cast<std::chrono::seconds>(sysTime);
    const std::time_t tt = std::chrono::system_clock::to_time_t(seconds);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &tt);
#else
    gmtime_r(&tt, &tm);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof buffer, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buffer;
}

}

Result<nlohmann::json> RadiclePluginBackend::getDependencies(const nlohmann::json& arguments) const {
    auto keyResult = formKeyArgument(arguments);
    if (!keyResult) return std::unexpected(keyResult.error());
    const auto& key = *keyResult;

    OpLock lock(*this, OpLock::Mode::Read);
    const auto* sourceWinner = (*graph_).winner(key);
    if (sourceWinner == nullptr) {
        return std::unexpected(Error{
            "RecordNotFound",
            "No loaded plugin contains this FormKey: " + key.to_string()
        });
    }

    nlohmann::json rows = nlohmann::json::array();
    std::set<std::string> seen;
    for (const auto& hit : (*graph_).references_from(key)) {
        if (hit.plugin != sourceWinner->plugin) continue;
        const auto targetText = hit.target.to_string();
        if (!seen.insert(targetText).second) continue;

        const auto* targetWinner = (*graph_).winner(hit.target);
        std::string editor;
        std::string type;
        std::string pluginName = std::string(hit.target.owner().filename());
        if (targetWinner != nullptr) {
            pluginName = std::string(targetWinner->plugin.filename());
            type = targetWinner->signature.to_string();
            const auto* loaded = findPlugin(targetWinner->plugin.filename());
            if (loaded != nullptr) {
                if (const auto* record = fo4::find_record(loaded->plugin, hit.target)) {
                    editor = editorId(*record);
                }
            }
        }

        rows.push_back({
            {"Kind", targetWinner == nullptr ? "missing" : "link"},
            {"FormKey", targetText},
            {"EditorId", editor},
            {"Type", type},
            {"Plugin", pluginName}
        });
    }
    return nlohmann::json(rows.dump());
}

Result<nlohmann::json> RadiclePluginBackend::getProblems(const nlohmann::json& arguments) const {
    auto keyResult = formKeyArgument(arguments);
    if (!keyResult) return std::unexpected(keyResult.error());
    const auto& key = *keyResult;

    OpLock lock(*this, OpLock::Mode::Read);
    const auto* sourceWinner = (*graph_).winner(key);
    if (sourceWinner == nullptr) {
        return std::unexpected(Error{
            "RecordNotFound",
            "No loaded plugin contains this FormKey: " + key.to_string()
        });
    }

    nlohmann::json rows = nlohmann::json::array();
    std::set<std::pair<std::string, std::string>> seen;
    for (const auto& hit : (*graph_).references_from(key)) {
        if (hit.plugin != sourceWinner->plugin || (*graph_).winner(hit.target) != nullptr) continue;
        const auto target = hit.target.to_string();
        const auto signature = hit.subrecord.to_string();
        if (!seen.emplace(signature, target).second) continue;
        rows.push_back({
            {"Code", std::string(fo4::check_code_name(fo4::CheckCode::unresolved_link))},
            {"Detail", ""},
            {"Severity", "critical"},
            {"Description", "Missing " + signature + " FormLink target " + target}
        });
    }

    const auto* loaded = findPlugin(sourceWinner->plugin.filename());
    const auto* record = loaded == nullptr ? nullptr : fo4::find_record(loaded->plugin, key);
    if (record != nullptr) {
        for (const auto& issue : fo4::schema::preflight_record(*record, loaded->plugin.header.tes4_flags.is_localized())) {
            rows.push_back({
                {"Code", std::string(fo4::check_code_name(fo4::CheckCode::schema_preflight))},
                {"Detail", std::string(issue.code)},
                {"Severity", issue.severity == fo4::schema::PreflightSeverity::error
                                     ? "critical"
                                     : "warning"},
                {"Description", issue.path + ": " + issue.message}
            });
        }
    }

    return nlohmann::json(rows.dump());
}

Result<nlohmann::json> RadiclePluginBackend::getHistory(const nlohmann::json& arguments) const {
    auto keyResult = formKeyArgument(arguments);
    if (!keyResult) return std::unexpected(keyResult.error());
    const auto& key = *keyResult;

    OpLock lock(*this, OpLock::Mode::Read);
    const auto& versions = (*graph_).versions(key);
    if (versions.empty()) {
        return std::unexpected(Error{
            "RecordNotFound",
            "No loaded plugin contains this FormKey: " + key.to_string()
        });
    }
    nlohmann::json rows = nlohmann::json::array();
    const fo4::Record* previous = nullptr;

    for (std::size_t i = 0; i < versions.size(); ++i) {
        const auto& version = versions[i];
        const auto* loaded = findPlugin(version.plugin.filename());
        const auto* record = loaded == nullptr ? nullptr : fo4::find_record(loaded->plugin, key);
        rows.push_back({
            {"Plugin", std::string(version.plugin.filename())},
            {"LoadOrder", version.load_order},
            {"Action", i == 0 ? "Origin" : (i + 1 == versions.size() ? "Winning override" : "Override")},
            {"ChangedFields", i == 0 ? 0 : changedSubrecordCount(previous, record)},
            {"LastModified", loaded == nullptr ? std::string{} : lastModifiedIso(loaded->path)}
        });
        previous = record;
    }
    return nlohmann::json(rows.dump());
}

Result<nlohmann::json> RadiclePluginBackend::getPluginWorklist(
    const nlohmann::json& arguments) const {
    const auto pluginName = support::stringArgument(arguments, 0);
    const auto kindText = support::stringArgument(arguments, 1);
    auto signatureText = support::stringArgument(arguments, 2);
    std::transform(signatureText.begin(), signatureText.end(), signatureText.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
    const auto limit = sizeArgument(arguments, 3, 200, 5000);
    const auto offset = sizeArgument(arguments, 4, 0, 10000000);

    if (pluginName.empty()) {
        return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    }
    const auto kind = worklistKind(kindText);
    if (!kind.has_value()) {
        return std::unexpected(Error{
            "InvalidWorklistFilter",
            "Worklist filter must be all, added, overrides, winning, losing or conflicted: " +
                kindText
        });
    }
    std::optional<fo4::Signature> signature;
    if (!signatureText.empty()) {
        if (signatureText.size() != 4) {
            return std::unexpected(Error{
                "InvalidSignature", "Record signature must be four characters: " + signatureText});
        }
        signature = fo4::Signature::from_string(signatureText);
    }

    OpLock lock(*this, OpLock::Mode::Read);
    const auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    }

    std::size_t added = 0;
    std::size_t overrides = 0;
    std::size_t winning = 0;
    std::size_t losing = 0;
    std::size_t conflicted = 0;
    std::size_t total = 0;
    nlohmann::json rows = nlohmann::json::array();

    for (const auto* record : fo4::collect_records(loaded->plugin)) {
        if (record->header.signature == fo4::sig_tes4 || record->form_key.owner().empty()) continue;
        if (signature.has_value() && record->header.signature != *signature) continue;

        const auto& versions = (*graph_).versions(record->form_key);
        const auto self = std::find_if(versions.begin(), versions.end(), [&](const auto& version) {
            return version.plugin == loaded->plugin.key;
        });
        if (self == versions.end()) {
            return std::unexpected(Error{
                "ConflictGraphOutOfDate",
                "Conflict graph has no version of " + record->form_key.to_string() + " in " +
                    loaded->name
            });
        }

        const bool isAdded = record->form_key.owner() == loaded->plugin.key;
        const bool isWinner = self + 1 == versions.end();
        const bool isConflicted = versions.size() > 1;
        added += isAdded ? 1 : 0;
        overrides += isAdded ? 0 : 1;
        winning += isWinner ? 1 : 0;
        losing += isWinner ? 0 : 1;
        conflicted += isConflicted ? 1 : 0;

        bool include = true;
        switch (*kind) {
            case WorklistKind::all: break;
            case WorklistKind::added: include = isAdded; break;
            case WorklistKind::overrides: include = !isAdded; break;
            case WorklistKind::winning: include = isWinner; break;
            case WorklistKind::losing: include = !isWinner; break;
            case WorklistKind::conflicted: include = isConflicted; break;
        }
        if (!include) continue;
        const auto index = total++;
        if (index < offset || index - offset >= limit) continue;

        nlohmann::json chain = nlohmann::json::array();
        nlohmann::json overriddenBy = nlohmann::json::array();
        for (auto it = versions.begin(); it != versions.end(); ++it) {
            chain.push_back(std::string(it->plugin.filename()));
            if (it > self) overriddenBy.push_back(std::string(it->plugin.filename()));
        }

        rows.push_back({
            {"FormKey", record->form_key.to_string()},
            {"EditorID", editorId(*record)},
            {"Type", record->header.signature.to_string()},
            {"Kind", isAdded ? "Added" : "Override"},
            {"OverrideDepth", static_cast<std::size_t>(self - versions.begin())},
            {"Origin", std::string(record->form_key.owner().filename())},
            {"OriginLoaded", findPlugin(record->form_key.owner().filename()) != nullptr},
            {"Winner", std::string(versions.back().plugin.filename())},
            {"IsWinner", isWinner},
            {"OverriddenBy", std::move(overriddenBy)},
            {"Plugins", std::move(chain)}
        });
    }

    return nlohmann::json(nlohmann::json{
        {"Plugin", loaded->name},
        {"Total", total},
        {"Offset", std::min(offset, total)},
        {"Counts", {
            {"Added", added},
            {"Overrides", overrides},
            {"Winning", winning},
            {"Losing", losing},
            {"Conflicted", conflicted}
        }},
        {"Rows", std::move(rows)}
    }.dump());
}

}
