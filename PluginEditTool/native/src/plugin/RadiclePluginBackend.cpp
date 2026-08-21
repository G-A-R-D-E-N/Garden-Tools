#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <exception>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <fo4/plugin/reader.hpp>
#include <fo4/plugin/writer.hpp>
#include <fo4/schema/form_links.hpp>

namespace fo4ide::plugin {
namespace {

std::string stringArgument(const nlohmann::json& args, std::size_t index) {
    if (!args.is_array() || index >= args.size() || !args[index].is_string()) return {};
    return args[index].get<std::string>();
}

std::string pluginKind(const fo4::Plugin& plugin) {
    if (plugin.header.tes4_flags.is_small_file()) return "light";
    if (plugin.header.tes4_flags.is_master()) return "master";
    return "plugin";
}

std::pair<std::string, std::string> splitTreePath(std::string_view path) {
    const auto slash = path.find_first_of("\\/");
    if (slash == std::string_view::npos) return {std::string(path), {}};
    const auto second = path.find_first_of("\\/", slash + 1);
    if (second == std::string_view::npos) {
        return {std::string(path.substr(0, slash)), std::string(path.substr(slash + 1))};
    }
    return {std::string(path.substr(0, slash)), std::string(path.substr(slash + 1, second - slash - 1))};
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

std::string asciiLower(std::string_view value) {
    std::string out(value);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return out;
}

bool containsInsensitive(std::string_view value, std::string_view needle) {
    if (needle.empty()) return true;
    const auto haystackLower = asciiLower(value);
    const auto needleLower = asciiLower(needle);
    return haystackLower.find(needleLower) != std::string::npos;
}

std::string subrecordValue(const fo4::Subrecord& subrecord) {
    if (subrecord.signature == fo4::sig_edid) {
        if (subrecord.data.empty()) return "<empty>";
        const auto* begin = reinterpret_cast<const char*>(subrecord.data.data());
        const auto* end = begin + subrecord.data.size();
        const auto* nul = std::find(begin, end, '\0');
        return std::string(begin, nul);
    }
    if (subrecord.data.empty()) return "<empty>";

    constexpr std::size_t maxShown = 48;
    const auto shown = std::min(maxShown, subrecord.data.size());
    std::ostringstream out;
    out << subrecord.data.size() << " B: ";
    for (std::size_t i = 0; i < shown; ++i) {
        if (i != 0) out << ' ';
        out << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
            << std::to_integer<unsigned int>(subrecord.data[i]);
    }
    if (shown < subrecord.data.size()) out << " ...";
    return out.str();
}

const fo4::Subrecord* nthSubrecord(const fo4::Record& record,
                                   std::string_view signature,
                                   std::size_t ordinal) {
    std::size_t found = 0;
    for (const auto& subrecord : record.subrecords) {
        if (subrecord.signature.to_string() != signature) continue;
        if (found++ == ordinal) return &subrecord;
    }
    return nullptr;
}

nlohmann::json matrixStatuses(const std::vector<const fo4::Subrecord*>& cells) {
    nlohmann::json statuses = nlohmann::json::array();
    std::vector<std::size_t> present;
    present.reserve(cells.size());
    for (std::size_t i = 0; i < cells.size(); ++i) {
        if (cells[i] != nullptr) present.push_back(i);
    }

    bool allSame = present.size() > 1;
    if (allSame) {
        const auto& first = cells[present.front()]->data;
        for (const auto index : present) {
            if (cells[index]->data != first) {
                allSame = false;
                break;
            }
        }
    }

    const auto winner = present.empty() ? cells.size() : present.back();
    for (std::size_t i = 0; i < cells.size(); ++i) {
        if (cells[i] == nullptr) {
            statuses.push_back("notdefined");
        } else if (present.size() == 1) {
            statuses.push_back("only");
        } else if (allSame) {
            statuses.push_back(i == present.front() ? "master" : "identical");
        } else if (i == winner) {
            statuses.push_back("win");
        } else if (i == present.front()) {
            statuses.push_back("master");
        } else if (cells[i]->data == cells[winner]->data) {
            statuses.push_back("override");
        } else {
            statuses.push_back("lose");
        }
    }
    return statuses;
}

bool matrixDiffers(const std::vector<const fo4::Subrecord*>& cells) {
    if (cells.empty()) return false;
    const auto present = static_cast<std::size_t>(
        std::count_if(cells.begin(), cells.end(), [](const auto* cell) { return cell != nullptr; }));
    if (present != cells.size()) return true;
    if (present < 2) return false;
    const auto& first = cells.front()->data;
    return std::any_of(cells.begin() + 1, cells.end(), [&](const auto* cell) {
        return cell->data != first;
    });
}

nlohmann::json buildRawMatrixRows(const std::vector<const fo4::Record*>& records) {
    using SubrecordKey = std::pair<std::string, std::size_t>;
    std::set<SubrecordKey> keys;
    for (const auto* record : records) {
        if (record == nullptr) continue;
        std::map<std::string, std::size_t> ordinals;
        for (const auto& subrecord : record->subrecords) {
            const auto signature = subrecord.signature.to_string();
            keys.emplace(signature, ordinals[signature]++);
        }
    }

    nlohmann::json rows = nlohmann::json::array();
    nlohmann::json counts = nlohmann::json::array();
    bool countsDiffer = false;
    std::size_t firstCount = 0;
    bool haveFirst = false;
    for (const auto* record : records) {
        if (record == nullptr) {
            counts.push_back("");
            countsDiffer = true;
            continue;
        }
        const auto count = record->subrecords.size();
        counts.push_back(std::to_string(count));
        if (!haveFirst) {
            firstCount = count;
            haveFirst = true;
        } else if (count != firstCount) {
            countsDiffer = true;
        }
    }
    rows.push_back({
        {"Field", "Subrecords"},
        {"DisplayLabel", "Subrecords"},
        {"Level", 0},
        {"Values", std::move(counts)},
        {"Differs", countsDiffer},
        {"Statuses", nlohmann::json::array()},
        {"Severity", countsDiffer ? "conflict" : "none"},
        {"Kind", "Value"},
        {"Group", "Raw"},
        {"GroupLabel", "Raw record"},
        {"IsSummary", true},
        {"HasChildren", !keys.empty()},
        {"EditKind", "Text"},
        {"EnumOptions", nullptr},
        {"RefType", nullptr},
        {"RefTypes", nullptr}
    });

    for (const auto& [signature, ordinal] : keys) {
        std::vector<const fo4::Subrecord*> cells;
        cells.reserve(records.size());
        nlohmann::json values = nlohmann::json::array();
        for (const auto* record : records) {
            const auto* subrecord = record == nullptr ? nullptr : nthSubrecord(*record, signature, ordinal);
            cells.push_back(subrecord);
            values.push_back(subrecord == nullptr ? std::string{} : subrecordValue(*subrecord));
        }

        const auto differs = matrixDiffers(cells);
        rows.push_back({
            {"Field", "Subrecords\\" + signature + "[" + std::to_string(ordinal) + "]"},
            {"DisplayLabel", signature + (ordinal == 0 ? std::string{} : " [" + std::to_string(ordinal) + "]")},
            {"Level", 1},
            {"Values", std::move(values)},
            {"Differs", differs},
            {"Statuses", matrixStatuses(cells)},
            {"Severity", differs ? "conflict" : "none"},
            {"Kind", "Value"},
            {"Group", "Raw"},
            {"GroupLabel", "Raw record"},
            {"IsSummary", false},
            {"HasChildren", false},
            {"EditKind", "Text"},
            {"EnumOptions", nullptr},
            {"RefType", nullptr},
            {"RefTypes", nullptr}
        });
    }
    return rows;
}

}

Capabilities RadiclePluginBackend::capabilities() const noexcept {
    return {
        .available = true,
        .readPlugins = true,
        .writePlugins = true,
        .resolveReferences = true,
    };
}

Result<nlohmann::json> RadiclePluginBackend::invoke(std::string_view operation,
                                                     const nlohmann::json& arguments) {
    if (operation == "environment.LoadMo2") return loadEnvironment(arguments);
    if (operation == "appInterop.GetPlugins") return getPlugins();
    if (operation == "appInterop.GetChildren") return getChildren(arguments);
    if (operation == "appInterop.OpenRecord") return openRecord(arguments);
    if (operation == "appInterop.ScanConflicts") return scanConflicts();
    if (operation == "appInterop.ScanBrokenRefs") return scanBrokenRefs();
    if (operation == "backend.OpenPlugin") return openPlugin(arguments);
    if (operation == "backend.SavePlugin") return savePlugin(arguments);
    if (operation == "backend.GetEditablePlugins") return getEditablePlugins();
    if (operation == "backend.GetActivePlugins") return getActivePlugins();
    if (operation == "backend.GetRecordTypeIndex") return getRecordTypeIndex();
    if (operation == "backend.GetLoadOrderSummary") return getLoadOrderSummary();
    if (operation == "backend.GetConflicts") return getConflicts();
    if (operation == "backend.GetConflictMatrix") return getConflictMatrix(arguments);
    if (operation == "backend.GetReferencedBy") return getReferencedBy(arguments);
    if (operation == "backend.SearchRecords") return searchRecords(arguments);
    if (operation == "backend.GetProblems") return getProblems(arguments);
    if (operation == "backend.GetDependencies") return getDependencies(arguments);
    if (operation == "backend.GetHistory") return getHistory(arguments);
    if (operation == "masters.GetPlugins") return getEditablePlugins();
    if (operation == "masters.List") return listMasters(arguments);

    return std::unexpected(Error{
        "RadicleOperationNotImplemented",
        "Radicle backend operation is not implemented yet: " + std::string(operation)
    });
}

Result<nlohmann::json> RadiclePluginBackend::loadEnvironment(const nlohmann::json& arguments) {
    if (!arguments.is_object() || !arguments.contains("plugins") || !arguments["plugins"].is_array()) {
        return std::unexpected(Error{"InvalidEnvironment", "MO2 environment is missing its plugin list"});
    }

    std::vector<LoadedPlugin> loaded;
    nlohmann::json errors = nlohmann::json::array();

    for (const auto& entry : arguments["plugins"]) {
        if (!entry.is_object() || !entry.value("resolved", false)) continue;
        const auto name = entry.value("name", std::string{});
        const auto path = entry.value("path", std::string{});
        if (name.empty() || path.empty()) continue;

        try {
            auto plugin = fo4::read_plugin_file(std::filesystem::u8path(path));
            loaded.push_back({
                .name = name,
                .path = std::filesystem::u8path(path),
                .source = entry.value("source", std::string{}),
                .plugin = std::move(plugin),
            });
        } catch (const std::exception& error) {
            errors.push_back({{"plugin", name}, {"path", path}, {"error", error.what()}});
        }
    }

    std::vector<fo4::MasterDescriptor> knownKinds;
    knownKinds.reserve(loaded.size());
    for (const auto& item : loaded) {
        knownKinds.push_back({
            item.plugin.key,
            item.plugin.header.tes4_flags.is_small_file() ? fo4::PluginKind::light
                                                          : fo4::PluginKind::full,
        });
    }
    for (auto& item : loaded) {
        fo4::apply_master_kinds(item.plugin, knownKinds);
    }

    const auto loadedCount = loaded.size();
    {
        std::scoped_lock lock(mutex_);
        plugins_ = std::move(loaded);
        rebuildGraphLocked();
    }

    return nlohmann::json{
        {"ok", true},
        {"loadedPlugins", loadedCount},
        {"failedPlugins", errors.size()},
        {"errors", std::move(errors)}
    };
}

Result<nlohmann::json> RadiclePluginBackend::getPlugins() const {
    nlohmann::json rows = nlohmann::json::array();
    std::scoped_lock lock(mutex_);
    for (const auto& loaded : plugins_) {
        rows.push_back({
            {"Key", loaded.name},
            {"ConflictStatus", 0},
            {"IsRecordNode", false},
            {"HasChildren", !loaded.plugin.groups.empty()},
            {"FilePath", loaded.path.string()}
        });
    }
    return nlohmann::json(rows.dump());
}

Result<nlohmann::json> RadiclePluginBackend::getChildren(const nlohmann::json& arguments) const {
    const auto path = stringArgument(arguments, 0);
    if (path.empty()) return nlohmann::json("[]");
    const auto [pluginName, signature] = splitTreePath(path);

    std::scoped_lock lock(mutex_);
    const auto* loaded = findPlugin(pluginName);
    if (!loaded) return nlohmann::json("[]");

    nlohmann::json rows = nlohmann::json::array();
    const auto records = fo4::collect_records(loaded->plugin);

    if (signature.empty()) {
        std::map<std::string, std::size_t> counts;
        for (const auto* record : records) {
            if (record->header.signature == fo4::sig_tes4) continue;
            ++counts[record->header.signature.to_string()];
        }
        for (const auto& [sig, count] : counts) {
            rows.push_back({
                {"Key", sig + " (" + std::to_string(count) + ")"},
                {"ConflictStatus", 0},
                {"IsRecordNode", false},
                {"HasChildren", count != 0},
                {"FilePath", loaded->path.string()}
            });
        }
        return nlohmann::json(rows.dump());
    }

    const auto space = signature.find(' ');
    const auto normalizedSignature = signature.substr(0, space);
    for (const auto* record : records) {
        if (record->header.signature.to_string() != normalizedSignature) continue;
        if (record->form_key.owner().empty()) continue;
        rows.push_back({
            {"Key", record->form_key.to_string()},
            {"ConflictStatus", 0},
            {"IsRecordNode", true},
            {"HasChildren", false},
            {"FilePath", loaded->path.string()}
        });
    }
    return nlohmann::json(rows.dump());
}

Result<nlohmann::json> RadiclePluginBackend::openRecord(const nlohmann::json& arguments) const {
    const auto path = stringArgument(arguments, 0);
    if (path.empty()) return nlohmann::json("");
    const auto last = path.find_last_of("\\/");
    if (last == std::string::npos || last + 1 >= path.size()) return nlohmann::json("");
    return nlohmann::json(path.substr(last + 1));
}

Result<nlohmann::json> RadiclePluginBackend::openPlugin(const nlohmann::json& arguments) {
    const auto path = stringArgument(arguments, 0);
    if (path.empty()) return std::unexpected(Error{"PluginPathMissing", "Plugin path is empty"});

    try {
        auto plugin = fo4::read_plugin_file(std::filesystem::u8path(path));
        std::string name = plugin.key.filename().empty()
            ? std::filesystem::u8path(path).filename().string()
            : std::string(plugin.key.filename());
        std::scoped_lock lock(mutex_);
        auto* existing = findPlugin(name);
        if (existing) {
            existing->path = std::filesystem::u8path(path);
            existing->plugin = std::move(plugin);
        } else {
            plugins_.push_back({name, std::filesystem::u8path(path), {}, std::move(plugin)});
        }

        std::vector<fo4::MasterDescriptor> knownKinds;
        knownKinds.reserve(plugins_.size());
        for (const auto& item : plugins_) {
            knownKinds.push_back({
                item.plugin.key,
                item.plugin.header.tes4_flags.is_small_file() ? fo4::PluginKind::light
                                                              : fo4::PluginKind::full,
            });
        }
        for (auto& item : plugins_) fo4::apply_master_kinds(item.plugin, knownKinds);
        rebuildGraphLocked();
        return nlohmann::json{{"ok", true}, {"plugin", name}}.dump();
    } catch (const std::exception& error) {
        return std::unexpected(Error{"PluginReadFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::savePlugin(const nlohmann::json& arguments) {
    const auto name = stringArgument(arguments, 0);
    const auto requestedPath = stringArgument(arguments, 1);
    if (name.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});

    std::scoped_lock lock(mutex_);
    auto* loaded = findPlugin(name);
    if (!loaded) return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + name});
    const auto output = requestedPath.empty() ? loaded->path : std::filesystem::u8path(requestedPath);
    try {
        fo4::write_plugin_file(loaded->plugin, output);
        loaded->path = output;
        return nlohmann::json{{"ok", true}, {"plugin", name}, {"path", output.string()}}.dump();
    } catch (const std::exception& error) {
        return std::unexpected(Error{"PluginWriteFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::getEditablePlugins() const {
    nlohmann::json rows = nlohmann::json::array();
    std::scoped_lock lock(mutex_);
    for (const auto& loaded : plugins_) rows.push_back(loaded.name);
    return nlohmann::json(rows.dump());
}

Result<nlohmann::json> RadiclePluginBackend::getActivePlugins() const {
    nlohmann::json rows = nlohmann::json::array();
    std::scoped_lock lock(mutex_);
    for (std::size_t index = 0; index < plugins_.size(); ++index) {
        const auto& loaded = plugins_[index];
        rows.push_back({
            {"Name", loaded.name},
            {"Kind", pluginKind(loaded.plugin)},
            {"LoadOrder", index},
            {"Editable", true},
            {"Size", loaded.plugin.original_bytes.size()}
        });
    }
    return nlohmann::json(rows.dump());
}

Result<nlohmann::json> RadiclePluginBackend::getRecordTypeIndex() const {
    std::map<std::string, std::size_t> counts;
    std::scoped_lock lock(mutex_);
    for (const auto& loaded : plugins_) {
        for (const auto* record : fo4::collect_records(loaded.plugin)) {
            if (record->header.signature == fo4::sig_tes4) continue;
            ++counts[record->header.signature.to_string()];
        }
    }

    nlohmann::json rows = nlohmann::json::array();
    for (const auto& [signature, count] : counts) {
        rows.push_back({{"Type", signature}, {"FriendlyName", signature}, {"Count", count}});
    }
    return nlohmann::json(rows.dump());
}

Result<nlohmann::json> RadiclePluginBackend::getLoadOrderSummary() const {
    std::size_t totalRecords = 0;
    nlohmann::json names = nlohmann::json::array();
    std::scoped_lock lock(mutex_);
    for (const auto& loaded : plugins_) {
        names.push_back(loaded.name);
        for (const auto* record : fo4::collect_records(loaded.plugin)) {
            if (record->header.signature != fo4::sig_tes4) ++totalRecords;
        }
    }
    return nlohmann::json(nlohmann::json{
        {"TotalRecords", totalRecords},
        {"PluginCount", plugins_.size()},
        {"Plugins", std::move(names)}
    }.dump());
}

Result<nlohmann::json> RadiclePluginBackend::getConflicts() const {
    std::scoped_lock lock(mutex_);
    nlohmann::json rows = nlohmann::json::array();
    for (const auto& key : graph_.conflicts()) {
        const auto& versions = graph_.versions(key);
        const auto* winner = graph_.winner(key);
        if (winner == nullptr) continue;

        nlohmann::json pluginNames = nlohmann::json::array();
        for (const auto& version : versions) {
            pluginNames.push_back(std::string(version.plugin.filename()));
        }

        std::string editor;
        const auto* winningPlugin = findPlugin(winner->plugin.filename());
        if (winningPlugin != nullptr) {
            if (const auto* record = fo4::find_record(winningPlugin->plugin, key)) {
                editor = editorId(*record);
            }
        }

        rows.push_back({
            {"FormKey", key.to_string()},
            {"EditorID", editor},
            {"Type", winner->signature.to_string()},
            {"Plugins", std::move(pluginNames)},
            {"Winner", std::string(winner->plugin.filename())},
            {"InvolvesMod", versions.size() > 1}
        });
    }
    return nlohmann::json(rows.dump());
}

Result<nlohmann::json> RadiclePluginBackend::getConflictMatrix(const nlohmann::json& arguments) const {
    const auto formKeyText = stringArgument(arguments, 0);
    if (formKeyText.empty()) return nlohmann::json("");

    fo4::FormKey key;
    try {
        key = fo4::FormKey::parse(formKeyText);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"InvalidFormKey", error.what()});
    }

    std::scoped_lock lock(mutex_);
    const auto& versions = graph_.versions(key);
    const auto* winner = graph_.winner(key);
    if (winner == nullptr) return nlohmann::json("");

    nlohmann::json pluginNames = nlohmann::json::array();
    std::vector<const fo4::Record*> records;
    records.reserve(versions.size());
    for (const auto& version : versions) {
        const auto name = std::string(version.plugin.filename());
        pluginNames.push_back(name);
        const auto* loaded = findPlugin(name);
        records.push_back(loaded == nullptr ? nullptr : fo4::find_record(loaded->plugin, key));
    }

    std::string editor;
    if (!records.empty() && records.back() != nullptr) editor = editorId(*records.back());
    auto rows = buildRawMatrixRows(records);
    const auto hasDifferences = std::any_of(rows.begin(), rows.end(), [](const auto& row) {
        return row.is_object() && row.value("Differs", false) && !row.value("HasChildren", false);
    });

    return nlohmann::json(nlohmann::json{
        {"FormKey", key.to_string()},
        {"EditorID", editor},
        {"Type", winner->signature.to_string()},
        {"Winner", std::string(winner->plugin.filename())},
        {"Plugins", std::move(pluginNames)},
        {"Rows", std::move(rows)},
        {"Level", versions.size() == 1 ? "onlyone" : (hasDifferences ? "conflict" : "override")}
    }.dump());
}

Result<nlohmann::json> RadiclePluginBackend::getReferencedBy(const nlohmann::json& arguments) const {
    const auto formKeyText = stringArgument(arguments, 0);
    if (formKeyText.empty()) return nlohmann::json("[]");

    fo4::FormKey key;
    try {
        key = fo4::FormKey::parse(formKeyText);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"InvalidFormKey", error.what()});
    }

