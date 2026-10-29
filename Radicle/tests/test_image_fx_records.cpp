#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;

constexpr std::string_view k_hdr_mult_iad{"\x00IAD", 4};

struct Sub {
  std::string_view signature;
  Bytes data;
};

Bytes u32(std::uint32_t value) {
  fo4::test::ByteWriter out;
  out.u32(value);
  return out.bytes;
}

Bytes f32(float value) {
  fo4::test::ByteWriter out;
  out.f32(value);
  return out.bytes;
}

Bytes zstr(std::string_view text) {
  Bytes out;
  for (const char ch : text) out.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  out.push_back(std::byte{0});
  return out;
}

Bytes zeros(std::size_t size) { return Bytes(size, std::byte{0}); }

void put(Bytes& data, std::size_t offset, const Bytes& value) {
  std::copy(value.begin(), value.end(), data.begin() + static_cast<std::ptrdiff_t>(offset));
}

Bytes slice(const Bytes& data, std::size_t offset, std::size_t size) {
  return Bytes(data.begin() + static_cast<std::ptrdiff_t>(offset),
               data.begin() + static_cast<std::ptrdiff_t>(offset + size));
}

fo4::FormKey key(std::uint32_t local) {
  return fo4::FormKey{fo4::PluginKey::from_filename("Patch.esp"), fo4::LocalFormId{local}};
}

void write_record(fo4::test::ByteWriter& out, std::string_view signature, std::uint32_t form_id,
                  const std::vector<Sub>& subrecords, std::uint16_t form_version = 131) {
  const auto group = fo4::test::begin_group(out, signature, 0);
  const auto record = fo4::test::begin_record(out, signature, 0, form_id, form_version);
  for (const auto& subrecord : subrecords) {
    fo4::test::write_subrecord(out, subrecord.signature, std::span<const std::byte>{subrecord.data});
  }
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
}

// source:wbDefinitionsFO4.pas @01a11729
std::vector<std::byte> make_plugin() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0u, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(3100);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  Bytes imad_full = zeros(252);
  put(imad_full, 0, u32(1));
  put(imad_full, 4, f32(1.0f));
  Bytes imad_tnam = zeros(40);
  put(imad_tnam, 0, f32(0.5f));
  put(imad_tnam, 16, f32(1.0f));
  put(imad_tnam, 20, f32(2.5f));
  Bytes imad_nam3 = zeros(20);
  put(imad_nam3, 0, f32(1.5f));
  Bytes imad_bnam = zeros(16);
  put(imad_bnam, 0, f32(0.25f));
  put(imad_bnam, 4, f32(2.5f));
  put(imad_bnam, 8, f32(0.75f));
  put(imad_bnam, 12, f32(-4.0f));
  Bytes imad_iad = zeros(16);
  put(imad_iad, 0, f32(0.5f));
  put(imad_iad, 4, f32(6.0f));
  put(imad_iad, 8, f32(0.75f));
  put(imad_iad, 12, f32(1.5f));
  Bytes imad_mult = zeros(16);
  put(imad_mult, 0, f32(0.125f));
  put(imad_mult, 4, f32(3.5f));
  put(imad_mult, 8, f32(1.25f));
  put(imad_mult, 12, f32(-2.5f));
  Bytes imad_nam5 = zeros(8);
  put(imad_nam5, 0, f32(0.5f));
  put(imad_nam5, 4, f32(1.5f));
  Bytes imad_nam6 = zeros(8);
  put(imad_nam6, 0, f32(0.125f));
  put(imad_nam6, 4, f32(0.5f));
  write_record(out, "IMAD", 3001,
               {{"EDID", zstr("RadicleImageAdapter")}, {"DNAM", imad_full}, {"BNAM", imad_bnam},
                {"TNAM", imad_tnam}, {"NAM3", imad_nam3}, {k_hdr_mult_iad, imad_mult},
                {"@IAD", imad_iad}, {"NAM5", imad_nam5},
                {"NAM6", imad_nam6}});
  write_record(out, "IMAD", 3002,
               {{"EDID", zstr("RadicleImageAdapterTwo")}, {"DNAM", zeros(244)}, {"NAM6", zeros(8)}},
               113);

  Bytes zoom = zeros(24);
  put(zoom, 0, f32(1.0f));
  put(zoom, 4, u32(2));
  put(zoom, 8, u32(3001));
  write_record(out, "ZOOM", 3003, {{"EDID", zstr("RadicleZoom")}, {"GNAM", zoom}});

  Bytes camera = zeros(64);
  put(camera, 0, u32(2));
  put(camera, 4, u32(2));
  put(camera, 12, u32(5));
  put(camera, 24, f32(1.0f));
  put(camera, 52, f32(4.0f));
  put(camera, 60, f32(-2.0f));
  write_record(out, "CAMS", 3004, {{"EDID", zstr("RadicleCameraShot")}, {"DATA", camera}, {"MNAM", u32(3001)}});
  Bytes camera_short = zeros(44);
  put(camera_short, 0, u32(1));
  write_record(out, "CAMS", 3005, {{"EDID", zstr("RadicleCameraShotShort")}, {"DATA", camera_short}});

  Bytes spgd = zeros(96);
  put(spgd, 0, f32(9.0f));
  put(spgd, 80, u32(4096));
  put(spgd, 88, f32(7.5f));
  write_record(out, "SPGD", 3006, {{"EDID", zstr("RadicleParticleGeometry")}, {"DATA", spgd}, {"MNAM", zstr("particle.dds")}});

  Bytes gdry = zeros(60);
  put(gdry, 24, f32(1.5f));
  write_record(out, "GDRY", 3007, {{"EDID", zstr("RadicleGodRays")}, {"DATA", gdry}});

  Bytes dnam = zeros(24);
  put(dnam, 14, Bytes{std::byte{0x00}, std::byte{0x40}});
  put(dnam, 16, f32(0.5f));
  put(dnam, 20, f32(0.25f));
  write_record(out, "IMGS", 3008,
               {{"EDID", zstr("RadicleImageSpace")}, {"ENAM", zeros(56)}, {"HNAM", zeros(36)}, {"CNAM", zeros(12)},
                {"TNAM", zeros(16)}, {"DNAM", dnam}, {"TX00", zstr("lut.dds")}});
  write_record(out, "IMGS", 3009, {{"EDID", zstr("RadicleImageSpaceShort")}, {"DNAM", zeros(16)}});

  write_record(out, "EFSH", 3010,
               {{"EDID", zstr("RadicleEffectShader")}, {"ICON", zstr("fill.dds")}, {"NAM7", zstr("holes.dds")},
                {"NAM8", zstr("membrane.dds")}, {"DATA", Bytes{}}, {"DNAM", zeros(157)}});
  return out.bytes;
}

