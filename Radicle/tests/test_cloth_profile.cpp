#include "fo4/cloth/cloth_profile.hpp"
#include "fo4/error.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using fo4::cloth::BoneEntry;
using fo4::cloth::BoneScale;
using fo4::cloth::ClothProfile;
using fo4::cloth::cloth_profile_path;
using fo4::cloth::compare_cloth_bones;
using fo4::cloth::parse_cloth_profile;
using fo4::cloth::serialize_cloth_profile;

namespace {

std::string shipped_text() {
  return "[\r\n"
         "  {\r\n"
         "    \"Name\": \"Chest_Rear_Skin\",\r\n"
         "    \"Scale\": {\r\n"
         "      \"x\": 1.0000003576278687,\r\n"
         "      \"y\": 1.3468589782714844,\r\n"
         "      \"z\": 0.7103740572929382\r\n"
         "    }\r\n"
         "  },\r\n"
         "  {\r\n"
         "    \"Name\": \"Belly_skin\",\r\n"
         "    \"Scale\": {\r\n"
         "      \"x\": 1.0,\r\n"
         "      \"y\": 1.0,\r\n"
         "      \"z\": 1.0215158462524414\r\n"
         "    }\r\n"
         "  }\r\n"
         "]";
}

ClothProfile shipped_profile() {
  ClothProfile profile;
  profile.bones.push_back(BoneEntry{"Chest_Rear_Skin", BoneScale{1.0000003576278687, 1.3468589782714844, 0.7103740572929382}});
  profile.bones.push_back(BoneEntry{"Belly_skin", BoneScale{1.0, 1.0, 1.0215158462524414}});
  return profile;
}

std::string one_entry(const std::string& body) {
  return "[{\"Name\":\"Bone_skin\",\"Scale\":" + body + "}]";
}

}  // namespace

// Detects: a reader that must accept the shipped shape and report every name and axis.
TEST(ClothProfile, ParsesTheShippedShape) {
  const auto profile = parse_cloth_profile(shipped_text());
  EXPECT_EQ(profile, shipped_profile());
}

// Detects: a writer that must reproduce the shipped bytes, including CRLF and the shortest float digits.
TEST(ClothProfile, ReproducesTheShippedBytes) {
  const auto text = serialize_cloth_profile(shipped_profile());
  EXPECT_EQ(text, shipped_text());
  EXPECT_EQ(parse_cloth_profile(text), shipped_profile());
}

// Detects: a writer that must keep bone order instead of sorting names.
TEST(ClothProfile, PreservesBoneOrder) {
  ClothProfile profile = shipped_profile();
  std::swap(profile.bones[0], profile.bones[1]);
  const auto parsed = parse_cloth_profile(serialize_cloth_profile(profile));
  EXPECT_EQ(parsed.bones[0].name, "Belly_skin");
  EXPECT_EQ(parsed.bones[1].name, "Chest_Rear_Skin");
}

// Detects: an empty profile that must round-trip as a bare empty array.
TEST(ClothProfile, SerializesAnEmptyProfileAsAnEmptyArray) {
  ClothProfile profile;
  EXPECT_EQ(serialize_cloth_profile(profile), "[]");
  EXPECT_EQ(parse_cloth_profile("[]"), profile);
}

// Detects: the measured pairing invariant, where a profile must map from its outfit NIF beside it.
TEST(ClothProfile, MapsAnOutfitNifPathToItsProfile) {
  EXPECT_EQ(cloth_profile_path("Meshes\\Armor\\Outfit.nif"), "Meshes\\Armor\\Outfit.sclp");
  EXPECT_EQ(cloth_profile_path("Meshes/Armor/Outfit.nif"), "Meshes/Armor/Outfit.sclp");
  EXPECT_EQ(cloth_profile_path("Meshes\\Armor\\Outfit.NIF"), "Meshes\\Armor\\Outfit.sclp");
}

