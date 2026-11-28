#include <fo4ide/hkx/HkxService.hpp>

#include <fo4/hkx/behavior_edit.hpp>
#include <fo4/hkx/behavior_graph.hpp>
#include <fo4/hkx/behavior_validate.hpp>
#include <fo4/hkx/packfile.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace fo4ide::hkx {
namespace {

using fo4::hkx::HkxBehaviorEdit;
using fo4::hkx::HkxBehaviorEditKind;
using fo4::hkx::HkxGraphModel;
using fo4::hkx::HkxGraphTimeInterval;
using fo4::hkx::HkxGraphTransition;
using fo4::hkx::HkxGraphValidation;
using fo4::hkx::HkxStatus;

constexpr std::size_t kIssueLimit = 2000;
constexpr std::size_t kEditLimit = 1000;

constexpr std::array<std::string_view, 4> kConditionKinds{"none", "expression", "string", "other"};

struct KindName {
    std::string_view name;
    HkxBehaviorEditKind kind;
};

constexpr std::array<KindName, 23> kKindNames{{
    {"set_transition", HkxBehaviorEditKind::SetTransition},
    {"add_transition", HkxBehaviorEditKind::AddTransition},
    {"remove_transition", HkxBehaviorEditKind::RemoveTransition},
    {"add_event", HkxBehaviorEditKind::AddEvent},
    {"rename_event", HkxBehaviorEditKind::RenameEvent},
    {"set_event_flags", HkxBehaviorEditKind::SetEventFlags},
    {"remove_event", HkxBehaviorEditKind::RemoveEvent},
    {"add_variable", HkxBehaviorEditKind::AddVariable},
    {"remove_variable", HkxBehaviorEditKind::RemoveVariable},
    {"set_clip_animation", HkxBehaviorEditKind::SetClipAnimation},
    {"rename_variable", HkxBehaviorEditKind::RenameVariable},
    {"set_start_state", HkxBehaviorEditKind::SetStartState},
    {"add_state", HkxBehaviorEditKind::AddState},
    {"remove_state", HkxBehaviorEditKind::RemoveState},
    {"set_binding", HkxBehaviorEditKind::SetBinding},
    {"add_binding", HkxBehaviorEditKind::AddBinding},
    {"remove_binding", HkxBehaviorEditKind::RemoveBinding},
    {"set_generator_field", HkxBehaviorEditKind::SetGeneratorField},
    {"add_blend_child", HkxBehaviorEditKind::AddBlendChild},
    {"remove_blend_child", HkxBehaviorEditKind::RemoveBlendChild},
    {"add_selector_child", HkxBehaviorEditKind::AddSelectorChild},
    {"remove_selector_child", HkxBehaviorEditKind::RemoveSelectorChild},
    {"remove_subtree", HkxBehaviorEditKind::RemoveSubtree},
}};

Result<std::vector<std::byte>> readAll(const std::filesystem::path& path) {
    if (path.empty()) return std::unexpected(Error{"InvalidPath", "HKX path is empty"});
    if (!std::filesystem::is_regular_file(path)) {
        return std::unexpected(Error{"FileNotFound", "HKX not found: " + path.string()});
    }
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) return std::unexpected(Error{"FileNotFound", "Could not open " + path.string()});
    const auto size = static_cast<std::size_t>(stream.tellg());
    std::vector<std::byte> bytes(size);
    stream.seekg(0);
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    if (!stream) return std::unexpected(Error{"ReadFailed", "Could not read " + path.string()});
    return bytes;
}

Result<HkxGraphModel> readModel(std::span<const std::byte> bytes) {
    fo4::hkx::HkxPackfile packfile;
    if (const auto status = fo4::hkx::read_packfile(bytes, packfile); status != HkxStatus::Ok) {
        return std::unexpected(Error{"HkxRead", "packfile read failed (status " + std::to_string(static_cast<int>(status)) + ")"});
    }
    HkxGraphModel model;
    if (const auto status = fo4::hkx::read_behavior_model(packfile, model); status != HkxStatus::Ok) {
        return std::unexpected(Error{"HkxRead", "behavior model read failed (status " + std::to_string(static_cast<int>(status)) + ")"});
    }
    return model;
}

