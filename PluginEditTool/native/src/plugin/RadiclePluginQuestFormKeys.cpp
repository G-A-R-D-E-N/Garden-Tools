#include "RadiclePluginQuestInternal.hpp"

#include <exception>
#include <string>
#include <string_view>

namespace fo4ide::plugin::questinternal {

bool isPlacedSignature(fo4::Signature signature) {
    const auto text = signature.to_string();
    return text == "REFR" || text == "ACHR" || text == "PGRE" || text == "PHZD" ||
           text == "PMIS" || text == "PARW" || text == "PBAR" || text == "PBEA" ||
           text == "PCON" || text == "PFLA";
}

std::optional<Error> requireLoadedFill(
    const fo4::ConflictGraph& graph,
    const fo4::FormKey& key,
    std::initializer_list<std::string_view> accepted,
    bool placedTargets,
    std::string_view slotName) {
    const auto* winner = graph.winner(key);
    if (winner == nullptr) {
        return Error{
            "InvalidTypedFormKey",
            std::string(slotName) + " must resolve to a loaded record: " + key.to_string()
        };
    }
    const auto signature = winner->signature.to_string();
    if (placedTargets && isPlacedSignature(winner->signature)) return std::nullopt;
    for (const auto name : accepted) {
        if (signature == name) return std::nullopt;
    }
    std::string expected;
    if (placedTargets) expected = "placed reference";
    for (const auto name : accepted) {
        if (!expected.empty()) expected += " or ";
        expected += std::string(name);
    }
    return Error{
        "InvalidTypedFormKey",
        std::string(slotName) + " " + key.to_string() + " is a " + signature +
            ", but this slot holds a " + expected
    };
}

std::optional<Error> parseFillFormKey(const nlohmann::json& object,
                                      const char* key,
                                      std::optional<fo4::FormKey>& out) {
    const auto found = object.find(key);
    if (found == object.end() || found->is_null()) {
        out = std::nullopt;
        return std::nullopt;
    }
    if (!found->is_string()) {
        return Error{"InvalidQuestAliasPayload", std::string(key) + " must be a FormKey string"};
    }
    const auto text = found->get<std::string>();
    if (text.empty()) {
        out = std::nullopt;
        return std::nullopt;
    }
    try {
        out = fo4::FormKey::parse(text);
        return std::nullopt;
    } catch (const std::exception& error) {
        return Error{"InvalidFormKey", std::string(key) + ": " + error.what()};
    }
}

std::optional<Error> parseOptionalTypedFormKey(
    const nlohmann::json& object,
    const char* key,
    const fo4::ConflictGraph& graph,
    std::initializer_list<std::string_view> accepted,
    std::optional<fo4::FormKey>& out,
    std::vector<fo4::FormKey>& fillKeys) {
    if (const auto error = parseFillFormKey(object, key, out)) return error;
    if (!out.has_value()) return std::nullopt;
    if (const auto error = requireLoadedFill(graph, *out, accepted, false, key)) {
        return error;
    }
    fillKeys.push_back(*out);
    return std::nullopt;
}

}
