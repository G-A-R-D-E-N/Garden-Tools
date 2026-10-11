#pragma once

#include "quest_plugin_fixture.hpp"
#include "fo4/forms/quest.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"

#include <functional>
#include <string_view>

namespace fo4::test {

inline Subrecord quest_member(std::string_view signature, std::vector<std::byte> bytes = {}) {
  return {Signature::from_string(signature), std::move(bytes), false};
}

inline Subrecord quest_u32(std::string_view signature, std::uint32_t value) {
  ByteWriter out;
  out.u32(value);
  return quest_member(signature, out.bytes);
}

inline Subrecord quest_string(std::string_view signature, std::string_view text) {
  ByteWriter out;
  out.raw(text);
  out.u8(0);
  return quest_member(signature, out.bytes);
}

inline Subrecord quest_condition(std::uint16_t function) {
  CtdaCondition condition;
  condition.function = function;
  condition.flags_byte = 0x20;
  condition.comparison_raw = 0x3f800000;
  condition.param1 = -3;
  condition.param2 = 5;
  condition.run_on = 2;
  condition.reference = 0x800;
  return quest_member("CTDA", serialize_ctda_condition(condition));
}

inline void change_quest(Plugin& plugin, const std::function<void(Record&)>& change) {
  visit_records(plugin, [&](Record& record) {
    if (record.header.signature != Signature::from_string("QUST")) return;
    change(record);
    record.original_range.clear();
    record.decompressed_payload.clear();
    record.header.data_size = 0;
    record.uncompressed_size = 0;
  });
  plugin.original_range.clear();
}

inline Plugin quest_structure_plugin(bool localized = false) {
  auto plugin = read_plugin(make_quest_plugin(1), "Quests.esp");
  plugin.header.tes4_flags = RecordFlags{localized ? 0x80u : 0u};
  plugin.tes4.header.flags = plugin.header.tes4_flags;
  plugin.tes4.original_range.clear();
  change_quest(plugin, [&](Record& record) {
    ByteWriter stage;
    stage.u16(10);
    stage.u8(2);
    stage.u8(7);
    ByteWriter empty_stage;
    empty_stage.u16(20);
    empty_stage.u8(0);
    empty_stage.u8(0);
    ByteWriter objective;
    objective.u16(30);
    ByteWriter target;
    target.i32(-1);
    target.u32(6);
    target.u32(0x800);
    record.subrecords = {
        quest_string("EDID", "TestQuest"), quest_condition(70), quest_member("NEXT"),
        quest_condition(277), quest_member("INDX", stage.bytes),
        quest_member("QSDT", {std::byte{1}}), quest_condition(72),
        quest_string("CIS1", ""), quest_string("CIS2", "Garden\tPatch"), quest_string("NAM2", ""),
        localized ? quest_u32("CNAM", 0x1234) : quest_string("CNAM", "Log"),
        quest_member("INDX", empty_stage.bytes), quest_member("QOBJ", objective.bytes),
        quest_u32("FNAM", 2), localized ? quest_u32("NNAM", 0x2345) : quest_string("NNAM", ""),
        quest_member("QSTA", target.bytes), quest_condition(73), quest_u32("ANAM", 5),
        quest_u32("ALST", 1), quest_string("ALID", "ref"), quest_u32("FNAM", 7),
        quest_u32("ALFI", 0xfffffffd), quest_u32("ALFR", 0x800), quest_condition(74), quest_member("ALED"),
        quest_u32("ALLS", 2), quest_string("ALID", "loc"), quest_u32("FNAM", 9),
        quest_u32("ALFA", 1), quest_u32("KNAM", 0x800), quest_condition(75),
        quest_u32("ALCC", 0xfffffffe), quest_member("ALED"), quest_u32("ALCS", 3),
        quest_member("ALMI", {std::byte{2}}), quest_u32("ALCS", 4), quest_string("NNAM", "tail")};
  });
  return plugin;
}

inline FormKey structure_quest_key() { return FormKey::parse("000800:Quests.esp"); }

}
