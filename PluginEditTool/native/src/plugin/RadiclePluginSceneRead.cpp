#include "RadiclePluginSupport.hpp"
#include "RadiclePluginQuestInternal.hpp"
#include "RadiclePluginFragmentSupport.hpp"
#include "RadiclePluginReadJson.hpp"

#include <fo4/environment/dialogue.hpp>
#include <fo4/environment/scene_layout.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/schema/condition_scopes.hpp>
#include <fo4/schema/ctda.hpp>
#include <fo4/signature.hpp>
#include <fo4/text/encoding.hpp>

#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4ide::plugin {

using namespace questinternal;

namespace {

constexpr auto k_scen = fo4::Signature::from_string("SCEN");
constexpr auto k_dial = fo4::Signature::from_string("DIAL");
constexpr auto k_info = fo4::Signature::from_string("INFO");
constexpr auto k_trda = fo4::Signature::from_string("TRDA");
constexpr auto k_nam1 = fo4::Signature::from_string("NAM1");

using namespace readjson;

using PhaseSpan = fo4::env::detail::ScenePhaseSpan;
using ActionSpan = fo4::env::detail::SceneActionSpan;
using SceneLayout = fo4::env::detail::SceneLayout;

// Requires: Radicle's scanner is the only structural reader, so both sides agree on a malformed scene.
std::optional<SceneLayout> scanLayout(const fo4::Record& record) {
    try {
        return fo4::env::detail::scene_layout(record.subrecords);
    } catch (const fo4::env::detail::SceneLayoutError&) {
        return std::nullopt;
    }
}

std::vector<fo4::CtdaCondition> conditionsIn(const fo4::Record& record, std::size_t begin, std::size_t end) {
    try {
        return fo4::read_ctda_conditions_in(record, begin, end);
    } catch (const std::exception&) {
        return {};
    }
}

const fo4::Subrecord* firstOfIn(const fo4::Record& record, std::size_t begin, std::size_t end,
                                fo4::Signature signature) {
    for (std::size_t at = begin; at < end && at < record.subrecords.size(); ++at) {
        if (record.subrecords[at].signature == signature) return &record.subrecords[at];
    }
    return nullptr;
}

nlohmann::json phaseJson(const fo4::Plugin& plugin, const fo4::Record& record, std::size_t index,
                         const PhaseSpan& span, const fo4::SceneVmadExtras* fragments) {
    nlohmann::json phase{{"index", index}};
    if (const auto* name = firstOfIn(record, span.open + 1, span.close,
                                     fo4::Signature::from_string("NAM0")); name != nullptr) {
        putText(phase, "name", plugin, *name);
    }
    if (const auto* subrecord = firstOfIn(record, span.open + 1, span.close,
                                         fo4::Signature::from_string("WNAM"));
        subrecord != nullptr && sized(*subrecord, 4)) {
        phase["editorWidth"] = readAt<std::uint32_t>(subrecord->data, 0);
    }
    if (const auto* subrecord = firstOfIn(record, span.open + 1, span.close,
                                         fo4::Signature::from_string("FNAM"));
        subrecord != nullptr && sized(*subrecord, 2)) {
        phase["flags"] = readAt<std::uint16_t>(subrecord->data, 0);
    }
    if (const auto* subrecord = firstOfIn(record, span.open + 1, span.close,
                                         fo4::Signature::from_string("SCQS"));
        subrecord != nullptr && sized(*subrecord, 4)) {
        phase["questStageOnStart"] = readAt<std::int16_t>(subrecord->data, 0);
        phase["questStageOnCompletion"] = readAt<std::int16_t>(subrecord->data, 2);
    }
    phase["startConditions"] = conditionsJson(plugin, conditionsIn(record, span.open + 1, span.first_next));
    phase["completionConditions"] =
        conditionsJson(plugin, conditionsIn(record, span.first_next + 1, span.second_next));
    if (fragments != nullptr) {
        for (const auto& fragment : fragments->phases) {
            if (fragment.index != index) continue;
            nlohmann::json binding{{"script", fragment.script}, {"function", fragment.function}};
            if (fragment.flag == 1) {
                phase["startFragment"] = std::move(binding);
            } else if (fragment.flag == 2) {
                phase["completionFragment"] = std::move(binding);
            }
        }
    }
    return phase;
}

nlohmann::json actorJson(const fo4::Record& record, std::size_t index) {
    const auto& subs = record.subrecords;
    return nlohmann::json{
        {"index", index},
        {"alias", readAt<std::int32_t>(subs[index].data, 0)},
        {"flags", readAt<std::uint32_t>(subs[index + 1].data, 0)},
        {"behaviorFlags", readAt<std::uint32_t>(subs[index + 2].data, 0)},
    };
}

struct TopicInfo {
    fo4::FormKey key;
    std::vector<std::pair<std::uint8_t, std::string>> responses;
};

std::optional<TopicInfo> infoOf(const fo4::Plugin& plugin, const fo4::Record& record) {
    TopicInfo out;
    out.key = record.form_key;
    for (const auto& subrecord : record.subrecords) {
        if (subrecord.signature != k_trda) continue;
        if (!sized(subrecord, 20)) continue;
        out.responses.emplace_back(readAt<std::uint8_t>(subrecord.data, 4), std::string{});
    }
    std::size_t at = 0;
    for (const auto& subrecord : record.subrecords) {
        if (subrecord.signature == k_trda) {
            ++at;
            continue;
        }
        if (at == 0 || at > out.responses.size()) continue;
        if (subrecord.signature == k_nam1) {
            if (plugin.header.tes4_flags.is_localized() && subrecord.data.size() == 4) {
                out.responses[at - 1].second =
                    "#" + std::to_string(readAt<std::uint32_t>(subrecord.data, 0));
            } else {
                out.responses[at - 1].second = cp1252Text(subrecord.data);
            }
        }
    }
    return out;
}

const fo4::Group* childGroupOf(const fo4::Plugin& plugin, const fo4::Group& parent, std::int32_t type,
                               std::uint32_t label) {
    (void)plugin;
    for (const auto& child : parent.children) {
        const auto* group = std::get_if<fo4::Group>(&child);
        if (group == nullptr) continue;
        if (group->header.group_type != type) continue;
        if (group->header.label_as_u32() != label) continue;
        return group;
    }
    return nullptr;
}

nlohmann::json topicJson(const fo4::Plugin& plugin, const fo4::Record& topic) {
    nlohmann::json out{{"formKey", topic.form_key.to_string()},
                       {"editorId", support::editorId(topic)},
                       {"infos", nlohmann::json::array()}};
    if (const auto* name = firstOf(topic, fo4::Signature::from_string("FULL")); name != nullptr) {
        putText(out, "name", plugin, *name);
    }
    std::uint32_t label = 0;
    try {
        label = fo4::encode_plugin_form_id(fo4::FormKey{topic.form_key}, fo4::plugin_form_id_context(plugin)).value();
    } catch (const std::exception&) {
        return out;
    }
    for (const auto& top : plugin.groups) {
        for (const auto& child : top.children) {
            const auto* parent = std::get_if<fo4::Group>(&child);
            if (parent == nullptr) continue;
            const auto* infos = childGroupOf(plugin, *parent, static_cast<std::int32_t>(fo4::GroupType::topic_children), label);
            if (infos == nullptr) continue;
            for (const auto& infoChild : infos->children) {
                const auto* info = std::get_if<fo4::Record>(&infoChild);
                if (info == nullptr || info->header.signature != k_info) continue;
                if (auto resolved = infoOf(plugin, *info)) {
                    nlohmann::json entry{{"formKey", resolved->key.to_string()},
                                         {"responses", nlohmann::json::array()}};
                    for (const auto& [number, text] : resolved->responses) {
                        entry["responses"].push_back({{"number", number}, {"text", text}});
                    }
                    out["infos"].push_back(std::move(entry));
                }
            }
            return out;
        }
    }
    return out;
}

std::vector<fo4::FormKey> actionTopicKeys(const fo4::Plugin& plugin, const fo4::Record& record,
                                          const ActionSpan& span) {
    static constexpr std::array<const char*, 10> k_topicTags{
        "DATA", "PTOP", "NTOP", "NETO", "QTOP", "NPOT", "NNGT", "NNUT", "NQUT", "STSC"};
    std::vector<fo4::FormKey> keys;
    for (std::size_t at = span.open + 1; at < span.close; ++at) {
        if (!sized(record.subrecords[at], 4)) continue;
        bool wanted = false;
        for (const auto* tag : k_topicTags) {
            if (record.subrecords[at].signature == fo4::Signature::from_string(tag)) {
                wanted = true;
                break;
            }
        }
        if (!wanted) continue;
        const auto raw = rawLink(record.subrecords[at]);
        if (raw == 0 || raw == 0xFFFFFFFFu) continue;
        try {
            const auto key = fo4::resolve_plugin_link(fo4::RawFormId{raw}, fo4::plugin_form_id_context(plugin));
            keys.push_back(key);
        } catch (const std::exception&) {
        }
    }
    return keys;
}

nlohmann::json actionJson(const fo4::Plugin& plugin, const fo4::Record& record, std::size_t index,
                          const ActionSpan& span) {
    const auto& subs = record.subrecords;
    const auto type = readAt<std::uint16_t>(subs[span.open].data, 0);
    nlohmann::json action{{"index", index},
                          {"type", type},
                          {"typeName", std::string{fo4::env::scene_action_type_name(type)}},
                          {"payload", nlohmann::json::object()}};
    const auto& header = subs;
    for (std::size_t at = span.open + 1; at < span.header_end; ++at) {
        const auto tag = header[at].signature;
        if (tag == fo4::Signature::from_string("NAM0")) {
            putText(action, "name", plugin, header[at]);
        } else if (tag == fo4::Signature::from_string("ALID") && sized(header[at], 4)) {
            action["actorAlias"] = readAt<std::int32_t>(header[at].data, 0);
        } else if (tag == fo4::Signature::from_string("INAM") && sized(header[at], 4)) {
            action["actionIndex"] = readAt<std::uint32_t>(header[at].data, 0);
        } else if (tag == fo4::Signature::from_string("FNAM") && sized(header[at], 4)) {
            action["flags"] = readAt<std::uint32_t>(header[at].data, 0);
        } else if (tag == fo4::Signature::from_string("SNAM") && sized(header[at], 4)) {
            action["startPhase"] = readAt<std::uint32_t>(header[at].data, 0);
        } else if (tag == fo4::Signature::from_string("ENAM") && sized(header[at], 4)) {
            action["endPhase"] = readAt<std::uint32_t>(header[at].data, 0);
        }
    }
    auto& payload = action["payload"];
    auto putPayloadLink = [&](const char* key, const char* tag) {
        for (std::size_t at = span.header_end; at < span.close; ++at) {
            if (subs[at].signature != fo4::Signature::from_string(tag)) continue;
            if (!sized(subs[at], 4)) continue;
            payload[key] = linkJson(plugin, rawLink(subs[at]));
        }
    };
    // Constraint: a key is read only for the action types the scene writer accepts it on, so a shared tag never carries two labels.
    const bool dialogue = type == 0;
    const bool playerDialogue = type == 3;
    const bool npcResponse = type == 5;
    const bool radio = type == 6;
    if (dialogue || radio) {
        putPayloadLink("topic", "DATA");
        putPayloadLink("dialogueSubtype", "VENC");
    }
    if (dialogue) putPayloadLink("animArchType", "PNAM");
    if (dialogue || playerDialogue || npcResponse) putPayloadLink("audioOutputOverride", "ONAM");
    if (type == 4) putPayloadLink("startSceneTopic", "STSC");
    if (radio) putPayloadLink("playSound", "HTID");
    if (playerDialogue || npcResponse) {
        putPayloadLink("playerPositiveResponse", "PTOP");
        putPayloadLink("playerNegativeResponse", "NTOP");
        putPayloadLink("playerNeutralResponse", "NETO");
        putPayloadLink("playerQuestionResponse", "QTOP");
        putPayloadLink("playerPositiveDialogueSubtype", "VENC");
        putPayloadLink("playerNegativeDialogueSubtype", "PLVD");
        putPayloadLink("playerNeutralDialogueSubtype", "JOUT");
        putPayloadLink("playerQuestionDialogueSubtype", "DALC");
    }
    if (playerDialogue) {
        putPayloadLink("npcPositiveResponse", "NPOT");
        putPayloadLink("npcNegativeResponse", "NNGT");
        putPayloadLink("npcNeutralResponse", "NNUT");
        putPayloadLink("npcQuestionResponse", "NQUT");
        putPayloadLink("npcPositiveDialogueSubtype", "NPOS");
        putPayloadLink("npcNegativeDialogueSubtype", "NNGS");
        putPayloadLink("npcNeutralDialogueSubtype", "NNUS");
        putPayloadLink("npcQuestionDialogueSubtype", "NQUS");
    }
    for (std::size_t at = span.header_end; at < span.close; ++at) {
        const auto tag = subs[at].signature;
        if (tag == fo4::Signature::from_string("CRIS")) {
            putFloat(payload, "cameraFovOnPlayerCamera", subs[at], 0);
            putFloat(payload, "cameraRateOfChange", subs[at], 4);
        } else if (tag == fo4::Signature::from_string("DMAX")) {
            putFloat(payload, "loopingMax", subs[at], 0);
        } else if (tag == fo4::Signature::from_string("DMIN")) {
            putFloat(payload, "loopingMin", subs[at], 0);
        } else if (tag == fo4::Signature::from_string("DEMO") && sized(subs[at], 4)) {
            payload["emotionType"] = readAt<std::uint32_t>(subs[at].data, 0);
        } else if (tag == fo4::Signature::from_string("DEVA") && sized(subs[at], 4)) {
            payload["emotionValue"] = readAt<std::uint32_t>(subs[at].data, 0);
        } else if (tag == fo4::Signature::from_string("DTGT") && sized(subs[at], 4)) {
            payload["dialogueTargetActor"] = readAt<std::int32_t>(subs[at].data, 0);
        } else if (tag == fo4::Signature::from_string("SNAM") && sized(subs[at], 4)) {
            putFloat(payload, "timerMax", subs[at], 0);
        } else if (tag == fo4::Signature::from_string("TNAM") && sized(subs[at], 4)) {
            putFloat(payload, "timerMin", subs[at], 0);
        } else if (tag == fo4::Signature::from_string("SCQS") && sized(subs[at], 4)) {
            payload["questStageOnStart"] = readAt<std::int16_t>(subs[at].data, 0);
            payload["questStageOnCompletion"] = readAt<std::int16_t>(subs[at].data, 2);
        } else if (tag == fo4::Signature::from_string("HTID") && type == 0 && sized(subs[at], 4)) {
            payload["headtracking"].push_back(readAt<std::int32_t>(subs[at].data, 0));
        } else if (tag == fo4::Signature::from_string("DTID") && sized(subs[at], 4)) {
            payload["npcHeadtracking"].push_back(readAt<std::int32_t>(subs[at].data, 0));
        } else if (tag == fo4::Signature::from_string("LCEP") && sized(subs[at], 4)) {
            nlohmann::json scene{{"scene", linkJson(plugin, rawLink(subs[at]))}};
            if (at + 1 < span.close && subs[at + 1].signature == fo4::Signature::from_string("INTT") &&
                sized(subs[at + 1], 2)) {
                scene["phaseIndex"] = readAt<std::uint16_t>(subs[at + 1].data, 0);
            }
            payload["startScenes"].push_back(std::move(scene));
        }
    }
    for (const auto& key : actionTopicKeys(plugin, record, span)) {
        const auto* topic = fo4::find_record(plugin, key);
        if (topic == nullptr || topic->header.signature != k_dial) continue;
        action["topics"].push_back(topicJson(plugin, *topic));
    }
    return action;
}

nlohmann::json sceneJson(const fo4::Plugin& plugin, const fo4::Record& record) {
    const auto layout = scanLayout(record);
    if (!layout) {
        return nlohmann::json{{"formKey", record.form_key.to_string()},
                              {"editorId", support::editorId(record)},
                              {"structureError", "the scene groups do not match the stored layout"}};
    }
    nlohmann::json out{{"formKey", record.form_key.to_string()},
                       {"editorId", support::editorId(record)},
                       {"phases", nlohmann::json::array()},
                       {"actors", nlohmann::json::array()},
                       {"actions", nlohmann::json::array()}};
    if (const auto* flags = firstOfIn(record, 0, layout->first_group,
                                      fo4::Signature::from_string("FNAM"));
        flags != nullptr && sized(*flags, 4)) {
        out["flags"] = readAt<std::uint32_t>(flags->data, 0);
    }
    const auto sceneFragments = fragmentread::read_scene_fragments(record);
    const auto* fragments = sceneFragments ? &*sceneFragments : nullptr;
    for (std::size_t index = 0; index < layout->phases.size(); ++index) {
        out["phases"].push_back(phaseJson(plugin, record, index, layout->phases[index], fragments));
    }
    for (std::size_t index = 0; index < layout->actors.size(); ++index) {
        out["actors"].push_back(actorJson(record, layout->actors[index]));
    }
    for (std::size_t index = 0; index < layout->actions.size(); ++index) {
        out["actions"].push_back(actionJson(plugin, record, index, layout->actions[index]));
    }
    const auto tail = std::span{record.subrecords}.subspan(std::min(layout->tail, record.subrecords.size()));
    for (const auto& subrecord : tail) {
        if (subrecord.signature == fo4::Signature::from_string("PNAM") && sized(subrecord, 4)) {
            out["parentQuest"] = linkJson(plugin, rawLink(subrecord));
        } else if (subrecord.signature == fo4::Signature::from_string("TNAM") && sized(subrecord, 4)) {
            out["templateScene"] = linkJson(plugin, rawLink(subrecord));
        } else if (subrecord.signature == fo4::Signature::from_string("INAM") && sized(subrecord, 4)) {
            out["lastActionIndex"] = readAt<std::uint32_t>(subrecord.data, 0);
        } else if (subrecord.signature == fo4::Signature::from_string("XNAM") && sized(subrecord, 4)) {
            out["sceneIndex"] = readAt<std::uint32_t>(subrecord.data, 0);
        } else if (subrecord.signature == fo4::Signature::from_string("NNAM")) {
            putText(out, "notes", plugin, subrecord);
        } else if (subrecord.signature == fo4::Signature::from_string("CNAM") && sized(subrecord, 4)) {
            putFloat(out, "cameraDistanceOverride", subrecord, 0);
        } else if (subrecord.signature == fo4::Signature::from_string("ACTV") && sized(subrecord, 4)) {
            putFloat(out, "dialogueDistanceOverride", subrecord, 0);
        } else if (subrecord.signature == fo4::Signature::from_string("CRIS") && sized(subrecord, 4)) {
            putFloat(out, "fovOverride", subrecord, 0);
        }
    }
    const auto root = fo4::schema::read_root_conditions(record);
    if (root.ok) out["conditions"] = conditionsJson(plugin, root.conditions);
    return out;
}

}

Result<nlohmann::json> RadiclePluginBackend::getScene(const nlohmann::json& arguments) const {
    const auto pluginName = questStringArg(arguments, 0);
    const auto recordText = questStringArg(arguments, 1);
    if (pluginName.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    if (recordText.empty()) return std::unexpected(Error{"RecordMissing", "Record is empty"});

    OpLock lock(*this, OpLock::Mode::Read);
    const auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});

    auto scene = support::formKeyArgument(recordText);
    if (!scene) return std::unexpected(scene.error());
    const auto* record = fo4::find_record(loaded->plugin, *scene);
    if (record == nullptr) {
        return std::unexpected(Error{"RecordNotFound", "Record is not present in " + pluginName + ": " + recordText});
    }
    if (record->header.signature != k_scen) {
        return std::unexpected(Error{"RecordTypeMismatch", "GetScene requires a SCEN record"});
    }
    try {
        return sceneJson(loaded->plugin, *record);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"GetSceneFailed", error.what()});
    }
}

}
