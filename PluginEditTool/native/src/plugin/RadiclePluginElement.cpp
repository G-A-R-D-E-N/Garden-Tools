#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "RadiclePluginSupport.hpp"

#include <fo4/plugin/field_access.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/plugin/subrecord_authoring.hpp>
#include <fo4/plugin/writer.hpp>
#include <fo4/schema/formlink_lists.hpp>
#include <fo4/schema/scalar.hpp>

namespace fo4ide::plugin {
namespace {

std::string elementStringArgument(const nlohmann::json& args, std::size_t index) {
    if (!args.is_array() || index >= args.size() || !args[index].is_string()) return {};
    return args[index].get<std::string>();
}

bool elementDeltaArgument(const nlohmann::json& args, std::size_t index, std::int64_t& value) {
    if (!args.is_array() || index >= args.size() || !args[index].is_number_integer()) return false;
    value = args[index].get<std::int64_t>();
    return true;
}

std::optional<support::AuthoringPath> authoringPath(const fo4::Record& record, std::string_view path) {
    const auto parsed = support::parseAuthoringPath(path);
    if (!parsed) return std::nullopt;
    const auto constraint = fo4::schema::record_authoring_constraint(record.header.signature);
    if (!constraint) return std::nullopt;
    const auto rules = fo4::schema::authoring_subrecords(*constraint);
    if (std::none_of(rules.begin(), rules.end(), [&](const auto& rule) {
            return rule.signature == parsed->signature;
        })) return std::nullopt;
    return parsed;
}

std::optional<Error> authoringStatusError(fo4::SubrecordAddStatus status) {
    switch (status) {
        case fo4::SubrecordAddStatus::added:
        case fo4::SubrecordAddStatus::already_present: return std::nullopt;
        case fo4::SubrecordAddStatus::record_missing:
            return Error{"RecordNotFound", "Record is not present"};
        case fo4::SubrecordAddStatus::not_authorable:
            return Error{"ElementNotAuthorable", "Subrecord is coupled or has no independent authoring rule"};
        case fo4::SubrecordAddStatus::unknown_order:
            return Error{"ElementUnknownOrder", "A valid insertion order cannot be established"};
        case fo4::SubrecordAddStatus::no_default:
            return Error{"ElementNoDefault", "No usable default is proven for this subrecord"};
    }
    return Error{"ElementAddFailed", "Unknown authoring status"};
}

std::optional<Error> authoringStatusError(fo4::SubrecordRemoveStatus status) {
    switch (status) {
        case fo4::SubrecordRemoveStatus::removed: return std::nullopt;
        case fo4::SubrecordRemoveStatus::record_missing:
            return Error{"RecordNotFound", "Record is not present"};
        case fo4::SubrecordRemoveStatus::absent:
            return Error{"ElementIndexOutOfRange", "Subrecord occurrence is not present"};
        case fo4::SubrecordRemoveStatus::below_minimum:
            return Error{"ElementBelowMinimum", "Removal would violate the required subrecord count"};
        case fo4::SubrecordRemoveStatus::not_authorable:
            return Error{"ElementNotAuthorable", "Subrecord is coupled or has no independent authoring rule"};
        case fo4::SubrecordRemoveStatus::unknown_order:
            return Error{"ElementUnknownOrder",
                "Removal cannot be proven safe: the record has subrecords the authoring order does not describe"};
    }
    return Error{"ElementRemoveFailed", "Unknown authoring status"};
}

std::string normaliseElementPath(fo4::Signature recordSignature, std::string_view input) {
    std::string path(input);
    std::replace(path.begin(), path.end(), '.', '\\');
    std::replace(path.begin(), path.end(), '/', '\\');

    std::string collapsed;
    collapsed.reserve(path.size());
    for (const char ch : path) {
        if (ch == '\\' && !collapsed.empty() && collapsed.back() == '\\') continue;
        collapsed.push_back(ch);
    }
    while (!collapsed.empty() && collapsed.front() == '\\') collapsed.erase(collapsed.begin());

    const auto prefix = recordSignature.to_string() + "\\";
    if (!collapsed.starts_with(prefix) && !collapsed.starts_with("Subrecords\\")) {
        collapsed = prefix + collapsed;
    }
    return collapsed;
}

struct ObjectTemplateTarget {
    std::optional<std::size_t> index;
};

std::optional<ObjectTemplateTarget> objectTemplateTarget(const fo4::Record& record, std::string_view input) {
    const auto signature = record.header.signature;
    if (signature != fo4::sig_armo && signature != fo4::sig_furn && signature != fo4::sig_npc_ &&
        signature != fo4::sig_weap) {
        return std::nullopt;
    }
    std::string path = normaliseElementPath(signature, input);
    const auto prefix = signature.to_string() + "\\";
    if (!path.starts_with(prefix)) return std::nullopt;
    path.erase(0, prefix.size());
    if (path.ends_with("\\Count")) path.resize(path.size() - 6);
    ObjectTemplateTarget target;
    if (path.ends_with(']')) {
        const auto open = path.rfind('[');
        if (open == std::string::npos) return std::nullopt;
        std::size_t index = 0;
        if (!support::parseIndex(std::string_view(path).substr(open + 1, path.size() - open - 2), index)) {
            return std::nullopt;
        }
        target.index = index;
        path.resize(open);
    }
    if (path != "ObjectTemplates" && path != "Authoring\\OBTE" && path != "Authoring\\OBTS") return std::nullopt;
    return target;
}

std::string objectTemplateGroup(const fo4::Record& record) {
    return record.header.signature.to_string() + "\\ObjectTemplateCombinations";
}

Error objectTemplateBlockError(const fo4::schema::SubrecordGroupError& error) {
    using Kind = fo4::schema::SubrecordGroupBlockError::Kind;
    if (const auto* block = dynamic_cast<const fo4::schema::SubrecordGroupBlockError*>(&error)) {
        switch (block->kind()) {
            case Kind::no_insertion_point:
                return Error{"ElementUnknownOrder", "A valid insertion order cannot be established"};
            case Kind::referenced:
                return Error{"ElementTemplateReferenced",
                    "Another combination names this one or a later one as its parent; repoint its parent first"};
            case Kind::malformed: break;
        }
    }
    return Error{"ElementTemplateBlockMalformed", "The object template block is malformed and was left untouched"};
}

std::optional<Error> objectTemplateError(fo4::FieldStatus status) {
    switch (status) {
        case fo4::FieldStatus::ok: return std::nullopt;
        case fo4::FieldStatus::record_missing: return Error{"RecordNotFound", "Record is not present"};
        case fo4::FieldStatus::record_mismatch:
        case fo4::FieldStatus::unknown_path:
        case fo4::FieldStatus::wrong_kind:
            return Error{"ElementNotAuthorable", "Record type carries no object templates"};
        case fo4::FieldStatus::index_out_of_range:
            return Error{"ElementIndexOutOfRange", "Object template combination is not present"};
        default: break;
    }
    return Error{"ElementMutationFailed", "Unknown object template status"};
}

struct RawSubrecordPath {
    fo4::Signature signature{};
    std::size_t ordinal{0};
};

std::optional<RawSubrecordPath> rawSubrecordPath(std::string_view path) {
    constexpr std::string_view prefix = "Subrecords\\";
    if (!path.starts_with(prefix) || path.empty() || path.back() != ']') return std::nullopt;
    const auto open = path.rfind('[');
    if (open == std::string_view::npos || open <= prefix.size()) return std::nullopt;
    const auto signatureText = path.substr(prefix.size(), open - prefix.size());
    if (signatureText.size() != 4) return std::nullopt;
    std::size_t ordinal = 0;
    if (!support::parseIndex(path.substr(open + 1, path.size() - open - 2), ordinal)) {
        return std::nullopt;
    }
    try {
        return RawSubrecordPath{
            .signature = fo4::Signature::from_string(signatureText),
            .ordinal = ordinal,
        };
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

struct ResolvedElementList {
    bool nested{false};
    fo4::schema::FormLinkListDescriptor simple{};
    fo4::schema::NestedFormLinkListDescriptor nestedDescriptor{};
    std::size_t containerIndex{0};
    std::optional<std::size_t> index;
    std::string basePath;
};

std::optional<ResolvedElementList> resolveRawElementPath(
    const fo4::Record& record,
    const RawSubrecordPath& raw,
    std::string& error) {
    std::optional<ResolvedElementList> match;

    for (const auto& descriptor : fo4::schema::k_generated_formlink_lists) {
        if (descriptor.record != record.header.signature || descriptor.subrecord != raw.signature) continue;
        if (match.has_value()) {
            error = "Raw subrecord path is ambiguous across multiple schema lists";
            return std::nullopt;
        }
        match = ResolvedElementList{
            .nested = false,
            .simple = descriptor,
            .index = raw.ordinal,
            .basePath = std::string(descriptor.path),
        };
    }

    for (const auto& descriptor : fo4::schema::k_generated_nested_formlink_lists) {
        if (descriptor.record != record.header.signature || descriptor.subrecord != raw.signature) continue;
        if (match.has_value()) {
            error = "Raw subrecord path is ambiguous across simple and nested schema lists";
            return std::nullopt;
        }

        std::size_t currentContainer = 0;
        std::size_t localIndex = 0;
        std::size_t globalOrdinal = 0;
        bool found = false;
        for (const auto& subrecord : record.subrecords) {
            if (subrecord.signature == descriptor.terminator) {
                ++currentContainer;
                localIndex = 0;
                continue;
            }
            if (subrecord.signature != descriptor.subrecord) continue;
            if (globalOrdinal == raw.ordinal) {
                match = ResolvedElementList{
                    .nested = true,
                    .nestedDescriptor = descriptor,
                    .containerIndex = currentContainer,
                    .index = localIndex,
                    .basePath = std::string(descriptor.container_path) + "[" +
                                std::to_string(currentContainer) + "]\\" +
                                std::string(descriptor.member),
                };
                found = true;
                break;
            }
            ++globalOrdinal;
            ++localIndex;
        }
        if (!found) {
            error = "Raw nested-list element index is out of range";
            return std::nullopt;
        }
    }

    if (!match.has_value()) {
        error = "Subrecord is not registered as a native generic list";
    }
    return match;
}

std::optional<ResolvedElementList> resolveElementPath(
    const fo4::Record& record,
    std::string_view input,
    std::string& error) {
    auto path = normaliseElementPath(record.header.signature, input);
    if (path.ends_with("\\Count")) path.resize(path.size() - 6);
    if (const auto raw = rawSubrecordPath(path); raw.has_value()) {
        return resolveRawElementPath(record, *raw, error);
    }

    if (const auto indexed = fo4::schema::indexed_formlink_list_path(path); indexed.has_value()) {
        if (indexed->descriptor.record != record.header.signature) {
            error = "List path does not apply to this record type";
            return std::nullopt;
        }
        return ResolvedElementList{
            .nested = false,
            .simple = indexed->descriptor,
            .index = indexed->index,
            .basePath = std::string(indexed->descriptor.path),
        };
    }

    if (const auto descriptor = fo4::schema::formlink_list_descriptor(
            record.header.signature, path); descriptor.has_value()) {
        return ResolvedElementList{
            .nested = false,
            .simple = *descriptor,
            .index = std::nullopt,
            .basePath = std::string(descriptor->path),
        };
    }

    if (const auto indexed = fo4::schema::indexed_nested_formlink_list_path(path);
        indexed.has_value()) {
        if (indexed->descriptor.record != record.header.signature) {
            error = "Nested list path does not apply to this record type";
            return std::nullopt;
        }
        return ResolvedElementList{
            .nested = true,
            .nestedDescriptor = indexed->descriptor,
            .containerIndex = indexed->container_index,
            .index = indexed->index,
            .basePath = std::string(indexed->descriptor.container_path) + "[" +
                        std::to_string(indexed->container_index) + "]\\" +
                        std::string(indexed->descriptor.member),
        };
    }

    if (const auto scoped = fo4::schema::scoped_nested_formlink_list_path(path);
        scoped.has_value()) {
        if (scoped->descriptor.record != record.header.signature) {
            error = "Nested list path does not apply to this record type";
            return std::nullopt;
        }
        return ResolvedElementList{
            .nested = true,
            .nestedDescriptor = scoped->descriptor,
            .containerIndex = scoped->container_index,
            .index = std::nullopt,
            .basePath = std::string(scoped->descriptor.container_path) + "[" +
                        std::to_string(scoped->container_index) + "]\\" +
                        std::string(scoped->descriptor.member),
        };
    }

    error = "Path is not a schema-registered native list";
    return std::nullopt;
}

std::optional<std::size_t> elementListCount(
    const fo4::Plugin& plugin,
    const fo4::FormKey& recordKey,
    const ResolvedElementList& path) {
    if (path.nested) {
        return fo4::schema::record_nested_formlink_list_size(
            plugin, recordKey, path.nestedDescriptor, path.containerIndex);
    }
    return fo4::schema::record_formlink_list_size(plugin, recordKey, path.simple);
}

bool elementNullable(const ResolvedElementList& path) {
    return path.nested ? path.nestedDescriptor.nullable : path.simple.nullable;
}

bool elementCanInsert(const ResolvedElementList& path) {
    return path.nested ? path.nestedDescriptor.can_insert : path.simple.can_insert;
}

std::string elementTemplate(const ResolvedElementList& path) {
    const auto targets = path.nested
        ? fo4::schema::nested_formlink_list_targets(path.nestedDescriptor)
        : fo4::schema::formlink_list_targets(path.simple);
    if (targets.empty()) return "FormLink";
    if (targets.size() == 1) return "FormLink<" + targets.front().to_string() + ">";
    return "FormLink";
}

std::optional<Error> mutationStatusError(
    fo4::schema::FormLinkListMutationStatus status,
    std::string_view path) {
    switch (status) {
        case fo4::schema::FormLinkListMutationStatus::updated:
        case fo4::schema::FormLinkListMutationStatus::unchanged:
            return std::nullopt;
        case fo4::schema::FormLinkListMutationStatus::record_missing:
            return Error{"RecordNotFound", "Record is not present while editing " + std::string(path)};
        case fo4::schema::FormLinkListMutationStatus::record_mismatch:
            return Error{"ElementRecordMismatch", "List path does not match the record type: " + std::string(path)};
        case fo4::schema::FormLinkListMutationStatus::container_index_out_of_range:
            return Error{"ElementContainerIndexOutOfRange", "Nested list container is out of range: " + std::string(path)};
        case fo4::schema::FormLinkListMutationStatus::index_out_of_range:
            return Error{"ElementIndexOutOfRange", "List element index is out of range: " + std::string(path)};
        case fo4::schema::FormLinkListMutationStatus::target_type_mismatch:
            return Error{"ElementTargetTypeMismatch", "List element target type is invalid: " + std::string(path)};
        case fo4::schema::FormLinkListMutationStatus::insertion_boundary_unavailable:
            return Error{
                "ElementInsertionBoundaryUnavailable",
                "A safe binary insertion boundary is not available for: " + std::string(path)
            };
    }
    return Error{"ElementMutationFailed", "Unknown list mutation status"};
}

struct GenericElementList {
    std::string path;
    std::optional<std::size_t> index;
    std::size_t count{0};
    bool group{false};
};

// Invariant: a generic list is a counted_array or subrecord_group schema row; a trailing [n] is the element index and an inner Parent[i]\Child scope stays in the path.
std::optional<GenericElementList> resolveGenericList(
    const fo4::Plugin& plugin,
    const fo4::FormKey& recordKey,
    const fo4::Record& record,
    std::string_view input,
    std::string& error) {
    auto path = normaliseElementPath(record.header.signature, input);
    if (path.starts_with("Subrecords\\")) return std::nullopt;
    if (path.ends_with("\\Count")) path.resize(path.size() - 6);
    GenericElementList list;
    if (path.ends_with(']')) {
        const auto open = path.rfind('[');
        std::size_t index = 0;
        if (open == std::string::npos ||
            !support::parseIndex(std::string_view(path).substr(open + 1, path.size() - open - 2), index)) {
            return std::nullopt;
        }
        list.index = index;
        path.resize(open);
    }
    if (path.find('[') == std::string::npos) {
        const auto rows = fo4::record_field_schema_rows(record.header.signature);
        const auto row = std::find_if(rows.begin(), rows.end(), [&](const auto& candidate) {
            return candidate.path == path;
        });
        if (row == rows.end()) return std::nullopt;
        if (row->kind != fo4::FieldKind::counted_array && row->kind != fo4::FieldKind::subrecord_group) {
            return std::nullopt;
        }
        list.group = row->kind == fo4::FieldKind::subrecord_group;
    } else {
        fo4::ScopedGroupPath scoped;
        const auto status = fo4::resolve_group_path(plugin, recordKey, path, scoped);
        if (status == fo4::FieldStatus::index_out_of_range) {
            error = "List parent element index is out of range";
            return std::nullopt;
        }
        if (status != fo4::FieldStatus::ok) return std::nullopt;
        list.group = true;
    }
    if (fo4::record_list_size(plugin, recordKey, path, list.count) != fo4::FieldStatus::ok) {
        error = "List container is not present in this record";
        return std::nullopt;
    }
    list.path = std::move(path);
    return list;
}

std::optional<Error> fieldStatusError(fo4::FieldStatus status, std::string_view path) {
    switch (status) {
        case fo4::FieldStatus::ok: return std::nullopt;
        case fo4::FieldStatus::record_missing:
            return Error{"RecordNotFound", "Record is not present while editing " + std::string(path)};
        case fo4::FieldStatus::record_mismatch:
            return Error{"ElementRecordMismatch", "List path does not match the record type: " + std::string(path)};
        case fo4::FieldStatus::unknown_path:
        case fo4::FieldStatus::wrong_kind:
            return Error{"ElementPathUnsupported", "Path is not a schema-registered native list: " + std::string(path)};
        case fo4::FieldStatus::occurrence_missing:
            return Error{"ElementContainerMissing", "List container is not present: " + std::string(path)};
        case fo4::FieldStatus::index_out_of_range:
            return Error{"ElementIndexOutOfRange", "List element index is out of range: " + std::string(path)};
        case fo4::FieldStatus::target_type_mismatch:
            return Error{"ElementTargetTypeMismatch", "List element target type is invalid: " + std::string(path)};
        case fo4::FieldStatus::boundary_unavailable:
            return Error{"ElementInsertionBoundaryUnavailable",
                         "A safe binary insertion boundary is not available for: " + std::string(path)};
        case fo4::FieldStatus::would_empty:
            return Error{"ElementBelowMinimum", "Removal would leave the list empty: " + std::string(path)};
        case fo4::FieldStatus::next_alias_id_too_low:
            break;
    }
    return Error{"ElementMutationFailed", "List mutation was refused: " + std::string(path)};
}

// Requires: from and to are below list.count; a group moved to the last position is appended by write_record_field, which adopts its optional members.
fo4::FieldStatus moveGenericElement(fo4::Plugin& plugin, const fo4::FormKey& recordKey,
                                    const GenericElementList& list, std::size_t from, std::size_t to) {
    const auto* record = fo4::find_record(plugin, recordKey);
    if (record == nullptr) return fo4::FieldStatus::record_missing;
    std::string hex;
    if (const auto status = fo4::read_record_field(plugin, *record, list.path, from, hex);
        status != fo4::FieldStatus::ok) {
        return status;
    }
    if (const auto status = fo4::remove_record_list_element(plugin, recordKey, list.path, from);
        status != fo4::FieldStatus::ok) {
        return status;
    }
    if (!(list.group && to + 1 == list.count)) {
        if (const auto status = fo4::insert_record_list_element(plugin, recordKey, list.path, to, nullptr);
            status != fo4::FieldStatus::ok) {
            return status;
        }
    }
    return fo4::write_record_field(plugin, recordKey, list.path, to, hex);
}

Result<fo4::FormKey> elementRecordKey(std::string_view text) {
    if (text.empty()) return std::unexpected(Error{"RecordMissing", "Record FormKey is empty"});
    try {
        return fo4::FormKey::parse(text);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"InvalidFormKey", error.what()});
    }
}

}  // namespace

Result<nlohmann::json> RadiclePluginBackend::describeElement(
    const nlohmann::json& arguments) const {
    const auto pluginName = elementStringArgument(arguments, 0);
    const auto recordText = elementStringArgument(arguments, 1);
    const auto requestedPath = elementStringArgument(arguments, 2);
    auto keyResult = elementRecordKey(recordText);
    if (!keyResult) return std::unexpected(keyResult.error());

    OpLock lock(*this, OpLock::Mode::Read);
    const auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) {
        return nlohmann::json(nlohmann::json{{"error", "Plugin is not loaded: " + pluginName}}.dump());
    }
    const auto* record = fo4::find_record(loaded->plugin, *keyResult);
    if (record == nullptr) {
        return nlohmann::json(nlohmann::json{{"error", "Record '" + recordText + "' not found."}}.dump());
    }

