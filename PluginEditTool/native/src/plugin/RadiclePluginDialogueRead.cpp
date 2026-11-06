#include "RadiclePluginSupport.hpp"
#include "RadiclePluginQuestInternal.hpp"

#include <fo4ide/plugin/ConditionCodec.hpp>

#include <fo4/forms/dialogue.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/schema/ctda.hpp>
#include <fo4/signature.hpp>
#include <fo4/text/encoding.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fo4ide::plugin {

using namespace questinternal;

namespace {

constexpr auto k_dlbr = fo4::Signature::from_string("DLBR");
constexpr auto k_dial = fo4::Signature::from_string("DIAL");
constexpr auto k_full = fo4::Signature::from_string("FULL");
constexpr auto k_pnam = fo4::Signature::from_string("PNAM");
constexpr auto k_bnam = fo4::Signature::from_string("BNAM");
constexpr auto k_knam = fo4::Signature::from_string("KNAM");
constexpr auto k_tnam = fo4::Signature::from_string("TNAM");
constexpr auto k_dnam = fo4::Signature::from_string("DNAM");
constexpr auto k_enam = fo4::Signature::from_string("ENAM");
constexpr auto k_trda = fo4::Signature::from_string("TRDA");
constexpr auto k_nam1 = fo4::Signature::from_string("NAM1");
constexpr auto k_nam2 = fo4::Signature::from_string("NAM2");

template <class T>
T readAt(const std::vector<std::byte>& payload, std::size_t offset) {
    T value{};
    if (offset + sizeof(T) <= payload.size()) std::memcpy(&value, payload.data() + offset, sizeof(T));
    return value;
}

std::vector<std::byte> payloadOf(const fo4::Record& record, fo4::Signature signature) {
    for (const auto& subrecord : record.subrecords) {
        if (subrecord.signature == signature) return subrecord.data;
    }
    return {};
}

std::string cp1252Text(const std::vector<std::byte>& payload) {
    std::string_view bytes(reinterpret_cast<const char*>(payload.data()), payload.size());
    if (!bytes.empty() && bytes.back() == '\0') bytes.remove_suffix(1);
    return fo4::text::windows1252_to_utf8(bytes);
}

void putText(nlohmann::json& object, const char* key, const fo4::Plugin& plugin,
             const std::vector<std::byte>& payload) {
    if (plugin.header.tes4_flags.is_localized() && payload.size() == 4) {
        object[std::string(key) + "Id"] = readAt<std::uint32_t>(payload, 0);
        return;
    }
    object[key] = cp1252Text(payload);
}

nlohmann::json linkJson(const fo4::Plugin& plugin, std::uint32_t raw) {
    if (raw == 0 || raw == 0xFFFFFFFFu) return nullptr;
    return fo4::resolve_plugin_link(fo4::RawFormId{raw}, fo4::plugin_form_id_context(plugin)).to_string();
}

bool isGroupOf(const fo4::GroupHeader& header, fo4::GroupType type, std::uint32_t label) {
    return header.group_type == static_cast<std::int32_t>(type) && header.label_as_u32() == label;
}

const fo4::Group* questChildrenOf(const fo4::Plugin& plugin, fo4::RawFormId quest) {
    for (const auto& top : plugin.groups) {
        if (top.header.top_label() != fo4::sig_qust) continue;
        for (const auto& child : top.children) {
            const auto* group = std::get_if<fo4::Group>(&child);
            if (group != nullptr && isGroupOf(group->header, fo4::GroupType::quest_children, quest.value())) {
                return group;
            }
        }
    }
    return nullptr;
}

nlohmann::json conditionsJson(const fo4::Plugin& plugin, const fo4::Record& record) {
    nlohmann::json conditions = nlohmann::json::array();
    for (const auto& condition : fo4::read_ctda_conditions(record)) {
        conditions.push_back(describeCondition(condition, plugin));
    }
    return conditions;
}

nlohmann::json responsesOf(const fo4::Plugin& plugin, const fo4::Record& info) {
    nlohmann::json responses = nlohmann::json::array();
    for (const auto& subrecord : info.subrecords) {
        if (subrecord.signature == k_trda) {
            responses.push_back({
                {"emotion", linkJson(plugin, readAt<std::uint32_t>(subrecord.data, 0))},
                {"number", readAt<std::uint8_t>(subrecord.data, 4)},
                {"sound", linkJson(plugin, readAt<std::uint32_t>(subrecord.data, 5))},
                {"interruptPercent", readAt<std::uint16_t>(subrecord.data, 10)},
                {"cameraTargetAlias", readAt<std::int32_t>(subrecord.data, 12)},
                {"cameraLocationAlias", readAt<std::int32_t>(subrecord.data, 16)},
            });
            continue;
        }
        if (responses.empty()) continue;
        if (subrecord.signature == k_nam1) putText(responses.back(), "text", plugin, subrecord.data);
        if (subrecord.signature == k_nam2) putText(responses.back(), "scriptNotes", plugin, subrecord.data);
    }
    return responses;
}

nlohmann::json infoJson(const fo4::Plugin& plugin, const fo4::Record& info) {
    const auto enam = payloadOf(info, k_enam);
    const auto resetEncoded = readAt<std::uint16_t>(enam, 2);
    return nlohmann::json{
        {"formKey", info.form_key.to_string()},
        {"flags", readAt<std::uint16_t>(enam, 0)},
        {"resetHours", static_cast<double>(resetEncoded) / fo4::forms::k_info_reset_units_per_hour},
        {"resetTimeEncoded", resetEncoded},
        {"previous", linkJson(plugin, readAt<std::uint32_t>(payloadOf(info, k_pnam), 0))},
        {"responses", responsesOf(plugin, info)},
        {"conditions", conditionsJson(plugin, info)},
    };
}

nlohmann::json infosOf(const fo4::Plugin& plugin, const std::vector<fo4::GroupChild>& children,
                       std::size_t index, fo4::RawFormId dial) {
    nlohmann::json infos = nlohmann::json::array();
    if (index + 1 >= children.size()) return infos;
    const auto* group = std::get_if<fo4::Group>(&children[index + 1]);
    if (group == nullptr || !isGroupOf(group->header, fo4::GroupType::topic_children, dial.value())) {
        return infos;
    }
    for (const auto& child : group->children) {
        const auto* info = std::get_if<fo4::Record>(&child);
        if (info != nullptr && info->header.signature == fo4::sig_info) {
            infos.push_back(infoJson(plugin, *info));
        }
    }
    return infos;
}

nlohmann::json branchJson(const fo4::Plugin& plugin, const fo4::Record& record) {
    return nlohmann::json{
        {"formKey", record.form_key.to_string()},
        {"editorId", support::editorId(record)},
        {"category", readAt<std::uint32_t>(payloadOf(record, k_tnam), 0)},
        {"flags", readAt<std::uint32_t>(payloadOf(record, k_dnam), 0)},
        {"topic", linkJson(plugin, readAt<std::uint32_t>(payloadOf(record, fo4::sig_snam), 0))},
    };
}

nlohmann::json topicJson(const fo4::Plugin& plugin, const fo4::Record& record) {
    const auto data = payloadOf(record, fo4::sig_data);
    nlohmann::json topic{
        {"formKey", record.form_key.to_string()},
        {"editorId", support::editorId(record)},
        {"priority", readAt<float>(payloadOf(record, k_pnam), 0)},
        {"branch", linkJson(plugin, readAt<std::uint32_t>(payloadOf(record, k_bnam), 0))},
        {"keyword", linkJson(plugin, readAt<std::uint32_t>(payloadOf(record, k_knam), 0))},
        {"topicFlags", readAt<std::uint8_t>(data, 0)},
        {"category", readAt<std::uint8_t>(data, 1)},
        {"subtype", readAt<std::uint16_t>(data, 2)},
        {"subtypeName", cp1252Text(payloadOf(record, fo4::sig_snam))},
    };
    putText(topic, "name", plugin, payloadOf(record, k_full));
    return topic;
}

nlohmann::json buildDialogue(const fo4::Plugin& plugin, const fo4::Record& quest) {
    nlohmann::json branches = nlohmann::json::array();
    nlohmann::json topics = nlohmann::json::array();
    if (const auto* children = questChildrenOf(plugin, quest.header.form_id); children != nullptr) {
        for (std::size_t index = 0; index < children->children.size(); ++index) {
            const auto* record = std::get_if<fo4::Record>(&children->children[index]);
            if (record == nullptr) continue;
            if (record->header.signature == k_dlbr) {
                branches.push_back(branchJson(plugin, *record));
            } else if (record->header.signature == k_dial) {
                auto topic = topicJson(plugin, *record);
                topic["infos"] = infosOf(plugin, children->children, index, record->header.form_id);
                topics.push_back(std::move(topic));
            }
        }
    }
    return nlohmann::json{{"branches", branches}, {"topics", topics}};
}

}

Result<nlohmann::json> RadiclePluginBackend::getDialogue(const nlohmann::json& arguments) const {
    const auto pluginName = questStringArg(arguments, 0);
    const auto recordText = questStringArg(arguments, 1);
    if (pluginName.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    if (recordText.empty()) return std::unexpected(Error{"RecordMissing", "Record is empty"});

    OpLock lock(*this, OpLock::Mode::Read);
    const auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});

    auto quest = support::formKeyArgument(recordText);
    if (!quest) return std::unexpected(quest.error());
    const auto* record = fo4::find_record(loaded->plugin, *quest);
    if (record == nullptr) {
        return std::unexpected(Error{"RecordNotFound", "Record is not present in " + pluginName + ": " + recordText});
    }
    if (record->header.signature != fo4::sig_qust) {
        return std::unexpected(Error{"RecordTypeMismatch", "GetDialogue requires a QUST record"});
    }
    try {
        return buildDialogue(loaded->plugin, *record);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"GetDialogueFailed", error.what()});
    }
}

}
