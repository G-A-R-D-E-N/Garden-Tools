#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/field_address.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;

struct Sub {
  std::string_view signature;
  Bytes data;
};

enum class EditKind { scalar, text, link };

struct Edit {
  EditKind kind;
  std::string_view record;
  std::string_view form;
  std::string_view path;
  std::string_view value;
  std::string_view changed;
};

struct Entry {
  std::string_view signature;
  std::string_view form;
};

constexpr Entry kRecords[] = {{"AECH", "000807"}, {"ASPC", "000800"}, {"REVB", "000801"}, {"SNCT", "000802"},
                              {"SOPM", "000803"}, {"SCSN", "000804"}, {"STAG", "000805"}, {"MUST", "000806"}};

constexpr Edit kEdits[] = {
    {EditKind::scalar, "AECH", "000807", "AECH\\EffectType", "411269967", "KNAM"},
    {EditKind::link, "ASPC", "000800", "ASPC\\EnvironmentType", "000801", "BNAM"},
    {EditKind::scalar, "REVB", "000801", "REVB\\DecayTime", "2000", "DATA"},
    {EditKind::link, "SNCT", "000802", "SNCT\\ParentCategory", "000802", "PNAM"},
    {EditKind::scalar, "SOPM", "000803", "SOPM\\StaticAttenuation", "-200", "VNAM"},
    {EditKind::scalar, "SCSN", "000804", "SCSN\\Priority", "7", "PNAM"},
    {EditKind::text, "MUST", "000806", "MUST\\TrackFileName", "new.wav", "ANAM"},
};

Bytes le(std::uint32_t value, std::size_t width) {
  Bytes out;
  for (std::size_t i = 0; i < width; ++i) {
    out.push_back(std::byte{static_cast<std::uint8_t>(value >> (8 * i))});
  }
  return out;
}

Bytes f32(float value) {
  std::uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof bits);
  return le(bits, 4);
}

Bytes cat(std::initializer_list<Bytes> parts) {
  Bytes out;
  for (const auto& part : parts) out.insert(out.end(), part.begin(), part.end());
  return out;
}

Bytes zstr(std::string_view text) {
  Bytes out;
  for (const char ch : text) out.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  out.push_back(std::byte{0});
  return out;
}

constexpr std::uint32_t kOverdrive = 2252866750u;
constexpr std::uint32_t kStateVariableFilter = 4015480703u;
constexpr std::uint32_t kDelayEffect = 411269967u;

Bytes overdrive_dnam() {
  return cat({le(1, 4), f32(1.0f), f32(2.0f), f32(3.0f), f32(4.0f)});
}

Bytes filter_dnam() {
  return cat({le(0, 4), f32(90.0f), f32(1.5f), le(1, 4)});
}

Bytes delay_dnam() {
  return cat({le(1, 4), f32(0.5f), f32(0.25f), le(250, 4)});
}

fo4::FormKey key(std::string_view local) {
  return fo4::FormKey::parse(std::string(local) + ":Patch.esp");
}

void add_record(fo4::test::ByteWriter& out, std::string_view signature, std::uint32_t form_id,
                std::initializer_list<Sub> subs) {
  const auto group = fo4::test::begin_group(out, signature, 0);
  const auto record = fo4::test::begin_record(out, signature, 0, form_id);
  for (const auto& sub : subs) fo4::test::write_subrecord(out, sub.signature, sub.data);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
}

