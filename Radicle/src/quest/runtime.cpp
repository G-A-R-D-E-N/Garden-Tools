#include "fo4/quest/runtime.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/signature.hpp"

#include <cstring>
#include <exception>
#include <span>
#include <string>
#include <vector>

namespace fo4::quest {
namespace {

constexpr Signature k_dnam = Signature::from_string("DNAM");

const Subrecord* single_subrecord(const Record& record,
                                  Signature signature,
                                  const char* label,
                                  std::vector<std::string>& unsupported) {
  const Subrecord* found = nullptr;
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature != signature) continue;
    if (found != nullptr) {
      unsupported.push_back(std::string("QUST has more than one ") + label +
                            " subrecord; the first one was used");
      break;
    }
    found = &subrecord;
  }
  return found;
}

bool read_start_game_enabled(const Record& record, std::vector<std::string>& unsupported) {
  const auto* dnam = single_subrecord(record, k_dnam, "DNAM", unsupported);
  if (dnam == nullptr) return false;
  if (dnam->data.size() != 12) {
    unsupported.push_back("QUST DNAM is not the measured 12 bytes, so the start-game flag is unknown");
    return false;
  }
  std::uint16_t flags = 0;
  std::memcpy(&flags, dnam->data.data(), sizeof(flags));
  return (flags & 0x0001u) != 0u;
}

QuestRuntimeData quest_runtime_data_record(const FormKey& quest, const Record* record) {
  QuestRuntimeData data;
  data.quest = quest;

  if (record == nullptr) {
    data.unsupported.push_back("QUST record " + quest.to_string() + " was not found");
    return data;
  }
  if (record->header.signature != sig_qust) {
    data.unsupported.push_back(quest.to_string() + " is not a QUST record");
    return data;
  }

  data.start_game_enabled = read_start_game_enabled(*record, data.unsupported);

  const auto* vmad = single_subrecord(*record, sig_vmad, "VMAD", data.unsupported);
  if (vmad == nullptr) return data;

  try {
    const VmadScripts scripts = read_vmad_scripts(vmad->data);
    data.scripts = scripts.scripts;
    if (scripts.unsupported) {
      data.unsupported.push_back("a VMAD property is unsupported, so the script list stops there");
    }
    if (scripts.trailing_bytes == 0) return data;

    const auto offset = vmad->data.size() - scripts.trailing_bytes;
    const QuestVmadExtras extras = read_quest_vmad_extras(
        std::span<const std::byte>(vmad->data).subspan(offset), scripts.object_format);
    data.fragment_script = extras.script_name;
    data.fragment_properties = extras.properties;
    data.fragments = extras.fragments;
    data.alias_scripts = extras.aliases;
    if (extras.unsupported) {
      data.unsupported.push_back(extras.unsupported_reason);
    } else if (!extras.fully_consumed) {
      data.unsupported.push_back("the quest VMAD extras were not fully consumed");
    }
  } catch (const std::exception& error) {
    data.unsupported.push_back(std::string("VMAD decode failed: ") + error.what());
  }
  return data;
}

}  // namespace

QuestRuntimeData quest_runtime_data(const Plugin& plugin, const FormKey& quest) {
  return quest_runtime_data_record(quest, find_record(plugin, quest));
}

QuestRuntimeData quest_runtime_data(const RecordIndex& index, const FormKey& quest) {
  return quest_runtime_data_record(quest, index.find(quest));
}

}
