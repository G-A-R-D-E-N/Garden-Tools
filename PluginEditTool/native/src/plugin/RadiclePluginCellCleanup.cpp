#include "RadiclePluginSupport.hpp"

#include <cstddef>
#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <fo4/environment/cell_view.hpp>
#include <fo4/forms/cell_edit.hpp>

namespace fo4ide::plugin {

namespace {

struct CleanupRequest {
    std::string cellId;
    std::string patchName;
    std::string modeText;
    fo4::forms::CleanupMode mode{fo4::forms::CleanupMode::dedupe};
    std::size_t maxCount{0};
    bool byModel{false};
};

Result<CleanupRequest> cleanupRequest(const nlohmann::json& arguments) {
    CleanupRequest request;
    request.cellId = support::stringArgument(arguments, 0);
    request.patchName = support::stringArgument(arguments, 1);
    request.modeText = support::stringArgument(arguments, 2);
    if (request.cellId.empty() || request.patchName.empty()) {
        return std::unexpected(Error{"CellCleanupArgumentsMissing", "Cell cleanup requires a cell and a patch plugin"});
    }
    if (request.modeText == "dedupe") {
        request.mode = fo4::forms::CleanupMode::dedupe;
    } else if (request.modeText == "excess") {
        request.mode = fo4::forms::CleanupMode::excess;
    } else {
        return std::unexpected(Error{"CellCleanupModeInvalid", "Cleanup mode must be dedupe or excess"});
    }
    const bool hasMax = arguments.is_array() && arguments.size() > 3 && arguments[3].is_number_integer();
    if (hasMax) {
        const auto count = arguments[3].get<long long>();
        if (count < 0) return std::unexpected(Error{"CellCleanupArgumentsInvalid", "maxCount cannot be negative"});
        request.maxCount = static_cast<std::size_t>(count);
    }
    if (request.mode == fo4::forms::CleanupMode::excess && !hasMax) {
        return std::unexpected(Error{"CellCleanupArgumentsMissing", "Excess cleanup requires maxCount"});
    }
    request.byModel = arguments.is_array() && arguments.size() > 4 && arguments[4].is_boolean() &&
                      arguments[4].get<bool>();
    return request;
}

Result<fo4::forms::CleanupPlan> planCleanup(const fo4::env::Environment& env, const CleanupRequest& request) {
    const auto cell = fo4::env::resolve_cell_id(env, request.cellId);
    if (!cell) return std::unexpected(Error{"CellNotFound", "Cell not found: " + request.cellId});
    auto plan = fo4::forms::plan_cell_cleanup(env, *cell, request.mode, request.maxCount, request.byModel);
    switch (plan.status) {
        case fo4::forms::CleanupPlanStatus::planned:
            return plan;
        case fo4::forms::CleanupPlanStatus::cell_missing:
            return std::unexpected(Error{"CellNotFound", "Cell not found: " + request.cellId});
        case fo4::forms::CleanupPlanStatus::cell_deleted:
            return std::unexpected(Error{"CellDeleted", "Cell is deleted: " + request.cellId});
        case fo4::forms::CleanupPlanStatus::not_a_cell:
            return std::unexpected(Error{"CellNotACell", "Not a cell record: " + request.cellId});
    }
    return std::unexpected(Error{"CellCleanupFailed", request.cellId});
}

std::string_view reasonText(fo4::forms::CleanupReason reason) {
    if (reason == fo4::forms::CleanupReason::duplicate) return "duplicate";
    if (reason == fo4::forms::CleanupReason::duplicate_of_special) return "duplicateOfSpecial";
    return "excess";
}

nlohmann::json cleanupPlanJson(const fo4::forms::CleanupPlan& plan, const CleanupRequest& request) {
    nlohmann::json entries = nlohmann::json::array();
    std::size_t duplicates = 0;
    std::size_t specials = 0;
    std::size_t excess = 0;
    for (const auto& entry : plan.entries) {
        if (entry.reason == fo4::forms::CleanupReason::duplicate) {
            ++duplicates;
        } else if (entry.reason == fo4::forms::CleanupReason::duplicate_of_special) {
            ++specials;
        } else {
            ++excess;
        }
        nlohmann::json item{
            {"formKey", entry.reference.to_string()},
            {"keptFormKey", entry.reason == fo4::forms::CleanupReason::excess
                                ? nlohmann::json(nullptr)
                                : nlohmann::json(entry.kept.to_string())},
            {"reason", std::string(reasonText(entry.reason))}
        };
        entries.push_back(std::move(item));
    }
    return {
        {"plugin", request.patchName},
        {"cell", plan.cell.to_string()},
        {"mode", request.modeText},
        {"entries", entries},
        {"counts", {{"total", plan.entries.size()}, {"duplicate", duplicates},
                    {"duplicateOfSpecial", specials}, {"excess", excess}}}
    };
}

Error cleanupRefusal(const fo4::forms::CleanupPlan& plan, const fo4::forms::CleanupApplyResult& result) {
    for (std::size_t index = 0; index < result.entry_status.size(); ++index) {
        const auto key = plan.entries[index].reference.to_string();
        if (result.entry_status[index] == fo4::forms::DeleteStatus::referenced) {
            return Error{"PlacedReferenceReferenced", key + " is referenced by other records and cannot be deleted"};
        }
        if (result.entry_status[index] == fo4::forms::DeleteStatus::reference_missing) {
            return Error{"PlacedReferenceNotFound", "Placed reference is not present: " + key};
        }
    }
    return Error{"CellCleanupRefused", "Cell cleanup was refused"};
}

}  // namespace

Result<nlohmann::json> RadiclePluginBackend::planCellCleanup(const nlohmann::json& arguments) const {
    const auto request = cleanupRequest(arguments);
    if (!request) return std::unexpected(request.error());

    OpLock lock(*this, OpLock::Mode::Read);
    if (findPlugin(request->patchName) == nullptr) {
        return std::unexpected(Error{"TargetPluginNotLoaded", "Patch plugin is not loaded yet: " + request->patchName});
    }
    try {
        const auto env = borrowedEnvironment();
        const auto plan = planCleanup(*env, *request);
        if (!plan) return std::unexpected(plan.error());
        return nlohmann::json(cleanupPlanJson(*plan, *request).dump());
    } catch (const std::exception& error) {
        return std::unexpected(Error{"CellCleanupFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::applyCellCleanup(const nlohmann::json& arguments) {
    const auto request = cleanupRequest(arguments);
    if (!request) return std::unexpected(request.error());

    OpLock lock(*this, OpLock::Mode::Write, {request->patchName});
    auto* patch = findPlugin(request->patchName);
    if (patch == nullptr) {
        return std::unexpected(Error{"TargetPluginNotLoaded", "Patch plugin is not loaded yet: " + request->patchName});
    }
    if (auto guard = writeGuard(*patch); guard.has_value()) return std::unexpected(*guard);

    try {
        CommitScope commit;
        const auto env = borrowedEnvironment();
        const auto plan = planCleanup(*env, *request);
        if (!plan) return std::unexpected(plan.error());
        if (const auto error = applyCellCleanupLocked(*patch, *plan)) return std::unexpected(*error);
        auto response = cleanupPlanJson(*plan, *request);
        response["removed"] = plan->entries.size();
        return nlohmann::json(response.dump());
    } catch (const std::exception& error) {
        return std::unexpected(Error{"CellCleanupFailed", error.what()});
    }
}

std::optional<Error> RadiclePluginBackend::applyCellCleanupLocked(
    LoadedPlugin& patch, const fo4::forms::CleanupPlan& plan) {
    CommitScope commit;
    const auto env = borrowedEnvironment();
    auto mutated = patch.plugin;
    const auto result = fo4::forms::apply_cell_cleanup(mutated, *env, plan);
    if (result.status == fo4::forms::CleanupApplyStatus::refused) return cleanupRefusal(plan, result);
    if (!plan.entries.empty()) stageCellEditLocked(patch, std::move(mutated));
    return std::nullopt;
}

}  // namespace fo4ide::plugin
