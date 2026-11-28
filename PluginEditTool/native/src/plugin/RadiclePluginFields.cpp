#include "RadiclePluginFields.hpp"

#include <bit>
#include <cstdint>

#include <fo4/schema/scalar.hpp>

namespace fo4ide::plugin {

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
    return fo4::semantic_field_address(path);
}

Result<std::vector<SemanticFieldCell>> semanticFieldCells(const fo4::Plugin& plugin,
                                                         const fo4::Record& record) {
    auto cells = fo4::semantic_field_cells(plugin, record);
    if (!cells) return std::unexpected(Error{cells.error().code, cells.error().message});
    return std::move(*cells);
}

}