nlohmann::json validationJson(const HkxGraphValidation& validation) {
    auto issues = nlohmann::json::array();
    for (std::size_t i = 0; i < validation.issues.size() && i < kIssueLimit; ++i) {
        const auto& issue = validation.issues[i];
        issues.push_back({{"severity", issue.severity == fo4::hkx::HkxGraphIssueSeverity::Error ? "error" : "warning"},
                          {"code", issue.code},
                          {"table", issue.table},
                          {"index", issue.index},
                          {"message", issue.message}});
    }
    return nlohmann::json{{"errors", validation.errors},
                          {"warnings", validation.warnings},
                          {"issue_total", validation.issues.size()},
                          {"issues", std::move(issues)}};
}

nlohmann::json intervalJson(const HkxGraphTimeInterval& interval) {
    return nlohmann::json{{"enter_event_id", interval.enter_event_id},
                          {"exit_event_id", interval.exit_event_id},
                          {"enter_time", interval.enter_time},
                          {"exit_time", interval.exit_time}};
}

nlohmann::json transitionJson(std::size_t index, const HkxGraphTransition& t) {
    const auto kind = static_cast<std::size_t>(t.condition_kind);
    return nlohmann::json{{"index", index},
                          {"state_machine", t.state_machine},
                          {"state", t.state},
                          {"event_id", t.event_id},
                          {"to_state_id", t.to_state_id},
                          {"from_nested_state_id", t.from_nested_state_id},
                          {"to_nested_state_id", t.to_nested_state_id},
                          {"priority", t.priority},
                          {"flags", t.flags},
                          {"trigger_interval", intervalJson(t.trigger_interval)},
                          {"initiate_interval", intervalJson(t.initiate_interval)},
                          {"effect_node", t.effect_node},
                          {"condition_kind", std::string(kConditionKinds[kind < kConditionKinds.size() ? kind : 3])},
                          {"condition", t.condition},
                          {"condition_class", t.condition_class}};
}

bool readInt(const nlohmann::json& object, const char* key, long long low, long long high, bool required,
             long long& value, std::string& error) {
    const auto found = object.find(key);
    if (found == object.end()) {
        if (!required) return true;
        error = std::string("missing field ") + key;
        return false;
    }
    if (!found->is_number_integer()) {
        error = std::string(key) + " must be an integer";
        return false;
    }
    const bool tooBig = found->is_number_unsigned() &&
                        found->get<unsigned long long>() > static_cast<unsigned long long>(high);
    if (tooBig || (!found->is_number_unsigned() && (found->get<long long>() < low || found->get<long long>() > high))) {
        error = std::string(key) + " is outside " + std::to_string(low) + ".." + std::to_string(high);
        return false;
    }
    value = found->is_number_unsigned() ? static_cast<long long>(found->get<unsigned long long>()) : found->get<long long>();
    return true;
}

bool readFloat(const nlohmann::json& object, const char* key, bool required, float& value, std::string& error) {
    const auto found = object.find(key);
    if (found == object.end()) {
        if (!required) return true;
        error = std::string("missing field ") + key;
        return false;
    }
    if (!found->is_number()) {
        error = std::string(key) + " must be a number";
        return false;
    }
    const double number = found->get<double>();
    if (!std::isfinite(number) || std::fabs(number) > static_cast<double>(std::numeric_limits<float>::max())) {
        error = std::string(key) + " must be a finite single-precision number";
        return false;
    }
    value = static_cast<float>(number);
    return true;
}

bool readInterval(const nlohmann::json& object, const char* key, bool required, HkxGraphTimeInterval& interval,
                  std::string& error) {
    const auto found = object.find(key);
    if (found == object.end()) {
        if (!required) return true;
        error = std::string("missing field ") + key;
        return false;
    }
    if (!found->is_object()) {
        error = std::string(key) + " must be an object";
        return false;
    }
    long long enter = interval.enter_event_id;
    long long leave = interval.exit_event_id;
    constexpr long long low = std::numeric_limits<std::int32_t>::min();
    constexpr long long high = std::numeric_limits<std::int32_t>::max();
    if (!readInt(*found, "enter_event_id", low, high, required, enter, error) ||
        !readInt(*found, "exit_event_id", low, high, required, leave, error) ||
        !readFloat(*found, "enter_time", required, interval.enter_time, error) ||
        !readFloat(*found, "exit_time", required, interval.exit_time, error)) {
        error = std::string(key) + ": " + error;
        return false;
    }
    interval.enter_event_id = static_cast<std::int32_t>(enter);
    interval.exit_event_id = static_cast<std::int32_t>(leave);
    return true;
}

