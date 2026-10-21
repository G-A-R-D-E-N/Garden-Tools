#include "object_base_fixture.hpp"

using namespace object_base_fixture;

TEST(ObjectBaseRecordsSynthetic, ModelInfoRowsDecodeEveryTextureAndMaterialEntry) {
  const auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto door_key = fo4::FormKey::parse("000803:Patch.esp");
  const auto cont_key = fo4::FormKey::parse("000802:Patch.esp");
  const auto* door = fo4::find_record(plugin, door_key);
  const auto* cont = fo4::find_record(plugin, cont_key);
  ASSERT_NE(door, nullptr);
  ASSERT_NE(cont, nullptr);

  EXPECT_EQ(decoded_slots(*door, "DOOR\\ModelInfoTextureFileHash"),
            model_values({k_door_textures[0].file_hash, k_door_textures[1].file_hash}));
  EXPECT_EQ(decoded_slots(*door, "DOOR\\ModelInfoTextureExtension"),
            model_values({four_cc(k_door_textures[0].extension), four_cc(k_door_textures[1].extension)}));
  EXPECT_EQ(decoded_slots(*door, "DOOR\\ModelInfoTextureFolderHash"),
            model_values({k_door_textures[0].folder_hash, k_door_textures[1].folder_hash}));
  EXPECT_EQ(decoded_slots(*door, "DOOR\\ModelInfoAddonNode"), model_values({k_door_addon_nodes[0]}));
  EXPECT_TRUE(decoded_slots(*door, "DOOR\\ModelInfoMaterialFileHash").empty());
  EXPECT_EQ(fo4::record_field_count(plugin, door_key, "DOOR\\ModelInfoTextureFileHash"), 2u);
  EXPECT_EQ(fo4::record_field_count(plugin, door_key, "DOOR\\ModelInfoAddonNode"), 1u);
  EXPECT_EQ(fo4::record_field_count(plugin, door_key, "DOOR\\ModelInfoMaterialFileHash"), 0u);

  EXPECT_EQ(decoded_slots(*cont, "CONT\\ModelInfoTextureFileHash"),
            model_values({k_cont_textures[0].file_hash, k_cont_textures[1].file_hash}));
  EXPECT_EQ(decoded_slots(*cont, "CONT\\ModelInfoTextureExtension"),
            model_values({four_cc(k_cont_textures[0].extension), four_cc(k_cont_textures[1].extension)}));
  EXPECT_EQ(decoded_slots(*cont, "CONT\\ModelInfoTextureFolderHash"),
            model_values({k_cont_textures[0].folder_hash, k_cont_textures[1].folder_hash}));
  EXPECT_EQ(decoded_slots(*cont, "CONT\\ModelInfoMaterialFileHash"),
            model_values({k_cont_materials[0].file_hash}));
  EXPECT_EQ(decoded_slots(*cont, "CONT\\ModelInfoMaterialExtension"),
            model_values({four_cc(k_cont_materials[0].extension)}));
  EXPECT_TRUE(decoded_slots(*cont, "CONT\\ModelInfoAddonNode").empty());
  EXPECT_EQ(fo4::record_field_count(plugin, cont_key, "CONT\\ModelInfoTextureFileHash"), 2u);
  EXPECT_EQ(fo4::record_field_count(plugin, cont_key, "CONT\\ModelInfoMaterialFileHash"), 1u);
  EXPECT_EQ(fo4::record_field_count(plugin, cont_key, "CONT\\ModelInfoAddonNode"), 0u);

  const auto extension = fo4::schema::scalar_field_descriptor(
      fo4::Signature::from_string("CONT"), "CONT\\ModelInfoMaterialExtension");
  ASSERT_TRUE(extension.has_value());
  const auto* modt = find_subrecord(*cont, fo4::Signature::from_string("MODT"));
  ASSERT_NE(modt, nullptr);
  const auto slots = fo4::schema::resolve_field_slots(
      *cont, fo4::schema::scalar_field_shape(extension->scalar), extension->scalar.address);
  ASSERT_EQ(slots.slots.size(), 1u);
  const auto bytes = fo4::schema::scalar_slot_bytes(modt->data, slots.slots[0], extension->scalar);
  EXPECT_EQ(Bytes(bytes.begin(), bytes.end()),
            (Bytes{std::byte{'b'}, std::byte{'g'}, std::byte{'s'}, std::byte{'m'}}));

  expect_no_model_info_issue(*door);
  expect_no_model_info_issue(*cont);
  for (const auto& issue : fo4::schema::preflight_authoring_record(plugin, door_key)) {
    EXPECT_NE(issue.code, "subrecord_order_invalid") << issue.path;
  }
}