    try {
        if (const auto target = objectTemplateTarget(*record, requestedPath)) {
            const auto group = objectTemplateGroup(*record);
            std::size_t count = 0;
            if (fo4::record_list_size(loaded->plugin, *keyResult, group, count) != fo4::FieldStatus::ok) {
                return nlohmann::json(nlohmann::json{{"error", "List container is not present in this record"}}.dump());
            }
            if (target->index.has_value() && *target->index >= count) {
                return nlohmann::json(nlohmann::json{{"error", "List element index is out of range."}}.dump());
            }
            bool canAdd = false;
            bool canRemove = false;
            if (!writeGuard(*loaded)) {
                const auto succeeds = [&](auto&& mutate) {
                    auto probe = loaded->plugin;
                    try {
                        return mutate(probe) == fo4::FieldStatus::ok;
                    } catch (const fo4::schema::SubrecordGroupBlockError& refused) {
                        if (refused.kind() == fo4::schema::SubrecordGroupBlockError::Kind::malformed) throw;
                        return false;
                    }
                };
                canAdd = !target->index.has_value() && succeeds([&](fo4::Plugin& probe) {
                    return fo4::insert_record_list_element(probe, *keyResult, group, count, nullptr);
                });
                canRemove = target->index.has_value() && succeeds([&](fo4::Plugin& probe) {
                    return fo4::remove_record_list_element(probe, *keyResult, group, *target->index);
                });
            }
            return nlohmann::json(nlohmann::json{
                {"canAdd", canAdd}, {"canRemove", canRemove}, {"canCreate", false},
                {"templates", canAdd ? nlohmann::json::array({"ObjectTemplate"}) : nlohmann::json::array()},
                {"elementType", "ObjectTemplate"}, {"canMoveUp", false}, {"canMoveDown", false},
                {"canClear", false}, {"count", count}
            }.dump());
        }
        if (const auto path = authoringPath(*record, requestedPath)) {
            const auto count = std::count_if(record->subrecords.begin(), record->subrecords.end(),
                [&](const auto& subrecord) { return subrecord.signature == path->signature; });
            bool canAdd = false;
            bool canRemove = false;
            bool canCreate = false;
            if (!writeGuard(*loaded)) {
                auto probe = loaded->plugin;
                canAdd = fo4::add_record_subrecord(probe, *keyResult, path->signature) ==
                    fo4::SubrecordAddStatus::added;
                if (path->occurrence) {
                    probe = loaded->plugin;
                    canRemove = fo4::remove_record_subrecord(probe, *keyResult, path->signature,
                        *path->occurrence) == fo4::SubrecordRemoveStatus::removed;
                }
                probe = loaded->plugin;
                canCreate = fo4::create_record(probe, record->header.signature, "").status ==
                    fo4::RecordCreateStatus::created;
            }
            return nlohmann::json(nlohmann::json{
                {"canAdd", canAdd}, {"canRemove", canRemove}, {"canCreate", canCreate},
                {"templates", canAdd ? nlohmann::json::array({path->signature.to_string()}) : nlohmann::json::array()},
                {"elementType", "Subrecord"}, {"canMoveUp", false}, {"canMoveDown", false},
                {"canClear", false}, {"count", count}
            }.dump());
        }
        std::string error;
        const auto resolved = resolveElementPath(*record, requestedPath, error);
        if (!resolved.has_value()) {
            std::string genericError;
            const auto generic = resolveGenericList(loaded->plugin, *keyResult, *record, requestedPath, genericError);
            if (!generic.has_value()) {
                return nlohmann::json(nlohmann::json{{"error", genericError.empty() ? std::move(error) : std::move(genericError)}}.dump());
            }
            if (generic->index.has_value() && *generic->index >= generic->count) {
                return nlohmann::json(nlohmann::json{{"error", "List element index is out of range."}}.dump());
            }
            const std::string elementType = generic->group ? "Struct" : "Element";
            const bool writable = !writeGuard(*loaded).has_value();
            const bool indexed = generic->index.has_value();
            const auto index = generic->index.value_or(0);
            const auto succeeds = [&](auto&& mutate) {
                auto probe = loaded->plugin;
                try {
                    return mutate(probe) == fo4::FieldStatus::ok;
                } catch (const std::exception&) {
                    return false;
                }
            };
            const bool canAdd = writable && succeeds([&](fo4::Plugin& probe) {
                return fo4::insert_record_list_element(
                    probe, *keyResult, generic->path, generic->index.value_or(generic->count), nullptr);
            });
            const bool canRemove = writable && indexed && succeeds([&](fo4::Plugin& probe) {
                return fo4::remove_record_list_element(probe, *keyResult, generic->path, index);
            });
            const bool canMoveUp = writable && indexed && index > 0 && succeeds([&](fo4::Plugin& probe) {
                return moveGenericElement(probe, *keyResult, *generic, index, index - 1);
            });
            const bool canMoveDown = writable && indexed && index + 1 < generic->count &&
                succeeds([&](fo4::Plugin& probe) {
                    return moveGenericElement(probe, *keyResult, *generic, index, index + 1);
                });
            return nlohmann::json(nlohmann::json{
                {"canAdd", canAdd},
                {"templates", canAdd ? nlohmann::json::array({elementType}) : nlohmann::json::array()},
                {"elementType", elementType},
                {"canRemove", canRemove},
                {"canMoveUp", canMoveUp},
                {"canMoveDown", canMoveDown},
                {"canClear", false},
                {"count", generic->count}
            }.dump());
        }
        const auto count = elementListCount(loaded->plugin, *keyResult, *resolved);
        if (!count.has_value()) {
            return nlohmann::json(nlohmann::json{{"error", "List container is not present in this record."}}.dump());
        }
        if (resolved->index.has_value() && *resolved->index >= *count) {
            return nlohmann::json(nlohmann::json{{"error", "List element index is out of range."}}.dump());
        }

        const bool writable = !writeGuard(*loaded).has_value();
        bool canAdd = false;
        if (writable && elementCanInsert(*resolved) && elementNullable(*resolved)) {
            auto probe = loaded->plugin;
            const auto insertion = resolved->index.value_or(*count);
            const auto status = resolved->nested
                ? fo4::schema::insert_record_nested_formlink_list_null_element(
                    probe, *keyResult, resolved->nestedDescriptor, resolved->containerIndex, insertion)
                : fo4::schema::insert_record_formlink_list_null_element(
                    probe, *keyResult, resolved->simple, insertion);
            canAdd = status == fo4::schema::FormLinkListMutationStatus::updated;
        }
        nlohmann::json templates = nlohmann::json::array();
        if (canAdd) templates.push_back(elementTemplate(*resolved));

        const bool indexed = resolved->index.has_value();
        const auto index = resolved->index.value_or(0);
        return nlohmann::json(nlohmann::json{
            {"canAdd", canAdd},
            {"templates", std::move(templates)},
            {"elementType", "FormLink"},
            {"canRemove", writable && indexed},
            {"canMoveUp", writable && indexed && index > 0},
            {"canMoveDown", writable && indexed && index + 1 < *count},
            {"canClear", writable && !indexed && *count > 0},
            {"count", *count}
        }.dump());
    } catch (const std::exception& error) {
        return nlohmann::json(nlohmann::json{{"error", error.what()}}.dump());
    }
}

