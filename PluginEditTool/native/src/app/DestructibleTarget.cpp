#include <fo4ide/app/DestructibleTarget.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace fo4ide::app {
namespace {

using json = nlohmann::json;

constexpr std::string_view kNoPlugin = "no plugin is open in the destructible group editor";

std::string trimmed(std::string_view text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
    return std::string(text.substr(begin, end - begin));
}

bool sameName(std::string_view left, std::string_view right) {
    return std::ranges::equal(left, right, [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    });
}

std::string fileName(std::string_view path) {
    const auto cut = path.find_last_of("/\\");
    return std::string(cut == std::string_view::npos ? path : path.substr(cut + 1));
}

json text(const json& value) {
    return json(value.dump(-1, ' ', false, json::error_handler_t::replace));
}

json errorOnly(std::string message) {
    return json{{"error", std::move(message)}};
}

json errorOnly(const Error& error) {
    return errorOnly(error.message);
}

Result<json> call(plugin::IPluginBackend& backend, std::string_view operation, json arguments) {
    auto reply = backend.invoke(operation, arguments);
    if (!reply) return std::unexpected(reply.error());
    if (!reply->is_string()) return *reply;
    json parsed = json::parse(reply->get<std::string>(), nullptr, false);
    if (parsed.is_discarded()) {
        return std::unexpected(Error{"DestructibleFailed", std::string(operation) + " returned text that is not JSON"});
    }
    return parsed;
}

json snapshot(const json& backendReply, json formId, bool statusIsError) {
    json out{{"path", backendReply.value("path", "")},
             {"plugin", backendReply.value("plugin", "")},
             {"formId", std::move(formId)},
             {"dirty", backendReply.value("dirty", false)},
             {"error", nullptr},
             {"group", backendReply.contains("group") ? backendReply.at("group") : json(nullptr)}};
    const auto status = backendReply.value("status", "");
    if (statusIsError && !status.empty() && status != "Updated" && status != "Present" && status != "Unchanged") {
        out["error"] = status;
    }
    return out;
}

struct RecordRef {
    std::string key;
    std::string hex;
};

Result<RecordRef> recordRef(const json& args, std::string_view plugin) {
    std::string value = args.is_array() && !args.empty() && args[0].is_string() ? args[0].get<std::string>() : "";
    value = trimmed(value);
    value = value.substr(0, value.find(':'));
    const auto first = value.find_first_not_of("0xX");
    value = first == std::string::npos ? std::string{} : value.substr(first);
    if (value.empty()) return std::unexpected(Error{"InvalidFormId", "A record form ID is required."});
    std::uint32_t local = 0;
    const auto* end = value.data() + value.size();
    const auto [stop, error] = std::from_chars(value.data(), end, local, 16);
    if (value.size() > 8 || error != std::errc{} || stop != end || local > 0xFFFFFFu) {
        return std::unexpected(Error{"InvalidFormId", "Not a record form ID: " + value});
    }
    auto hex = std::format("{:06X}", local);
    return RecordRef{hex + ":" + std::string(plugin), std::move(hex)};
}

}  // namespace

DestructibleTarget::DestructibleTarget(plugin::IPluginBackend& backend) : backend_(backend) {}

Result<json> DestructibleTarget::invoke(std::string_view method, const json& args) {
    std::lock_guard lock(mutex_);
    if (method == "OpenGroupEditor") return text(open(args));
    if (method == "CloseGroupEditor") return text(close());
    if (method == "GroupState") return text(state());
    if (method == "ReadGroup") return text(read(args));
    if (method == "SaveGroup") return text(save());
    if (method == "CreateGroupRecord") return text(create(args));
    if (method == "SetGroupHeader") return text(setHeader(args));
    if (method == "InsertGroupHeader") return text(edit(args, "insertHeader"));
    if (method == "RemoveGroupHeader") return text(edit(args, "removeHeader"));
    if (method == "InsertGroupStage") return text(edit(args, "insertStage"));
    if (method == "RemoveGroupStage") return text(edit(args, "removeStage"));
    if (method == "InsertGroupStageMember") return text(edit(args, "insertMember"));
    if (method == "RemoveGroupStageMember") return text(edit(args, "removeMember"));
    if (method == "SetGroupStageValue") return text(edit(args, "setStageValue"));
    if (method == "SetGroupResistanceValue") return text(edit(args, "setResistanceValue"));
    if (method == "SetGroupResistanceDamageType") return text(edit(args, "setResistanceDamageType"));
    return std::unexpected(Error{"UnknownMethod", "destructible has no method " + std::string(method)});
}