fo4::Plugin load() { return fo4::read_plugin(make_plugin(), "Patch.esp"); }

fo4::schema::ScalarFieldDescriptor scalar(std::string_view path) {
  const auto field = fo4::schema::scalar_field_descriptor(path);
  if (!field) throw std::invalid_argument("unknown scalar row " + std::string(path));
  return *field;
}

fo4::schema::FormLinkFieldDescriptor link(std::string_view path) {
  const auto field = fo4::schema::formlink_field_descriptor(path);
  if (!field) throw std::invalid_argument("unknown FormLink row " + std::string(path));
  return *field;
}

Bytes payload(const fo4::Plugin& plugin, std::uint32_t local, std::string_view signature) {
  const auto* record = fo4::find_record(plugin, key(local));
  if (record == nullptr) return {};
  for (const auto& subrecord : record->subrecords) {
    if (subrecord.signature == fo4::Signature::from_string(signature)) return subrecord.data;
  }
  return {};
}

std::string decoded_value(const fo4::Plugin& plugin, std::uint32_t local, std::string_view path) {
  const auto field = scalar(path);
  const auto data = payload(plugin, local, field.scalar.subrecord.to_string());
  return fo4::schema::decode_scalar(fo4::schema::scalar_field_bytes(data, field.scalar), field.scalar);
}

constexpr std::pair<std::uint32_t, std::string_view> k_records[] = {
    {3001, "IMAD"}, {3002, "IMAD"}, {3003, "ZOOM"}, {3004, "CAMS"}, {3005, "CAMS"}, {3006, "SPGD"},
    {3007, "GDRY"}, {3008, "IMGS"}, {3009, "IMGS"}, {3010, "EFSH"}};

}  // namespace