Result<nlohmann::json> RadiclePluginBackend::addElement(
    const nlohmann::json& arguments) {
    const auto pluginName = elementStringArgument(arguments, 0);
    const auto recordText = elementStringArgument(arguments, 1);
    const auto requestedPath = elementStringArgument(arguments, 2);
    auto keyResult = elementRecordKey(recordText);
    if (!keyResult) return std::unexpected(keyResult.error());

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    if (const auto guard = writeGuard(*loaded)) return std::unexpected(*guard);
    const auto* record = fo4::find_record(loaded->plugin, *keyResult);
    if (record == nullptr) return std::unexpected(Error{"RecordNotFound", "Record is not present: " + recordText});

    try {
        if (const auto target = objectTemplateTarget(*record, requestedPath)) {
            if (target->index.has_value()) {
                return std::unexpected(Error{"ElementInsertIndexUnsupported",
                    "Object template combinations are appended; omit the index"});
            }
            const auto group = objectTemplateGroup(*record);
            std::size_t count = 0;
            auto mutated = loaded->plugin;
            try {
                const auto sized = fo4::record_list_size(mutated, *keyResult, group, count);
                const auto status = sized != fo4::FieldStatus::ok
                    ? sized
                    : fo4::insert_record_list_element(mutated, *keyResult, group, count, nullptr);
                if (const auto error = objectTemplateError(status)) return std::unexpected(*error);
            } catch (const fo4::schema::SubrecordGroupError& error) {
                return std::unexpected(objectTemplateBlockError(error));
            }
            stagePluginMutationLocked(*loaded, std::move(mutated));
            return nlohmann::json("Added ObjectTemplate " + std::to_string(count) + " in " + pluginName + ".");
        }
        if (const auto path = authoringPath(*record, requestedPath)) {
            auto mutated = loaded->plugin;
            const auto status = fo4::add_record_subrecord(mutated, *keyResult, path->signature);
            if (const auto error = authoringStatusError(status)) return std::unexpected(*error);
            if (status == fo4::SubrecordAddStatus::already_present) return nlohmann::json("Subrecord is already present.");
            stagePluginMutationLocked(*loaded, std::move(mutated));
            return nlohmann::json("Added " + path->signature.to_string() + " in " + pluginName + ".");
        }
        const auto templateText = elementStringArgument(arguments, 3);
        if (templateText.size() == 4) {
            const auto signature = fo4::Signature::from_string(templateText);
            auto mutated = loaded->plugin;
            const auto status = fo4::add_record_subrecord(mutated, *keyResult, signature);
            if (const auto error = authoringStatusError(status)) return std::unexpected(*error);
            if (status == fo4::SubrecordAddStatus::already_present) return nlohmann::json("Subrecord is already present.");
            stagePluginMutationLocked(*loaded, std::move(mutated));
            return nlohmann::json("Added " + signature.to_string() + " in " + pluginName + ".");
        }
        std::string error;
        const auto resolved = resolveElementPath(*record, requestedPath, error);
        if (!resolved.has_value()) {
            const auto generic = resolveGenericList(loaded->plugin, *keyResult, *record, requestedPath, error);
            if (!generic.has_value()) return std::unexpected(Error{"ElementPathUnsupported", std::move(error)});
            const auto insertion = generic->index.value_or(generic->count);
            if (insertion > generic->count) {
                return std::unexpected(Error{"ElementIndexOutOfRange", "List element index is out of range: " + generic->path});
            }
            auto mutated = loaded->plugin;
            const auto status = fo4::insert_record_list_element(mutated, *keyResult, generic->path, insertion, nullptr);
            if (const auto statusError = fieldStatusError(status, generic->path)) return std::unexpected(*statusError);
            stagePluginMutationLocked(*loaded, std::move(mutated));
            return nlohmann::json(
                "Added an element at " + generic->path + "[" + std::to_string(insertion) + "] in " + pluginName + ".");
        }
        if (!elementNullable(*resolved)) {
            return std::unexpected(Error{
                "ElementTemplateUnsupported",
                "Native generic insertion currently requires a schema-nullable FormLink list: " + resolved->basePath
            });
        }
        const auto count = elementListCount(loaded->plugin, *keyResult, *resolved);
        if (!count.has_value()) return std::unexpected(Error{"ElementContainerMissing", "List container is not present"});
        const auto insertion = resolved->index.value_or(*count);

        auto mutated = loaded->plugin;
        const auto status = resolved->nested
            ? fo4::schema::insert_record_nested_formlink_list_null_element(
                  mutated, *keyResult, resolved->nestedDescriptor,
                  resolved->containerIndex, insertion)
            : fo4::schema::insert_record_formlink_list_null_element(
                  mutated, *keyResult, resolved->simple, insertion);
        if (const auto statusError = mutationStatusError(status, resolved->basePath)) {
            return std::unexpected(*statusError);
        }
        if (status == fo4::schema::FormLinkListMutationStatus::unchanged) {
            return nlohmann::json("No change for " + resolved->basePath + " in " + pluginName + ".");
        }

        stagePluginMutationLocked(*loaded, std::move(mutated));
        return nlohmann::json(
            "Added a FormLink at " + resolved->basePath + "[" +
            std::to_string(insertion) + "] in " + pluginName + ".");
    } catch (const std::exception& error) {
        return std::unexpected(Error{"ElementAddFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::removeElement(
    const nlohmann::json& arguments) {
    const auto pluginName = elementStringArgument(arguments, 0);
    const auto recordText = elementStringArgument(arguments, 1);
    const auto requestedPath = elementStringArgument(arguments, 2);
    auto keyResult = elementRecordKey(recordText);
    if (!keyResult) return std::unexpected(keyResult.error());

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    if (const auto guard = writeGuard(*loaded)) return std::unexpected(*guard);
    const auto* record = fo4::find_record(loaded->plugin, *keyResult);
    if (record == nullptr) return std::unexpected(Error{"RecordNotFound", "Record is not present: " + recordText});

    try {
        if (const auto target = objectTemplateTarget(*record, requestedPath)) {
            if (!target->index.has_value()) return std::unexpected(Error{"ElementIndexRequired", "Removal requires a combination index"});
            auto mutated = loaded->plugin;
            try {
                const auto status =
                    fo4::remove_record_list_element(mutated, *keyResult, objectTemplateGroup(*record), *target->index);
                if (const auto error = objectTemplateError(status)) return std::unexpected(*error);
            } catch (const fo4::schema::SubrecordGroupError& error) {
                return std::unexpected(objectTemplateBlockError(error));
            }
            stagePluginMutationLocked(*loaded, std::move(mutated));
            return nlohmann::json("Removed ObjectTemplate " + std::to_string(*target->index) + " in " + pluginName + ".");
        }
        if (const auto path = authoringPath(*record, requestedPath)) {
            if (!path->occurrence) return std::unexpected(Error{"ElementIndexRequired", "Removal requires a subrecord occurrence"});
            auto mutated = loaded->plugin;
            const auto status = fo4::remove_record_subrecord(mutated, *keyResult, path->signature, *path->occurrence);
            if (const auto error = authoringStatusError(status)) return std::unexpected(*error);
            stagePluginMutationLocked(*loaded, std::move(mutated));
            return nlohmann::json("Removed " + path->signature.to_string() + " in " + pluginName + ".");
        }
        std::string error;
        const auto resolved = resolveElementPath(*record, requestedPath, error);
        if (!resolved.has_value()) {
            const auto generic = resolveGenericList(loaded->plugin, *keyResult, *record, requestedPath, error);
            if (!generic.has_value()) return std::unexpected(Error{"ElementPathUnsupported", std::move(error)});
            if (!generic->index.has_value()) {
                return std::unexpected(Error{"ElementIndexRequired", "RemoveElement requires a list entry path"});
            }
            auto mutated = loaded->plugin;
            const auto status = fo4::remove_record_list_element(mutated, *keyResult, generic->path, *generic->index);
            if (const auto statusError = fieldStatusError(status, generic->path)) return std::unexpected(*statusError);
            stagePluginMutationLocked(*loaded, std::move(mutated));
            return nlohmann::json(
                "Removed " + requestedPath + " from " + recordText + " in " + pluginName + ".");
        }
        if (!resolved->index.has_value()) {
            return std::unexpected(Error{"ElementIndexRequired", "RemoveElement requires a list entry path"});
        }

        auto mutated = loaded->plugin;
        const auto status = resolved->nested
            ? fo4::schema::remove_record_nested_formlink_list_element(
                  mutated, *keyResult, resolved->nestedDescriptor,
                  resolved->containerIndex, *resolved->index)
            : fo4::schema::remove_record_formlink_list_element(
                  mutated, *keyResult, resolved->simple, *resolved->index);
        if (const auto statusError = mutationStatusError(status, resolved->basePath)) {
            return std::unexpected(*statusError);
        }
        if (status == fo4::schema::FormLinkListMutationStatus::unchanged) {
            return nlohmann::json("No change for " + resolved->basePath + " in " + pluginName + ".");
        }

        stagePluginMutationLocked(*loaded, std::move(mutated));
        return nlohmann::json(
            "Removed " + requestedPath + " from " + recordText + " in " + pluginName + ".");
    } catch (const std::exception& error) {
        return std::unexpected(Error{"ElementRemoveFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::moveElement(
    const nlohmann::json& arguments) {
    const auto pluginName = elementStringArgument(arguments, 0);
    const auto recordText = elementStringArgument(arguments, 1);
    const auto requestedPath = elementStringArgument(arguments, 2);
    std::int64_t delta = 0;
    if (!elementDeltaArgument(arguments, 3, delta) || delta == 0) {
        return std::unexpected(Error{"ElementDeltaInvalid", "MoveElement requires a non-zero integer delta"});
    }
    auto keyResult = elementRecordKey(recordText);
    if (!keyResult) return std::unexpected(keyResult.error());

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    if (const auto guard = writeGuard(*loaded)) return std::unexpected(*guard);
    const auto* record = fo4::find_record(loaded->plugin, *keyResult);
    if (record == nullptr) return std::unexpected(Error{"RecordNotFound", "Record is not present: " + recordText});

    try {
        std::string error;
        const auto resolved = resolveElementPath(*record, requestedPath, error);
        std::optional<GenericElementList> generic;
        if (!resolved.has_value()) {
            generic = resolveGenericList(loaded->plugin, *keyResult, *record, requestedPath, error);
            if (!generic.has_value()) return std::unexpected(Error{"ElementPathUnsupported", std::move(error)});
        }
        const auto fromIndex = resolved.has_value() ? resolved->index : generic->index;
        if (!fromIndex.has_value()) {
            return std::unexpected(Error{"ElementIndexRequired", "MoveElement requires a list entry path"});
        }
        const auto count = resolved.has_value() ? elementListCount(loaded->plugin, *keyResult, *resolved)
                                                : std::optional<std::size_t>(generic->count);
        if (!count.has_value() || *fromIndex >= *count) {
            return std::unexpected(Error{"ElementIndexOutOfRange", "List element index is out of range"});
        }
        if (*fromIndex > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max())) {
            return std::unexpected(Error{"ElementIndexOutOfRange", "List element index cannot be represented safely"});
        }
        const auto fromSigned = static_cast<std::int64_t>(*fromIndex);
        if ((delta > 0 && fromSigned > std::numeric_limits<std::int64_t>::max() - delta) ||
            (delta < 0 && fromSigned < std::numeric_limits<std::int64_t>::min() - delta)) {
            return std::unexpected(Error{"ElementIndexOutOfRange", "List move target overflows"});
        }
        const auto toSigned = fromSigned + delta;
        if (toSigned < 0 || static_cast<std::uint64_t>(toSigned) >= static_cast<std::uint64_t>(*count)) {
            return std::unexpected(Error{
                "ElementIndexOutOfRange",
                "List element is already at the requested boundary"
            });
        }
        const auto to = static_cast<std::size_t>(toSigned);

        if (generic.has_value()) {
            auto mutated = loaded->plugin;
            const auto status = moveGenericElement(mutated, *keyResult, *generic, *fromIndex, to);
            if (const auto statusError = fieldStatusError(status, generic->path)) return std::unexpected(*statusError);
            stagePluginMutationLocked(*loaded, std::move(mutated));
            return nlohmann::json(
                "Moved " + requestedPath + " to index " + std::to_string(to) + " in " + pluginName + ".");
        }

        auto mutated = loaded->plugin;
        const auto status = resolved->nested
            ? fo4::schema::move_record_nested_formlink_list_element(
                  mutated, *keyResult, resolved->nestedDescriptor,
                  resolved->containerIndex, *resolved->index, to)
            : fo4::schema::move_record_formlink_list_element(
                  mutated, *keyResult, resolved->simple, *resolved->index, to);
        if (const auto statusError = mutationStatusError(status, resolved->basePath)) {
            return std::unexpected(*statusError);
        }
        if (status == fo4::schema::FormLinkListMutationStatus::unchanged) {
            return nlohmann::json("No change for " + resolved->basePath + " in " + pluginName + ".");
        }

        stagePluginMutationLocked(*loaded, std::move(mutated));
        return nlohmann::json(
            "Moved " + requestedPath + " to index " + std::to_string(to) +
            " in " + pluginName + ".");
    } catch (const std::exception& error) {
        return std::unexpected(Error{"ElementMoveFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::clearElement(
    const nlohmann::json& arguments) {
    const auto pluginName = elementStringArgument(arguments, 0);
    const auto recordText = elementStringArgument(arguments, 1);
    const auto requestedPath = elementStringArgument(arguments, 2);
    auto keyResult = elementRecordKey(recordText);
    if (!keyResult) return std::unexpected(keyResult.error());

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    if (const auto guard = writeGuard(*loaded)) return std::unexpected(*guard);
    const auto* record = fo4::find_record(loaded->plugin, *keyResult);
    if (record == nullptr) return std::unexpected(Error{"RecordNotFound", "Record is not present: " + recordText});

    try {
        std::string error;
        const auto resolved = resolveElementPath(*record, requestedPath, error);
        if (!resolved.has_value()) return std::unexpected(Error{"ElementPathUnsupported", std::move(error)});
        if (resolved->index.has_value()) {
            return std::unexpected(Error{"ElementListRequired", "ClearElement requires a list path, not an entry"});
        }
        const auto before = elementListCount(loaded->plugin, *keyResult, *resolved);
        if (!before.has_value()) return std::unexpected(Error{"ElementContainerMissing", "List container is not present"});

        auto mutated = loaded->plugin;
        const auto status = resolved->nested
            ? fo4::schema::clear_record_nested_formlink_list(
                  mutated, *keyResult, resolved->nestedDescriptor, resolved->containerIndex)
            : fo4::schema::clear_record_formlink_list(mutated, *keyResult, resolved->simple);
        if (const auto statusError = mutationStatusError(status, resolved->basePath)) {
            return std::unexpected(*statusError);
        }
        if (status == fo4::schema::FormLinkListMutationStatus::unchanged) {
            return nlohmann::json("No change for " + resolved->basePath + " in " + pluginName + ".");
        }

        stagePluginMutationLocked(*loaded, std::move(mutated));
        return nlohmann::json(
            "Cleared " + resolved->basePath + " on " + recordText + " in " + pluginName +
            " (" + std::to_string(*before) + " item(s) removed).");
    } catch (const std::exception& error) {
        return std::unexpected(Error{"ElementClearFailed", error.what()});
    }
}

}  // namespace fo4ide::plugin