bool readTransition(const nlohmann::json& edit, bool complete, HkxGraphTransition& out, std::string& error) {
    const auto found = edit.find("transition");
    if (found == edit.end() || !found->is_object()) {
        error = "transition must be an object";
        return false;
    }
    const auto& t = *found;
    constexpr long long low = std::numeric_limits<std::int32_t>::min();
    constexpr long long high = std::numeric_limits<std::int32_t>::max();
    long long event = -1;
    long long to = 0;
    long long fromNested = 0;
    long long toNested = 0;
    long long priority = 0;
    long long flags = 0;
    long long effect = -1;
    if (!readInt(t, "event_id", low, high, true, event, error) || !readInt(t, "to_state_id", low, high, true, to, error) ||
        !readInt(t, "from_nested_state_id", low, high, complete, fromNested, error) ||
        !readInt(t, "to_nested_state_id", low, high, complete, toNested, error) ||
        !readInt(t, "priority", std::numeric_limits<std::int16_t>::min(), std::numeric_limits<std::int16_t>::max(), complete,
                 priority, error) ||
        !readInt(t, "flags", 0, std::numeric_limits<std::uint16_t>::max(), complete, flags, error) ||
        !readInt(t, "effect_node", -1, high, complete, effect, error) ||
        !readInterval(t, "trigger_interval", complete, out.trigger_interval, error) ||
        !readInterval(t, "initiate_interval", complete, out.initiate_interval, error)) {
        error = "transition: " + error;
        return false;
    }
    out.event_id = static_cast<std::int32_t>(event);
    out.to_state_id = static_cast<std::int32_t>(to);
    out.from_nested_state_id = static_cast<std::int32_t>(fromNested);
    out.to_nested_state_id = static_cast<std::int32_t>(toNested);
    out.priority = static_cast<std::int16_t>(priority);
    out.flags = static_cast<std::uint16_t>(flags);
    out.effect_node = static_cast<std::int32_t>(effect);
    return true;
}

