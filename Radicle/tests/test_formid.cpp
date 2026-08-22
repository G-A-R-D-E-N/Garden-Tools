#include "fo4/formid.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using fo4::FormKey;
using fo4::LocalFormId;
using fo4::MasterDescriptor;
using fo4::PluginFormIdContext;
using fo4::PluginKey;
using fo4::PluginKind;
using fo4::RawFormId;
using fo4::RuntimeFormIdContext;

namespace {

PluginFormIdContext sample_plugin_context() {
  PluginFormIdContext ctx;
  ctx.current_plugin = PluginKey::from_filename("Patch.esp");
  ctx.masters = {
      MasterDescriptor{PluginKey::from_filename("Fallout4.esm"), PluginKind::full},
      MasterDescriptor{PluginKey::from_filename("DLC01.esm"), PluginKind::full},
      MasterDescriptor{PluginKey::from_filename("RadiationOverhaul.esp"), PluginKind::full},
      MasterDescriptor{PluginKey::from_filename("AnotherMod.esp"), PluginKind::full},
  };
  return ctx;
}

}  // namespace

TEST(FormKey, ParsesSymbolicIdentity) {
  const auto key = FormKey::parse("00009A:RadiationOverhaul.esp");
  EXPECT_EQ(key.owner(), PluginKey::from_filename("RadiationOverhaul.esp"));
  EXPECT_EQ(key.local(), LocalFormId{0x9A});
  EXPECT_EQ(key.to_string(), "00009A:RadiationOverhaul.esp");
}

TEST(FormKey, PluginNamesAreCaseInsensitive) {
  EXPECT_EQ(PluginKey::from_filename("Fallout4.esm"),
            PluginKey::from_filename("fallout4.ESM"));
}

TEST(FormKey, EslExtensionLooksLight) {
  EXPECT_TRUE(PluginKey::from_filename("LightDependency.esl").looks_light());
  EXPECT_TRUE(PluginKey::from_filename("Light.ESL").looks_light());
  EXPECT_FALSE(PluginKey::from_filename("RadiationOverhaul.esp").looks_light());
  EXPECT_FALSE(PluginKey::from_filename("Fallout4.esm").looks_light());
}

TEST(FormId, ResolvesOnDiskMasterIndexToFormKey) {
  const auto ctx = sample_plugin_context();
  const auto key = fo4::resolve_plugin_form_id(RawFormId{0x0200009A}, ctx);
  EXPECT_EQ(key, FormKey::parse("00009A:RadiationOverhaul.esp"));
}

TEST(FormId, ResolvesLocalRecordsAgainstTheCurrentPlugin) {
  const auto ctx = sample_plugin_context();
  const auto key = fo4::resolve_plugin_form_id(RawFormId{0x04000800}, ctx);
  EXPECT_EQ(key, FormKey::parse("000800:Patch.esp"));
}

TEST(FormId, MasterRemovalDoesNotCorruptRemainingReferences) {
  auto ctx = sample_plugin_context();
  const auto radiation = fo4::resolve_plugin_form_id(RawFormId{0x0200009A}, ctx);
  const auto another = fo4::resolve_plugin_form_id(RawFormId{0x03000010}, ctx);
  const auto local = fo4::resolve_plugin_form_id(RawFormId{0x04000800}, ctx);

  ctx.masters.erase(ctx.masters.begin() + 1);

  EXPECT_EQ(fo4::encode_plugin_form_id(radiation, ctx), RawFormId{0x0100009A});
  EXPECT_EQ(fo4::encode_plugin_form_id(another, ctx), RawFormId{0x02000010});
  EXPECT_EQ(fo4::encode_plugin_form_id(local, ctx), RawFormId{0x03000800});
}

TEST(FormId, LightCurrentPluginMasksLocalToTwelveBits) {
  PluginFormIdContext ctx;
  ctx.current_plugin = PluginKey::from_filename("FlaggedLight.esp");
  ctx.current_kind = PluginKind::light;
  ctx.masters = {
      MasterDescriptor{PluginKey::from_filename("Fallout4.esm"), PluginKind::full},
  };

  const auto key = FormKey{ctx.current_plugin, LocalFormId{0x812}};
  EXPECT_EQ(fo4::encode_plugin_form_id(key, ctx), RawFormId{0x01000812});

  const auto fat = FormKey{ctx.current_plugin, LocalFormId{0x10812}};
  EXPECT_EQ(fo4::encode_plugin_form_id(fat, ctx), RawFormId{0x01000812});
}

TEST(FormId, EspFlaggedLightMasterMasksLocalToTwelveBits) {
  PluginFormIdContext ctx;
  ctx.current_plugin = PluginKey::from_filename("Patch.esp");
  ctx.masters = {
      MasterDescriptor{PluginKey::from_filename("Fallout4.esm"), PluginKind::full},
      MasterDescriptor{PluginKey::from_filename("FlaggedLight.esp"), PluginKind::light},
  };

  const auto key =
      FormKey{PluginKey::from_filename("FlaggedLight.esp"), LocalFormId{0x10812}};
  EXPECT_EQ(fo4::encode_plugin_form_id(key, ctx), RawFormId{0x01000812});
  EXPECT_FALSE(PluginKey::from_filename("FlaggedLight.esp").looks_light());
}