// Detects: paths that must be refused because they do not name an outfit NIF.
TEST(ClothProfile, RejectsPathsWithoutANifExtension) {
  EXPECT_THROW(cloth_profile_path("Meshes\\v1.2\\Outfit"), std::invalid_argument);
  EXPECT_THROW(cloth_profile_path("Meshes\\Armor\\Outfit.dds"), std::invalid_argument);
  EXPECT_THROW(cloth_profile_path(""), std::invalid_argument);
}

// Detects: a top-level value that must be an array, and truncated JSON that must not be accepted.
TEST(ClothProfile, RejectsNonArrayDocuments) {
  EXPECT_THROW(parse_cloth_profile("{}"), fo4::parse_error);
  EXPECT_THROW(parse_cloth_profile("["), fo4::parse_error);
  EXPECT_THROW(parse_cloth_profile("[1]"), fo4::parse_error);
}

// Detects: bone object keys beyond Name and Scale, which must be refused instead of ignored.
TEST(ClothProfile, RejectsUnexpectedBoneKeys) {
  EXPECT_THROW(parse_cloth_profile("[{\"Name\":\"Bone_skin\",\"Scale\":{\"x\":1,\"y\":1,\"z\":1},\"Extra\":1}]"),
               fo4::parse_error);
}

// Detects: a bone entry without a Scale object, which must be refused.
TEST(ClothProfile, RejectsAMissingScale) {
  EXPECT_THROW(parse_cloth_profile("[{\"Name\":\"Bone_skin\"}]"), fo4::parse_error);
}

// Detects: a missing, empty or non-string bone name, which must all be refused.
TEST(ClothProfile, RejectsBadBoneNames) {
  EXPECT_THROW(parse_cloth_profile("[{\"Scale\":{\"x\":1,\"y\":1,\"z\":1}}]"), fo4::parse_error);
  EXPECT_THROW(parse_cloth_profile("[{\"Name\":\"\",\"Scale\":{\"x\":1,\"y\":1,\"z\":1}}]"), fo4::parse_error);
  EXPECT_THROW(parse_cloth_profile("[{\"Name\":7,\"Scale\":{\"x\":1,\"y\":1,\"z\":1}}]"), fo4::parse_error);
}

// Detects: a repeated bone name inside one profile, which must be refused.
TEST(ClothProfile, RejectsRepeatedBoneNames) {
  const std::string text =
      "[{\"Name\":\"Bone_skin\",\"Scale\":{\"x\":1,\"y\":1,\"z\":1}},"
      "{\"Name\":\"Bone_skin\",\"Scale\":{\"x\":1,\"y\":1,\"z\":1}}]";
  EXPECT_THROW(parse_cloth_profile(text), fo4::parse_error);
}

// Detects: scale objects with a missing or extra axis, which must not be filled in silently.
TEST(ClothProfile, RejectsBadScaleAxes) {
  EXPECT_THROW(parse_cloth_profile(one_entry("{\"x\":1,\"y\":1}")), fo4::parse_error);
  EXPECT_THROW(parse_cloth_profile(one_entry("{\"x\":1,\"y\":1,\"z\":1,\"w\":1}")), fo4::parse_error);
  EXPECT_THROW(parse_cloth_profile(one_entry("{\"x\":1,\"y\":1,\"z\":\"1\"}")), fo4::parse_error);
}

// Detects: a zero or negative axis scale, which must be refused as a degenerate cloth scale.
TEST(ClothProfile, RejectsNonPositiveScales) {
  EXPECT_THROW(parse_cloth_profile(one_entry("{\"x\":0,\"y\":1,\"z\":1}")), fo4::parse_error);
  EXPECT_THROW(parse_cloth_profile(one_entry("{\"x\":1,\"y\":-1,\"z\":1}")), fo4::parse_error);
}

// Detects: an over-large profile that must be refused before it builds an unbounded bone list.
TEST(ClothProfile, RejectsMoreBonesThanTheReaderAccepts) {
  ClothProfile profile;
  for (int index = 0; index < 1025; ++index) {
    profile.bones.push_back(BoneEntry{"Bone_" + std::to_string(index), BoneScale{}});
  }
  EXPECT_THROW(parse_cloth_profile(serialize_cloth_profile(profile)), fo4::parse_error);
}