bool parseEdit(const nlohmann::json& item, HkxBehaviorEdit& edit, std::string& error) {
    if (!item.is_object()) {
        error = "must be an object";
        return false;
    }
    const auto kind = item.find("kind");
    if (kind == item.end() || !kind->is_string()) {
        error = "kind must be a string";
        return false;
    }
    const auto name = kind->get<std::string>();
    std::optional<HkxBehaviorEditKind> known;
    for (const auto& entry : kKindNames) {
        if (entry.name == name) known = entry.kind;
    }
    if (!known) {
        error = "unknown kind '" + name + "'";
        return false;
    }
    edit.kind = *known;

    const bool transitionEdit = edit.kind == HkxBehaviorEditKind::SetTransition ||
                                edit.kind == HkxBehaviorEditKind::AddTransition ||
                                edit.kind == HkxBehaviorEditKind::RemoveTransition;
    const bool machineEdit = edit.kind == HkxBehaviorEditKind::AddState || edit.kind == HkxBehaviorEditKind::SetStartState ||
                             edit.kind == HkxBehaviorEditKind::RemoveState;
    const bool bindingEdit = edit.kind == HkxBehaviorEditKind::SetBinding || edit.kind == HkxBehaviorEditKind::AddBinding ||
                             edit.kind == HkxBehaviorEditKind::RemoveBinding;
    const bool appendEdit = edit.kind == HkxBehaviorEditKind::AddEvent || edit.kind == HkxBehaviorEditKind::AddVariable ||
                            edit.kind == HkxBehaviorEditKind::AddSelectorChild || edit.kind == HkxBehaviorEditKind::RemoveSubtree;
    constexpr long long maxIndex = std::numeric_limits<std::int32_t>::max();
    long long index = 0;
    if (!readInt(item, "index", 0, maxIndex, !appendEdit && !machineEdit, index, error)) return false;
    edit.index = static_cast<std::size_t>(index);
    if (transitionEdit) {
        long long machine = 0;
        long long state = -1;
        if (!readInt(item, "state_machine", 0, maxIndex, true, machine, error) ||
            !readInt(item, "state", -1, maxIndex, true, state, error)) {
            return false;
        }
        edit.state_machine = static_cast<std::size_t>(machine);
        edit.state = static_cast<std::int32_t>(state);
        if (edit.kind != HkxBehaviorEditKind::RemoveTransition &&
            !readTransition(item, edit.kind == HkxBehaviorEditKind::SetTransition, edit.transition, error)) {
            return false;
        }
        return true;
    }
    const bool needsName = edit.kind == HkxBehaviorEditKind::AddEvent || edit.kind == HkxBehaviorEditKind::RenameEvent ||
                           edit.kind == HkxBehaviorEditKind::AddVariable || edit.kind == HkxBehaviorEditKind::RenameVariable ||
                           edit.kind == HkxBehaviorEditKind::SetClipAnimation || edit.kind == HkxBehaviorEditKind::AddState ||
                           edit.kind == HkxBehaviorEditKind::AddBinding || edit.kind == HkxBehaviorEditKind::SetGeneratorField;
    if (needsName) {
        const auto text = item.find("name");
        if (text == item.end() || !text->is_string()) {
            error = "name must be a string";
            return false;
        }
        edit.name = text->get<std::string>();
    }
    if (bindingEdit) {
        constexpr long long low = std::numeric_limits<std::int32_t>::min();
        long long value = 0;
        long long ownerKind = 0;
        if (!readInt(item, "value", low, maxIndex, edit.kind != HkxBehaviorEditKind::RemoveBinding, value, error) ||
            !readInt(item, "flags", 0, 255, edit.kind == HkxBehaviorEditKind::AddBinding, ownerKind, error)) {
            return false;
        }
        edit.value = static_cast<std::int32_t>(value);
        edit.flags = static_cast<std::uint32_t>(ownerKind);
        return true;
    }
    if (edit.kind == HkxBehaviorEditKind::SetGeneratorField) {
        constexpr long long low = std::numeric_limits<std::int32_t>::min();
        long long value = 0;
        long long ownerKind = 0;
        if (!readInt(item, "flags", 0, 255, true, ownerKind, error) || !readInt(item, "value", low, maxIndex, false, value, error) ||
            !readFloat(item, "real", false, edit.real, error)) {
            return false;
        }
        if (item.find("value") == item.end() && item.find("real") == item.end()) {
            error = "missing field value or real";
            return false;
        }
        edit.flags = static_cast<std::uint32_t>(ownerKind);
        edit.value = static_cast<std::int32_t>(value);
        return true;
    }
    if (edit.kind == HkxBehaviorEditKind::AddBlendChild || edit.kind == HkxBehaviorEditKind::AddSelectorChild ||
        edit.kind == HkxBehaviorEditKind::RemoveSubtree) {
        constexpr long long low = std::numeric_limits<std::int32_t>::min();
        long long node = -1;
        long long value = 0;
        const bool selector = edit.kind == HkxBehaviorEditKind::AddSelectorChild;
        if (!readInt(item, "node", -1, maxIndex, true, node, error) || !readInt(item, "value", low, maxIndex, selector, value, error) ||
            !readFloat(item, "real", edit.kind == HkxBehaviorEditKind::AddBlendChild, edit.real, error)) {
            return false;
        }
        edit.node = static_cast<std::int32_t>(node);
        edit.value = static_cast<std::int32_t>(value);
        return true;
    }
    if (edit.kind == HkxBehaviorEditKind::RemoveBlendChild || edit.kind == HkxBehaviorEditKind::RemoveSelectorChild) return true;
    if (machineEdit) {
        constexpr long long low = std::numeric_limits<std::int32_t>::min();
        long long machine = 0;
        long long state = -1;
        long long value = 0;
        long long node = -1;
        if (!readInt(item, "state_machine", 0, maxIndex, true, machine, error) ||
            !readInt(item, "state", -1, maxIndex, edit.kind == HkxBehaviorEditKind::RemoveState, state, error) ||
            !readInt(item, "value", low, maxIndex, edit.kind != HkxBehaviorEditKind::RemoveState, value, error) ||
            !readInt(item, "node", -1, maxIndex, edit.kind == HkxBehaviorEditKind::AddState, node, error)) {
            return false;
        }
        edit.state_machine = static_cast<std::size_t>(machine);
        edit.state = static_cast<std::int32_t>(state);
        edit.value = static_cast<std::int32_t>(value);
        edit.node = static_cast<std::int32_t>(node);
        return true;
    }
    long long flags = 0;
    if (!readInt(item, "flags", 0, std::numeric_limits<std::uint32_t>::max(),
                 edit.kind == HkxBehaviorEditKind::SetEventFlags || edit.kind == HkxBehaviorEditKind::AddVariable, flags,
                 error)) {
        return false;
    }
    edit.flags = static_cast<std::uint32_t>(flags);
    return true;
}