TEST(ImageFxSynthetic, EveryNewRowReadsItsFixturePayload) {
  const auto plugin = load();
  std::size_t decoded = 0;
  for (const auto& [local, signature] : k_records) {
    const auto record = fo4::Signature::from_string(signature);
    for (const auto& field : fo4::schema::scalar_fields(record)) {
      if (fo4::record_field_count(plugin, key(local), field.path) != 1u) continue;
      const auto data = payload(plugin, local, field.scalar.subrecord.to_string());
      const auto bytes = fo4::schema::scalar_field_bytes(data, field.scalar);
      if (bytes.empty()) continue;
      static_cast<void>(fo4::schema::decode_scalar(bytes, field.scalar));
      ++decoded;
    }
  }
  EXPECT_GT(decoded, 0u);
  EXPECT_EQ(decoded_value(plugin, 3006, "SPGD\\GravityVelocity"), "9");
  EXPECT_EQ(decoded_value(plugin, 3004, "CAMS\\RotationOffsetZ"), "-2");
}

TEST(ImageFxSynthetic, RowsWithMinimumPayloadStayAbsentOnShortSubrecords) {
  auto plugin = load();
  EXPECT_EQ(fo4::record_field_count(plugin, key(3004), "CAMS\\RotationOffsetZ"), 1u);
  EXPECT_EQ(fo4::record_field_count(plugin, key(3005), "CAMS\\NearTargetDistance"), 1u);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(3005), scalar("CAMS\\LocationSpring"), 0, "1"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(3005), scalar("CAMS\\RotationOffsetX"), 0, "1"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::record_field_count(plugin, key(3006), "SPGD\\BoxSize"), 1u);
  EXPECT_EQ(fo4::record_field_count(plugin, key(3008), "IMGS\\VignetteStrength"), 1u);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(3009), scalar("IMGS\\VignetteRadius"), 0, "1"),
            fo4::ScalarMutationStatus::subrecord_missing);
}

TEST(ImageFxSynthetic, InterpolatorMembersEditOneStrideElement) {
  auto plugin = load();
  EXPECT_EQ(fo4::record_field_count(plugin, key(3001), "IMAD\\TintColorTime"), 2u);
  EXPECT_EQ(fo4::record_field_count(plugin, key(3001), "IMAD\\FadeColorAlpha"), 1u);
  EXPECT_EQ(fo4::record_field_count(plugin, key(3002), "IMAD\\TintColorTime"), 0u);
  EXPECT_EQ(decoded_value(plugin, 3001, "IMAD\\TintColorTime"), "0.5");
  EXPECT_EQ(decoded_value(plugin, 3001, "IMAD\\TintColorAlpha"), "1");
  EXPECT_EQ(decoded_value(plugin, 3001, "IMAD\\FadeColorTime"), "1.5");

  EXPECT_EQ(fo4::set_record_scalar(plugin, key(3001), scalar("IMAD\\TintColorTime"), 1, "3.5"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(3001), scalar("IMAD\\FadeColorTime"), 0, "4.5"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(3001), scalar("IMAD\\TintColorTime"), 2, "1"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(3002), scalar("IMAD\\FadeColorTime"), 0, "1"),
            fo4::ScalarMutationStatus::subrecord_missing);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(payload(reread, 3001, "TNAM").size(), 40u);
  EXPECT_EQ(slice(payload(reread, 3001, "TNAM"), 0, 4), f32(0.5f));
  EXPECT_EQ(slice(payload(reread, 3001, "TNAM"), 16, 4), f32(1.0f));
  EXPECT_EQ(slice(payload(reread, 3001, "TNAM"), 20, 4), f32(3.5f));
  EXPECT_EQ(payload(reread, 3001, "NAM3").size(), 20u);
  EXPECT_EQ(slice(payload(reread, 3001, "NAM3"), 0, 4), f32(4.5f));
}

