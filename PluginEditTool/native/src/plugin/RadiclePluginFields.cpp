#include "RadiclePluginFields.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <exception>

#include <fo4/plugin/mutation.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/schema/formlink_lists.hpp>
#include <fo4/schema/localized_fields.hpp>
#include <fo4/schema/record_constraints.hpp>
#include <fo4/schema/scalar.hpp>
#include <fo4/text/encoding.hpp>

namespace fo4ide::plugin {
namespace {

std::string indexedPath(std::string_view path, const fo4::schema::FieldAddress& address,
                        std::size_t index) {
    if (!address.repeated && address.stride == 0) return std::string(path);
    return std::string(path) + "[" + std::to_string(index) + "]";
}

}

nlohmann::json scalarPresentationJson(fo4::Signature record, std::string_view path) {
    const auto field = fo4::schema::scalar_field_descriptor(record, path);
    if (!field) return nullptr;
    const auto& scalar = field->scalar;
    nlohmann::json names = nlohmann::json::array();
    if (scalar.presentation == fo4::schema::ScalarPresentation::enumeration) {
        for (const auto& entry : scalar.names) {
            names.push_back({{"value", entry.value}, {"name", std::string(entry.name)}});
        }
        return {{"kind", "enum"}, {"names", std::move(names)}};
    }
    if (scalar.presentation == fo4::schema::ScalarPresentation::flags) {
        for (const auto& entry : scalar.names) {
            if (entry.value == 0) continue;
            names.push_back({{"bit", std::countr_zero(static_cast<std::uint64_t>(entry.value))},
                {"name", std::string(entry.name)}});
        }
        return {{"kind", "flags"}, {"names", std::move(names)}};
    }
    return nullptr;
}

std::optional<SemanticFieldAddress> semanticFieldAddress(std::string_view path) {
    if (fo4::schema::scalar_field_descriptor(path) ||
        fo4::schema::formlink_field_descriptor(path)) {
        return SemanticFieldAddress{path, 0};
    }
    if (path.empty() || path.back() != ']') return std::nullopt;
    const auto open = path.rfind('[');
    if (open == std::string_view::npos) return std::nullopt;
    const auto base = path.substr(0, open);
    const auto scalar = fo4::schema::scalar_field_descriptor(base);
    const auto link = fo4::schema::formlink_field_descriptor(base);
    if (!scalar && !link) return std::nullopt;
    const auto& address = scalar ? scalar->scalar.address : link->address;
    if (!address.repeated && address.stride == 0) return std::nullopt;
    const auto digits = path.substr(open + 1, path.size() - open - 2);
    std::size_t index = 0;
    const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), index);
    if (error != std::errc{} || end != digits.data() + digits.size()) return std::nullopt;
    return SemanticFieldAddress{base, index};
}

