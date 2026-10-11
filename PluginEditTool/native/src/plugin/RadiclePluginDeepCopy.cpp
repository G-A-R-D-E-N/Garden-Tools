#include "RadiclePluginSupport.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <fo4/plugin/mutation.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/plugin/visit.hpp>
#include <fo4/plugin/writer.hpp>
#include <fo4/schema/form_links.hpp>
#include <fo4/signature.hpp>

namespace fo4ide::plugin {

using support::editorId;

namespace {

std::string deepStringArgument(const nlohmann::json& args, std::size_t index) {
    if (!args.is_array() || index >= args.size() || !args[index].is_string()) return {};
    return args[index].get<std::string>();
}

bool deepBoolArgument(const nlohmann::json& args,
                      std::size_t index,
                      bool fallback = false) {
    if (!args.is_array() || index >= args.size() || !args[index].is_boolean()) return fallback;
    return args[index].get<bool>();
}

bool deepOptionalBoolArgument(const nlohmann::json& args, std::size_t index, bool& value) {
    value = false;
    if (!args.is_array() || index >= args.size() || args[index].is_null()) return true;
    if (!args[index].is_boolean()) return false;
    value = args[index].get<bool>();
    return true;
}

std::size_t deepLimitArgument(const nlohmann::json& args,
                              std::size_t index,
                              std::size_t fallback) {
    if (!args.is_array() || index >= args.size() || !args[index].is_number_integer()) return fallback;
    const auto raw = args[index].get<std::int64_t>();
    return raw > 0 ? static_cast<std::size_t>(raw) : fallback;
}

std::string deepLower(std::string_view value) {
    std::string out(value);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return out;
}

const fo4::Record* deepTopLevelRecord(const fo4::Plugin& plugin,
                                      const fo4::FormKey& key) {
    for (const auto& group : plugin.groups) {
        if (group.header.group_type != static_cast<std::int32_t>(fo4::GroupType::top)) continue;
        for (const auto& child : group.children) {
            const auto* record = std::get_if<fo4::Record>(&child);
            if (record != nullptr && record->form_key == key) return record;
        }
    }
    return nullptr;
}

std::string deepCapNotice(std::size_t cap) {
    return " (capped at " + std::to_string(cap) + "; more remained in the FormLink graph)";
}

std::string typeSummary(const fo4::Plugin& source,
                        const std::vector<fo4::FormKey>& keys) {
    std::map<std::string, std::size_t> counts;
    for (const auto& key : keys) {
        if (const auto* record = fo4::find_record(source, key)) {
            ++counts[record->header.signature.to_string()];
        }
    }

    std::vector<std::pair<std::string, std::size_t>> ordered(counts.begin(), counts.end());
    std::sort(ordered.begin(), ordered.end(), [](const auto& left, const auto& right) {
        if (left.second != right.second) return left.second > right.second;
        return left.first < right.first;
    });

    std::string out;
    for (const auto& [type, count] : ordered) {
        if (!out.empty()) out += ", ";
        out += type + " x" + std::to_string(count);
    }
    return out;
}

}  // namespace

Result<nlohmann::json> RadiclePluginBackend::deepCopyAsOverride(
    const nlohmann::json& arguments) {
    const auto requestedSourceName = deepStringArgument(arguments, 0);
    const auto recordId = deepStringArgument(arguments, 1);
    const auto targetName = deepStringArgument(arguments, 2);
    const bool apply = deepBoolArgument(arguments, 3, false);
    const bool overwrite = deepBoolArgument(arguments, 4, false);
    const auto cap = deepLimitArgument(arguments, 5, 200);
    bool allowLosing = false;
    if (!deepOptionalBoolArgument(arguments, 6, allowLosing)) {
        return std::unexpected(Error{"InvalidAllowLosingFlag", "Allow-losing flag must be boolean"});
    }

    if (recordId.empty() || targetName.empty()) {
        return std::unexpected(Error{
            "DeepCopyArgumentsMissing",
            "DeepCopyAsOverride requires source record and target plugin"
        });
    }

    OpLock lock(*this, OpLock::Mode::Write, {targetName, requestedSourceName});
    auto* target = findPlugin(targetName);
    if (apply && target == nullptr) {
        return std::unexpected(Error{
            "TargetPluginNotLoaded",
            "Target plugin is not loaded yet: " + targetName
        });
    }

    const LoadedPlugin* source = nullptr;
    fo4::FormKey rootKey;
    bool keyResolved = false;
    if (recordId.find(':') != std::string::npos) {
        try {
            rootKey = fo4::FormKey::parse(recordId);
            keyResolved = true;
        } catch (const std::exception& error) {
            return std::unexpected(Error{"InvalidFormKey", error.what()});
        }
    }

    if (!requestedSourceName.empty()) {
        source = findPlugin(requestedSourceName);
        if (source == nullptr) {
            return std::unexpected(Error{
                "SourcePluginNotLoaded",
                "Source plugin is not loaded: " + requestedSourceName
            });
        }
        if (!keyResolved) {
            const auto wanted = deepLower(recordId);
            fo4::visit_records(source->plugin, [&](const fo4::Record& record) {
                if (!keyResolved && deepLower(editorId(record)) == wanted) {
                    rootKey = record.form_key;
                    keyResolved = true;
                }
            });
        }
    } else if (keyResolved) {
        for (auto it = plugins_.rbegin(); it != plugins_.rend(); ++it) {
            if (fo4::find_record(it->plugin, rootKey) == nullptr) continue;
            source = &*it;
            break;
        }
    } else {
        const auto wanted = deepLower(recordId);
        for (auto it = plugins_.rbegin(); it != plugins_.rend() && source == nullptr; ++it) {
            fo4::visit_records(it->plugin, [&](const fo4::Record& record) {
                if (source == nullptr && deepLower(editorId(record)) == wanted) {
                    source = &*it;
                    rootKey = record.form_key;
                    keyResolved = true;
                }
            });
        }
    }

    if (!keyResolved || source == nullptr) {
        return std::unexpected(Error{
            "SourceRecordNotFound",
            "Could not find source record '" + recordId + "'"
        });
    }

    const auto* root = fo4::find_record(source->plugin, rootKey);
    if (root == nullptr) {
        return std::unexpected(Error{
            "SourceRecordNotFound",
            "Could not find " + rootKey.to_string() + " in " + source->name
        });
    }

    const auto sigCell = fo4::Signature::from_string("CELL");
    const auto sigWorld = fo4::Signature::from_string("WRLD");
    if (root->header.signature == sigCell || root->header.signature == sigWorld) {
        return nlohmann::json(
            root->header.signature.to_string() +
            " does not support deep copy here: its nested cell/reference hierarchy "
            "cannot be reproduced by the generic override copier.");
    }

    try {
        const auto links = fo4::collect_form_links(source->plugin);
        std::vector<fo4::FormKey> toCopy{rootKey};
        std::set<fo4::FormKey> visited{rootKey};
        std::size_t cursor = 0;
        bool capped = false;

        while (cursor < toCopy.size()) {
            const auto current = toCopy[cursor++];
            for (const auto& link : links) {
                if (link.record != current || link.key.owner() != source->plugin.key) continue;
                if (visited.contains(link.key)) continue;
                if (fo4::find_record(source->plugin, link.key) == nullptr) continue;
                if (toCopy.size() >= cap) {
                    capped = true;
                    break;
                }
                visited.insert(link.key);
                toCopy.push_back(link.key);
            }
            if (capped) break;
        }

        for (const auto& key : toCopy) {
            if (deepTopLevelRecord(source->plugin, key) != nullptr) continue;
            if (target != nullptr && fo4::find_record(target->plugin, key) != nullptr) continue;
            return nlohmann::json(
                "Deep copy is blocked by nested record " + key.to_string() +
                ": native containment-aware insertion is not available for this record yet.");
        }

        std::vector<fo4::FormKey> existing;
        if (target != nullptr) {
            for (const auto& key : toCopy) {
                if (fo4::find_record(target->plugin, key) != nullptr) existing.push_back(key);
            }
        }

        std::size_t losingCount = 0;
        std::string losingSample;
        if (target != nullptr) {
            for (const auto& key : toCopy) {
                const auto later = laterOverridesLocked(*target, key);
                if (later.empty()) continue;
                if (++losingCount > 5) continue;
                if (!losingSample.empty()) losingSample += "; ";
                losingSample += key.to_string() + " loses to ";
                for (std::size_t index = 0; index < later.size(); ++index) {
                    losingSample += (index == 0 ? "" : ", ") + later[index];
                }
            }
            if (losingCount > 5) losingSample += "; and " + std::to_string(losingCount - 5) + " more";
        }

        if (!apply) {
            std::string preview =
                "DRY RUN: deep copy of " + rootKey.to_string() + " would bring " +
                std::to_string(toCopy.size()) + " record(s) into " + targetName;
            if (capped) preview += deepCapNotice(cap);
            preview += ": " + typeSummary(source->plugin, toCopy) + ".";
            if (!existing.empty()) {
                preview += " " + std::to_string(existing.size()) +
                           " already exist in the target and will require a separate overwrite confirmation.";
            }
            if (losingCount > 0) {
                preview += " " + std::to_string(losingCount) + " would not win because " +
                           targetName + " loads before other plugins that override them (" +
                           losingSample + ") and will require load-order confirmation.";
            }
            preview += capped ? " Raise the cap, then re-run with apply=true."
                              : " Re-run with apply=true.";
            return nlohmann::json(std::move(preview));
        }

        if (target == nullptr) {
            return std::unexpected(Error{
                "TargetPluginNotLoaded",
                "Target plugin disappeared before deep-copy apply: " + targetName
            });
        }
        if (capped) {
            return std::unexpected(Error{
                "DeepCopyCapped",
                "Deep copy of " + rootKey.to_string() + " refused" + deepCapNotice(cap) +
                ": nothing was staged."
            });
        }
        if (const auto guard = writeGuard(*target)) return std::unexpected(*guard);

        if (losingCount > 0 && !allowLosing) {
            return nlohmann::json(
                "LOSES: " + targetName + " loads before other plugins that also override " +
                std::to_string(losingCount) + " of the " + std::to_string(toCopy.size()) +
                " record(s) in this deep copy: " + losingSample +
                ". The copied overrides will not win until " + targetName + " loads after them.");
        }

        if (!existing.empty() && !overwrite) {
            std::string sample;
            const auto shown = std::min<std::size_t>(existing.size(), 5);
            for (std::size_t index = 0; index < shown; ++index) {
                if (!sample.empty()) sample += ", ";
                const auto* record = fo4::find_record(source->plugin, existing[index]);
                const auto editor = record == nullptr ? std::string{} : editorId(*record);
                if (!editor.empty()) sample += editor + " ";
                sample += "(" + existing[index].to_string() + ")";
            }
            if (existing.size() > shown) {
                sample += " and " + std::to_string(existing.size() - shown) + " more";
            }
            return nlohmann::json(
                "EXISTS: '" + targetName + "' already contains " +
                std::to_string(existing.size()) + " of the " +
                std::to_string(toCopy.size()) + " record(s) in this deep copy: " + sample +
                ". Overwrite all existing records with fresh copies from '" + source->name + "'?");
        }

        auto mutated = target->plugin;
        std::size_t copied = 0;
        for (auto it = toCopy.rbegin(); it != toCopy.rend(); ++it) {
            const auto status = fo4::copy_record_as_override(
                mutated, source->plugin, *it, overwrite);
            switch (status) {
                case fo4::RecordCopyStatus::inserted:
                case fo4::RecordCopyStatus::replaced:
                    ++copied;
                    break;
                case fo4::RecordCopyStatus::already_exists:
                    return std::unexpected(Error{
                        "DeepCopyTargetCollision",
                        "Target record appeared after collision preflight: " + it->to_string()
                    });
                case fo4::RecordCopyStatus::source_missing:
                    return std::unexpected(Error{
                        "DeepCopySourceMissing",
                        "Source record disappeared during copy: " + it->to_string()
                    });
                case fo4::RecordCopyStatus::source_containment_unsupported:
                    return std::unexpected(Error{
                        "DeepCopyContainmentUnsupported",
                        "Cannot insert nested source record: " + it->to_string()
                    });
            }
        }

        stagePluginMutationLocked(*target, std::move(mutated));

        return nlohmann::json(
            "Deep-copied " + std::to_string(copied) + " of " +
            std::to_string(toCopy.size()) + " record(s) rooted at " +
            rootKey.to_string() + " into " + targetName +
            ". Ensure " + targetName +
            " loads after the conflicting plugins. SavePlugin to persist.");
    } catch (const std::exception& error) {
        return std::unexpected(Error{"DeepCopyFailed", error.what()});
    }
}

}  // namespace fo4ide::plugin
