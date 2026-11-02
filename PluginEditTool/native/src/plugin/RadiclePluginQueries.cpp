#include "RadiclePluginSupport.hpp"
#include "RadiclePluginFields.hpp"

#include <algorithm>
#include <cstddef>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fo4/plugin/reader.hpp>
#include <fo4/schema/form_links.hpp>

namespace fo4ide::plugin {
using namespace support;

namespace {

std::string pluginKind(const fo4::Plugin& plugin) {
    if (fo4::classify_plugin_kind(plugin.header.tes4_flags, plugin.key) == fo4::PluginKind::light) return "light";
    if (fo4::is_master_plugin(plugin.header.tes4_flags, plugin.key)) return "master";
    return "plugin";
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
        return fo4::text::windows1252_to_utf8(std::string(begin, nul));
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

template <class Cell, class Value>
nlohmann::json matrixStatuses(const std::vector<const Cell*>& cells, Value value) {
    nlohmann::json statuses = nlohmann::json::array();
    std::vector<std::size_t> present;
    present.reserve(cells.size());
    for (std::size_t i = 0; i < cells.size(); ++i) {
        if (cells[i] != nullptr) present.push_back(i);
    }

    bool allSame = present.size() > 1;
    if (allSame) {
        const auto& first = value(*cells[present.front()]);
        for (const auto index : present) {
            if (value(*cells[index]) != first) {
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
        } else if (value(*cells[i]) == value(*cells[winner])) {
            statuses.push_back("override");
        } else {
            statuses.push_back("lose");
        }
    }
    return statuses;
}

template <class Cell, class Value>
bool matrixDiffers(const std::vector<const Cell*>& cells, Value value) {
    if (cells.empty()) return false;
    const auto present = static_cast<std::size_t>(
        std::count_if(cells.begin(), cells.end(), [](const auto* cell) { return cell != nullptr; }));
    if (present == 0) return false;
    if (present != cells.size()) return true;
    if (present < 2) return false;
    const auto& first = value(*cells.front());
    return std::any_of(cells.begin() + 1, cells.end(), [&](const auto* cell) {
        return value(*cell) != first;
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

        const auto differs = matrixDiffers(cells, [](const auto& cell) -> const auto& { return cell.data; });
        rows.push_back({
            {"Field", "Subrecords\\" + signature + "[" + std::to_string(ordinal) + "]"},
            {"DisplayLabel", signature + (ordinal == 0 ? std::string{} : " [" + std::to_string(ordinal) + "]")},
            {"Level", 1},
            {"Values", std::move(values)},
            {"Differs", differs},
            {"Statuses", matrixStatuses(cells, [](const auto& cell) -> const auto& { return cell.data; })},
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

Result<nlohmann::json> RadiclePluginBackend::getEditablePlugins() const {
    nlohmann::json rows = nlohmann::json::array();
    OpLock lock(*this, OpLock::Mode::Read);
    for (const auto& loaded : plugins_) {
        if (isEditable(loaded)) rows.push_back(loaded.name);
    }
    return nlohmann::json(rows.dump());
}

Result<nlohmann::json> RadiclePluginBackend::getActivePlugins() const {
    nlohmann::json rows = nlohmann::json::array();
    OpLock lock(*this, OpLock::Mode::Read);
    for (std::size_t index = 0; index < plugins_.size(); ++index) {
        const auto& loaded = plugins_[index];
        rows.push_back({
            {"Name", loaded.name},
            {"Kind", pluginKind(loaded.plugin)},
            {"LoadOrder", index},
            {"Editable", isEditable(loaded)},
            {"Pending", loaded.dirty || hasStagedStringTables(loaded.name)},
            {"Size", fo4::plugin_original_bytes(loaded.plugin).size()}
        });
    }
    return nlohmann::json(rows.dump());
}

Result<nlohmann::json> RadiclePluginBackend::getRecordTypeIndex() const {
    std::map<std::string, std::size_t> counts;
    OpLock lock(*this, OpLock::Mode::Read);
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
    OpLock lock(*this, OpLock::Mode::Read);
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
    OpLock lock(*this, OpLock::Mode::Read);
    nlohmann::json rows = nlohmann::json::array();
    for (const auto& key : (*graph_).conflicts()) {
        const auto& versions = (*graph_).versions(key);
        const auto* winner = (*graph_).winner(key);
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

    OpLock lock(*this, OpLock::Mode::Read);
    const auto& versions = (*graph_).versions(key);
    const auto* winner = (*graph_).winner(key);
    if (winner == nullptr) {
        return std::unexpected(Error{
            "RecordNotFound",
            "No loaded plugin contains this FormKey: " + formKeyText
        });
    }

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
    auto rows = nlohmann::json::array();
    std::vector<std::vector<SemanticFieldCell>> projected;
    std::vector<bool> writable;
    std::vector<std::string> paths{"EDID"};
    for (std::size_t index = 0; index < records.size(); ++index) {
        const auto* loaded = findPlugin(versions[index].plugin.filename());
        std::vector<SemanticFieldCell> cells;
        if (loaded != nullptr && records[index] != nullptr) {
            auto semantic = semanticFieldCells(loaded->plugin, *records[index]);
            if (semantic) {
                cells = std::move(*semantic);
            } else {
                cells.push_back(SemanticFieldCell{"SemanticUnavailable", semantic.error().message, false, false});
            }
            cells.insert(cells.begin(), SemanticFieldCell{"EDID", editorId(*records[index]), true, false});
        }
        for (const auto& cell : cells) {
            if (std::find(paths.begin(), paths.end(), cell.path) == paths.end()) paths.push_back(cell.path);
        }
        projected.push_back(std::move(cells));
        writable.push_back(loaded != nullptr && isEditable(*loaded));
    }
    for (const auto& path : paths) {
        std::vector<const SemanticFieldCell*> cells;
        auto values = nlohmann::json::array();
        auto editable = nlohmann::json::array();
        bool reference = false;
        for (std::size_t index = 0; index < projected.size(); ++index) {
            const auto& version = projected[index];
            const auto found = std::find_if(version.begin(), version.end(), [&](const auto& cell) {
                return cell.path == path;
            });
            const auto* cell = found == version.end() ? nullptr : &*found;
            cells.push_back(cell == nullptr || !cell->available ? nullptr : cell);
            values.push_back(cell == nullptr ? std::string{} : cell->value);
            editable.push_back(cell != nullptr && cell->available && cell->writable && writable[index]);
            reference = reference || (cell != nullptr && cell->reference);
        }
        const auto value = [](const auto& cell) -> const auto& { return cell.value; };
        const auto differs = matrixDiffers(cells, value);
        auto row = nlohmann::json{
            {"Field", path}, {"DisplayLabel", path}, {"Level", 0},
            {"Values", std::move(values)}, {"Editable", std::move(editable)},
            {"Differs", differs}, {"Statuses", matrixStatuses(cells, value)},
            {"Severity", differs ? "conflict" : "none"},
            {"Kind", reference ? "FormID" : "Value"},
            {"Group", "Semantic"}, {"GroupLabel", "Semantic fields"},
            {"IsSummary", false}, {"HasChildren", false},
            {"EditKind", reference ? "Ref" : "Text"}, {"EnumOptions", nullptr},
            {"RefType", nullptr}, {"RefTypes", nullptr}
        };
        const auto presentation = scalarPresentationJson(winner->signature, path);
        if (!presentation.is_null()) row["Presentation"] = presentation;
        rows.push_back(std::move(row));
    }
    auto raw = buildRawMatrixRows(records);
    for (auto& row : raw) {
        row["Editable"] = nlohmann::json::array();
        for (std::size_t index = 0; index < records.size(); ++index) row["Editable"].push_back(false);
        rows.push_back(std::move(row));
    }
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

    OpLock lock(*this, OpLock::Mode::Read);
    nlohmann::json rows = nlohmann::json::array();
    std::set<std::pair<std::string, std::string>> seen;
    for (const auto& hit : (*graph_).referenced_by(key)) {
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
    OpLock lock(*this, OpLock::Mode::Read);
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
    OpLock lock(*this, OpLock::Mode::Read);
    const auto count = (*graph_).conflicts().size();
    return nlohmann::json(
        "Found " + std::to_string(count) + " conflicted record" +
        (count == 1 ? std::string{} : "s") + " across " +
        std::to_string(plugins_.size()) + " loaded plugins.");
}

Result<nlohmann::json> RadiclePluginBackend::scanBrokenRefs() const {
    OpLock lock(*this, OpLock::Mode::Read);

    std::set<fo4::FormKey> knownRecords;
    for (const auto& loaded : plugins_) {
        for (const auto* record : fo4::collect_records(loaded.plugin)) {
            if (!record->form_key.owner().empty()) knownRecords.insert(record->form_key);
        }
    }

    std::set<std::string> missing;
    std::size_t recordsWithIncompleteVmad = 0;
    for (const auto& loaded : plugins_) {
        const auto collection = fo4::collect_form_links_detailed(loaded.plugin);
        recordsWithIncompleteVmad += collection.records_with_incomplete_vmad;
        for (const auto& link : collection.links) {
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
    if (recordsWithIncompleteVmad != 0) {
        report << "NOTE: " << recordsWithIncompleteVmad
               << " record(s) contain VMAD script data Radicle could not fully scan"
               << " (unsupported property types, a parse failure, trailing bytes, or an"
               << " unconsumed remainder); FormLinks from those records may be"
               << " incomplete, so the results below are not exhaustive.\n\n";
    }

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

    OpLock lock(*this, OpLock::Mode::Read);
    const auto* loaded = findPlugin(name);
    if (!loaded) return nlohmann::json("{}");

    nlohmann::json masters = nlohmann::json::array();
    std::set<std::string> usedMasters;
    const auto collection = fo4::collect_form_links_detailed(loaded->plugin);
    for (const auto& link : collection.links) {
        if (!link.key.owner().empty()) {
            usedMasters.insert(std::string(link.key.owner().filename()));
        }
    }
    for (const auto* record : fo4::collect_records(loaded->plugin)) {
        if (!record->form_key.owner().empty()) {
            usedMasters.insert(std::string(record->form_key.owner().filename()));
        }
    }
    if (collection.records_with_incomplete_vmad != 0) {
        for (const auto& master : loaded->plugin.header.masters) {
            usedMasters.insert(std::string(master.key.filename()));
        }
    }
    usedMasters.erase(std::string(loaded->plugin.key.filename()));
    for (std::size_t index = 0; index < loaded->plugin.header.masters.size(); ++index) {
        const auto& master = loaded->plugin.header.masters[index];
        masters.push_back({
            {"index", index},
            {"name", std::string(master.key.filename())},
            {"size", master.data_size},
            {"used", usedMasters.contains(std::string(master.key.filename()))},
            {"kind", master.kind == fo4::PluginKind::light ? "light" : "full"}
        });
    }
    return nlohmann::json(nlohmann::json{
        {"pluginName", loaded->name},
        {"masters", std::move(masters)},
        {"light", fo4::classify_plugin_kind(loaded->plugin.header.tes4_flags, loaded->plugin.key) == fo4::PluginKind::light}
    }.dump());
}

}