Bytes fixture() {
  fo4::test::ByteWriter out;
  out.bytes = fo4::test::make_tes4_plugin(0, 1.0f, {});
  add_record(out, "AECH", 0x807,
             {{"EDID", zstr("RadicleChain")},
              {"KNAM", le(kOverdrive, 4)},
              {"DNAM", overdrive_dnam()},
              {"KNAM", le(kStateVariableFilter, 4)},
              {"DNAM", filter_dnam()},
              {"KNAM", le(kDelayEffect, 4)},
              {"DNAM", delay_dnam()}});
  add_record(out, "ASPC", 0x800,
             {{"EDID", zstr("RadicleAcoustic")},
              {"OBND", cat({le(0, 2), le(0, 2), le(0, 2), le(0, 2), le(0, 2), le(0, 2)})},
              {"XTRI", le(1, 1)}, {"WNAM", le(150, 2)},
              {"SNAM", le(0x809, 4)}, {"RDAT", le(0x80a, 4)}, {"BNAM", le(0x80c, 4)}});
  add_record(out, "REVB", 0x801,
             {{"EDID", zstr("RadicleReverb")},
              {"DATA", cat({le(1250, 2), le(800, 2), le(0, 1), le(0, 1), le(0, 1), le(0, 1), le(100, 1),
                            le(0, 1), le(0, 1), le(100, 1), le(100, 1), le(0, 1)})},
              {"ANAM", le(2, 4)}});
  add_record(out, "SNCT", 0x802,
             {{"EDID", zstr("RadicleCategory")}, {"FNAM", le(0x41, 4)}, {"PNAM", le(0x80b, 4)},
              {"ONAM", le(0x802, 4)}, {"VNAM", le(65535, 2)}, {"UNAM", le(32768, 2)},
              {"MNAM", f32(0.1f)}, {"CNAM", f32(1.0f)}});
  add_record(out, "SOPM", 0x803,
             {{"EDID", zstr("RadicleOutput")}, {"NAM1", cat({le(0x09, 1), le(0, 2), le(40, 1)})},
              {"MNAM", le(1, 4)}, {"VNAM", le(0xff9c, 2)},
              {"ONAM", cat({le(100, 1), le(100, 1), le(0, 1), le(0, 1), le(50, 1), le(50, 1), le(50, 1),
                            le(50, 1), le(80, 1), le(80, 1), le(0, 1), le(0, 1), le(0, 1), le(0, 1),
                            le(0, 1), le(0, 1)})},
              {"ATTN", cat({f32(1.0f), f32(2.0f), f32(3.0f), f32(4.0f), le(0, 1), le(50, 1), le(80, 1),
                            le(95, 1), le(50, 1), le(20, 1), le(5, 1), le(0, 1)})},
              {"ENAM", le(0x807, 4)}});
  add_record(out, "SCSN", 0x804,
             {{"EDID", zstr("RadicleSnapshot")}, {"PNAM", le(5, 2)},
              {"CNAM", cat({le(0x802, 4), f32(0.5f)})}});
  add_record(out, "STAG", 0x805, {{"EDID", zstr("RadicleTags")}});
  add_record(out, "MUST", 0x806,
             {{"EDID", zstr("RadicleTrack")}, {"CNAM", le(1859641416, 4)}, {"FLTV", f32(2.5f)},
              {"DNAM", f32(0.5f)}, {"ANAM", zstr("a.wav")}, {"BNAM", zstr("b.wav")},
              {"LNAM", cat({f32(0.25f), f32(1.5f), le(3, 4)})},
              {"FNAM", cat({f32(12.5f), f32(30.0f)})},
              {"SNAM", cat({le(0x806, 4), le(0x806, 4)})}});
  return out.bytes;
}

const fo4::Subrecord* first_subrecord(const fo4::Record& record, fo4::Signature signature) {
  for (const auto& sub : record.subrecords) {
    if (sub.signature == signature) return &sub;
  }
  return nullptr;
}

std::string row_text(const fo4::Record& record,
                     const fo4::schema::ScalarFieldDescriptor& field,
                     std::size_t index) {
  const auto resolved = fo4::schema::resolve_field_slots(
      record, fo4::schema::scalar_field_shape(field.scalar), field.scalar.address);
  if (index >= resolved.slots.size()) return {};
  const auto slot = resolved.slots[index];
  if (!slot.valid) return {};
  return fo4::schema::decode_scalar(
      fo4::schema::scalar_slot_bytes(record.subrecords[slot.subrecord].data, slot, field.scalar),
      field.scalar);
}

std::string any_row_text(const fo4::Record& record, const fo4::schema::ScalarFieldDescriptor& field) {
  const auto resolved = fo4::schema::resolve_field_slots(
      record, fo4::schema::scalar_field_shape(field.scalar), field.scalar.address);
  for (const auto& slot : resolved.slots) {
    if (!slot.valid) continue;
    return fo4::schema::decode_scalar(
        fo4::schema::scalar_slot_bytes(record.subrecords[slot.subrecord].data, slot, field.scalar),
        field.scalar);
  }
  return {};
}

void expect_only_changed(const fo4::Record& original, const fo4::Record& edited, std::string_view changed) {
  ASSERT_EQ(original.subrecords.size(), edited.subrecords.size());
  for (std::size_t i = 0; i < original.subrecords.size(); ++i) {
    if (original.subrecords[i].signature.to_string() == changed) continue;
    EXPECT_EQ(original.subrecords[i].data, edited.subrecords[i].data) << changed << ' ' << i;
  }
}

}  // namespace

