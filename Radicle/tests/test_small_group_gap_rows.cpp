#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/forms/record_flags.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/record_flags.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using fo4::Signature;
using fo4::test::ByteWriter;

constexpr std::string_view k_key = "000800:Patch.esp";

Bytes zstr(std::string_view text) {
  ByteWriter out;
  out.raw(text);
  out.u8(0);
  return out.bytes;
}

Bytes u32_bytes(std::uint32_t value) {
  ByteWriter out;
  out.u32(value);
  return out.bytes;
}

std::uint32_t read_u32(const Bytes& data, std::size_t offset) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[offset + i])) << (8 * i);
  }
  return value;
}

void write_record(ByteWriter& out, std::string_view signature, std::uint32_t form_id,
                  std::initializer_list<std::pair<std::string_view, Bytes>> subrecords,
                  std::uint32_t flags = 0) {
  const auto group = fo4::test::begin_group(out, signature, 0);
  const auto record = fo4::test::begin_record(out, signature, flags, form_id);
  for (const auto& [name, data] : subrecords) fo4::test::write_subrecord(out, name, data);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
}

// Constraint: a formlink target's signature is read back from the plugin, so a test that
// writes a typed link also has to author the target record.
fo4::Plugin make_plugin(std::string_view signature,
                        std::initializer_list<std::pair<std::string_view, Bytes>> subrecords,
                        std::uint32_t form_id = 0x000800,
                        std::string_view target_signature = {},
                        std::uint32_t flags = 0) {
  ByteWriter out;
  // No masters: the fixture's form ids stay plugin-local (000800:Patch.esp), as the other
  // single-record suites do.
  out.bytes = fo4::test::make_tes4_plugin(0, 1.0f, {});
  write_record(out, signature, form_id, subrecords, flags);
  if (!target_signature.empty()) write_record(out, target_signature, 0x000901, {});
  return fo4::read_plugin(out.bytes, "Patch.esp");
}

fo4::Plugin reparse(const fo4::Plugin& plugin) {
  return fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
}

fo4::schema::ScalarFieldDescriptor scalar(std::string_view path) {
  return fo4::schema::scalar_field_descriptor(path).value();
}

fo4::schema::FormLinkFieldDescriptor link(std::string_view path) {
  return fo4::schema::formlink_field_descriptor(path).value();
}

const Bytes* payload_of(const fo4::Plugin& plugin, Signature signature, std::size_t index = 0) {
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(k_key));
  if (record == nullptr) return nullptr;
  std::size_t seen = 0;
  for (const auto& subrecord : record->subrecords) {
    if (subrecord.signature != signature) continue;
    if (seen++ == index) return &subrecord.data;
  }
  return nullptr;
}

std::vector<std::string> signatures_of(const fo4::Plugin& plugin) {
  std::vector<std::string> out;
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(k_key));
  if (record == nullptr) return out;
  for (const auto& subrecord : record->subrecords) out.push_back(subrecord.signature.to_string());
  return out;
}

std::string read_scalar(const fo4::Plugin& plugin, std::string_view path, std::size_t index = 0) {
  const auto field = scalar(path);
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(k_key));
  if (record == nullptr) return "absent";
  const auto resolved = fo4::schema::resolve_field_slots(
      *record, fo4::schema::scalar_field_shape(field.scalar), field.scalar.address);
  if (index >= resolved.slots.size() || !resolved.slots[index].valid) return "absent";
  const auto slot = resolved.slots[index];
  return fo4::schema::decode_scalar(
      fo4::schema::scalar_slot_bytes(record->subrecords[slot.subrecord].data, slot, field.scalar),
      field.scalar);
}

}  // namespace

// C-16: QUST NAM0 (Next Quest) inside the Log Entry repeat group had no row at all.
TEST(SmallGroupGapRows, QustNextQuestRowKeepsTheLogEntryLinkAddressable) {
  const auto next = link("QUST\\NextQuest");
  EXPECT_EQ(next.subrecord.to_string(), "NAM0");
  EXPECT_EQ(next.offset, 0u);
  EXPECT_TRUE(next.address.repeated);
  EXPECT_FALSE(next.nullable);
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(next, Signature::from_string("QUST")));
  EXPECT_FALSE(fo4::schema::formlink_field_target_allowed(next, Signature::from_string("INFO")));

  auto plugin =
      make_plugin("QUST", {{"EDID", zstr("RadicleQuest")}, {"NAM0", u32_bytes(0x000900)}}, 0x000800, "QUST");
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, fo4::FormKey::parse(k_key), next, 0,
                                           fo4::FormKey::parse("000901:Patch.esp")),
            fo4::FormLinkFieldMutationStatus::updated);

  const auto after = reparse(plugin);
  const auto* nam0 = payload_of(after, Signature::from_string("NAM0"));
  ASSERT_NE(nam0, nullptr);
  ASSERT_EQ(nam0->size(), 4u);
  EXPECT_EQ(read_u32(*nam0, 0), 0x000901u);
}

