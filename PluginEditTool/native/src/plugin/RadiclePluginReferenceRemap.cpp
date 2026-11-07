#include "RadiclePluginSupport.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <fo4/forms/placed_reference.hpp>
#include <fo4/plugin/mutation.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/plugin/writer.hpp>
#include <fo4/schema/form_links.hpp>
#include <fo4/signature.hpp>

namespace fo4ide::plugin {

using support::editorId;

namespace {

std::string remapStringArgument(const nlohmann::json& args, std::size_t index) {
    if (!args.is_array() || index >= args.size() || !args[index].is_string()) return {};
    return args[index].get<std::string>();
}

bool remapBoolArgument(const nlohmann::json& args,
                       std::size_t index,
                       bool fallback = false) {
    if (!args.is_array() || index >= args.size() || !args[index].is_boolean()) return fallback;
    return args[index].get<bool>();
}

std::size_t remapLimitArgument(const nlohmann::json& args,
                               std::size_t index,
                               std::size_t fallback) {
    if (!args.is_array() || index >= args.size() || !args[index].is_number_integer()) return fallback;
    const auto raw = args[index].get<std::int64_t>();
    if (raw <= 0) return fallback;
    return std::min<std::size_t>(static_cast<std::size_t>(raw), 2000);
}

std::string remapLower(std::string_view value) {
    std::string out(value);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return out;
}

bool remapHasMaster(const fo4::Plugin& plugin, const fo4::PluginKey& owner) {
    return std::any_of(plugin.header.masters.begin(), plugin.header.masters.end(),
                       [&](const auto& master) { return master.key == owner; });
}

}  // namespace

Result<nlohmann::json> RadiclePluginBackend::changeReferencingRecords(
    const nlohmann::json& arguments) {
    const auto fromText = remapStringArgument(arguments, 0);
    const auto toText = remapStringArgument(arguments, 1);
    const auto patchName = remapStringArgument(arguments, 2);
    const bool apply = remapBoolArgument(arguments, 3, false);
    const auto cap = remapLimitArgument(arguments, 4, 300);
    if (fromText.empty() || toText.empty() || patchName.empty()) {
        return std::unexpected(Error{
            "ReferenceRemapArgumentsMissing",
            "ChangeReferencingRecords requires from, to, and patch plugin"
        });
    }

    OpLock lock(*this, OpLock::Mode::Write, {patchName});

    const auto resolveKey = [&](std::string_view text) -> Result<fo4::FormKey> {
        if (text.find(':') != std::string_view::npos) {
            try {
                return fo4::FormKey::parse(text);
            } catch (const std::exception& error) {
                return std::unexpected(Error{"InvalidFormKey", error.what()});
            }
        }
        const auto wanted = remapLower(text);
        for (auto pluginIt = plugins_.rbegin(); pluginIt != plugins_.rend(); ++pluginIt) {
            for (const auto* record : fo4::collect_records(pluginIt->plugin)) {
                if (remapLower(editorId(*record)) == wanted) return record->form_key;
            }
        }
        return std::unexpected(Error{
            "RecordNotFound",
            "No loaded record has EditorID '" + std::string(text) + "'"
        });
    };

    auto fromResult = resolveKey(fromText);
    if (!fromResult) return std::unexpected(fromResult.error());
    auto toResult = resolveKey(toText);
    if (!toResult) return std::unexpected(toResult.error());
    const auto fromKey = *fromResult;
    const auto toKey = *toResult;
    if (fromKey == toKey) {
        return std::unexpected(Error{"ReferenceRemapSameKey", "'from' and 'to' are the same record"});
    }
    const auto* toWinner = (*graph_).winner(toKey);
    if (toWinner == nullptr) {
        return std::unexpected(Error{
            "ReferenceRemapTargetMissing",
            "Replacement target does not resolve in the loaded environment: " + toKey.to_string()
        });
    }

    auto* patch = findPlugin(patchName);
    if (apply && patch == nullptr) {
        return std::unexpected(Error{
            "TargetPluginNotLoaded",
            "Patch plugin is not loaded yet: " + patchName
        });
    }

    std::size_t patchIndex = plugins_.size();
    if (patch != nullptr) {
        for (std::size_t index = 0; index < plugins_.size(); ++index) {
            if (&plugins_[index] == patch) {
                patchIndex = index;
                break;
            }
        }
        if (patchIndex == plugins_.size()) {
            return std::unexpected(Error{"PluginStateInvalid", "Patch load-order index could not be resolved"});
        }
    }

    if (const auto error = requireReferenceCoverage(fromKey, "reference remap")) {
        return std::unexpected(*error);
    }

    struct ReferenceWork {
        fo4::FormKey key;
        const LoadedPlugin* source{nullptr};
        std::size_t sourceIndex{0};
        std::string editorId;
        std::string type;
    };
    std::map<fo4::FormKey, ReferenceWork> unique;

    for (const auto& hit : (*graph_).referenced_by(fromKey)) {
        const auto* winner = (*graph_).winner(hit.source);
        if (winner == nullptr || winner->plugin != hit.plugin) continue;
        const auto* source = findPlugin(winner->plugin.filename());
        if (source == nullptr) continue;

        std::size_t sourceIndex = plugins_.size();
        for (std::size_t index = 0; index < plugins_.size(); ++index) {
            if (&plugins_[index] == source) {
                sourceIndex = index;
                break;
            }
        }
        if (sourceIndex == plugins_.size()) continue;
        const auto* record = fo4::find_record(source->plugin, hit.source);
        if (record == nullptr) continue;
        unique[hit.source] = {
            .key = hit.source,
            .source = source,
            .sourceIndex = sourceIndex,
            .editorId = editorId(*record),
            .type = record->header.signature.to_string(),
        };
    }

    if (unique.empty()) {
        return nlohmann::json(
            "Nothing references " + fromKey.to_string() + "; nothing to change.");
    }

    std::vector<ReferenceWork> work;
    work.reserve(std::min<std::size_t>(unique.size(), cap));
    for (const auto& [key, item] : unique) {
        static_cast<void>(key);
        if (work.size() >= cap) break;
        work.push_back(item);
    }

    if (!apply) {
        std::string preview =
            "DRY RUN: " + std::to_string(work.size()) + " record(s) reference " +
            fromKey.to_string() + "; would repoint them at " + toKey.to_string() +
            " in " + patchName + ":";
        const auto shown = std::min<std::size_t>(work.size(), 50);
        for (std::size_t index = 0; index < shown; ++index) {
            preview += "\n  " + work[index].type + " " + work[index].key.to_string();
            if (!work[index].editorId.empty()) preview += " '" + work[index].editorId + "'";
            preview += " in " + work[index].source->name;
        }
        if (work.size() > shown) {
            preview += "\n  ... and " + std::to_string(work.size() - shown) + " more";
        }
        if (unique.size() > work.size()) {
            preview += "\n  capped at " + std::to_string(cap) + " of " +
                       std::to_string(unique.size()) + " referencing record(s)";
        }
        preview += "\nRe-run with apply=true.";
        return nlohmann::json(std::move(preview));
    }

    if (patch == nullptr) {
        return std::unexpected(Error{
            "TargetPluginNotLoaded",
            "Patch plugin disappeared before reference-remap apply: " + patchName
        });
    }
    if (const auto guard = writeGuard(*patch)) return std::unexpected(*guard);
    for (const auto& item : work) {
        if (item.sourceIndex > patchIndex) {
            return std::unexpected(Error{
                "PatchLoadOrderTooEarly",
                "Refused before changing any records: '" + patchName +
                "' loads before '" + item.source->name + "', which carries the winning version of " +
                item.key.to_string() + ". Move the patch after that plugin first."
            });
        }
    }

    try {
        auto mutated = patch->plugin;
        const auto sigRefr = fo4::Signature::from_string("REFR");
        const auto sigAchr = fo4::Signature::from_string("ACHR");

        for (const auto& item : work) {
            if (fo4::find_record(mutated, item.key) != nullptr) continue;
            const auto* sourceRecord = fo4::find_record(item.source->plugin, item.key);
            if (sourceRecord == nullptr) {
                return std::unexpected(Error{
                    "ReferenceSourceMissing",
                    "Winning source record disappeared: " + item.key.to_string()
                });
            }

            if (sourceRecord->header.signature == sigRefr || sourceRecord->header.signature == sigAchr) {
                const auto status = fo4::forms::copy_placed_reference_as_override(
                    mutated, item.source->plugin, item.key, false);
                if (status != fo4::forms::PlacedReferenceCopyStatus::inserted) {
                    return std::unexpected(Error{
                        "ReferenceCopyFailed",
                        "Could not copy placed reference override: " + item.key.to_string()
                    });
                }
            } else {
                const auto status = fo4::copy_record_as_override(
                    mutated, item.source->plugin, item.key, false);
                if (status != fo4::RecordCopyStatus::inserted) {
                    const auto code = status == fo4::RecordCopyStatus::source_containment_unsupported
                        ? "ReferenceContainmentUnsupported"
                        : "ReferenceCopyFailed";
                    return std::unexpected(Error{
                        code,
                        "Could not copy winning record into patch: " + item.key.to_string()
                    });
                }
            }
        }

        if (toKey.owner() != mutated.key && !remapHasMaster(mutated, toKey.owner())) {
            auto kind = toKey.owner().looks_light() ? fo4::PluginKind::light : fo4::PluginKind::full;
            for (const auto& candidate : plugins_) {
                if (candidate.plugin.key != toKey.owner()) continue;
                kind = fo4::classify_plugin_kind(candidate.plugin.header.tes4_flags, candidate.plugin.key);
                break;
            }
            std::vector<fo4::MasterDescriptor> masters;
            masters.reserve(mutated.header.masters.size() + 1);
            for (const auto& master : mutated.header.masters) {
                masters.push_back({master.key, master.kind});
            }
            masters.push_back({toKey.owner(), kind});
            mutated = fo4::remap_plugin_masters(std::move(mutated), std::move(masters));
        }

        fo4::rekey_form_links(mutated, fromKey, toKey);
        stagePluginMutationLocked(*patch, std::move(mutated));

        return nlohmann::json(
            "Repointed " + std::to_string(work.size()) + " of " +
            std::to_string(unique.size()) + " referencing record(s) from " +
            fromKey.to_string() + " to " + toKey.to_string() + " in " + patchName +
            ". Ensure " + patchName +
            " loads after the plugins it copied from. SavePlugin to persist.");
    } catch (const std::exception& error) {
        return std::unexpected(Error{"ReferenceRemapFailed", error.what()});
    }
}

}  // namespace fo4ide::plugin