json DestructibleTarget::open(const json& args) {
    const auto path = trimmed(args.is_array() && !args.empty() && args[0].is_string() ? args[0].get<std::string>() : "");
    if (path.empty()) return errorOnly("A plugin path is required.");
    const auto name = fileName(path);

    if (!plugin_.empty() && !sameName(plugin_, name)) {
        const auto current = call(backend_, "destructible.Read", json::array({plugin_}));
        if (current && current->value("dirty", false)) {
            return json{{"error", plugin_ + " has unsaved edits; save them before opening another plugin."},
                        {"path", current->value("path", "")},
                        {"plugin", current->value("plugin", plugin_)},
                        {"dirty", true}};
        }
    }

    auto loaded = call(backend_, "destructible.Read", json::array({name}));
    if (!loaded && loaded.error().type == "PluginNotLoaded") {
        const auto opened = call(backend_, "backend.OpenPlugin", json::array({path}));
        if (!opened) return errorOnly(opened.error());
        loaded = call(backend_, "destructible.Read", json::array({opened->value("plugin", name)}));
    }
    if (!loaded) return errorOnly(loaded.error());
    plugin_ = loaded->value("plugin", name);
    return snapshot(*loaded, nullptr, false);
}

json DestructibleTarget::close() {
    plugin_.clear();
    return json{{"plugin", ""}, {"dirty", false}, {"error", nullptr}, {"group", nullptr}};
}

json DestructibleTarget::state() {
    if (plugin_.empty()) return errorOnly(std::string(kNoPlugin));
    const auto loaded = call(backend_, "destructible.Read", json::array({plugin_}));
    if (!loaded) return errorOnly(loaded.error());
    return snapshot(*loaded, nullptr, false);
}

json DestructibleTarget::read(const json& args) {
    if (plugin_.empty()) return errorOnly(std::string(kNoPlugin));
    const auto ref = recordRef(args, plugin_);
    if (!ref) return errorOnly(ref.error());
    const auto loaded = call(backend_, "destructible.Read", json::array({plugin_, ref->key}));
    if (!loaded) return errorOnly(loaded.error());
    return snapshot(*loaded, ref->hex, false);
}

json DestructibleTarget::edit(const json& args, const char* action) {
    if (plugin_.empty()) return errorOnly(std::string(kNoPlugin));
    const auto ref = recordRef(args, plugin_);
    if (!ref) return errorOnly(ref.error());
    json forwarded = json::array({plugin_, ref->key, action});
    for (std::size_t index = 1; args.is_array() && index < args.size(); ++index) forwarded.push_back(args[index]);
    const auto edited = call(backend_, "destructible.Edit", std::move(forwarded));
    if (!edited) return errorOnly(edited.error());
    return snapshot(*edited, ref->hex, true);
}

json DestructibleTarget::setHeader(const json& args) {
    if (!args.is_array() || args.size() < 3 || !args[1].is_number_integer()) {
        return errorOnly("The DEST health must be a whole number.");
    }
    if (!args[2].is_number_integer() || args[2].get<std::int64_t>() < 0 || args[2].get<std::int64_t>() > 255) {
        return errorOnly("The DEST flags byte is a u8.");
    }
    return edit(args, "setHeader");
}

json DestructibleTarget::create(const json& args) {
    if (plugin_.empty()) return errorOnly(std::string(kNoPlugin));
    const auto signature = args.is_array() && !args.empty() && args[0].is_string() ? args[0].get<std::string>() : "";
    const auto editorId = args.is_array() && args.size() > 1 && args[1].is_string() ? args[1].get<std::string>() : "";
    const auto created = call(backend_, "destructible.Create", json::array({plugin_, signature, editorId}));
    if (!created) return errorOnly(created.error());
    return snapshot(*created, created->value("formId", ""), true);
}

json DestructibleTarget::save() {
    if (plugin_.empty()) return errorOnly(std::string(kNoPlugin));
    const auto saved = backend_.invoke("backend.SavePlugin", json::array({plugin_}));
    if (!saved) return errorOnly(saved.error());
    const auto loaded = call(backend_, "destructible.Read", json::array({plugin_}));
    if (!loaded) return errorOnly(loaded.error());
    return json{{"saved", loaded->value("path", "")},
                {"reloaded", true},
                {"path", loaded->value("path", "")},
                {"plugin", loaded->value("plugin", plugin_)},
                {"dirty", loaded->value("dirty", false)}};
}

}  // namespace fo4ide::app
