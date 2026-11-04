#include <fo4/papyrus/decompiler.hpp>
#include <fo4/papyrus/script_names.hpp>

#include "decompiler_internal.hpp"

#include <cctype>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fo4::papyrus {

using fo4::pex::PexFile;
using fo4::pex::PexFunction;
using fo4::pex::PexObject;
using fo4::pex::PexProperty;
using fo4::pex::PexStruct;
using fo4::pex::PexStructMember;
using fo4::pex::PexValue;
using decompiler_detail::BodyState;
using decompiler_detail::iequals;
using decompiler_detail::join;
using decompiler_detail::reconstructStatements;
using decompiler_detail::startsWith;
using decompiler_detail::translateInstructions;

namespace {

std::string cap(std::string s) {
    if (s.empty()) return s;
    s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    return s;
}

constexpr std::string_view kEventNames[] = {
    "OnAction", "OnActivate", "OnAliasInit", "OnAliasReset", "OnAliasShutdown", "OnAnimationEvent",
    "OnAnimationEventUnregistered", "OnBegin", "OnBeginState", "OnCellAttach", "OnCellDetach",
    "OnCellLoad", "OnChange", "OnClose", "OnCombatStateChanged", "OnCommandModeCompleteCommand",
    "OnCommandModeEnter", "OnCommandModeExit", "OnCommandModeGiveCommand", "OnCompanionDismiss",
    "OnConsciousnessStateChanged", "OnContainerChanged", "OnCripple", "OnDeath", "OnDeferredKill",
    "OnDestructionStageChanged", "OnDifficultyChanged", "OnDistanceGreaterThan",
    "OnDistanceLessThan", "OnDying", "OnEffectFinish", "OnEffectStart", "OnEnd", "OnEndState",
    "OnEnterBleedout", "OnEnterSneaking", "OnEntryRun", "OnEquipped", "OnEscortWaitStart",
    "OnEscortWaitStop", "OnExitFurniture", "OnGainLOS", "OnGetUp", "OnGrab", "OnHit",
    "OnHolotapeChatter", "OnHolotapePlay", "OnInit", "OnItemAdded", "OnItemEquipped",
    "OnItemRemoved", "OnItemUnequipped", "OnKill", "OnLoad", "OnLocationChange",
    "OnLocationCleared", "OnLocationLoaded", "OnLockStateChanged", "OnLooksMenuEvent", "OnLostLOS",
    "OnMagicEffectApply", "OnMenuItemRun", "OnMenuOpenCloseEvent", "OnOpen", "OnPackageChange",
    "OnPackageEnd", "OnPackageStart", "OnPartialCripple", "OnPhaseBegin", "OnPhaseEnd",
    "OnPickpocketFailed", "OnPipboyRadioDetection", "OnPlayerCreateRobot", "OnPlayerDialogueTarget",
    "OnPlayerEnterVertibird", "OnPlayerFallLongDistance", "OnPlayerFireWeapon",
    "OnPlayerHealTeammate", "OnPlayerLoadGame", "OnPlayerModArmorWeapon", "OnPlayerModRobot",
    "OnPlayerSleepStart", "OnPlayerSleepStop", "OnPlayerSwimming", "OnPlayerTeleport",
    "OnPlayerUseWorkBench", "OnPlayerWaitStart", "OnPlayerWaitStop", "OnPowerOff", "OnPowerOn",
    "OnQuestInit", "OnQuestShutdown", "OnRaceSwitchComplete", "OnRadiationDamage", "OnRead",
    "OnRelease", "OnReset", "OnSell", "OnSit", "OnSpeechChallengeAvailable", "OnSpellCast",
    "OnStageSet", "OnStart", "OnStoryActivateActor", "OnStoryActorAttach", "OnStoryAddToPlayer",
    "OnStoryArrest", "OnStoryAssaultActor", "OnStoryAttractionObject", "OnStoryBribeNPC",
    "OnStoryCastMagic", "OnStoryChangeLocation", "OnStoryClearLocation", "OnStoryCraftItem",
    "OnStoryCrimeGold", "OnStoryCure", "OnStoryDialogue", "OnStoryDiscoverDeadBody",
    "OnStoryEscapeJail", "OnStoryFlatterNPC", "OnStoryHackTerminal", "OnStoryHello",
    "OnStoryIncreaseLevel", "OnStoryInfection", "OnStoryIntimidateNPC", "OnStoryIronSights",
    "OnStoryJail", "OnStoryKillActor", "OnStoryLocationLoaded", "OnStoryMineExplosion",
    "OnStoryNewVoicePower", "OnStoryPayFine", "OnStoryPickLock", "OnStoryPickPocket",
    "OnStoryPlayerGetsFavor", "OnStoryRelationshipChange", "OnStoryRemoveFromPlayer",
    "OnStoryScript", "OnStoryServedTime", "OnStoryTrespass", "OnTimer", "OnTimerGameTime",
    "OnTrackedStatsEvent", "OnTranslationAlmostComplete", "OnTranslationComplete",
    "OnTranslationFailed", "OnTrapHitStart", "OnTrapHitStop", "OnTriggerEnter", "OnTriggerLeave",
    "OnTutorialEvent", "OnUnequipped", "OnUnload", "OnWorkshopMode", "OnWorkshopNPCTransfer",
    "OnWorkshopObjectDestroyed", "OnWorkshopObjectGrabbed", "OnWorkshopObjectMoved",
    "OnWorkshopObjectPlaced", "OnWorkshopObjectRepaired",
};

bool isKnownEventName(std::string_view name) {
    for (const auto event : kEventNames) {
        if (iequals(name, event)) return true;
    }
    return false;
}

std::vector<const PexStructMember*> orderedMembers(const PexFile& pex, const std::string& objectName,
                                                   const PexStruct& st) {
    std::vector<const PexStructMember*> out;
    std::vector<bool> used(st.members.size(), false);
    for (const auto& order : pex.structOrders) {
        if (!iequals(order.objectName, objectName) || !iequals(order.orderName, st.name)) continue;
        for (const auto& name : order.memberNames) {
            for (std::size_t i = 0; i < st.members.size(); ++i) {
                if (used[i] || !iequals(st.members[i].name, name)) continue;
                used[i] = true;
                out.push_back(&st.members[i]);
            }
        }
        break;
    }
    for (std::size_t i = 0; i < st.members.size(); ++i) {
        if (!used[i]) out.push_back(&st.members[i]);
    }
    return out;
}

}  // namespace