TEST(AudioRecordsSynthetic, EveryDeclaredRowDecodesFromTheFixture) {
  const auto plugin = fo4::read_plugin(fixture(), "Patch.esp");
  for (const auto& entry : kRecords) {
    SCOPED_TRACE(std::string(entry.signature));
    const auto signature = fo4::Signature::from_string(entry.signature);
    const auto* record = fo4::find_record(plugin, key(entry.form));
    ASSERT_NE(record, nullptr);
    for (const auto& field : fo4::schema::scalar_fields(signature)) {
      EXPECT_FALSE(any_row_text(*record, field).empty()) << std::string(field.path);
    }
    for (const auto& link : fo4::schema::formlink_fields(signature)) {
      EXPECT_NE(first_subrecord(*record, link.subrecord), nullptr) << std::string(link.path);
    }
  }
}

TEST(AudioRecordsSynthetic, OneEditPerRecordRoundTripsAndTouchesNoOtherSubrecord) {
  for (const auto& edit : kEdits) {
    SCOPED_TRACE(std::string(edit.path));
    const auto signature = fo4::Signature::from_string(edit.record);
    const auto record_key = key(edit.form);
    const auto original = fo4::read_plugin(fixture(), "Patch.esp");
    auto plugin = fo4::read_plugin(fixture(), "Patch.esp");

    if (edit.kind == EditKind::link) {
      const auto field = fo4::schema::formlink_field_descriptor(signature, edit.path);
      ASSERT_TRUE(field.has_value());
      EXPECT_EQ(fo4::set_record_formlink_field(plugin, record_key, *field, key(edit.value)),
                fo4::FormLinkFieldMutationStatus::updated);
    } else if (edit.kind == EditKind::text) {
      EXPECT_EQ(fo4::set_record_string(plugin, record_key, edit.path, 0, edit.value),
                fo4::ScalarMutationStatus::updated);
    } else {
      const auto field = fo4::schema::scalar_field_descriptor(signature, edit.path);
      ASSERT_TRUE(field.has_value());
      EXPECT_EQ(fo4::set_record_scalar(plugin, record_key, *field, edit.value),
                fo4::ScalarMutationStatus::updated);
    }

    const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
    const auto* before = fo4::find_record(original, record_key);
    const auto* after = fo4::find_record(reread, record_key);
    ASSERT_NE(before, nullptr);
    ASSERT_NE(after, nullptr);
    expect_only_changed(*before, *after, edit.changed);

    if (edit.kind == EditKind::link) {
      const auto* sub = first_subrecord(*after, fo4::Signature::from_string(edit.changed));
      ASSERT_NE(sub, nullptr);
      const auto field = fo4::schema::formlink_field_descriptor(signature, edit.path);
      ASSERT_TRUE(field.has_value());
      const auto target = std::stoul(std::string(edit.value), nullptr, 16);
      EXPECT_EQ(std::vector<std::byte>(sub->data.begin() + static_cast<std::ptrdiff_t>(field->offset),
                                       sub->data.begin() + static_cast<std::ptrdiff_t>(field->offset + 4)),
                le(static_cast<std::uint32_t>(target), 4));
    } else {
      const auto field = fo4::schema::scalar_field_descriptor(signature, edit.path);
      ASSERT_TRUE(field.has_value());
      EXPECT_EQ(any_row_text(*after, *field), std::string(edit.value));
    }
  }
}

