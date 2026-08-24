#include <fo4ide/app/PluginBackendTarget.hpp>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace fo4ide::app {
namespace {

std::string stringArgument(const nlohmann::json& args, std::size_t index) {
    if (!args.is_array() || index >= args.size() || !args[index].is_string()) return {};
    return args[index].get<std::string>();
}

int intArgument(const nlohmann::json& args, std::size_t index, int fallback) {
    if (!args.is_array() || index >= args.size() || !args[index].is_number_integer()) return fallback;
    return args[index].get<int>();
}

std::string lower(std::string_view value) {
    std::string out(value);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return out;
}

std::string trim(std::string_view value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) ++begin;
    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) --end;
    return std::string(value.substr(begin, end - begin));
}

Result<nlohmann::json> parseStringResult(Result<nlohmann::json> result,
                                          std::string_view operation) {
    if (!result) return std::unexpected(result.error());
    if (!result->is_string()) {
        return std::unexpected(Error{
            "BackendContractMismatch",
            std::string(operation) + " did not return the expected JSON string"
        });
    }
    try {
        return nlohmann::json::parse(result->get<std::string>());
    } catch (const std::exception& error) {
        return std::unexpected(Error{
            "BackendContractMismatch",
            std::string(operation) + " returned invalid JSON: " + error.what()
        });
    }
}

Result<std::string> resolveFormKey(plugin::IPluginBackend& backend,
                                   std::string_view pluginName,
                                   std::string_view recordId) {
    if (recordId.find(':') != std::string_view::npos) return std::string(recordId);

    auto search = parseStringResult(
        backend.invokeBackend(
            "SearchRecords",
            nlohmann::json::array({std::string(recordId), std::string{}})),
        "SearchRecords");
    if (!search) return std::unexpected(search.error());
    if (!search->is_array()) {
        return std::unexpected(Error{
            "BackendContractMismatch",
            "SearchRecords did not return an array"
        });
    }

    const auto wantedId = lower(recordId);
    const auto wantedOwner = lower(std::filesystem::path(std::string(pluginName)).filename().string());
    for (const auto& hit : *search) {
        if (!hit.is_object()) continue;
        const auto editorId = hit.value("EditorID", hit.value("EditorId", std::string{}));
        const auto formKey = hit.value("FormKey", std::string{});
        if (lower(editorId) != wantedId || formKey.empty()) continue;
        const auto colon = formKey.find(':');
        if (colon == std::string::npos) continue;
        if (lower(formKey.substr(colon + 1)) == wantedOwner) return formKey;
    }

    return std::unexpected(Error{
        "RecordNotFound",
        "Could not resolve record '" + std::string(recordId) + "' in " + std::string(pluginName)
    });
}

Result<nlohmann::json> legacyRenumberFormId(plugin::IPluginBackend& backend,
                                             const nlohmann::json& args) {
    const auto pluginName = stringArgument(args, 0);
    const auto recordId = stringArgument(args, 1);
    auto hex = trim(stringArgument(args, 2));
    if (pluginName.empty() || recordId.empty() || hex.empty()) {
        return std::unexpected(Error{
            "RenumberArgumentsMissing",
            "RenumberFormId requires plugin, record, and new hex FormID"
        });
    }
    if (hex.size() >= 2 && hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) hex.erase(0, 2);

    std::uint32_t localId = 0;
    const auto [ptr, error] = std::from_chars(hex.data(), hex.data() + hex.size(), localId, 16);
    if (error != std::errc{} || ptr != hex.data() + hex.size() || localId == 0 || localId > 0xFFFFFFu) {
        return std::unexpected(Error{
            "InvalidFormId",
            "New FormID must be a non-zero 24-bit hexadecimal object id"
        });
    }

    auto oldKey = resolveFormKey(backend, pluginName, recordId);
    if (!oldKey) return std::unexpected(oldKey.error());

    std::ostringstream local;
    local << std::uppercase << std::hex << std::setw(6) << std::setfill('0') << localId;
    const auto owner = std::filesystem::path(pluginName).filename().string();
    const auto newKey = local.str() + ":" + owner;
    return backend.invokeBackend(
        "RenumberRecord",
        nlohmann::json::array({pluginName, *oldKey, newKey}));
}