std::string Decompiler::toSource(const PexFile& pex) {
    std::ostringstream sb;

    std::unordered_map<int, std::string> flagNames;
    for (const auto& uf : pex.userFlags) flagNames[uf.index] = uf.name;

    const auto flags = [&](std::uint32_t mask, const std::vector<std::string>& omit) {
        std::vector<std::string> names;
        for (int b = 0; b < 32; ++b) {
            if ((mask & (1u << b)) == 0) continue;
            const auto found = flagNames.find(b);
            if (found == flagNames.end()) continue;
            bool omitted = false;
            for (const auto& o : omit) {
                if (iequals(found->second, o)) {
                    omitted = true;
                    break;
                }
            }
            if (!omitted) names.push_back(cap(found->second));
        }
        if (names.empty()) return std::string();
        return " " + join(names, " ");
    };

    for (const auto& obj : pex.objects) {
        if (!safeScriptObjectName(obj.name)) {
            throw std::runtime_error(
                "PEX object name contains an unsafe filesystem component: " + obj.name);
        }
        sb << "ScriptName " << obj.name;
        if (!obj.parentClassName.empty()) sb << " extends " << obj.parentClassName;
        if (obj.isConst) sb << " Const";
        sb << flags(obj.userFlags, {});
        sb << "\n";
        if (!obj.docString.empty()) sb << "{ " << obj.docString << " }\n";
        sb << "\n";

        for (const auto& st : obj.structs) {
            sb << "Struct " << st.name << "\n";
            for (const auto* mp : orderedMembers(pex, obj.name, st)) {
                const auto& m = *mp;
                sb << "\t" << m.type << " " << m.name;
                if (m.defaultValue && !m.defaultValue->isNone()) sb << " = " << m.defaultValue->toString();
                if (m.isConst) sb << " Const";
                sb << flags(m.userFlags, {});
                sb << "\n";
                if (!m.docString.empty()) sb << "\t{ " << m.docString << " }\n";
            }
            sb << "EndStruct\n";
            sb << "\n";
        }

        const auto emitProperty = [&](const PexProperty& p) {
            if (p.isAutoVar()) {
                std::optional<PexValue> backingDefault;
                bool backingConst = false;
                for (const auto& v : obj.variables) {
                    if (iequals(v.name, p.autoVarName)) {
                        backingDefault = v.defaultValue;
                        backingConst = v.isConst;
                        break;
                    }
                }
                sb << p.type << " Property " << p.name;
                if (backingDefault && !backingDefault->isNone()) sb << " = " << backingDefault->toString();
                sb << (p.canWrite() ? " Auto" : " AutoReadOnly");
                if (backingConst) sb << " Const";
                sb << flags(p.userFlags, {});
                sb << "\n";
                if (!p.docString.empty()) sb << "{ " << p.docString << " }\n";
            } else {
                sb << p.type << " Property " << p.name << flags(p.userFlags, {}) << "\n";
                if (!p.docString.empty()) sb << "{ " << p.docString << " }\n";
                if (p.readHandler) {
                    emitFunction(sb, obj, *p.readHandler, flagNames, "\t",
                                 p.type + " Function Get");
                }
                if (p.writeHandler) {
                    emitFunction(sb, obj, *p.writeHandler, flagNames, "\t", "Function Set");
                }
                sb << "EndProperty\n";
            }
            sb << "\n";
        };

        std::vector<bool> emitted(obj.properties.size(), false);
        const auto emitNamed = [&](const std::string& name) {
            for (std::size_t i = 0; i < obj.properties.size(); ++i) {
                if (emitted[i] || !iequals(obj.properties[i].name, name)) continue;
                emitted[i] = true;
                emitProperty(obj.properties[i]);
            }
        };
        for (const auto& group : pex.propertyGroups) {
            if (!iequals(group.objectName, obj.name)) continue;
            const bool named = !group.groupName.empty();
            if (named) {
                sb << "Group " << group.groupName << flags(group.userFlags, {}) << "\n";
                if (!group.docString.empty()) sb << "{ " << group.docString << " }\n";
            }
            for (const auto& name : group.propertyNames) emitNamed(name);
            if (named) sb << "EndGroup\n\n";
        }
        for (std::size_t i = 0; i < obj.properties.size(); ++i) {
            if (!emitted[i]) emitProperty(obj.properties[i]);
        }

        bool anyVar = false;
        for (const auto& v : obj.variables) {
            if (startsWith(v.name, "::")) continue;
            sb << v.type << " " << v.name;
            if (v.defaultValue && !v.defaultValue->isNone()) sb << " = " << v.defaultValue->toString();
            if (v.isConst) sb << " Const";
            sb << flags(v.userFlags, {});
            sb << "\n";
            anyVar = true;
        }
        if (anyVar) sb << "\n";

        for (const auto& state : obj.states) {
            const bool isDefault = state.name.empty();
            if (!isDefault) {
                const bool isAuto = iequals(state.name, obj.autoStateName);
                sb << (isAuto ? "Auto " : "") << "State " << state.name << "\n";
                sb << "\n";
            }
            for (const auto& fn : state.functions) {
                emitFunction(sb, obj, fn, flagNames, isDefault ? "" : "\t");
                sb << "\n";
            }
            if (!isDefault) sb << "EndState\n" << "\n";
        }
    }
    std::string result = sb.str();
    while (!result.empty() && (result.back() == '\n' || result.back() == ' ')) result.pop_back();
    result += "\n";
    return result;
}