TEST(ObjectBaseRecordsSynthetic, ModelInfoTextureWriteRoundTripsByteExact) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto key = fo4::FormKey::parse("000803:Patch.esp");
  const auto field = fo4::schema::scalar_field_descriptor(fo4::Signature::from_string("DOOR"),
                                                          "DOOR\\ModelInfoTextureFileHash");
  ASSERT_TRUE(field.has_value());
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *field, 1, "3735928559"),
            fo4::ScalarMutationStatus::updated);

  const auto array = fo4::schema::counted_array_descriptor(fo4::Signature::from_string("DOOR"),
                                                           "DOOR\\ModelInfoTextures");
  ASSERT_TRUE(array.has_value());
  EXPECT_THROW(fo4::insert_record_counted_element(plugin, key, *array, 0),
               fo4::schema::CountedArrayError);
  EXPECT_THROW(fo4::remove_record_counted_element(plugin, key, *array, 1),
               fo4::schema::CountedArrayError);

  const auto* door = fo4::find_record(plugin, key);
  ASSERT_NE(door, nullptr);
  expect_no_model_info_issue(*door);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* saved = fo4::find_record(reread, key);
  ASSERT_NE(saved, nullptr);
  const auto* modt = find_subrecord(*saved, fo4::Signature::from_string("MODT"));
  ASSERT_NE(modt, nullptr);
  const std::vector<ModelTexture> expected{k_door_textures[0],
                                           {3735928559u, k_door_textures[1].extension,
                                            k_door_textures[1].folder_hash}};
  EXPECT_EQ(modt->data, modt_data(expected, {k_door_addon_nodes[0]}, {}, 1));
  EXPECT_EQ(decoded_slots(*saved, "DOOR\\ModelInfoTextureFileHash"),
            (std::vector<std::string>{std::to_string(k_door_textures[0].file_hash), "3735928559"}));
}

TEST(ObjectBaseRecordsSynthetic, ModelInfoPayloadsOutsideTheModelledArmAreAbsent) {
  const auto field = fo4::schema::scalar_field_descriptor(fo4::Signature::from_string("DOOR"),
                                                          "DOOR\\ModelInfoTextureFileHash");
  ASSERT_TRUE(field.has_value());
  const auto door_record = [](const Bytes& modt) {
    return std::vector<Sub>{{"EDID", zstr("RadicleDoor")},
                            {"OBND", zeros(12)},
                            {"MODL", zstr("Door.nif")},
                            {"MODT", modt}};
  };
  const auto assert_absent = [&](const std::vector<Sub>& subs, std::uint16_t form_version) {
    auto plugin = template_record_plugin("DOOR", 0x813, subs, form_version);
    const auto key = fo4::FormKey::parse("000813:Patch.esp");
    const auto* record = only_record(plugin, "DOOR");
    ASSERT_NE(record, nullptr);
    EXPECT_TRUE(decoded_slots(*record, "DOOR\\ModelInfoTextureFileHash").empty());
    EXPECT_TRUE(decoded_slots(*record, "DOOR\\ModelInfoMaterialFileHash").empty());
    EXPECT_EQ(fo4::record_field_count(plugin, key, "DOOR\\ModelInfoAddonNode"), 0u);
    EXPECT_EQ(fo4::set_record_scalar(plugin, key, *field, 0, "7"),
              fo4::ScalarMutationStatus::subrecord_missing);
    expect_no_model_info_issue(*record);
  };

  ByteWriter short_header;
  short_header.u32(4);
  short_header.u32(2);
  assert_absent(door_record(short_header.bytes), 131);

  ByteWriter overrun;
  overrun.bytes = modt_data({k_door_textures[0]}, {}, {}, 1);
  overrun.patch_u32(4, 2);
  assert_absent(door_record(overrun.bytes), 131);

  ByteWriter wide_header;
  wide_header.u32(9);
  for (std::size_t index = 0; index < 9u; ++index) {
    wide_header.u32(0);
  }
  assert_absent(door_record(wide_header.bytes), 131);

  assert_absent(door_record(modt_data({k_door_textures[0], k_door_textures[1]}, {}, {}, 1)), 39);
}
