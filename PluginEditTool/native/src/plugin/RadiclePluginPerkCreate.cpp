#include "RadiclePluginPerkInternal.hpp"

#include <fo4/environment/strings.hpp>
#include <fo4/forms/perk.hpp>
#include <fo4/text/encoding.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <exception>
#include <optional>
#include <string>
#include <string_view>

namespace fo4ide::plugin {

using namespace lifecycle;
using namespace perk_detail;

namespace {

Result<std::string> perkLanguage(const nlohmann::json& payload) {
    std::string language = "en";
    if (const auto found = payload.find("language"); found != payload.end()) {
        if (!found->is_string()) {
            return std::unexpected(Error{"InvalidRecordCreationPayload", "PERK language must be a string"});
        }
        language = found->get<std::string>();
    }
    const bool valid = !language.empty() && std::all_of(language.begin(), language.end(), [](unsigned char ch) {
        return std::isalnum(ch) != 0;
    });
    if (!valid) return std::unexpected(Error{"InvalidStringLanguage", "PERK language must be an alphanumeric token"});
    return language;
}

}  // namespace

Result<fo4::FormKey> RadiclePluginBackend::createPerkLocked(const LoadedPlugin& loaded,
                                                            fo4::Plugin& mutated,
                                                            const nlohmann::json& payload,
                                                            LocalizedStringAllocation& strings) const {
    const auto* editor = editorIdPayloadField(payload);
    if (editor == nullptr || !editor->is_string() || editor->get<std::string>().empty()) {
        return std::unexpected(Error{
            "InvalidRecordCreationPayload",
            "PERK creation requires a non-empty string editorId"
        });
    }
    const auto editorId = editor->get<std::string>();
    if (const auto stored = support::encodeEditorId(editorId); !stored) {
        return std::unexpected(stored.error());
    }

    bool valid = true;
    const auto full = optionalStringField(payload, "full", valid);
    if (!valid) return std::unexpected(Error{"InvalidRecordCreationPayload", "PERK full must be a string or null"});
    const auto description = optionalStringField(payload, "desc", valid);
    if (!valid) return std::unexpected(Error{"InvalidRecordCreationPayload", "PERK desc must be a string or null"});
    const auto swf = optionalStringField(payload, "swf", valid);
    if (!valid) return std::unexpected(Error{"InvalidRecordCreationPayload", "PERK swf must be a string or null"});
    const auto language = perkLanguage(payload);
    if (!language) return std::unexpected(language.error());

    const bool localized = mutated.header.tes4_flags.is_localized();
    const bool hasFull = full.has_value() && !full->empty();
    const bool hasDescription = description.has_value() && !description->empty();
    if (localized && (hasFull || hasDescription)) {
        auto opened = openLocalizedStrings(loaded, *language);
        if (!opened) return std::unexpected(opened.error());
        strings = std::move(*opened);
    }

    fo4::forms::PerkCreation creation;
    creation.editor_id = editorId;
    const auto textValue = [&](const std::optional<std::string>& utf8, fo4::env::StringSource source,
                               std::optional<fo4::schema::LocalizedString>& out) -> std::optional<Error> {
        if (!utf8.has_value() || utf8->empty()) return std::nullopt;
        fo4::schema::LocalizedString value;
        if (localized) {
            auto id = allocateLocalizedString(strings, source, *utf8);
            if (!id) return id.error();
            value.id = *id;
        } else {
            auto stored = fo4::text::utf8_to_windows1252(*utf8);
            if (!stored) return Error{"InvalidRecordCreationPayload", "PERK text is not representable in Windows-1252"};
            value.text = std::move(*stored);
        }
        out = std::move(value);
        return std::nullopt;
    };
    if (auto error = textValue(full, fo4::env::StringSource::normal, creation.full_name)) {
        return std::unexpected(*error);
    }
    if (auto error = textValue(description, fo4::env::StringSource::dl, creation.description)) {
        return std::unexpected(*error);
    }
    if (!lifecycleUnsignedField(payload, "trait", std::uint8_t{0}, creation.trait) ||
        !lifecycleUnsignedField(payload, "level", std::uint8_t{0}, creation.level) ||
        !lifecycleUnsignedField(payload, "ranks", std::uint8_t{1}, creation.ranks) ||
        !lifecycleUnsignedField(payload, "playable", std::uint8_t{1}, creation.playable)) {
        return std::unexpected(Error{
            "InvalidRecordCreationPayload",
            "PERK trait, level, ranks and playable must be uint8"
        });
    }
    creation.swf = optionalStringView(swf).value_or(std::string_view{});

    const PerkFormKeyResolver resolve = [this](std::string_view text) { return resolveListValue(text); };
    const PerkParseContext context{mutated, graph_, resolve};
    if (const auto found = payload.find("conditions"); found != payload.end()) {
        std::string failure;
        auto built = buildConditions(*found, mutated, {}, conditionResolver(context), failure);
        if (!built) return std::unexpected(Error{"InvalidRecordCreationPayload", failure});
        creation.conditions = std::move(*built);
    }
    if (auto error = parseFormKeyField(payload, "nextPerk", creation.next_perk)) return std::unexpected(*error);
    if (creation.next_perk != fo4::FormKey{}) {
        if (auto error = checkLoadedSlot(graph_, creation.next_perk, {"PERK"}, "PERK next")) {
            return std::unexpected(*error);
        }
        if (auto error = requireDeclaredOwner(mutated, creation.next_perk, "PERK next")) {
            return std::unexpected(*error);
        }
    }
    if (auto error = parseFormKeyField(payload, "sound", creation.sound)) return std::unexpected(*error);
    if (creation.sound != fo4::FormKey{}) {
        if (auto error = checkLoadedSlot(graph_, creation.sound, {"SNDR"}, "PERK sound")) {
            return std::unexpected(*error);
        }
        if (auto error = requireDeclaredOwner(mutated, creation.sound, "PERK sound")) {
            return std::unexpected(*error);
        }
    }
    if (const auto found = payload.find("effects"); found != payload.end()) {
        auto effects = perkEffectsFromJson(*found, context, {});
        if (!effects) return std::unexpected(effects.error());
        creation.effects = std::move(*effects);
    }
    try {
        return fo4::forms::create_perk(mutated, creation);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"PerkCreateFailed", error.what()});
    }
}

}  // namespace fo4ide::plugin