TEST(AudioRecordsSynthetic, EffectTypeAndEnabledAddressEachEffect) {
  auto plugin = fo4::read_plugin(fixture(), "Patch.esp");
  const auto aech = fo4::Signature::from_string("AECH");
  const auto type = fo4::schema::scalar_field_descriptor(aech, "AECH\\EffectType");
  const auto enabled = fo4::schema::scalar_field_descriptor(aech, "AECH\\EffectEnabled");
  ASSERT_TRUE(type.has_value());
  ASSERT_TRUE(enabled.has_value());
  EXPECT_TRUE(type->scalar.address.repeated);
  EXPECT_TRUE(enabled->scalar.address.repeated);
  EXPECT_EQ(type->scalar.subrecord, fo4::Signature::from_string("KNAM"));
  EXPECT_EQ(enabled->scalar.subrecord, fo4::Signature::from_string("DNAM"));

  const auto* record = fo4::find_record(plugin, key("000807"));
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(row_text(*record, *type, 0), "2252866750");
  EXPECT_EQ(row_text(*record, *type, 1), "4015480703");
  EXPECT_EQ(row_text(*record, *type, 2), "411269967");
  EXPECT_EQ(row_text(*record, *type, 3), "");
  EXPECT_EQ(row_text(*record, *enabled, 0), "1");
  EXPECT_EQ(row_text(*record, *enabled, 1), "0");
  EXPECT_EQ(row_text(*record, *enabled, 2), "1");
  const auto overdrive = fo4::schema::scalar_value_name(type->scalar, 2252866750);
  const auto filter = fo4::schema::scalar_value_name(type->scalar, 4015480703);
  const auto delay = fo4::schema::scalar_value_name(type->scalar, 411269967);
  ASSERT_TRUE(overdrive.has_value());
  ASSERT_TRUE(filter.has_value());
  ASSERT_TRUE(delay.has_value());
  EXPECT_EQ(*overdrive, "BSOverdrive");
  EXPECT_EQ(*filter, "BSStateVariableFilter");
  EXPECT_EQ(*delay, "BSDelayEffect");
  EXPECT_FALSE(fo4::schema::scalar_value_name(type->scalar, 0).has_value());

  EXPECT_EQ(fo4::set_record_scalar(plugin, key("000807"), *enabled, 1, "1"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key("000807"), *enabled, 3, "1"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_THROW(
      static_cast<void>(fo4::set_record_scalar(plugin, key("000807"), *type, 1, "4294967296")),
      fo4::schema::ScalarError);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* after = fo4::find_record(reread, key("000807"));
  ASSERT_NE(after, nullptr);
  EXPECT_EQ(row_text(*after, *enabled, 1), "1");
  EXPECT_EQ(row_text(*after, *type, 1), "4015480703");

  std::size_t seen = 0;
  const fo4::Subrecord* second_dnam = nullptr;
  for (const auto& sub : after->subrecords) {
    if (sub.signature != fo4::Signature::from_string("DNAM")) continue;
    if (seen++ == 1) {
      second_dnam = &sub;
      break;
    }
  }
  ASSERT_NE(second_dnam, nullptr);
  EXPECT_EQ(second_dnam->data, cat({le(1, 4), f32(90.0f), f32(1.5f), le(1, 4)}));
}

std::vector<Bytes> dnam_payloads(const fo4::Record& record) {
  std::vector<Bytes> out;
  for (const auto& sub : record.subrecords) {
    if (sub.signature == fo4::Signature::from_string("DNAM")) out.push_back(sub.data);
  }
  return out;
}

TEST(AudioRecordsSynthetic, EffectDataArmsFollowTheirEffectOrdinal) {
  const auto aech = fo4::Signature::from_string("AECH");
  const auto row = [&](std::string_view path) {
    const auto field = fo4::schema::scalar_field_descriptor(aech, path);
    EXPECT_TRUE(field.has_value()) << std::string(path);
    return field;
  };
  const auto input_gain = row("AECH\\OverdriveInputGain");
  const auto output_gain = row("AECH\\OverdriveOutputGain");
  const auto upper = row("AECH\\OverdriveUpperThreshold");
  const auto lower = row("AECH\\OverdriveLowerThreshold");
  const auto center = row("AECH\\FilterCenterFreq");
  const auto q_value = row("AECH\\FilterQValue");
  const auto mode = row("AECH\\FilterMode");
  const auto feedback = row("AECH\\DelayFeedback");
  const auto wet_mix = row("AECH\\DelayWetMix");
  const auto delay_ms = row("AECH\\DelayMs");
  ASSERT_TRUE(input_gain && output_gain && upper && lower && center && q_value && mode && feedback &&
              wet_mix && delay_ms);
  EXPECT_EQ(input_gain->scalar.address.when.count, 1u);
  EXPECT_EQ(input_gain->scalar.address.when.subrecord, fo4::Signature::from_string("KNAM"));
  EXPECT_EQ(input_gain->scalar.payload_size, 20u);
  EXPECT_EQ(center->scalar.payload_size, 16u);
  EXPECT_EQ(delay_ms->scalar.payload_size, 16u);

  auto plugin = fo4::read_plugin(fixture(), "Patch.esp");
  const auto* record = fo4::find_record(plugin, key("000807"));
  ASSERT_NE(record, nullptr);
  const auto before = dnam_payloads(*record);
  ASSERT_EQ(before.size(), 3u);
  EXPECT_EQ(before[0], overdrive_dnam());
  EXPECT_EQ(before[1], filter_dnam());
  EXPECT_EQ(before[2], delay_dnam());

  EXPECT_EQ(row_text(*record, *input_gain, 0), "1");
  EXPECT_EQ(row_text(*record, *output_gain, 0), "2");
  EXPECT_EQ(row_text(*record, *upper, 0), "3");
  EXPECT_EQ(row_text(*record, *lower, 0), "4");
  EXPECT_EQ(row_text(*record, *center, 1), "90");
  EXPECT_EQ(row_text(*record, *q_value, 1), "1.5");
  EXPECT_EQ(row_text(*record, *mode, 1), "1");
  const auto mode_name = fo4::schema::scalar_value_name(mode->scalar, 1);
  ASSERT_TRUE(mode_name.has_value());
  EXPECT_EQ(*mode_name, "Low Pass");
  EXPECT_EQ(row_text(*record, *feedback, 2), "0.5");
  EXPECT_EQ(row_text(*record, *wet_mix, 2), "0.25");
  EXPECT_EQ(row_text(*record, *delay_ms, 2), "250");

  EXPECT_EQ(row_text(*record, *input_gain, 1), "");
  EXPECT_EQ(row_text(*record, *input_gain, 2), "");
  EXPECT_EQ(row_text(*record, *center, 0), "");
  EXPECT_EQ(row_text(*record, *center, 2), "");
  EXPECT_EQ(row_text(*record, *delay_ms, 0), "");
  EXPECT_EQ(row_text(*record, *delay_ms, 1), "");
  EXPECT_EQ(fo4::set_record_scalar(plugin, key("000807"), *input_gain, 1, "9"),
            fo4::ScalarMutationStatus::subrecord_missing);

  EXPECT_EQ(fo4::set_record_scalar(plugin, key("000807"), *q_value, 1, "2.5"),
            fo4::ScalarMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* after = fo4::find_record(reread, key("000807"));
  ASSERT_NE(after, nullptr);
  const auto written = dnam_payloads(*after);
  ASSERT_EQ(written.size(), 3u);
  EXPECT_EQ(written[0], before[0]);
  EXPECT_EQ(written[2], before[2]);
  EXPECT_EQ(written[1], cat({le(0, 4), f32(90.0f), f32(2.5f), le(1, 4)}));
  EXPECT_EQ(row_text(*after, *input_gain, 0), "1");
  EXPECT_EQ(row_text(*after, *delay_ms, 2), "250");
}

TEST(AudioRecordsSynthetic, EffectDataArmsHideWhenTheTypeCountDisagrees) {
  fo4::test::ByteWriter out;
  out.bytes = fo4::test::make_tes4_plugin(0, 1.0f, {});
  add_record(out, "AECH", 0x807,
             {{"EDID", zstr("RadicleChain")},
              {"KNAM", le(kOverdrive, 4)},
              {"DNAM", overdrive_dnam()},
              {"DNAM", overdrive_dnam()},
              {"KNAM", le(kStateVariableFilter, 4)},
              {"DNAM", filter_dnam()}});
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");

  const auto aech = fo4::Signature::from_string("AECH");
  const auto input_gain = fo4::schema::scalar_field_descriptor(aech, "AECH\\OverdriveInputGain");
  const auto center = fo4::schema::scalar_field_descriptor(aech, "AECH\\FilterCenterFreq");
  const auto enabled = fo4::schema::scalar_field_descriptor(aech, "AECH\\EffectEnabled");
  ASSERT_TRUE(input_gain.has_value());
  ASSERT_TRUE(center.has_value());
  ASSERT_TRUE(enabled.has_value());

  const auto* record = fo4::find_record(plugin, key("000807"));
  ASSERT_NE(record, nullptr);
  for (std::size_t index = 0; index < 3; ++index) {
    EXPECT_EQ(row_text(*record, *input_gain, index), "") << index;
    EXPECT_EQ(row_text(*record, *center, index), "") << index;
  }
  EXPECT_EQ(row_text(*record, *enabled, 0), "1");
  EXPECT_EQ(row_text(*record, *enabled, 1), "1");
  EXPECT_EQ(row_text(*record, *enabled, 2), "0");
  EXPECT_EQ(fo4::set_record_scalar(plugin, key("000807"), *input_gain, 0, "9"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key("000807"), *center, 2, "9"),
            fo4::ScalarMutationStatus::subrecord_missing);
}
