#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <cstddef>
#include <exception>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fo4/plugin/reader.hpp>

namespace fo4ide::plugin {
namespace {

std::string stringArgument(const nlohmann::json& args, std::size_t index) {
    if (!args.is_array() || index >= args.size() || !args[index].is_string()) return {};
    return args[index].get<std::string>();
}

Result<fo4::FormKey> formKeyArgument(const nlohmann::json& args) {
    const auto text = stringArgument(args, 0);
    if (text.empty()) {
        return std::unexpected(Error{"FormKeyMissing", "FormKey is empty"});
    }
    try {
        return fo4::FormKey::parse(text);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"InvalidFormKey", error.what()});
    }
}

std::string editorId(const fo4::Record& record) {
    for (const auto& subrecord : record.subrecords) {
        if (subrecord.signature != fo4::sig_edid || subrecord.data.empty()) continue;
        const auto* begin = reinterpret_cast<const char*>(subrecord.data.data());
        const auto* end = begin + subrecord.data.size();
        const auto* nul = std::find(begin, end, '\0');
        return std::string(begin, nul);
    }
    return {};
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

    std::scoped_lock lock(mutex_);
    const auto* sourceWinner = graph_.winner(key);
    if (sourceWinner == nullptr) {
        return std::unexpected(Error{
            "RecordNotFound",
            "No loaded plugin contains this FormKey: " + key.to_string()
        });
    }

    nlohmann::json rows = nlohmann::json::array();
    std::set<std::string> seen;
    for (const auto& hit : graph_.references_from(key)) {
        if (hit.plugin != sourceWinner->plugin) continue;
        const auto targetText = hit.target.to_string();
        if (!seen.insert(targetText).second) continue;

        const auto* targetWinner = graph_.winner(hit.target);
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

    std::scoped_lock lock(mutex_);
    const auto* sourceWinner = graph_.winner(key);
    if (sourceWinner == nullptr) {
        return std::unexpected(Error{
            "RecordNotFound",
            "No loaded plugin contains this FormKey: " + key.to_string()
        });
    }

    nlohmann::json rows = nlohmann::json::array();
    std::set<std::pair<std::string, std::string>> seen;
    for (const auto& hit : graph_.references_from(key)) {
        if (hit.plugin != sourceWinner->plugin || graph_.winner(hit.target) != nullptr) continue;
        const auto target = hit.target.to_string();
        const auto signature = hit.subrecord.to_string();
        if (!seen.emplace(signature, target).second) continue;
        rows.push_back({
            {"Severity", "critical"},
            {"Description", "Missing " + signature + " FormLink target " + target}
        });
    }
    return nlohmann::json(rows.dump());
}

Result<nlohmann::json> RadiclePluginBackend::getHistory(const nlohmann::json& arguments) const {
    auto keyResult = formKeyArgument(arguments);
    if (!keyResult) return std::unexpected(keyResult.error());
    const auto& key = *keyResult;

    std::scoped_lock lock(mutex_);
    const auto& versions = graph_.versions(key);
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

}