TEST(ImageFxSynthetic, TimeInterpolatorArraysDecodeAndEditOneStrideElement) {
  auto plugin = load();
  EXPECT_EQ(fo4::record_field_count(plugin, key(3001), "IMAD\\BlurRadiusTime"), 2u);
  EXPECT_EQ(fo4::record_field_count(plugin, key(3001), "IMAD\\HDREyeAdaptSpeedAddValue"), 2u);
  EXPECT_EQ(fo4::record_field_count(plugin, key(3002), "IMAD\\BlurRadiusTime"), 0u);
  EXPECT_EQ(decoded_value(plugin, 3001, "IMAD\\BlurRadiusTime"), "0.25");
  EXPECT_EQ(decoded_value(plugin, 3001, "IMAD\\BlurRadiusValue"), "2.5");
  EXPECT_EQ(decoded_value(plugin, 3001, "IMAD\\HDREyeAdaptSpeedAddTime"), "0.5");
  EXPECT_EQ(decoded_value(plugin, 3001, "IMAD\\HDREyeAdaptSpeedAddValue"), "6");

  EXPECT_EQ(fo4::set_record_scalar(plugin, key(3001), scalar("IMAD\\BlurRadiusValue"), 1, "7.5"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(3001), scalar("IMAD\\HDREyeAdaptSpeedAddValue"), 1, "8.5"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(3001), scalar("IMAD\\BlurRadiusTime"), 2, "9"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(3002), scalar("IMAD\\BlurRadiusTime"), 0, "9"),
            fo4::ScalarMutationStatus::subrecord_missing);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(payload(reread, 3001, "BNAM").size(), 16u);
  EXPECT_EQ(slice(payload(reread, 3001, "BNAM"), 0, 4), f32(0.25f));
  EXPECT_EQ(slice(payload(reread, 3001, "BNAM"), 4, 4), f32(2.5f));
  EXPECT_EQ(slice(payload(reread, 3001, "BNAM"), 8, 4), f32(0.75f));
  EXPECT_EQ(slice(payload(reread, 3001, "BNAM"), 12, 4), f32(7.5f));
  EXPECT_EQ(slice(payload(reread, 3001, "@IAD"), 4, 4), f32(6.0f));
  EXPECT_EQ(slice(payload(reread, 3001, "@IAD"), 12, 4), f32(8.5f));
}

TEST(ImageFxSynthetic, HdrMultInterpolatorRowsDecodeAndEditOneStrideElement) {
  auto plugin = load();
  EXPECT_EQ(fo4::record_field_count(plugin, key(3001), "IMAD\\HDREyeAdaptSpeedMultTime"), 2u);
  EXPECT_EQ(fo4::record_field_count(plugin, key(3001), "IMAD\\HDREyeAdaptSpeedMultValue"), 2u);
  EXPECT_EQ(fo4::record_field_count(plugin, key(3002), "IMAD\\HDREyeAdaptSpeedMultTime"), 0u);
  EXPECT_EQ(decoded_value(plugin, 3001, "IMAD\\HDREyeAdaptSpeedMultTime"), "0.125");
  EXPECT_EQ(decoded_value(plugin, 3001, "IMAD\\HDREyeAdaptSpeedMultValue"), "3.5");
  EXPECT_EQ(decoded_value(plugin, 3001, "IMAD\\HDREyeAdaptSpeedAddValue"), "6");

  EXPECT_EQ(fo4::set_record_scalar(plugin, key(3001), scalar("IMAD\\HDREyeAdaptSpeedMultValue"), 1, "9.5"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(3001), scalar("IMAD\\HDREyeAdaptSpeedMultTime"), 2, "1"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(3002), scalar("IMAD\\HDREyeAdaptSpeedMultTime"), 0, "1"),
            fo4::ScalarMutationStatus::subrecord_missing);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto mult = payload(reread, 3001, k_hdr_mult_iad);
  EXPECT_EQ(mult.size(), 16u);
  EXPECT_EQ(slice(mult, 0, 4), f32(0.125f));
  EXPECT_EQ(slice(mult, 4, 4), f32(3.5f));
  EXPECT_EQ(slice(mult, 8, 4), f32(1.25f));
  EXPECT_EQ(slice(mult, 12, 4), f32(9.5f));
  EXPECT_EQ(slice(payload(reread, 3001, "@IAD"), 4, 4), f32(6.0f));
  EXPECT_EQ(slice(payload(reread, 3001, "@IAD"), 12, 4), f32(1.5f));
}

