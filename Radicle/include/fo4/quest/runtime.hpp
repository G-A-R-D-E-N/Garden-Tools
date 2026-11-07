#pragma once

#include "fo4/formid.hpp"
#include "fo4/plugin/container.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/schema/vmad.hpp"

#include <string>
#include <vector>

namespace fo4::quest {

/*
 * The VMAD-derived runtime data Garden needs to start and drive one quest.
 *
 * Every field comes from the QUST record's VMAD and DNAM subrecords. Nothing is
 * inferred from a plugin name or a load order, and anything that cannot be decoded
 * is appended to `unsupported` instead of being guessed, so the caller can count it.
 */
struct QuestRuntimeData {
  FormKey quest;
  bool start_game_enabled{false};
  /* Scripts attached to the record, in file order, with their typed properties. */
  std::vector<VmadScript> scripts;
  /* The script the stage fragments belong to, and its typed properties. */
  std::string fragment_script;
  std::vector<VmadProperty> fragment_properties;
  /* Stage fragments in file order: stage, stage index, script and function names. */
  std::vector<QuestVmadFragment> fragments;
  /* Per-alias script attachments. */
  std::vector<QuestVmadAliasScripts> alias_scripts;
  /* Explicit, human-readable reasons data was left undecoded. */
  std::vector<std::string> unsupported;
};

QuestRuntimeData quest_runtime_data(const Plugin& plugin, const FormKey& quest);
QuestRuntimeData quest_runtime_data(const RecordIndex& index, const FormKey& quest);

}