// Detects: the shipped mixed-case suffix, which must be accepted because names are bone identifiers.
TEST(ClothProfile, AcceptsMixedCaseBoneNames) {
  const auto profile =
      parse_cloth_profile("[{\"Name\":\"Chest_Rear_Skin\",\"Scale\":{\"x\":1,\"y\":1,\"z\":1}}]");
  ASSERT_EQ(profile.bones.size(), 1u);
  EXPECT_EQ(profile.bones[0].name, "Chest_Rear_Skin");
}

// Detects: a reconciliation that must report both sides in their input order.
TEST(ClothProfile, ReportsBothSidesInTheirInputOrder) {
  const auto profile = parse_cloth_profile(
      "[{\"Name\":\"A_skin\",\"Scale\":{\"x\":1,\"y\":1,\"z\":1}},"
      "{\"Name\":\"B_skin\",\"Scale\":{\"x\":1,\"y\":1,\"z\":1}},"
      "{\"Name\":\"C_skin\",\"Scale\":{\"x\":1,\"y\":1,\"z\":1}}]");
  const std::vector<std::string> mesh{"B_skin", "C_skin", "D_skin"};
  const auto pairing = compare_cloth_bones(profile, mesh);
  EXPECT_EQ(pairing.profile_only, (std::vector<std::string>{"A_skin"}));
  EXPECT_EQ(pairing.mesh_only, (std::vector<std::string>{"D_skin"}));
}

// Detects: a comparison that must be exact rather than case-folded, so a case difference is reported.
TEST(ClothProfile, ReportsACaseDifference) {
  const auto profile =
      parse_cloth_profile("[{\"Name\":\"Chest_Rear_Skin\",\"Scale\":{\"x\":1,\"y\":1,\"z\":1}}]");
  const std::vector<std::string> mesh{"Chest_Rear_skin"};
  const auto pairing = compare_cloth_bones(profile, mesh);
  EXPECT_EQ(pairing.profile_only, (std::vector<std::string>{"Chest_Rear_Skin"}));
  EXPECT_EQ(pairing.mesh_only, (std::vector<std::string>{"Chest_Rear_skin"}));
}

// Detects: empty and repeated mesh names, which must not produce duplicate or empty findings.
TEST(ClothProfile, IgnoresEmptyAndRepeatedMeshNames) {
  const auto profile = parse_cloth_profile("[{\"Name\":\"A_skin\",\"Scale\":{\"x\":1,\"y\":1,\"z\":1}}]");
  const std::vector<std::string> mesh{"", "A_skin", "A_skin", "B_skin", "B_skin"};
  const auto pairing = compare_cloth_bones(profile, mesh);
  EXPECT_TRUE(pairing.profile_only.empty());
  EXPECT_EQ(pairing.mesh_only, (std::vector<std::string>{"B_skin"}));
}

// Detects: matching sets that must report nothing, and disjoint sets that must report every bone.
TEST(ClothProfile, ReportsNothingForMatchingAndEverythingForDisjointSets) {
  const auto profile = parse_cloth_profile("[{\"Name\":\"A_skin\",\"Scale\":{\"x\":1,\"y\":1,\"z\":1}}]");
  const std::vector<std::string> matching{"A_skin"};
  const auto clean = compare_cloth_bones(profile, matching);
  EXPECT_TRUE(clean.profile_only.empty());
  EXPECT_TRUE(clean.mesh_only.empty());

  const std::vector<std::string> disjoint{"B_skin"};
  const auto broken = compare_cloth_bones(profile, disjoint);
  EXPECT_EQ(broken.profile_only, (std::vector<std::string>{"A_skin"}));
  EXPECT_EQ(broken.mesh_only, (std::vector<std::string>{"B_skin"}));
}