void Decompiler::emitFunction(std::ostringstream& sb, const PexObject& obj,
                              const PexFunction& fn,
                              const std::unordered_map<int, std::string>& flagNames,
                              std::string_view indent,
                              std::optional<std::string> nameOverride) {
    const auto flagsStr = [&](std::uint32_t mask) {
        std::vector<std::string> names;
        for (int b = 0; b < 32; ++b) {
            if ((mask & (1u << b)) == 0) continue;
            const auto found = flagNames.find(b);
            if (found == flagNames.end()) continue;
            names.push_back(cap(found->second));
        }
        if (names.empty()) return std::string();
        return " " + join(names, " ");
    };

    const bool isRemote = !nameOverride && startsWith(fn.name, "::remote_");
    const bool isEvent = !nameOverride && !fn.isGlobal && iequals(fn.returnType, "None") &&
                         (isRemote || isKnownEventName(fn.name));

    std::vector<std::string> paramStrs;
    for (const auto& p : fn.params) paramStrs.push_back(p.type + " " + p.name);
    const auto ps = join(paramStrs, ", ");

    std::string header;
    if (nameOverride) {
        header = *nameOverride + "(" + ps + ")";
    } else if (isRemote) {
        auto rest = fn.name.substr(std::string("::remote_").size());
        std::string type = fn.params.empty() ? "" : fn.params[0].type;
        std::string ev = rest;
        if (!type.empty() && startsWith(rest, type + "_")) {
            ev = rest.substr(type.size() + 1);
        } else {
            const auto idx = rest.find("_On");
            if (idx != std::string::npos && idx > 0) {
                type = rest.substr(0, idx);
                ev = rest.substr(idx + 1);
            } else {
                ev = rest;
            }
        }
        header = type.empty() ? "Event " + ev + "(" + ps + ")"
                              : "Event " + type + "." + ev + "(" + ps + ")";
    } else if (isEvent) {
        header = "Event " + fn.name + "(" + ps + ")";
    } else {
        const std::string ret = iequals(fn.returnType, "None") ? "" : fn.returnType + " ";
        header = ret + "Function " + fn.name + "(" + ps + ")";
    }
    if (fn.isGlobal) header += " Global";
    if (fn.isNative) header += " Native";
    header += flagsStr(fn.userFlags);
    sb << indent << header << "\n";
    if (!fn.docString.empty()) sb << indent << "{ " << fn.docString << " }\n";

    if (fn.isNative) return;

    std::string body;
    try {
        body = decompileBody(obj, fn, std::string(indent) + "\t", true);
    } catch (const std::exception& ex) {
        body = std::string(indent) + "\t; [decompile failed: " + ex.what() +
               "; assembly follows]\n" + asmBody(fn, std::string(indent) + "\t");
    }
    sb << body;

    const std::string end = (!nameOverride && isEvent) ? "EndEvent" : "EndFunction";
    sb << indent << end << "\n";
}