TEST(FormId, LightMasterUsesMastOrdinalOnDiskNotFeSlot) {
  PluginFormIdContext ctx;
  ctx.current_plugin = PluginKey::from_filename("ConditionPatch.esp");
  ctx.masters = {
      MasterDescriptor{PluginKey::from_filename("Fallout4.esm"), PluginKind::full},
      MasterDescriptor{PluginKey::from_filename("FullDependency.esm"), PluginKind::full},
      MasterDescriptor{PluginKey::from_filename("LightDependency.esl"), PluginKind::light},
  };

  const auto key = FormKey{PluginKey::from_filename("LightDependency.esl"), LocalFormId{0x812}};
  EXPECT_EQ(fo4::encode_plugin_form_id(key, ctx), RawFormId{0x02000812});
  EXPECT_EQ(fo4::resolve_plugin_form_id(RawFormId{0x02000812}, ctx), key);
}

TEST(FormId, RuntimeFeEncodingIsRejectedInOrdinaryOnDiskPluginContext) {
  PluginFormIdContext ctx;
  ctx.current_plugin = PluginKey::from_filename("FlaggedLight.esp");
  ctx.current_kind = PluginKind::light;
  ctx.masters = {
      MasterDescriptor{PluginKey::from_filename("Fallout4.esm"), PluginKind::full},
  };

  EXPECT_THROW(
      fo4::resolve_plugin_form_id(RawFormId{0xFE000812}, ctx),
      fo4::FormIdError);
}

TEST(FormId, OnDiskFeByteRemainsAValidMastOrdinalAtTheTableBoundary) {
  PluginFormIdContext ctx;
  ctx.current_plugin = PluginKey::from_filename("HugePatch.esp");
  ctx.masters.reserve(0xFEu);
  for (std::size_t index = 0; index < 0xFEu; ++index) {
    ctx.masters.push_back(MasterDescriptor{
        PluginKey::from_filename("Master" + std::to_string(index) + ".esm"),
        PluginKind::full});
  }

  const auto key = fo4::resolve_plugin_form_id(RawFormId{0xFE001234}, ctx);
  EXPECT_EQ(key, FormKey::parse("001234:HugePatch.esp"));
  EXPECT_EQ(fo4::encode_plugin_form_id(key, ctx), RawFormId{0xFE001234});
}

TEST(FormId, ResolvingLightOwnerPreservesFullLocalUntilEncodeBoundary) {
  PluginFormIdContext ctx;
  ctx.current_plugin = PluginKey::from_filename("FlaggedLight.esp");
  ctx.current_kind = PluginKind::light;
  ctx.masters = {
      MasterDescriptor{PluginKey::from_filename("Fallout4.esm"), PluginKind::full},
  };

  const auto key = fo4::resolve_plugin_form_id(RawFormId{0x01010812}, ctx);
  EXPECT_EQ(key.local(), LocalFormId{0x010812});
  EXPECT_EQ(fo4::encode_plugin_form_id(key, ctx), RawFormId{0x01000812});
}

TEST(FormId, RuntimeFullCompileIndexIsNotCanonical) {
  RuntimeFormIdContext runtime;
  runtime.full_plugins.resize(0x57);
  runtime.full_plugins[0x56] =
      MasterDescriptor{PluginKey::from_filename("RadiationOverhaul.esp"), PluginKind::full};

  const auto key = fo4::resolve_runtime_form_id(RawFormId{0x5600009A}, runtime);
  EXPECT_EQ(key, FormKey::parse("00009A:RadiationOverhaul.esp"));
}

TEST(FormId, RuntimeHighestFullIndexIsFdBecauseFeIsReservedForLightPlugins) {
  RuntimeFormIdContext runtime;
  runtime.full_plugins.resize(0xFEu);
  runtime.full_plugins[0xFD] =
      MasterDescriptor{PluginKey::from_filename("LastFull.esp"), PluginKind::full};

  EXPECT_EQ(
      fo4::resolve_runtime_form_id(RawFormId{0xFD001234}, runtime),
      FormKey::parse("001234:LastFull.esp"));
  EXPECT_THROW(
      fo4::resolve_runtime_form_id(RawFormId{0xFE001234}, runtime),
      fo4::FormIdError);
}

TEST(FormId, RuntimeLightUsesFeSlotAndTwelveBitLocal) {
  RuntimeFormIdContext runtime;
  runtime.light_plugins.resize(0x57);
  runtime.light_plugins[0x56] =
      MasterDescriptor{PluginKey::from_filename("RadiationOverhaul.esl"), PluginKind::light};

  const auto key = fo4::resolve_runtime_form_id(RawFormId{0xFE05609A}, runtime);
  EXPECT_EQ(key, FormKey::parse("00009A:RadiationOverhaul.esl"));
}

TEST(FormId, RuntimeLightSupportsTheHighestTwelveBitSlot) {
  RuntimeFormIdContext runtime;
  runtime.light_plugins.resize(0x1000u);
  runtime.light_plugins[0xFFF] =
      MasterDescriptor{PluginKey::from_filename("LastLight.esl"), PluginKind::light};

  EXPECT_EQ(
      fo4::resolve_runtime_form_id(RawFormId{0xFEFFFABC}, runtime),
      FormKey::parse("000ABC:LastLight.esl"));
}

TEST(FormId, RejectsUnknownMasterIndex) {
  const auto ctx = sample_plugin_context();
  EXPECT_THROW(fo4::resolve_plugin_form_id(RawFormId{0x08000001}, ctx), fo4::FormIdError);
}