Result<std::vector<SemanticFieldCell>> semanticFieldCells(const fo4::Plugin& plugin,
                                                         const fo4::Record& record) {
    std::vector<SemanticFieldCell> cells;
    for (const auto& field : fo4::schema::scalar_fields(record.header.signature)) {
        try {
            static_cast<void>(fo4::record_field_count(plugin, record.form_key, field));
        } catch (const fo4::schema::ScalarError& error) {
            return std::unexpected(Error{"ScalarReadFailed", error.what()});
        }
        const auto& scalar = field.scalar;
        if (scalar.payload_size != 0 && std::any_of(record.subrecords.begin(), record.subrecords.end(),
                [&](const auto& subrecord) {
                    return subrecord.signature == scalar.subrecord && subrecord.data.size() != scalar.payload_size;
                })) {
            cells.push_back(SemanticFieldCell{std::string(field.path), "<opaque>", false, false});
            continue;
        }
        const auto resolved = fo4::schema::resolve_field_slots(
            record, fo4::schema::scalar_field_shape(scalar), scalar.address);
        if (resolved.misaligned) {
            return std::unexpected(Error{"ScalarReadFailed",
                record.form_key.to_string() + ": invalid " + std::string(field.path) + " storage"});
        }
        const auto count = scalar.address.repeated || scalar.address.stride != 0
            ? resolved.slots.size() : std::max(std::size_t{1}, resolved.slots.size());
        if (scalar.address.repeated || scalar.address.stride != 0) {
            cells.push_back(SemanticFieldCell{std::string(field.path) + "\\Count",
                std::to_string(count), true, false, false});
        }
        for (std::size_t index = 0; index < count; ++index) {
            SemanticFieldCell cell{indexedPath(field.path, scalar.address, index), {}, false, false};
            const bool localized = scalar.storage == fo4::schema::ScalarStorage::zstring &&
                plugin.header.tes4_flags.is_localized() &&
                (fo4::schema::is_localized_field(field.record, scalar.subrecord) ||
                 (scalar.localized && index < resolved.slots.size() && resolved.slots[index].valid));
            if (localized) {
                cell.value = "<opaque/localized>";
            } else if (index < resolved.slots.size() && resolved.slots[index].valid) {
                const auto& slot = resolved.slots[index];
                try {
                    cell.value = fo4::schema::decode_scalar(fo4::schema::scalar_slot_bytes(
                        record.subrecords[slot.subrecord].data, slot, scalar), scalar);
                    if (scalar.storage == fo4::schema::ScalarStorage::zstring ||
                        scalar.storage == fo4::schema::ScalarStorage::fixed_string) {
                        cell.value = fo4::text::windows1252_to_utf8(cell.value);
                    }
                    cell.available = true;
                } catch (const fo4::schema::ScalarError&) {
                    cell.value = "<opaque>";
                }
            }
            cells.push_back(std::move(cell));
        }
    }
    for (const auto& field : fo4::schema::formlink_fields(record.header.signature)) {
        try {
            static_cast<void>(fo4::record_field_count(plugin, record.form_key, field));
        } catch (const fo4::schema::FormLinkFieldError&) {
            cells.push_back(SemanticFieldCell{std::string(field.path), "<opaque>", false, true});
            continue;
        }
        const fo4::schema::FieldShape shape{.subrecord = field.subrecord,
                                            .offset = field.offset,
                                            .width = sizeof(std::uint32_t),
                                            .max_form_version = field.max_form_version};
        const auto resolved = fo4::schema::resolve_field_slots(record, shape, field.address);
        if (resolved.misaligned) {
            return std::unexpected(Error{"FormLinkReadFailed",
                record.form_key.to_string() + ": invalid " + std::string(field.path) + " storage"});
        }
        if (std::any_of(resolved.slots.begin(), resolved.slots.end(), [](const auto& slot) {
                return slot.misfit;
            })) {
            return std::unexpected(Error{"FormLinkReadFailed",
                record.form_key.to_string() + ": " + std::string(field.path) +
                    " does not fit in the subrecord payload"});
        }
        const auto count = field.address.repeated || field.address.stride != 0
            ? resolved.slots.size() : std::max(std::size_t{1}, resolved.slots.size());
        if (field.address.repeated || field.address.stride != 0) {
            cells.push_back(SemanticFieldCell{std::string(field.path) + "\\Count",
                std::to_string(count), true, false, false});
        }
        for (std::size_t index = 0; index < count; ++index) {
            SemanticFieldCell cell{indexedPath(field.path, field.address, index), {}, false, true};
            if (index < resolved.slots.size() && resolved.slots[index].valid) {
                const auto& slot = resolved.slots[index];
                std::uint32_t raw = 0;
                std::memcpy(&raw, record.subrecords[slot.subrecord].data.data() + slot.offset, sizeof(raw));
                try {
                    cell.value = fo4::resolve_plugin_form_id(
                        fo4::RawFormId{raw}, fo4::plugin_form_id_context(plugin)).to_string();
                    cell.available = true;
                } catch (const fo4::FormIdError&) {
                    cell.value = "<opaque>";
                }
            }
            cells.push_back(std::move(cell));
        }
    }
    for (const auto& descriptor : fo4::schema::k_generated_formlink_lists) {
        if (descriptor.record != record.header.signature) continue;
        try {
            const auto values = fo4::schema::record_formlink_list_values(plugin, record.form_key, descriptor);
            if (!values) continue;
            cells.push_back(SemanticFieldCell{std::string(descriptor.path) + "\\Count",
                std::to_string(values->size()), true, false, false});
            for (std::size_t index = 0; index < values->size(); ++index) {
                cells.push_back(SemanticFieldCell{std::string(descriptor.path) + "[" + std::to_string(index) + "]",
                    (*values)[index].to_string(), true, true});
            }
        } catch (const fo4::schema::FormLinkListError& error) {
            if (descriptor.segment_count == 0) {
                return std::unexpected(Error{"FormLinkReadFailed", error.what()});
            }
            cells.push_back(SemanticFieldCell{std::string(descriptor.path), "<opaque>", false, true});
        } catch (const std::exception& error) {
            return std::unexpected(Error{"FormLinkReadFailed", error.what()});
        }
    }
    if (const auto constraint = fo4::schema::record_authoring_constraint(record.header.signature)) {
        for (const auto& rule : fo4::schema::authoring_subrecords(*constraint)) {
            const auto count = static_cast<std::size_t>(std::count_if(
                record.subrecords.begin(), record.subrecords.end(), [&](const auto& subrecord) {
                    return subrecord.signature == rule.signature;
                }));
            const auto path = "Authoring\\" + rule.signature.to_string();
            cells.push_back(SemanticFieldCell{path, std::to_string(count), true, false, false});
            for (std::size_t index = 0; index < count; ++index) {
                cells.push_back(SemanticFieldCell{path + "[" + std::to_string(index) + "]",
                    "present", true, false, false});
            }
        }
    }
    return cells;
}

}