TEST(ImageFxSynthetic, DepthOfFieldVignetteInterpolatorsAreGatedByFormVersion118) {
  auto plugin = load();
  EXPECT_EQ(fo4::record_field_count(plugin, key(3001), "IMAD\\DepthOfFieldVignetteRadiusTime"), 1u);
  EXPECT_EQ(fo4::record_field_count(plugin, key(3001), "IMAD\\DepthOfFieldVignetteStrengthValue"), 1u);
  EXPECT_EQ(decoded_value(plugin, 3001, "IMAD\\DepthOfFieldVignetteRadiusValue"), "1.5");
  EXPECT_EQ(decoded_value(plugin, 3001, "IMAD\\DepthOfFieldVignetteStrengthValue"), "0.5");
  EXPECT_EQ(fo4::record_field_count(plugin, key(3002), "IMAD\\DepthOfFieldVignetteStrengthTime"), 1u);
  EXPECT_EQ(fo4::record_field_count(plugin, key(3002), "IMAD\\DepthOfFieldVignetteRadiusTime"), 0u);

  const auto before = payload(plugin, 3002, "NAM6");
  EXPECT_EQ(before.size(), 8u);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(3002), scalar("IMAD\\DepthOfFieldVignetteStrengthTime"), 0, "1"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(payload(plugin, 3002, "NAM6"), before);

  EXPECT_EQ(fo4::set_record_scalar(plugin, key(3001), scalar("IMAD\\DepthOfFieldVignetteStrengthTime"), 0, "4"),
            fo4::ScalarMutationStatus::updated);
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(payload(reread, 3001, "NAM5").size(), 8u);
  EXPECT_EQ(slice(payload(reread, 3001, "NAM5"), 0, 4), f32(0.5f));
  EXPECT_EQ(slice(payload(reread, 3001, "NAM5"), 4, 4), f32(1.5f));
  EXPECT_EQ(slice(payload(reread, 3001, "NAM6"), 0, 4), f32(4.0f));
  EXPECT_EQ(slice(payload(reread, 3001, "NAM6"), 4, 4), f32(0.5f));
}

TEST(ImageFxSynthetic, EditsOneRowPerRecordAndRereads) {
  auto plugin = load();
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(3004), scalar("CAMS\\Flags"), 0, "3"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(3006), scalar("SPGD\\Type"), 0, "1"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(3007), scalar("GDRY\\Intensity"), 0, "2.5"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(3008), scalar("IMGS\\SkyBlurRadius"), 0, "16672"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(3003), scalar("ZOOM\\Overlay"), 0, "5"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(3001), scalar("IMAD\\DoFFlags"), 0, "6"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_string(plugin, key(3010), "EFSH\\FillTexture", 0, "edited.dds"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_string(plugin, key(3008), "IMGS\\LUT", 0, "edited_lut.dds"),
            fo4::ScalarMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(slice(payload(reread, 3004, "DATA"), 12, 4), u32(3));
  EXPECT_EQ(slice(payload(reread, 3006, "DATA"), 72, 4), u32(1));
  EXPECT_EQ(slice(payload(reread, 3007, "DATA"), 24, 4), f32(2.5f));
  EXPECT_EQ(slice(payload(reread, 3008, "DNAM"), 14, 2), (Bytes{std::byte{0x20}, std::byte{0x41}}));
  EXPECT_EQ(slice(payload(reread, 3003, "GNAM"), 4, 4), u32(5));
  EXPECT_EQ(slice(payload(reread, 3001, "DNAM"), 225, 1), Bytes{std::byte{6}});
  EXPECT_EQ(payload(reread, 3010, "ICON"), zstr("edited.dds"));
  EXPECT_EQ(payload(reread, 3008, "TX00"), zstr("edited_lut.dds"));
  EXPECT_EQ(fo4::record_field_count(reread, key(3008), "IMGS\\VignetteRadius"), 1u);
}

TEST(ImageFxSynthetic, RetargetsFormLinksToSecondRecord) {
  auto plugin = load();
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key(3003), link("ZOOM\\ImageSpaceModifier"), key(3002)),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key(3004), link("CAMS\\ImageSpaceModifier"), key(3002)),
            fo4::FormLinkFieldMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(slice(payload(reread, 3003, "GNAM"), 8, 4), u32(3002));
  EXPECT_EQ(payload(reread, 3004, "MNAM"), u32(3002));
  EXPECT_EQ(fo4::record_field_count(reread, key(3004), "CAMS\\ImageSpaceModifier"), 1u);
}

TEST(ImageFxSynthetic, ShortPayloadRowsRejectWritesWithoutTouchingPlugin) {
  auto plugin = load();
  const auto original = fo4::test::original_bytes(plugin);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(3005), scalar("CAMS\\RotationOffsetX"), 0, "1"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(3005), scalar("CAMS\\LocationSpring"), 0, "1"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(3009), scalar("IMGS\\VignetteRadius"), 0, "1"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);
}
