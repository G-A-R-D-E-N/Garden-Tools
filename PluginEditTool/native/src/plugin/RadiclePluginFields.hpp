#pragma once

#include <fo4/plugin/container.hpp>
#include <fo4/plugin/semantic_fields.hpp>
#include <fo4/signature.hpp>
#include <fo4ide/core/Result.hpp>
#include <nlohmann/json.hpp>

#include <optional>
#include <string_view>
#include <vector>

namespace fo4ide::plugin {

using SemanticFieldCell = fo4::SemanticFieldCell;
using SemanticFieldAddress = fo4::SemanticFieldAddress;

std::optional<SemanticFieldAddress> semanticFieldAddress(std::string_view path);
Result<std::vector<SemanticFieldCell>> semanticFieldCells(const fo4::Plugin& plugin,
                                                         const fo4::Record& record);
nlohmann::json scalarPresentationJson(fo4::Signature record, std::string_view path);

}