// C-26/C-27: RACE MODT (model info) carries one payload per gender, male first. A single
// gender-neutral row would mix both payloads, so the rows are split into male (occurrence 0)
// and female (occurrence 1) variants.
TEST(SmallGroupGapRows, RaceModelInfoRowsSplitModtPerGender) {
  const auto record = Signature::from_string("RACE");
  const auto check = [&](std::string_view prefix, std::size_t occurrence) {
    const auto descriptor = [&](std::string_view name) {
      return fo4::schema::counted_array_descriptor(
          record, std::string("RACE\\") + std::string(prefix) + std::string(name));
    };
    const auto textures = descriptor("ModelInfoTextures");
    ASSERT_TRUE(textures.has_value()) << prefix;
    EXPECT_EQ(textures->subrecord.to_string(), "MODT");
    EXPECT_EQ(textures->stride, 12u);
    EXPECT_EQ(textures->count_word, 0u);
    EXPECT_EQ(textures->segment_index, 0u);
    EXPECT_EQ(textures->segment_count, 3u);
    EXPECT_TRUE(textures->has_subrecord_occurrence);
    EXPECT_EQ(textures->subrecord_occurrence, occurrence);

    const auto addons = descriptor("ModelInfoAddonNodes");
    ASSERT_TRUE(addons.has_value()) << prefix;
    EXPECT_EQ(addons->stride, 4u);
    EXPECT_EQ(addons->count_word, 1u);
    EXPECT_EQ(addons->subrecord_occurrence, occurrence);

    const auto materials = descriptor("ModelInfoMaterials");
    ASSERT_TRUE(materials.has_value()) << prefix;
    EXPECT_EQ(materials->stride, 12u);
    EXPECT_EQ(materials->count_word, 3u);
    EXPECT_EQ(materials->subrecord_occurrence, occurrence);

    // The member rows have to agree, or a write would land in the other gender's payload.
    const auto hash =
        scalar(std::string("RACE\\") + std::string(prefix) + "ModelInfoTextureFileHash");
    EXPECT_EQ(hash.scalar.subrecord.to_string(), "MODT");
    EXPECT_EQ(hash.scalar.offset, 0u);
    EXPECT_TRUE(hash.scalar.address.has_subrecord_occurrence);
    EXPECT_EQ(hash.scalar.address.subrecord_occurrence, occurrence);
  };
  check("Male", 0);
  check("Female", 1);

  // Regression: two MODT payloads with different counts. The female rows must read only the
  // second payload, and writing a female entry must leave the male payload byte-identical.
  const auto modt = [](std::uint32_t textures, std::uint32_t addons, std::uint32_t materials,
                       std::uint32_t seed) {
    ByteWriter out;
    out.u32(4);  // counter words that follow (the legacy MODT header)
    out.u32(textures);
    out.u32(addons);
    out.u32(0);
    out.u32(materials);
    for (std::uint32_t i = 0; i < textures; ++i) {
      out.u32(seed + i);
      out.u32(0x7478742E);  // ".txt"
      out.u32(seed + 100 + i);
    }
    for (std::uint32_t i = 0; i < addons; ++i) out.u32(seed + 200 + i);
    for (std::uint32_t i = 0; i < materials; ++i) {
      out.u32(seed + 300 + i);
      out.u32(0x6C74616D);  // "matl"
      out.u32(seed + 400 + i);
    }
    return out.bytes;
  };
  auto plugin = make_plugin("RACE", {{"EDID", zstr("RadicleRace")},
                                     {"MODT", modt(1, 1, 1, 0x1000)},
                                     {"MODT", modt(2, 0, 0, 0x2000)}});
  EXPECT_EQ(read_scalar(plugin, "RACE\\MaleModelInfoTextureFileHash", 0), "4096");
  EXPECT_EQ(read_scalar(plugin, "RACE\\FemaleModelInfoTextureFileHash", 0), "8192");
  EXPECT_EQ(read_scalar(plugin, "RACE\\FemaleModelInfoTextureFileHash", 1), "8193");
  EXPECT_EQ(read_scalar(plugin, "RACE\\FemaleModelInfoTextureFileHash", 2), "absent");
  EXPECT_EQ(read_scalar(plugin, "RACE\\MaleModelInfoAddonNode", 0), "4296");
  EXPECT_EQ(read_scalar(plugin, "RACE\\FemaleModelInfoAddonNode", 0), "absent");
  EXPECT_EQ(read_scalar(plugin, "RACE\\MaleModelInfoMaterialFileHash", 0), "4396");
  EXPECT_EQ(read_scalar(plugin, "RACE\\FemaleModelInfoMaterialFileHash", 0), "absent");

  const auto female_hash = scalar("RACE\\FemaleModelInfoTextureFileHash");
  EXPECT_EQ(fo4::set_record_scalar(plugin, fo4::FormKey::parse(k_key), female_hash, 0, "99"),
            fo4::ScalarMutationStatus::updated);
  const auto after = reparse(plugin);
  EXPECT_EQ(read_scalar(after, "RACE\\FemaleModelInfoTextureFileHash", 0), "99");
  EXPECT_EQ(read_scalar(after, "RACE\\MaleModelInfoTextureFileHash", 0), "4096");
  const auto* male = payload_of(after, Signature::from_string("MODT"), 0);
  const auto* female = payload_of(after, Signature::from_string("MODT"), 1);
  ASSERT_NE(male, nullptr);
  ASSERT_NE(female, nullptr);
  ASSERT_EQ(male->size(), 20u + 12u + 4u + 12u);
  ASSERT_EQ(female->size(), 20u + 2u * 12u);
  EXPECT_EQ(read_u32(*male, 20), 0x1000u);
  EXPECT_EQ(read_u32(*female, 20), 99u);
  EXPECT_EQ(read_u32(*female, 32), 0x2001u);  // second female texture, untouched
  EXPECT_EQ(read_u32(*female, 4), 2u);        // textures counter
  EXPECT_EQ(read_u32(*female, 8), 0u);        // addon node counter
}