std::string Decompiler::decompileBody(const PexObject& obj, const PexFunction& fn,
                                      std::string indent, bool inlineTemps) {
    BodyState s;
    s.obj = &obj;
    s.fn = &fn;
    s.indent = std::move(indent);
    s.inlineTemps = inlineTemps;

    for (const auto& p : fn.params) s.types[p.name] = p.type;
    for (const auto& l : fn.locals) s.types[l.name] = l.type;
    for (const auto& v : obj.variables) s.types[v.name] = v.type;
    for (const auto& pr : obj.properties) s.types[pr.name] = pr.type;

    for (const auto& pr : obj.properties) {
        if (pr.isAutoVar() && !pr.autoVarName.empty()) s.backing[pr.autoVarName] = pr.name;
    }

    s.stmt.resize(fn.instructions.size());
    s.jumps.resize(fn.instructions.size());
    translateInstructions(s);
    std::string body = reconstructStatements(s);
    if (!s.leftTemp.empty()) {
        body = s.indent + "; [decompile warning: compiler temporary " + s.leftTemp +
               " is read before it is assigned; decompiled as " + BodyState::san(s.leftTemp) +
               "]\n" + body;
    }
    return body;
}

std::string Decompiler::disassemble(const PexFile& pex) {
    std::ostringstream sb;
    sb << "; Disassembly of " << pex.sourceFileName << "\n";
    sb << "; compiled by " << pex.userName << "@" << pex.computerName << ", FO4 PEX v"
       << pex.majorVersion << "." << pex.minorVersion << "\n";
    sb << "\n";
    for (const auto& obj : pex.objects) {
        sb << ".object " << obj.name << " extends " << obj.parentClassName << "\n";
        for (const auto& st : obj.states) {
            sb << "  .state \"" << st.name << "\"\n";
            for (const auto& fn : st.functions) dumpFn(sb, fn, "    ");
        }
        for (const auto& p : obj.properties) {
            if (p.readHandler) {
                sb << "  .propertyGet " << p.name << "\n";
                dumpFn(sb, *p.readHandler, "    ");
            }
            if (p.writeHandler) {
                sb << "  .propertySet " << p.name << "\n";
                dumpFn(sb, *p.writeHandler, "    ");
            }
        }
        sb << ".endObject\n";
    }
    return sb.str();
}

void Decompiler::dumpFn(std::ostringstream& sb, const PexFunction& fn, std::string_view indent) {
    sb << indent << ".function " << fn.name << " -> " << fn.returnType
       << (fn.isGlobal ? " global" : "") << (fn.isNative ? " native" : "") << "\n";
    if (!fn.params.empty()) {
        std::vector<std::string> parts;
        for (const auto& p : fn.params) parts.push_back(p.type + " " + p.name);
        sb << indent << "  .params " << join(parts, ", ") << "\n";
    }
    if (!fn.locals.empty()) {
        std::vector<std::string> parts;
        for (const auto& l : fn.locals) parts.push_back(l.type + " " + l.name);
        sb << indent << "  .locals " << join(parts, ", ") << "\n";
    }
    sb << asmBody(fn, std::string(indent) + "  ");
    sb << indent << ".endFunction\n";
}

std::string Decompiler::asmBody(const PexFunction& fn, std::string_view indent) {
    std::ostringstream sb;
    for (std::size_t i = 0; i < fn.instructions.size(); ++i) {
        const auto& ins = fn.instructions[i];
        std::vector<std::string> parts;
        for (const auto& a : ins.args) parts.push_back(a.toString());
        const std::string argText = join(parts, " ");
        const std::string line = ins.line > 0 ? "L" + std::to_string(ins.line) : "";
        std::string lineText = std::string(indent) + "[" + std::to_string(i) + "] " + line + " " +
                               ins.mnemonic + " " + argText;
        while (!lineText.empty() && lineText.back() == ' ') lineText.pop_back();
        sb << lineText << "\n";
    }
    return sb.str();
}

}  // namespace fo4::papyrus