Result<nlohmann::json> resolveFormKeyLabels(plugin::IPluginBackend& backend,
                                             const nlohmann::json& args) {
    const auto csv = stringArgument(args, 0);
    nlohmann::json labels = nlohmann::json::object();
    std::size_t begin = 0;
    while (begin <= csv.size()) {
        const auto end = csv.find(',', begin);
        const auto token = trim(csv.substr(begin, end == std::string::npos ? csv.size() - begin
                                                                           : end - begin));
        if (!token.empty() && !labels.contains(token)) {
            auto details = parseStringResult(
                backend.invokeBackend("GetRecordDetails", nlohmann::json::array({token})),
                "GetRecordDetails");
            if (details && details->is_object()) {
                const auto editorId = details->value(
                    "EditorId", details->value("EditorID", std::string{}));
                labels[token] = editorId.empty() ? token : editorId + " [" + token + "]";
            } else {
                labels[token] = token;
            }
        }
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return nlohmann::json(labels.dump());
}

Result<nlohmann::json> legacyRecordTree(plugin::IPluginBackend& backend,
                                         const nlohmann::json& args) {
    const auto pluginName = stringArgument(args, 0);
    const auto recordId = stringArgument(args, 1);
    if (pluginName.empty() || recordId.empty()) return nlohmann::json("[]");

    auto formKey = resolveFormKey(backend, pluginName, recordId);
    if (!formKey) return std::unexpected(formKey.error());
    auto matrix = parseStringResult(
        backend.invokeBackend("GetConflictMatrix", nlohmann::json::array({*formKey})),
        "GetConflictMatrix");
    if (!matrix) return std::unexpected(matrix.error());
    if (!matrix->is_object()) {
        return std::unexpected(Error{"BackendContractMismatch", "GetConflictMatrix did not return an object"});
    }

    const auto plugins = matrix->value("Plugins", nlohmann::json::array());
    std::size_t pluginIndex = 0;
    bool foundPlugin = false;
    if (plugins.is_array()) {
        for (std::size_t i = 0; i < plugins.size(); ++i) {
            if (plugins[i].is_string() &&
                lower(plugins[i].get<std::string>()) == lower(pluginName)) {
                pluginIndex = i;
                foundPlugin = true;
                break;
            }
        }
    }
    if (!foundPlugin) {
        return std::unexpected(Error{
            "RecordVersionNotFound",
            "Record does not have a version in " + pluginName
        });
    }

    nlohmann::json nodes = nlohmann::json::array();
    const auto rows = matrix->value("Rows", nlohmann::json::array());
    if (rows.is_array()) {
        for (const auto& row : rows) {
            if (!row.is_object()) continue;
            const auto values = row.value("Values", nlohmann::json::array());
            std::string value;
            if (values.is_array() && pluginIndex < values.size() && values[pluginIndex].is_string()) {
                value = values[pluginIndex].get<std::string>();
            }
            nodes.push_back({
                {"key", row.value("Field", std::string{})},
                {"label", row.value("DisplayLabel", row.value("Field", std::string{}))},
                {"value", std::move(value)},
                {"values", nlohmann::json::object()},
                {"editKind", row.value("EditKind", std::string("Text"))},
                {"enumOptions", row.contains("EnumOptions") ? row["EnumOptions"] : nlohmann::json(nullptr)},
                {"isSummary", row.value("IsSummary", false)},
                {"hasChildren", false},
                {"children", nlohmann::json::array()}
            });
        }
    }
    return nlohmann::json(nodes.dump());
}

Result<nlohmann::json> legacySearchRecordType(plugin::IPluginBackend& backend,
                                               const nlohmann::json& args,
                                               std::string_view signature,
                                               int defaultLimit) {
    const auto query = stringArgument(args, 0);
    const auto requestedLimit = intArgument(args, 1, defaultLimit);
    const auto limit = requestedLimit <= 0 ? defaultLimit : requestedLimit;
    auto hits = parseStringResult(
        backend.invokeBackend(
            "SearchRecords",
            nlohmann::json::array({query, std::string(signature)})),
        "SearchRecords");
    if (!hits) return std::unexpected(hits.error());
    if (!hits->is_array()) {
        return std::unexpected(Error{
            "BackendContractMismatch",
            "SearchRecords did not return an array"
        });
    }
    if (hits->size() > static_cast<std::size_t>(limit)) {
        hits->erase(hits->begin() + limit, hits->end());
    }
    return nlohmann::json(hits->dump());
}

}

PluginBackendTarget::PluginBackendTarget(plugin::IPluginBackend& backend, std::string targetNamespace)
    : backend_(backend), targetNamespace_(std::move(targetNamespace)) {}

Result<nlohmann::json> PluginBackendTarget::invoke(std::string_view method,
                                                    const nlohmann::json& args) {
    if (targetNamespace_ == "backend") {
        if (method == "SavePatch") {
            const auto pluginName = stringArgument(args, 0);
            return backend_.invokeBackend(
                "SavePlugin", nlohmann::json::array({pluginName, std::string{}}));
        }
        if (method == "RenumberFormId") return legacyRenumberFormId(backend_, args);
        if (method == "ResolveConflict") {
            return backend_.invokeBackend(
                "CopyAsOverride",
                nlohmann::json::array({
                    stringArgument(args, 1),
                    stringArgument(args, 0),
                    stringArgument(args, 2),
                    false
                }));
        }
        if (method == "ResolveFormKeyLabels") return resolveFormKeyLabels(backend_, args);
        if (method == "GetRecordTree") return legacyRecordTree(backend_, args);
        if (method == "GetConditionFunctionParams") return backend_.conditionFunctionParams();
        if (method == "DescribeElement") return backend_.describeElement(args);
        if (method == "AddElement") return backend_.addElement(args);
        if (method == "RemoveElement") return backend_.removeElement(args);
        if (method == "MoveElement") return backend_.moveElement(args);
        if (method == "ClearElement") return backend_.clearElement(args);
        if (method == "CompactToEsl") return backend_.compactToEsl(args);
        return backend_.invokeLegacyBackend(method, args);
    }
    if (targetNamespace_ == "cell") {
        if (method == "SearchCells") return legacySearchRecordType(backend_, args, "CELL", 25);
        if (method == "SearchWorldspaces") return legacySearchRecordType(backend_, args, "WRLD", 100);
        if (method == "GetPlugins") {
            return backend_.invoke("appInterop.GetPlugins", nlohmann::json::array());
        }
        if (method == "GetPlacedReferences") return backend_.getPlacedReferences(args);
        if (method == "GetPlacedReferencesAtGrid") return backend_.getPlacedReferencesAtGrid(args);
        if (method == "SetPlacedReferenceTransform") return backend_.setPlacedReferenceTransform(args);
    }
    return backend_.invoke(targetNamespace_ + "." + std::string(method), args);
}

}