// C-27: the RACE bone data union (BSMP/BMMP markers with their BSMB/BSMS
// payloads) had no rows, so a morph bone marker could not be read.
TEST(SmallGroupGapRows, RaceBoneDataRowsCoverBothMarkerArms) {
  const auto bsmp = scalar("RACE\\BoneScaleMarker");
  EXPECT_EQ(bsmp.scalar.subrecord.to_string(), "BSMP");
  EXPECT_TRUE(bsmp.scalar.address.repeated);
  EXPECT_EQ(bsmp.scalar.storage, fo4::schema::ScalarStorage::u32);

  const auto bmmp = scalar("RACE\\MorphBoneMarker");
  EXPECT_EQ(bmmp.scalar.subrecord.to_string(), "BMMP");
  EXPECT_TRUE(bmmp.scalar.address.repeated);
  EXPECT_EQ(bmmp.scalar.storage, fo4::schema::ScalarStorage::u32);

  const auto bone = scalar("RACE\\BoneName");
  EXPECT_EQ(bone.scalar.storage, fo4::schema::ScalarStorage::zstring);
  EXPECT_EQ(bone.scalar.subrecord.to_string(), "BSMB");
  EXPECT_TRUE(bone.scalar.address.repeated);

  const auto delta = scalar("RACE\\BoneScaleDelta");
  EXPECT_EQ(delta.scalar.subrecord.to_string(), "BSMS");
  EXPECT_EQ(delta.scalar.storage, fo4::schema::ScalarStorage::bytes);
  EXPECT_TRUE(delta.scalar.address.repeated);

  auto plugin = make_plugin("RACE", {{"EDID", zstr("RadicleRace")}, {"BSMP", u32_bytes(6)},
                                     {"BMMP", u32_bytes(4)}});
  EXPECT_EQ(read_scalar(plugin, "RACE\\BoneScaleMarker", 0), "6");
  EXPECT_EQ(read_scalar(plugin, "RACE\\MorphBoneMarker", 0), "4");

  EXPECT_EQ(fo4::set_record_scalar(plugin, fo4::FormKey::parse(k_key), bsmp, 0, "1"),
            fo4::ScalarMutationStatus::updated);
  const auto after = reparse(plugin);
  const auto* bsmp_payload = payload_of(after, Signature::from_string("BSMP"));
  ASSERT_NE(bsmp_payload, nullptr);
  EXPECT_EQ(read_u32(*bsmp_payload, 0), 1u);
}

