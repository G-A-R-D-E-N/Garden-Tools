#include "RadiclePluginQuestInternal.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <fo4/plugin/reader.hpp>
#include <fo4/schema/formlink_lists.hpp>

namespace fo4ide::plugin::questinternal {

namespace {

bool fourByteMember(const fo4::Subrecord& member) {
    return member.data.size() == 4;
}

std::optional<Error> existingTypedFormIdUnrepresentable(
    const fo4::PluginFormIdContext& context,
    const fo4::ConflictGraph& graph,
    std::span<const std::byte> data,
    std::size_t offset,
    std::initializer_list<std::string_view> accepted,
    bool baseObjects,
    std::string_view slot,
    bool allowNull = false) {
    if (offset + 4 > data.size()) {
        return Error{
            "QuestAuthoringCoverageIncomplete",
            "existing QUST alias " + std::string(slot) + " cannot be reconstructed"
        };
    }
    std::uint32_t raw = 0;
    std::memcpy(&raw, data.data() + offset, 4);
    if (raw == 0) {
        if (allowNull) return std::nullopt;
        return Error{
            "QuestAuthoringCoverageIncomplete",
            "existing QUST alias " + std::string(slot) + " is null"
        };
    }
    fo4::FormKey key;
    try {
        key = fo4::resolve_plugin_form_id(fo4::RawFormId{raw}, context);
    } catch (const fo4::FormIdError&) {
        return Error{
            "QuestAuthoringCoverageIncomplete",
            "existing QUST alias " + std::string(slot) + " cannot be reconstructed"
        };
    }
    const auto* winner = graph.winner(key);
    if (winner == nullptr) {
        return Error{
            "QuestAuthoringCoverageIncomplete",
            "existing QUST alias " + std::string(slot) + " does not resolve to a loaded record"
        };
    }
    if (baseObjects) {
        if (!fo4::schema::is_sig_base_object(winner->signature)) {
            return Error{
                "QuestAuthoringCoverageIncomplete",
                "existing QUST alias " + std::string(slot) + " is a " +
                    winner->signature.to_string() + ", but this slot holds an item"
            };
        }
        return std::nullopt;
    }
    const auto signature = winner->signature.to_string();
    for (const auto name : accepted) {
        if (signature == name) return std::nullopt;
    }
    return Error{
        "QuestAuthoringCoverageIncomplete",
        "existing QUST alias " + std::string(slot) + " is a " + signature +
            ", which this adapter cannot reconstruct"
    };
}

}

std::optional<Error> extraAliasMembersUnrepresentable(
    const std::vector<fo4::Subrecord>& members,
    const fo4::Plugin& plugin,
    const fo4::ConflictGraph& graph) {
    enum class Phase {
        keywords,
        items,
        spor,
        ocor,
        gwor,
        ecor,
        linked,
        display,
        forcedVoice,
        deathItem,
        spells,
        factions,
        packages,
        voiceTypes,
        done
    };
    auto phase = Phase::keywords;
    std::size_t i = 0;
    const auto context = fo4::plugin_form_id_context(plugin);
    const auto fail = [](std::string_view message) {
        return Error{"QuestAuthoringCoverageIncomplete", std::string(message)};
    };
    const auto sig = [&](std::size_t index) {
        return members[index].signature.to_string();
    };
    const auto isExtraStart = [&](std::string_view name) {
        return name == "KSIZ" || name == "KWDA" || name == "COCT" || name == "CNTO" ||
               name == "COED" ||
               name == "SPOR" || name == "OCOR" || name == "GWOR" || name == "ECOR" ||
               name == "ALLA" || name == "ALDN" || name == "ALFV" || name == "ALDI" ||
               name == "ALSP" || name == "ALFC" || name == "ALPC" || name == "VTCK";
    };
    while (i < members.size() && sig(i) != "ALED" && !isExtraStart(sig(i))) {
        ++i;
    }
    while (i < members.size() && sig(i) != "ALED") {
        const auto name = sig(i);
        if (name == "KSIZ") {
            if (phase > Phase::keywords) return fail("existing QUST alias extra members are out of order");
            if (i + 1 >= members.size() || sig(i + 1) != "KWDA") {
                return fail("existing QUST alias KSIZ is missing KWDA");
            }
            if (!fourByteMember(members[i]) || members[i + 1].data.size() % 4 != 0) {
                return fail("existing QUST alias keywords cannot be reconstructed");
            }
            std::uint32_t count = 0;
            std::memcpy(&count, members[i].data.data(), 4);
            if (count != members[i + 1].data.size() / 4) {
                return fail("existing QUST alias KSIZ does not match KWDA");
            }
            for (std::uint32_t n = 0; n < count; ++n) {
                if (const auto error = existingTypedFormIdUnrepresentable(
                        context, graph, members[i + 1].data, n * 4u, {"KYWD"}, false,
                        "keywords")) {
                    return error;
                }
            }
            i += 2;
            phase = Phase::items;
            continue;
        }
        if (name == "KWDA") return fail("existing QUST alias KWDA is missing KSIZ");
        if (name == "COCT") {
            if (phase > Phase::items) return fail("existing QUST alias extra members are out of order");
            if (!fourByteMember(members[i])) {
                return fail("existing QUST alias items cannot be reconstructed");
            }
            std::uint32_t count = 0;
            std::memcpy(&count, members[i].data.data(), 4);
            ++i;
            for (std::uint32_t n = 0; n < count; ++n) {
                if (i >= members.size() || sig(i) != "CNTO" || members[i].data.size() != 8) {
                    return fail("existing QUST alias CNTO entries cannot be reconstructed");
                }
                std::int32_t itemCount = 0;
                std::memcpy(&itemCount, members[i].data.data() + 4, 4);
                if (itemCount < 1) {
                    return fail("existing QUST alias item count must be at least 1");
                }
                if (const auto error = existingTypedFormIdUnrepresentable(
                        context, graph, members[i].data, 0, {}, true, "items")) {
                    return error;
                }
                ++i;
                if (i < members.size() && sig(i) == "COED") {
                    if (members[i].data.size() != 12) {
                        return fail("existing QUST alias item extra cannot be reconstructed");
                    }
                    float itemCondition = 0.0f;
                    std::memcpy(&itemCondition, members[i].data.data() + 8, 4);
                    if (!std::isfinite(itemCondition)) {
                        return fail(
                            "existing QUST alias item extra itemCondition cannot be reconstructed");
                    }
                    std::uint32_t ownerRaw = 0;
                    std::memcpy(&ownerRaw, members[i].data.data(), 4);
                    if (ownerRaw == 0) {
                        std::uint32_t unused = 0;
                        std::memcpy(&unused, members[i].data.data() + 4, 4);
                        if (unused != 0) {
                            return fail(
                                "existing QUST alias item extra unused owner data cannot be reconstructed");
                        }
                    } else {
                        if (const auto error = existingTypedFormIdUnrepresentable(
                                context, graph, members[i].data, 0, {"NPC_", "FACT"}, false,
                                "item extra owner")) {
                            return error;
                        }
                        fo4::FormKey ownerKey;
                        try {
                            ownerKey = fo4::resolve_plugin_form_id(
                                fo4::RawFormId{ownerRaw}, context);
                        } catch (const fo4::FormIdError&) {
                            return fail("existing QUST alias item extra cannot be reconstructed");
                        }
                        const auto* ownerWinner = graph.winner(ownerKey);
                        if (ownerWinner == nullptr) {
                            return fail(
                                "existing QUST alias item extra owner does not resolve to a loaded record");
                        }
                        const auto ownerSig = ownerWinner->signature.to_string();
                        if (ownerSig == "NPC_") {
                            if (const auto error = existingTypedFormIdUnrepresentable(
                                    context, graph, members[i].data, 4, {"GLOB"}, false,
                                    "item extra global", true)) {
                                return error;
                            }
                        }
                    }
                    ++i;
                }
            }
            phase = Phase::spor;
            continue;
        }
        if (name == "CNTO") return fail("existing QUST alias CNTO is missing COCT");
        if (name == "COED") return fail("existing QUST alias COED is missing preceding CNTO");
        const auto uniqueLink = [&](Phase atMost,
                                    Phase next,
                                    std::initializer_list<std::string_view> accepted,
                                    std::string_view slot) -> std::optional<Error> {
            if (phase > atMost) {
                return fail("existing QUST alias extra members are out of order");
            }
            if (!fourByteMember(members[i])) {
                return fail("existing QUST alias " + std::string(slot) +
                            " cannot be reconstructed");
            }
            if (const auto error = existingTypedFormIdUnrepresentable(
                    context, graph, members[i].data, 0, accepted, false, slot)) {
                return error;
            }
            ++i;
            phase = next;
            return std::nullopt;
        };
        if (name == "SPOR") {
            if (const auto error = uniqueLink(
                    Phase::spor, Phase::ocor, {"FLST"}, "spectatorOverridePackageList")) {
                return error;
            }
            continue;
        }
        if (name == "OCOR") {
            if (const auto error = uniqueLink(
                    Phase::ocor, Phase::gwor, {"FLST"},
                    "observeDeadBodyOverridePackageList")) {
                return error;
            }
            continue;
        }
        if (name == "GWOR") {
            if (const auto error = uniqueLink(
                    Phase::gwor, Phase::ecor, {"FLST"}, "guardWarnOverridePackageList")) {
                return error;
            }
            continue;
        }
        if (name == "ECOR") {
            if (const auto error = uniqueLink(
                    Phase::ecor, Phase::linked, {"FLST"}, "combatOverridePackageList")) {
                return error;
            }
            continue;
        }
        if (name == "ALDN") {
            if (const auto error =
                    uniqueLink(Phase::display, Phase::forcedVoice, {"MESG"}, "displayName")) {
                return error;
            }
            continue;
        }
        if (name == "ALFV") {
            if (const auto error =
                    uniqueLink(Phase::forcedVoice, Phase::deathItem, {"VTYP"}, "forcedVoice")) {
                return error;
            }
            continue;
        }
        if (name == "ALDI") {
            if (const auto error =
                    uniqueLink(Phase::deathItem, Phase::spells, {"LVLI"}, "deathItem")) {
                return error;
            }
            continue;
        }
        if (name == "VTCK") {
            if (const auto error = uniqueLink(
                    Phase::voiceTypes, Phase::done, {"VTYP", "FLST", "NPC_", "FACT"},
                    "voiceTypes")) {
                return error;
            }
            continue;
        }
        if (name == "ALLA") {
            if (phase > Phase::linked) return fail("existing QUST alias extra members are out of order");
            if (members[i].data.empty() || members[i].data.size() % 8 != 0) {
                return fail("existing QUST alias linked aliases cannot be reconstructed");
            }
            for (std::size_t offset = 0; offset < members[i].data.size(); offset += 8) {
                if (const auto error = existingTypedFormIdUnrepresentable(
                        context, graph, members[i].data, offset, {"KYWD"}, false,
                        "linkedAliases")) {
                    return error;
                }
            }
            ++i;
            phase = Phase::display;
            continue;
        }
        if (name == "ALSP") {
            if (phase > Phase::spells) return fail("existing QUST alias extra members are out of order");
            if (!fourByteMember(members[i])) {
                return fail("existing QUST alias spells cannot be reconstructed");
            }
            if (const auto error = existingTypedFormIdUnrepresentable(
                    context, graph, members[i].data, 0, {"SPEL"}, false, "spells")) {
                return error;
            }
            ++i;
            phase = Phase::spells;
            continue;
        }
        if (name == "ALFC") {
            if (phase > Phase::factions) return fail("existing QUST alias extra members are out of order");
            if (!fourByteMember(members[i])) {
                return fail("existing QUST alias factions cannot be reconstructed");
            }
            if (const auto error = existingTypedFormIdUnrepresentable(
                    context, graph, members[i].data, 0, {"FACT"}, false, "factions")) {
                return error;
            }
            ++i;
            phase = Phase::factions;
            continue;
        }
        if (name == "ALPC") {
            if (phase > Phase::packages) return fail("existing QUST alias extra members are out of order");
            if (!fourByteMember(members[i])) {
                return fail("existing QUST alias packages cannot be reconstructed");
            }
            if (const auto error = existingTypedFormIdUnrepresentable(
                    context, graph, members[i].data, 0, {"PACK"}, false, "packages")) {
                return error;
            }
            ++i;
            phase = Phase::voiceTypes;
            continue;
        }
        return fail("existing QUST alias has unsupported member " + name);
    }
    return std::nullopt;
}

}