Result<std::vector<HkxBehaviorEdit>> parseEdits(const nlohmann::json& edits) {
    if (!edits.is_array() || edits.empty()) {
        return std::unexpected(Error{"InvalidEdits", "edits must be a non-empty array"});
    }
    if (edits.size() > kEditLimit) {
        return std::unexpected(Error{"InvalidEdits", "at most " + std::to_string(kEditLimit) + " edits per call"});
    }
    std::vector<HkxBehaviorEdit> parsed;
    parsed.reserve(edits.size());
    for (std::size_t i = 0; i < edits.size(); ++i) {
        HkxBehaviorEdit edit;
        std::string error;
        if (!parseEdit(edits[i], edit, error)) {
            return std::unexpected(Error{"InvalidEdits", "edit " + std::to_string(i) + ": " + error});
        }
        parsed.push_back(std::move(edit));
    }
    return parsed;
}

}  // namespace

// Invariant: every list is complete, because an editor that saw a cut-off list could write rows it never read.
Result<nlohmann::json> HkxService::readBehaviorAuthoring(const std::filesystem::path& path) const {
    const auto bytes = readAll(path);
    if (!bytes) return std::unexpected(bytes.error());
    const auto model = readModel(*bytes);
    if (!model) return std::unexpected(model.error());

    auto events = nlohmann::json::array();
    for (const auto& event : model->events) events.push_back({{"name", event.name}, {"flags", event.flags}});
    auto variables = nlohmann::json::array();
    for (const auto& variable : model->variables) variables.push_back({{"name", variable.name}, {"type", variable.type}});
    auto nodes = nlohmann::json::array();
    for (const auto& node : model->nodes) {
        nodes.push_back({{"name", node.name}, {"class_name", node.class_name}, {"kind", static_cast<int>(node.kind)}});
    }
    auto machines = nlohmann::json::array();
    for (const auto& machine : model->state_machines) {
        machines.push_back({{"node", machine.node},
                            {"start_state_id", machine.start_state_id},
                            {"start_state_mode", machine.start_state_mode},
                            {"change_event_id", machine.change_event_id},
                            {"sync_variable_index", machine.sync_variable_index},
                            {"first_state", machine.first_state},
                            {"state_count", machine.state_count},
                            {"first_wildcard", machine.first_wildcard},
                            {"wildcard_count", machine.wildcard_count}});
    }
    auto states = nlohmann::json::array();
    for (const auto& state : model->states) {
        states.push_back({{"state_machine", state.state_machine},
                          {"name", state.name},
                          {"state_id", state.state_id},
                          {"generator_node", state.generator_node},
                          {"enable", state.enable},
                          {"first_transition", state.first_transition},
                          {"transition_count", state.transition_count}});
    }
    auto transitions = nlohmann::json::array();
    for (std::size_t i = 0; i < model->transitions.size(); ++i) transitions.push_back(transitionJson(i, model->transitions[i]));
    auto clips = nlohmann::json::array();
    for (const auto& clip : model->clips) {
        clips.push_back({{"node", clip.node},
                         {"animation", clip.animation},
                         {"mode", clip.mode},
                         {"flags", clip.flags},
                         {"start_time", clip.start_time},
                         {"playback_speed", clip.playback_speed},
                         {"crop_start", clip.crop_start},
                         {"crop_end", clip.crop_end},
                         {"enforced_duration", clip.enforced_duration}});
    }
    auto blenders = nlohmann::json::array();
    for (const auto& blender : model->blenders) {
        blenders.push_back({{"node", blender.node},
                            {"first_child", blender.first_child},
                            {"child_count", blender.child_count},
                            {"blend_parameter", blender.blend_parameter},
                            {"min_cyclic_blend_parameter", blender.min_cyclic_blend_parameter},
                            {"max_cyclic_blend_parameter", blender.max_cyclic_blend_parameter},
                            {"index_of_sync_master_child", blender.index_of_sync_master_child}});
    }
    auto blendChildren = nlohmann::json::array();
    for (const auto& child : model->blend_children) {
        blendChildren.push_back({{"blender", child.blender},
                                 {"generator_node", child.generator_node},
                                 {"weight", child.weight},
                                 {"world_from_model_weight", child.world_from_model_weight}});
    }
    auto edges = nlohmann::json::array();
    for (const auto& edge : model->edges) {
        edges.push_back({{"parent", edge.parent},
                         {"child", edge.child},
                         {"role", static_cast<int>(edge.role)},
                         {"slot", edge.slot},
                         {"weight", edge.weight}});
    }
    auto bindings = nlohmann::json::array();
    for (const auto& binding : model->bindings) {
        bindings.push_back({{"owner_kind", static_cast<int>(binding.owner_kind)},
                            {"owner_index", binding.owner_index},
                            {"member_path", binding.member_path},
                            {"variable_index", binding.variable_index},
                            {"binding_type", binding.binding_type}});
    }

    return nlohmann::json{{"graph_name", model->graph_name},
                          {"root_node", model->root_node},
                          {"events", std::move(events)},
                          {"variables", std::move(variables)},
                          {"nodes", std::move(nodes)},
                          {"state_machines", std::move(machines)},
                          {"states", std::move(states)},
                          {"transitions", std::move(transitions)},
                          {"clips", std::move(clips)},
                          {"blenders", std::move(blenders)},
                          {"blend_children", std::move(blendChildren)},
                          {"edges", std::move(edges)},
                          {"bindings", std::move(bindings)},
                          {"validation", validationJson(fo4::hkx::validate_behavior_model(*model))}};
}

