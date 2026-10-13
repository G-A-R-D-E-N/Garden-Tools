#pragma once

#include <fo4/plugin/container.hpp>
#include <fo4/signature.hpp>
#include <fo4ide/core/Result.hpp>
#include <nlohmann/json.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fo4ide::plugin {

struct SemanticFieldCell {
    std::string path;
    std::string value;
    bool available{false};
    bool reference{false};
    bool writable{true};
};

struct SemanticFieldAddress {
    std::string_view path;
    std::size_t index{0};
};

std::optional<SemanticFieldAddress> semanticFieldAddress(std::string_view path);
Result<std::vector<SemanticFieldCell>> semanticFieldCells(const fo4::Plugin& plugin,
                                                         const fo4::Record& record);
nlohmann::json scalarPresentationJson(fo4::Signature record, std::string_view path);

}