// C-28/C-29: REFR XMRK is an empty marker and had no row, so its presence
// could not be reported or cleared through the field API.
TEST(SmallGroupGapRows, RefrEmptyMarkerRowExposesTheZeroByteMarker) {
  const auto marker = scalar("REFR\\EmptyMarker");
  EXPECT_EQ(marker.scalar.subrecord.to_string(), "XMRK");
  EXPECT_EQ(marker.scalar.storage, fo4::schema::ScalarStorage::bytes);
  EXPECT_EQ(marker.scalar.minimum_payload_size, 0u);

  auto plugin = make_plugin("REFR", {{"NAME", u32_bytes(0x000900)}, {"XMRK", Bytes{}}});
  EXPECT_EQ(read_scalar(plugin, "REFR\\EmptyMarker"), "");

  auto nonempty = make_plugin("REFR", {{"XMRK", Bytes{std::byte{0xAB}, std::byte{0xCD}}}});
  EXPECT_EQ(read_scalar(nonempty, "REFR\\EmptyMarker"), "abcd");
  EXPECT_EQ(fo4::set_record_scalar(nonempty, fo4::FormKey::parse(k_key), marker, 0, ""),
            fo4::ScalarMutationStatus::updated);
  const auto after = reparse(nonempty);
  const auto* xmrk = payload_of(after, Signature::from_string("XMRK"));
  ASSERT_NE(xmrk, nullptr);
  EXPECT_TRUE(xmrk->empty()) << "clearing the marker must leave a zero-byte XMRK";
}

// A-29: CELL MHDT and TVDT stay opaque byte payloads, but the two XCRI counts
// are now addressable so the combined-index header no longer needs raw indexing.
TEST(SmallGroupGapRows, CellOpaqueAndXcriHeaderRowsAreAddressed) {
  const auto max_height = scalar("CELL\\MaxHeightData");
  EXPECT_EQ(max_height.scalar.subrecord.to_string(), "MHDT");
  EXPECT_EQ(max_height.scalar.storage, fo4::schema::ScalarStorage::bytes);

  const auto unknown = scalar("CELL\\UnknownData");
  EXPECT_EQ(unknown.scalar.subrecord.to_string(), "TVDT");
  EXPECT_EQ(unknown.scalar.storage, fo4::schema::ScalarStorage::bytes);

  const auto meshes = scalar("CELL\\CombinedMeshCount");
  EXPECT_EQ(meshes.scalar.subrecord.to_string(), "XCRI");
  EXPECT_EQ(meshes.scalar.offset, 0u);
  EXPECT_EQ(meshes.scalar.storage, fo4::schema::ScalarStorage::u32);
  EXPECT_EQ(meshes.scalar.minimum_payload_size, 8u);

  const auto words = scalar("CELL\\CombinedReferenceWordCount");
  EXPECT_EQ(words.scalar.offset, 4u);
  EXPECT_EQ(words.scalar.minimum_payload_size, 8u);

  ByteWriter xcri;
  xcri.u32(3);
  xcri.u32(9);
  auto plugin = make_plugin("CELL", {{"DATA", Bytes(2, std::byte{0})}, {"XCRI", xcri.bytes},
                                     {"TVDT", Bytes{std::byte{0x11}, std::byte{0x22}}}});
  EXPECT_EQ(read_scalar(plugin, "CELL\\CombinedMeshCount"), "3");
  EXPECT_EQ(read_scalar(plugin, "CELL\\CombinedReferenceWordCount"), "9");
  EXPECT_EQ(read_scalar(plugin, "CELL\\UnknownData"), "1122");
}

