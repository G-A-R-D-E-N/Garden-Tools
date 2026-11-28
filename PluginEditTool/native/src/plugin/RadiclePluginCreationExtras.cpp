#include "RadiclePluginEffectRows.hpp"
#include "RadiclePluginSupport.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <fo4/plugin/reader.hpp>
#include <fo4/schema/formlink_lists.hpp>
#include <fo4/signature.hpp>

namespace fo4ide::plugin {

namespace {

bool carriesEffects(std::string_view signature) {
    return signature == "SPEL" || signature == "ALCH" || signature == "ENCH" ||
           signature == "INGR";
}

std::string effectLabel(std::size_t index) {
    return "Effect #" + std::to_string(index);
}

Error effectRefusal(std::size_t index, std::string message) {
    return Error{"MagicEffectEntryRefused", effectLabel(index) + ": " + std::move(message)};
}

Error effectStatus(effects::Write status, std::size_t index) {
    switch (status) {
        case effects::Write::written:
            break;
        case effects::Write::record_missing:
            return Error{"RecordNotFound", "The created record is not present in this plugin"};
        case effects::Write::unsupported_record:
            return Error{"MagicEffectListUnsupported", effectLabel(index) + ": the record type has no effect list"};
        case effects::Write::unexpected_layout:
            return Error{"MagicEffectListLayout", effectLabel(index) + ": the effect list does not match the schema"};
        case effects::Write::index_out_of_range:
            return Error{"MagicEffectListIndexOutOfRange", effectLabel(index) + ": index is outside the effect list"};
        case effects::Write::effect_not_magic_effect:
            return Error{"MagicEffectEntryRefused", effectLabel(index) + ": the target is not an MGEF"};
    }
    return Error{"MagicEffectEntryRefused", effectLabel(index) + ": the write reported no result"};
}

Result<effects::Entry> effectFromJson(const nlohmann::json& value,
                                      std::size_t index,
                                      const std::optional<fo4::FormKey>& key) {
    if (!value.is_object()) {
        return std::unexpected(effectRefusal(index, "an effect entry must be an object"));
    }
    if (!key.has_value()) {
        return std::unexpected(effectRefusal(index, "effect must be a FormKey or a loaded EditorID"));
    }
    effects::Entry entry;
    entry.effect = *key;
    if (value.contains("magnitude") && !value.at("magnitude").is_null()) {
        const auto magnitude = support::finiteFloat(value.at("magnitude"));
        if (!magnitude) return std::unexpected(effectRefusal(index, "magnitude must be a finite float"));
        entry.magnitude = *magnitude;
    }
    if (value.contains("area") && !value.at("area").is_null()) {
        const auto area = support::integerValue<std::uint32_t>(value.at("area"));
        if (!area) return std::unexpected(effectRefusal(index, "area must be a non-negative integer"));
        entry.area = *area;
    }
    if (value.contains("duration") && !value.at("duration").is_null()) {
        const auto duration = support::integerValue<std::uint32_t>(value.at("duration"));
        if (!duration) {
            return std::unexpected(effectRefusal(index, "duration must be a non-negative integer"));
        }
        entry.duration = *duration;
    }
    return entry;
}

}

Result<void> RadiclePluginBackend::applyCreationExtrasLocked(fo4::Plugin& mutated,
                                                             const fo4::FormKey& key,
                                                             std::string_view signature,
                                                             const nlohmann::json& payload) const {
    const auto effects = payload.find("effects");
    if (effects != payload.end() && !effects->is_null()) {
        if (!carriesEffects(signature)) {
            return std::unexpected(Error{
                "InvalidRecordCreationPayload",
                std::string(signature) + " does not carry a magic effect list"});
        }
        if (!effects->is_array()) {
            return std::unexpected(Error{
                "InvalidRecordCreationPayload", "effects must be an array"});
        }
        for (std::size_t index = 0; index < effects->size(); ++index) {
            const auto& item = (*effects)[index];
            const auto entry = effectFromJson(
                item, index, item.is_object() ? resolveListValue(support::stringField(item, "effect"))
                                              : std::nullopt);
            if (!entry) return std::unexpected(entry.error());
            if (const auto error = magicEffectTargetError(entry->effect)) {
                return std::unexpected(effectRefusal(index, error->message));
            }
            try {
                support::ensureOwnerMaster(mutated, entry->effect.owner(), plugins_);
                std::size_t position = 0;
                const auto status = effects::add(mutated, key, *entry, &position);
                if (status != effects::Write::written) {
                    return std::unexpected(effectStatus(status, index));
                }
            } catch (const support::MasterOrderError&) {
                throw;
            } catch (const std::exception& error) {
                return std::unexpected(effectRefusal(index, error.what()));
            }
        }
    }

    const auto entries = payload.find("entries");
    if (entries != payload.end() && !entries->is_null()) {
        if (signature != "FLST") {
            return std::unexpected(Error{
                "InvalidRecordCreationPayload",
                std::string(signature) + " does not carry a form list"});
        }
        if (!entries->is_array()) {
            return std::unexpected(Error{
                "InvalidRecordCreationPayload", "entries must be an array"});
        }
        const auto descriptor = fo4::schema::formlink_list_descriptor(
            fo4::Signature::from_string("FLST"), "FLST\\Items");
        if (!descriptor.has_value()) {
            return std::unexpected(Error{
                "FormListUnsupported", "The schema does not register the FLST Items list"});
        }
        for (std::size_t index = 0; index < entries->size(); ++index) {
            const auto& item = (*entries)[index];
            const auto text = item.is_string() ? item.get<std::string>() : std::string{};
            const auto target = resolveListValue(text);
            if (!target.has_value()) {
                return std::unexpected(Error{
                    "FormListEntryUnresolved",
                    "Entry #" + std::to_string(index) + ": '" + text +
                        "' is not a FormKey and no loaded record has that EditorID"});
            }
            if ((*graph_).winner(*target) == nullptr) {
                return std::unexpected(Error{
                    "FormListEntryUnresolved",
                    "Entry #" + std::to_string(index) + ": '" + text +
                        "' does not resolve in the loaded environment"});
            }
            try {
                support::ensureOwnerMaster(mutated, target->owner(), plugins_);
                const auto status = fo4::schema::insert_record_formlink_list_element(
                    mutated, key, *descriptor, index, *target);
                if (status != fo4::schema::FormLinkListMutationStatus::updated) {
                    return std::unexpected(Error{
                        "FormListEntryRefused",
                        "Entry #" + std::to_string(index) + " was refused by the list writer"});
                }
            } catch (const fo4::schema::FormLinkListError& error) {
                return std::unexpected(Error{
                    "FormListEntryRefused",
                    "Entry #" + std::to_string(index) + ": " + error.what()});
            } catch (const fo4::FormIdError& error) {
                return std::unexpected(Error{
                    "FormListEntryRefused",
                    "Entry #" + std::to_string(index) + ": " + error.what()});
            }
        }
    }

    return {};
}

}