    std::scoped_lock lock(mutex_);
    nlohmann::json rows = nlohmann::json::array();
    std::set<std::pair<std::string, std::string>> seen;
    for (const auto& hit : graph_.referenced_by(key)) {
        const auto source = hit.source.to_string();
        const auto pluginName = std::string(hit.plugin.filename());
        if (!seen.emplace(pluginName, source).second) continue;

        std::string editor;
        std::string type;
        const auto* loaded = findPlugin(pluginName);
        if (loaded != nullptr) {
            if (const auto* record = fo4::find_record(loaded->plugin, hit.source)) {
                editor = editorId(*record);
                type = record->header.signature.to_string();
            }
        }

        rows.push_back({
            {"Plugin", pluginName},
            {"FormKey", source},
            {"EditorID", editor},
            {"Type", type}
        });
    }
    return nlohmann::json(rows.dump());
}

Result<nlohmann::json> RadiclePluginBackend::searchRecords(const nlohmann::json& arguments) const {
    const auto query = stringArgument(arguments, 0);
    const auto typeFilter = stringArgument(arguments, 1);

    nlohmann::json rows = nlohmann::json::array();
    std::scoped_lock lock(mutex_);
    constexpr std::size_t maxResults = 500;

    for (const auto& loaded : plugins_) {
        for (const auto* record : fo4::collect_records(loaded.plugin)) {
            if (record->header.signature == fo4::sig_tes4 || record->form_key.owner().empty()) continue;
            const auto type = record->header.signature.to_string();
            if (!typeFilter.empty() && type != typeFilter) continue;

            const auto formKey = record->form_key.to_string();
            const auto editor = editorId(*record);
            if (!containsInsensitive(formKey, query) &&
                !containsInsensitive(editor, query) &&
                !containsInsensitive(type, query)) {
                continue;
            }

            rows.push_back({
                {"FormKey", formKey},
                {"EditorID", editor},
                {"Type", type},
                {"Plugin", loaded.name}
            });
            if (rows.size() >= maxResults) return nlohmann::json(rows.dump());
        }
    }
    return nlohmann::json(rows.dump());
}

Result<nlohmann::json> RadiclePluginBackend::scanConflicts() const {
    std::scoped_lock lock(mutex_);
    const auto count = graph_.conflicts().size();
    return nlohmann::json(
        "Found " + std::to_string(count) + " conflicted record" +
        (count == 1 ? std::string{} : "s") + " across " +
        std::to_string(plugins_.size()) + " loaded plugins.");
}

Result<nlohmann::json> RadiclePluginBackend::scanBrokenRefs() const {
    std::scoped_lock lock(mutex_);

    std::set<fo4::FormKey> knownRecords;
    for (const auto& loaded : plugins_) {
        for (const auto* record : fo4::collect_records(loaded.plugin)) {
            if (!record->form_key.owner().empty()) knownRecords.insert(record->form_key);
        }
    }

    std::set<std::string> missing;
    for (const auto& loaded : plugins_) {
        for (const auto& link : fo4::collect_form_links(loaded.plugin)) {
            if (link.key.owner().empty() || knownRecords.contains(link.key)) continue;
            const auto source = link.record.owner().empty()
                ? std::string("<TES4>")
                : link.record.to_string();
            missing.insert(source + " -> " + link.key.to_string() +
                           " [" + link.subrecord.to_string() + "]");
        }
    }

    std::ostringstream report;
    report << "Radicle crash-risk scan\n\n";
    report << "Loaded plugins: " << plugins_.size() << '\n';
    report << "Indexed records: " << knownRecords.size() << '\n';
    report << "Dangling FormLinks: " << missing.size() << "\n\n";

    if (missing.empty()) {
        report << "No dangling FormLinks were found in the currently loaded plugin set.";
    } else {
        constexpr std::size_t maxLines = 200;
        std::size_t emitted = 0;
        for (const auto& line : missing) {
            if (emitted++ == maxLines) break;
            report << line << '\n';
        }
        if (missing.size() > maxLines) {
            report << "... " << (missing.size() - maxLines) << " more omitted\n";
        }
    }
    return nlohmann::json(report.str());
}

Result<nlohmann::json> RadiclePluginBackend::listMasters(const nlohmann::json& arguments) const {
    const auto name = stringArgument(arguments, 0);
    if (name.empty()) return nlohmann::json("{}");

    std::scoped_lock lock(mutex_);
    const auto* loaded = findPlugin(name);
    if (!loaded) return nlohmann::json("{}");

    nlohmann::json masters = nlohmann::json::array();
    for (std::size_t index = 0; index < loaded->plugin.header.masters.size(); ++index) {
        const auto& master = loaded->plugin.header.masters[index];
        masters.push_back({
            {"index", index},
            {"name", std::string(master.key.filename())},
            {"size", master.data_size},
            {"used", true}
        });
    }
    return nlohmann::json(nlohmann::json{
        {"pluginName", loaded->name},
        {"masters", std::move(masters)},
        {"light", loaded->plugin.header.tes4_flags.is_small_file()}
    }.dump());
}

RadiclePluginBackend::LoadedPlugin* RadiclePluginBackend::findPlugin(std::string_view name) {
    const auto key = fo4::PluginKey::from_filename(name);
    const auto it = std::find_if(plugins_.begin(), plugins_.end(),
                                 [&key, name](const LoadedPlugin& plugin) {
                                     return plugin.name == name || plugin.plugin.key == key;
                                 });
    return it == plugins_.end() ? nullptr : &*it;
}

const RadiclePluginBackend::LoadedPlugin* RadiclePluginBackend::findPlugin(std::string_view name) const {
    const auto key = fo4::PluginKey::from_filename(name);
    const auto it = std::find_if(plugins_.begin(), plugins_.end(),
                                 [&key, name](const LoadedPlugin& plugin) {
                                     return plugin.name == name || plugin.plugin.key == key;
                                 });
    return it == plugins_.end() ? nullptr : &*it;
}

void RadiclePluginBackend::rebuildGraphLocked() {
    std::vector<const fo4::Plugin*> loadOrder;
    loadOrder.reserve(plugins_.size());
    for (const auto& loaded : plugins_) loadOrder.push_back(&loaded.plugin);
    graph_ = fo4::ConflictGraph::build(loadOrder);
}

}