// A-29: XCRI's body past the header is a mesh-id array followed by 8-byte
// {reference, mesh} pairs whose count word counts 4-byte words, not pairs. The
// rows have to split that payload exactly, including the halved pair count.
TEST(SmallGroupGapRows, CellXcriBodySplitsIntoMeshIdsAndReferencePairs) {
  const auto record = Signature::from_string("CELL");
  const auto meshes = fo4::schema::counted_array_descriptor(record, "CELL\\CombinedMeshIds");
  ASSERT_TRUE(meshes.has_value());
  EXPECT_EQ(meshes->subrecord.to_string(), "XCRI");
  EXPECT_EQ(meshes->stride, 4u);
  EXPECT_EQ(meshes->header_words, 2u);
  EXPECT_EQ(meshes->count_word, 0u);
  EXPECT_EQ(meshes->segment_index, 0u);
  EXPECT_EQ(meshes->segment_count, 2u);
  EXPECT_EQ(meshes->count_divisor, 1u);

  const auto references = fo4::schema::counted_array_descriptor(record, "CELL\\CombinedReferences");
  ASSERT_TRUE(references.has_value());
  EXPECT_EQ(references->subrecord.to_string(), "XCRI");
  EXPECT_EQ(references->stride, 8u);
  EXPECT_EQ(references->header_words, 2u);
  EXPECT_EQ(references->count_word, 1u);
  EXPECT_EQ(references->segment_index, 1u);
  EXPECT_EQ(references->segment_count, 2u);
  EXPECT_EQ(references->count_divisor, 2u);

  ByteWriter xcri;
  xcri.u32(2);         // 2 combined mesh ids
  xcri.u32(4);         // 4 reference words = 2 pairs
  xcri.u32(0x11);      // mesh ids
  xcri.u32(0x22);
  xcri.u32(0x000900);  // pair 0: reference form id, combined mesh id
  xcri.u32(0x11);
  xcri.u32(0x000901);  // pair 1
  xcri.u32(0x22);
  auto plugin = make_plugin("CELL", {{"DATA", Bytes(2, std::byte{0})}, {"XCRI", xcri.bytes}});

  EXPECT_EQ(read_scalar(plugin, "CELL\\CombinedMeshId", 0), "17");
  EXPECT_EQ(read_scalar(plugin, "CELL\\CombinedMeshId", 1), "34");
  EXPECT_EQ(read_scalar(plugin, "CELL\\CombinedReferenceFormId", 0), "2304");
  EXPECT_EQ(read_scalar(plugin, "CELL\\CombinedReferenceMeshId", 0), "17");
  EXPECT_EQ(read_scalar(plugin, "CELL\\CombinedReferenceFormId", 1), "2305");
  EXPECT_EQ(read_scalar(plugin, "CELL\\CombinedReferenceMeshId", 1), "34");
  // The halved pair count is the regression: reading past it must not see the
  // next payload word.
  EXPECT_EQ(read_scalar(plugin, "CELL\\CombinedReferenceMeshId", 2), "absent");
}

// A-26/A-43: EQUP ANAM is typed as AVIF by xEdit, but the vanilla masters hold
// a CONT (0001A02F) and 0xFFFFFFFF, so the row accepts both signatures.
TEST(SmallGroupGapRows, EqupUnusedRowAcceptsAvifAndContTargets) {
  const auto row = link("EQUP\\Unused");
  EXPECT_EQ(row.subrecord.to_string(), "ANAM");
  EXPECT_TRUE(row.nullable);
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(row, Signature::from_string("AVIF")));
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(row, Signature::from_string("CONT")));
  EXPECT_FALSE(fo4::schema::formlink_field_target_allowed(row, Signature::from_string("WEAP")));

  auto plugin =
      make_plugin("EQUP", {{"DATA", u32_bytes(0)}, {"ANAM", u32_bytes(0x000900)}}, 0x000800, "AVIF");
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, fo4::FormKey::parse(k_key), row, 0,
                                           fo4::FormKey::parse("000901:Patch.esp")),
            fo4::FormLinkFieldMutationStatus::updated);
  const auto after = reparse(plugin);
  const auto* anam = payload_of(after, Signature::from_string("ANAM"));
  ASSERT_NE(anam, nullptr);
  EXPECT_EQ(read_u32(*anam, 0), 0x000901u);
}

// B-21: an optional INFO response member can now be added and cleared on its
// own instead of only moving with a whole response run.
TEST(SmallGroupGapRows, InfoResponseOptionalMemberMovesSingly) {
  const auto group = fo4::schema::subrecord_group_descriptor(Signature::from_string("INFO"),
                                                             "INFO\\Response");
  ASSERT_TRUE(group.has_value());

  ByteWriter trada;
  trada.u32(0x810);
  trada.u8(1);
  trada.u32(0x811);
  trada.u8(0xAB);
  trada.u16(90);
  trada.u32(0xFFFFFFFF);
  trada.u32(42);
  auto plugin = make_plugin("INFO", {{"EDID", zstr("RadicleInfo")}, {"TRDA", trada.bytes},
                                     {"NAM1", zstr("Line")}, {"NAM2", zstr("")},
                                     {"NAM3", zstr("")}, {"NAM4", zstr("")}});
  const auto key = fo4::FormKey::parse(k_key);

  EXPECT_EQ(fo4::set_record_subrecord_group_member(plugin, key, *group, 0,
                                                   Signature::from_string("SNAM"), true),
            fo4::SubrecordGroupMutationStatus::updated);
  auto after = reparse(plugin);
  EXPECT_EQ(signatures_of(after),
            (std::vector<std::string>{"EDID", "TRDA", "NAM1", "NAM2", "NAM3", "NAM4", "SNAM"}));

  EXPECT_EQ(fo4::set_record_subrecord_group_member(after, key, *group, 0,
                                                   Signature::from_string("SNAM"), false),
            fo4::SubrecordGroupMutationStatus::updated);
  EXPECT_EQ(signatures_of(reparse(after)),
            (std::vector<std::string>{"EDID", "TRDA", "NAM1", "NAM2", "NAM3", "NAM4"}));
}