Result<nlohmann::json> HkxService::validateBehaviorGraph(const std::filesystem::path& path) const {
    const auto bytes = readAll(path);
    if (!bytes) return std::unexpected(bytes.error());
    const auto model = readModel(*bytes);
    if (!model) return std::unexpected(model.error());
    return validationJson(fo4::hkx::validate_behavior_model(*model));
}

// Constraint: an empty outPath previews and writes nothing; a written file is read back and must equal the bytes that were validated.
Result<nlohmann::json> HkxService::editBehaviorGraph(const std::filesystem::path& path,
                                                     const nlohmann::json& edits,
                                                     const std::filesystem::path& outPath,
                                                     bool overwrite) const {
    const auto parsed = parseEdits(edits);
    if (!parsed) return std::unexpected(parsed.error());
    if (!outPath.empty()) {
        if (auto allowed = checkSaveTarget(outPath, "A behavior graph"); !allowed) return std::unexpected(allowed.error());
    }
    const auto bytes = readAll(path);
    if (!bytes) return std::unexpected(bytes.error());

    std::vector<std::byte> out;
    const auto report = fo4::hkx::apply_behavior_edits(*bytes, *parsed, out);
    if (!report.ok) {
        return std::unexpected(Error{"BehaviorEditRefused",
                                     "edit " + std::to_string(report.refused_edit) + ": " + report.refusal});
    }
    nlohmann::json reply{{"ok", true},
                         {"applied", report.applied},
                         {"written", false},
                         {"size", out.size()},
                         {"before", validationJson(report.before)},
                         {"after", validationJson(report.after)}};
    if (outPath.empty()) return reply;

    if (auto published = publishHkxBytes(outPath, out, overwrite, "WriteFailed"); !published) {
        return std::unexpected(published.error());
    }
    const auto written = readAll(outPath);
    if (!written || *written != out) {
        return std::unexpected(Error{"WriteVerifyFailed", "The written file does not match the validated bytes: " + outPath.string()});
    }
    reply["written"] = true;
    reply["path"] = outPath.string();
    return reply;
}

}  // namespace fo4ide::hkx