// The distant-LOD generator filters contributors by the record header bit, so the bit has to
// be addressable as a schema row instead of a hardcoded 0x8000, on every vanilla record type
// whose record flags declare bit 15 as Has Distant LOD.
TEST(SmallGroupGapRows, VanillaDistantLodTypesCarryTheHasDistantLodFlagRow) {
  // wbDefinitionsFO4.pas @93cc0bc5 declares bit 15 as 'Has Distant LOD' in these nine records.
  constexpr std::string_view types[] = {"ACTI", "CONT", "DOOR", "FURN", "MSTT",
                                        "SCOL", "STAT", "TERM", "TREE"};
  for (const auto type : types) {
    const auto signature = Signature::from_string(type);
    const auto rows = fo4::schema::record_flags(signature);
    ASSERT_EQ(rows.size(), 1u) << type;
    EXPECT_EQ(rows[0].signature, signature) << type;
    EXPECT_EQ(rows[0].path, std::string_view(std::string(type) + "\\HasDistantLOD")) << type;
    EXPECT_EQ(rows[0].label, "Has Distant LOD") << type;
    EXPECT_EQ(rows[0].mask, 0x00008000u) << type;
  }
  EXPECT_EQ(fo4::schema::record_flag_descriptor("STAT\\HasTreeLOD"), nullptr);
  // A record type without the row stays empty rather than borrowing another type's mask.
  EXPECT_TRUE(fo4::schema::record_flags(Signature::from_string("MISC")).empty());
}

TEST(SmallGroupGapRows, DistantLodFilterReadsTheHeaderBitOfStatAndScol) {
  const auto* descriptor = fo4::schema::record_flag_descriptor("STAT\\HasDistantLOD");
  ASSERT_NE(descriptor, nullptr);

  // Detects: a fixture that hand-writes bit 15, which would keep passing if the schema row
  // or the guarded writer regressed. The bit only ever arrives through write_record_flag.
  auto flagged = reparse(make_plugin("STAT", {}, 0x000800, {}));
  ASSERT_EQ(fo4::write_record_flag(flagged, fo4::FormKey::parse(k_key), "STAT\\HasDistantLOD", true),
            fo4::RecordFlagWriteStatus::changed);
  const auto plain = reparse(make_plugin("STAT", {}));
  const auto* flagged_record = fo4::find_record(flagged, fo4::FormKey::parse(k_key));
  const auto* plain_record = fo4::find_record(plain, fo4::FormKey::parse(k_key));
  ASSERT_NE(flagged_record, nullptr);
  ASSERT_NE(plain_record, nullptr);

  EXPECT_TRUE(fo4::schema::record_flag_set(*flagged_record, *descriptor));
  EXPECT_FALSE(fo4::schema::record_flag_set(*plain_record, *descriptor));
  EXPECT_EQ(fo4::schema::record_flag_value(*flagged_record, "STAT\\HasDistantLOD"), true);
  EXPECT_EQ(fo4::schema::record_flag_value(*plain_record, "STAT\\HasDistantLOD"), false);
  // A row only answers for its own signature.
  EXPECT_EQ(fo4::schema::record_flag_value(*flagged_record, "SCOL\\HasDistantLOD"), std::nullopt);

  auto scol = reparse(make_plugin("SCOL", {}, 0x000800, {}));
  ASSERT_EQ(fo4::write_record_flag(scol, fo4::FormKey::parse(k_key), "SCOL\\HasDistantLOD", true),
            fo4::RecordFlagWriteStatus::changed);
  const auto* scol_record = fo4::find_record(scol, fo4::FormKey::parse(k_key));
  ASSERT_NE(scol_record, nullptr);
  EXPECT_EQ(fo4::schema::record_flag_value(*scol_record, "SCOL\\HasDistantLOD"), true);
}
