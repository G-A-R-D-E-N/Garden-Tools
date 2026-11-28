#include "fo4/hkx/character_save.hpp"

#include "fo4/archive/assets.hpp"
#include "fo4/hkx/character_workspace.hpp"
#include "fo4/hkx/skeleton_mapper.hpp"
#include "fo4/io/sha256.hpp"
#include "hkx_clip_fixture.hpp"
#include "hkx_mapper_fixture.hpp"
#include "hkx_physics_fixture.hpp"
#include "temp_data_dir.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace fo4::hkx;
using namespace fo4::test::hkx;

std::vector<std::byte> character_file_bytes() {
  PhysicsSpec spec = default_physics_spec();
  spec.animation_rig = {{"Root", -1, {0, 0, 0}}, {"Child", 0, {0, 1, 0}}, {"Tip", 1, {0, 1, 0}}};
  return physics_packfile(spec);
}

void put(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary);
  out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

std::vector<std::byte> take(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  const std::vector<char> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  const auto view = std::as_bytes(std::span(raw));
  return std::vector<std::byte>(view.begin(), view.end());
}

std::string text(const std::filesystem::path& path) { return path.string(); }

std::vector<HkxSkeletonEdit> rename_tip() {
  std::vector<HkxSkeletonEdit> edits(1);
  edits[0].kind = HkxSkeletonEditKind::Rename;
  edits[0].bone = 2;
  edits[0].name = "Tip2";
  return edits;
}

CharacterTransform unchanged() {
  return [](std::span<const std::byte>, std::vector<std::byte>& out) -> std::string {
    out.clear();
    return {};
  };
}

CharacterTransform rewriting() {
  return [](std::span<const std::byte>, std::vector<std::byte>& out) -> std::string {
    out.assign(8, std::byte{7});
    return {};
  };
}

CharacterTransform refusing(std::string why) {
  return [why = std::move(why)](std::span<const std::byte>, std::vector<std::byte>& out) -> std::string {
    out.clear();
    return why;
  };
}

CharacterSaveStage stage(CharacterDocumentKind kind, const std::filesystem::path& source, const std::filesystem::path& target,
                         CharacterTransform transform) {
  CharacterSaveStage out;
  out.kind = kind;
  out.source = text(source);
  out.target = target.empty() ? std::string() : text(target);
  out.transform = std::move(transform);
  return out;
}

}  // namespace

TEST(HkxCharacterSaveParse, ASkeletonEditListParsesAndNamesTheFailingEdit) {
  std::vector<HkxSkeletonEdit> edits;
  EXPECT_EQ(parse_skeleton_edits(nlohmann::json::parse(R"([{"kind":"rename","bone":2,"name":"X"},{"kind":"set_lock","bone":1,"locked":true}])"), edits), "");
  ASSERT_EQ(edits.size(), 2u);
  EXPECT_EQ(edits[0].kind, HkxSkeletonEditKind::Rename);
  EXPECT_EQ(edits[0].name, "X");
  EXPECT_TRUE(edits[1].locked);

  EXPECT_EQ(parse_skeleton_edits(nlohmann::json::parse("{}"), edits), "edits must be a JSON array");
  EXPECT_EQ(parse_skeleton_edits(nlohmann::json::parse(R"([{"kind":"rename","bone":0},{"kind":"reparent","bone":1}])"), edits),
            "edit 2: reparent needs a parent");
  EXPECT_EQ(parse_skeleton_edits(nlohmann::json::parse(R"([{"kind":"fly","bone":0}])"), edits),
            "edit 1: kind must be rename, reparent, remove, set_pose or set_lock");
  EXPECT_EQ(parse_skeleton_edits(nlohmann::json::parse(R"([{"kind":"rename","bone":-1}])"), edits), "edit 1: bone must be zero or more");
  EXPECT_EQ(parse_skeleton_edits(nlohmann::json::parse(R"([{"kind":"set_pose","bone":0,"translation":[0,0],"rotation":[0,0,0,1],"scale":[1,1,1]}])"), edits),
            "edit 1: set_pose: translation must be a list of 3 numbers");
  EXPECT_TRUE(edits.empty());
}

TEST(HkxCharacterSaveParse, ASkinEditParsesAndRefusesBadFields) {
  fo4::nif::SkinEditRequest request;
  EXPECT_EQ(parse_skin_edit(nlohmann::json::parse(R"({"kind":"set_influences","block":3,"vertex":1,"influences":[{"bone":2,"weight":0.5}]})"), request), "");
  EXPECT_EQ(request.kind, fo4::nif::SkinEditKind::set_influences);
  EXPECT_EQ(request.block, 3u);
  ASSERT_EQ(request.influences.size(), 1u);
  EXPECT_EQ(request.influences[0].bone, 2);

  EXPECT_EQ(parse_skin_edit(nlohmann::json::parse(R"({"kind":"mirror_weights","block":3,"lane":3})"), request), "lane must be 0, 1 or 2");
  EXPECT_EQ(parse_skin_edit(nlohmann::json::parse(R"({"kind":"nope","block":3})"), request),
            "kind must be set_influences, copy_bone or mirror_weights");
  EXPECT_EQ(parse_skin_edit(nlohmann::json::parse(R"({"kind":"copy_bone","block":-1})"), request), "block must be a block id from 0 to 4294967295");
  EXPECT_EQ(parse_skin_edit(nlohmann::json::parse(R"({"kind":"copy_bone","block":1,"influences":[{"bone":"a","weight":1}]})"), request),
            "influences must be a list of {bone, weight} objects");
}

TEST(HkxCharacterSaveParse, ASkinEditIndexOutsideTheirTypeIsRefusedByName) {
  fo4::nif::SkinEditRequest request;
  EXPECT_NE(parse_skin_edit(nlohmann::json::parse(R"({"kind":"copy_bone","block":4294967296})"), request).find("block must be a block id"), std::string::npos);
  EXPECT_EQ(parse_skin_edit(nlohmann::json::parse(R"({"kind":"copy_bone","block":4294967295})"), request), "");
  EXPECT_NE(parse_skin_edit(nlohmann::json::parse(R"({"kind":"set_influences","block":1,"vertex":4294967296})"), request).find("vertex must be a vertex index"),
            std::string::npos);
  EXPECT_NE(parse_skin_edit(nlohmann::json::parse(R"({"kind":"copy_bone","block":1,"from_bone":2147483648})"), request).find("from_bone"), std::string::npos);
  EXPECT_NE(parse_skin_edit(nlohmann::json::parse(R"({"kind":"copy_bone","block":1,"to_bone":-2147483649})"), request).find("to_bone"), std::string::npos);
  EXPECT_NE(parse_skin_edit(nlohmann::json::parse(R"({"kind":"set_influences","block":1,"influences":[{"bone":4294967297,"weight":1}]})"), request)
                .find("influences bone"),
            std::string::npos);
  EXPECT_EQ(parse_skin_edit(nlohmann::json::parse(R"({"kind":"copy_bone","block":1,"from_bone":2147483647,"to_bone":-1})"), request), "");
  EXPECT_EQ(request.from_bone, 2147483647);
}

TEST(HkxCharacterSaveParse, AMapperRowDeltaIsZeroForARowThatIsAlreadyDerived) {
  const auto bytes = character_file_bytes();
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  std::vector<HkxSkeletonMapper> mappers;
  ASSERT_EQ(read_skeleton_mappers(packfile, mappers), HkxStatus::Ok);
  ASSERT_FALSE(mappers.empty());
  ASSERT_FALSE(mappers[0].rows.empty());
  auto mapper = mappers[0];
  auto& row = mapper.rows[0];
  auto derived = row.a_from_b;
  ASSERT_EQ(recompute_mapping_row(mapper.skeleton_a, row.bone_a, mapper.skeleton_b, row.bone_b, derived), "");
  row.a_from_b = derived;
  const auto delta = mapper_row_delta(mapper, row);
  EXPECT_EQ(delta.error, "");
  EXPECT_LT(delta.translation_delta, 1e-5f);
  EXPECT_LT(delta.rotation_delta, 1e-3f);

  row.bone_b = 99;
  EXPECT_NE(mapper_row_delta(mapper, row).error, "");
}

TEST(HkxCharacterCheck, AMappingWhoseAnimationBoneIsNotInTheCharacterSkeletonIsNamed) {
  PhysicsSpec spec = default_physics_spec();
  spec.animation_rig = {{"Root", -1, {0, 0, 0}}, {"Elsewhere", 0, {0, 1, 0}}, {"Tip", 1, {0, 1, 0}}};
  const auto ragdoll = physics_packfile(spec);
  const auto character = character_file_bytes();
  CharacterAssets assets;
  assets.skeleton = character;
  assets.ragdoll = ragdoll;
  const auto report = check_character(assets);
  EXPECT_TRUE(std::any_of(report.findings.begin(), report.findings.end(), [](const CharacterFinding& item) {
    return item.level == CharacterLevel::warning && item.area == "ragdoll" && item.where == "mapping 1" &&
           item.message.find("(Elsewhere) is not in the character skeleton") != std::string::npos;
  }));
  EXPECT_FALSE(std::any_of(report.findings.begin(), report.findings.end(), [](const CharacterFinding& item) {
    return item.where == "mapping 0";
  }));
}

TEST(HkxCharacterCheck, AMappingBoneOutsideTheAnimationSkeletonIsAnErrorInTheReport) {
  PhysicsSpec spec = default_physics_spec();
  spec.animation_rig = {{"Root", -1, {0, 0, 0}}, {"Child", 0, {0, 1, 0}}, {"Tip", 1, {0, 1, 0}}};
  spec.rows = {{0, 0}, {9, 1}};
  const auto bytes = physics_packfile(spec);
  CharacterAssets assets;
  assets.skeleton = bytes;
  const auto report = check_character(assets);
  EXPECT_TRUE(std::any_of(report.findings.begin(), report.findings.end(), [](const CharacterFinding& item) {
    return item.level == CharacterLevel::error && item.area == "ragdoll" && item.where == "mapping 1" &&
           item.message.find("outside the animation skeleton") != std::string::npos;
  }));
  EXPECT_FALSE(report.ok());
}

TEST(HkxCharacterCheck, TheAnimationLinksOfTheProjectChainAreInTheReport) {
  fo4::test::TempDataDir dir;
  const auto root = dir.path() / "Meshes" / "Actors" / "Foo";
  put(root / "Foo.hkx", project_packfile({"Characters\\Foo.hkx"}));
  put(root / "Characters" / "Foo.hkx",
      character_packfile("CharacterAssets\\skeleton.hkx", "Behaviors\\FooBehavior.hkx", {{"Animations\\walk.hkx"}}));
  put(root / "CharacterAssets" / "skeleton.hkx", character_file_bytes());
  const std::vector<std::filesystem::path> roots{dir.path()};
  const auto resolver = fo4::archive::AssetResolver::open_roots(roots, {});

  CharacterAssets assets;
  const auto skeleton = character_file_bytes();
  assets.skeleton = skeleton;
  assets.assets = &resolver;
  assets.project_hkx = "Meshes/Actors/Foo/Foo.hkx";
  const auto report = check_character(assets);
  EXPECT_TRUE(report.chain.checked);
  EXPECT_EQ(report.chain.animations, 1u);
  EXPECT_EQ(report.chain.missing_animations, 1u);
  EXPECT_FALSE(report.chain.problems.empty());
  EXPECT_TRUE(std::any_of(report.findings.begin(), report.findings.end(), [](const CharacterFinding& item) {
    return item.level == CharacterLevel::error && item.area == "animation";
  }));

  CharacterAssets bare;
  bare.skeleton = skeleton;
  EXPECT_FALSE(check_character(bare).chain.checked);
}

TEST(HkxCharacterSaveChain, ASkeletonRenameThenCopyThroughStepsChainAndEveryFileIsReadBack) {
  fo4::test::TempDataDir dir;
  const auto source = dir.path() / "char.hkx";
  put(source, character_file_bytes());
  const auto original = take(source);

  CharacterSaveRequest request;
  request.skeleton_edits = rename_tip();
  request.stages = {stage(CharacterDocumentKind::mapper, source, dir.path() / "m.hkx", unchanged()),
                    stage(CharacterDocumentKind::skeleton, source, dir.path() / "s.hkx", skeleton_transform(rename_tip())),
                    stage(CharacterDocumentKind::ragdoll, source, dir.path() / "r.hkx", unchanged())};
  const auto result = save_character(request);
  ASSERT_TRUE(result.saved) << result.refusal;
  ASSERT_EQ(result.steps.size(), 3u);
  EXPECT_EQ(result.steps[0].kind, CharacterDocumentKind::skeleton);
  EXPECT_TRUE(result.steps[0].changed);
  EXPECT_EQ(result.steps[1].kind, CharacterDocumentKind::ragdoll);
  EXPECT_TRUE(result.steps[1].written);
  EXPECT_EQ(result.written.size(), 3u);
  EXPECT_EQ(take(dir.path() / "s.hkx"), take(dir.path() / "r.hkx"));
  EXPECT_EQ(take(dir.path() / "r.hkx"), take(dir.path() / "m.hkx"));
  EXPECT_NE(take(dir.path() / "s.hkx"), original);
  EXPECT_EQ(take(source), original);
  EXPECT_EQ(result.steps[0].output_sha256, fo4::io::sha256_hex(take(dir.path() / "s.hkx")));
}

TEST(HkxCharacterSaveChain, ARefusingStepWritesNothingAtAll) {
  fo4::test::TempDataDir dir;
  const auto source = dir.path() / "char.hkx";
  put(source, character_file_bytes());

  CharacterSaveRequest request;
  request.stages = {stage(CharacterDocumentKind::skeleton, source, dir.path() / "s.hkx", skeleton_transform(rename_tip())),
                    stage(CharacterDocumentKind::mapper, source, dir.path() / "m.hkx", refusing("row 4 is outside the skeleton"))};
  const auto result = save_character(request);
  EXPECT_FALSE(result.saved);
  EXPECT_FALSE(result.partial);
  EXPECT_NE(result.refusal.find("step 2 (mapper) refused: row 4 is outside the skeleton"), std::string::npos);
  EXPECT_TRUE(result.written.empty());
  EXPECT_FALSE(std::filesystem::exists(dir.path() / "s.hkx"));
  EXPECT_FALSE(std::filesystem::exists(dir.path() / "m.hkx"));
}

TEST(HkxCharacterSaveChain, AFailureMidwayNamesTheStepAndTheFilesAlreadyWritten) {
  fo4::test::TempDataDir dir;
  const auto source = dir.path() / "char.hkx";
  put(source, character_file_bytes());

  CharacterSaveRequest request;
  request.stages = {stage(CharacterDocumentKind::skeleton, source, dir.path() / "s.hkx", skeleton_transform(rename_tip())),
                    stage(CharacterDocumentKind::ragdoll, source, dir.path() / "missing_folder" / "r.hkx", unchanged())};
  const auto result = save_character(request);
  EXPECT_FALSE(result.saved);
  EXPECT_TRUE(result.partial);
  EXPECT_EQ(result.failed_stage, 1u);
  ASSERT_EQ(result.written.size(), 1u);
  EXPECT_EQ(result.written[0], text(dir.path() / "s.hkx"));
  EXPECT_NE(result.refusal.find("step 2 (ragdoll) failed"), std::string::npos);
  EXPECT_NE(result.refusal.find("already written: " + text(dir.path() / "s.hkx")), std::string::npos);
  EXPECT_TRUE(result.steps[0].written);
  EXPECT_FALSE(result.steps[1].written);
  EXPECT_TRUE(std::filesystem::exists(dir.path() / "s.hkx"));
}

TEST(HkxCharacterSaveChain, ATargetThatExistsIsRefusedBeforeAnyWriteUnlessOverwriteIsGiven) {
  fo4::test::TempDataDir dir;
  const auto source = dir.path() / "char.hkx";
  put(source, character_file_bytes());
  put(dir.path() / "r.hkx", {std::byte{1}});

  CharacterSaveRequest request;
  request.stages = {stage(CharacterDocumentKind::skeleton, source, dir.path() / "s.hkx", skeleton_transform(rename_tip())),
                    stage(CharacterDocumentKind::ragdoll, source, dir.path() / "r.hkx", unchanged())};
  const auto refused = save_character(request);
  EXPECT_FALSE(refused.saved);
  EXPECT_TRUE(refused.written.empty());
  EXPECT_FALSE(std::filesystem::exists(dir.path() / "s.hkx"));
  EXPECT_NE(refused.refusal.find("already exists"), std::string::npos);

  request.overwrite = true;
  const auto allowed = save_character(request);
  EXPECT_TRUE(allowed.saved) << allowed.refusal;
  EXPECT_EQ(take(dir.path() / "r.hkx"), take(dir.path() / "s.hkx"));
}

// Constraint: detects an overwrite that replaces a file with no .bak, a failed backup that still lets the chain write, and a mid-chain failure that loses the originals.
TEST(HkxCharacterSaveChain, OverwriteKeepsABakOfEveryReplacedFileAndAFailedBackupWritesNothing) {
  fo4::test::TempDataDir dir;
  const auto source = dir.path() / "char.hkx";
  put(source, character_file_bytes());
  const auto original = take(source);
  put(dir.path() / "s.hkx", {std::byte{1}});

  CharacterSaveRequest request;
  request.overwrite = true;
  request.stages = {stage(CharacterDocumentKind::skeleton, source, dir.path() / "s.hkx", skeleton_transform(rename_tip())),
                    stage(CharacterDocumentKind::ragdoll, source, dir.path() / "missing_folder" / "r.hkx", unchanged())};
  const auto partial = save_character(request);
  EXPECT_TRUE(partial.partial);
  ASSERT_EQ(partial.backups.size(), 1u);
  EXPECT_EQ(partial.backups[0], text(dir.path() / "s.hkx.bak"));
  EXPECT_EQ(take(dir.path() / "s.hkx.bak"), std::vector<std::byte>{std::byte{1}});
  EXPECT_NE(take(dir.path() / "s.hkx"), take(dir.path() / "s.hkx.bak"));

  std::filesystem::remove(dir.path() / "s.hkx.bak");
  std::filesystem::create_directories(dir.path() / "s.hkx.bak");
  put(dir.path() / "s.hkx", {std::byte{2}});
  put(dir.path() / "r.hkx", {std::byte{3}});
  request.stages = {stage(CharacterDocumentKind::skeleton, source, dir.path() / "s.hkx", skeleton_transform(rename_tip())),
                    stage(CharacterDocumentKind::ragdoll, source, dir.path() / "r.hkx", unchanged())};
  const auto refused = save_character(request);
  EXPECT_FALSE(refused.saved);
  EXPECT_FALSE(refused.partial);
  EXPECT_NE(refused.refusal.find("no backup could be kept"), std::string::npos);
  EXPECT_NE(refused.refusal.find("nothing was written"), std::string::npos);
  EXPECT_TRUE(refused.written.empty());
  EXPECT_EQ(take(dir.path() / "s.hkx"), std::vector<std::byte>{std::byte{2}});
  EXPECT_EQ(take(dir.path() / "r.hkx"), std::vector<std::byte>{std::byte{3}});
  EXPECT_EQ(take(source), original);

  std::filesystem::remove_all(dir.path() / "s.hkx.bak");
  put(dir.path() / "s.hkx.bak", {std::byte{9}});
  request.stages = {stage(CharacterDocumentKind::skeleton, source, dir.path() / "s.hkx", skeleton_transform(rename_tip()))};
  const auto kept = save_character(request);
  EXPECT_TRUE(kept.saved);
  EXPECT_TRUE(kept.backups.empty());
  EXPECT_EQ(take(dir.path() / "s.hkx.bak"), std::vector<std::byte>{std::byte{9}});
}

TEST(HkxCharacterSaveChain, ASourceThatChangedSinceItWasOpenedIsRefusedAndAnInPlaceNoOpWritesNothing) {
  fo4::test::TempDataDir dir;
  const auto source = dir.path() / "char.hkx";
  const auto bytes = character_file_bytes();
  put(source, bytes);

  CharacterSaveRequest request;
  request.stages = {stage(CharacterDocumentKind::skeleton, source, {}, unchanged())};
  request.stages[0].expected_source_sha256 = std::string(64, '0');
  const auto stale = save_character(request);
  EXPECT_FALSE(stale.saved);
  EXPECT_NE(stale.refusal.find("changed on disk since it was opened"), std::string::npos);

  CharacterSaveRequest chained;
  chained.stages = {stage(CharacterDocumentKind::skeleton, source, dir.path() / "s.hkx", skeleton_transform(rename_tip())),
                    stage(CharacterDocumentKind::ragdoll, source, dir.path() / "r.hkx", unchanged())};
  chained.stages[1].expected_source_sha256 = std::string(64, '0');
  const auto stale_second = save_character(chained);
  EXPECT_FALSE(stale_second.saved);
  EXPECT_NE(stale_second.refusal.find("step 2"), std::string::npos);
  EXPECT_FALSE(std::filesystem::exists(dir.path() / "s.hkx"));

  request.stages[0].expected_source_sha256 = fo4::io::sha256_hex(bytes);
  request.overwrite = true;
  const auto noop = save_character(request);
  EXPECT_TRUE(noop.saved) << noop.refusal;
  ASSERT_EQ(noop.steps.size(), 1u);
  EXPECT_FALSE(noop.steps[0].changed);
  EXPECT_FALSE(noop.steps[0].written);
  EXPECT_TRUE(noop.written.empty());
}

TEST(HkxCharacterSaveChain, AnInPlaceSkeletonSaveIsRefusedWithoutOverwriteAndWritesNothing) {
  fo4::test::TempDataDir dir;
  const auto source = dir.path() / "char.hkx";
  put(source, character_file_bytes());
  const auto original = take(source);

  CharacterSaveRequest request;
  request.stages = {stage(CharacterDocumentKind::skeleton, source, {}, skeleton_transform(rename_tip()))};
  const auto refused = save_character(request);
  EXPECT_FALSE(refused.saved);
  EXPECT_TRUE(refused.written.empty());
  EXPECT_NE(refused.refusal.find("replace its source file in place"), std::string::npos);
  EXPECT_EQ(take(source), original);

  request.overwrite = true;
  const auto allowed = save_character(request);
  EXPECT_TRUE(allowed.saved) << allowed.refusal;
  EXPECT_NE(take(source), original);
  EXPECT_EQ(take(text(source) + ".bak"), original);
  ASSERT_EQ(allowed.backups.size(), 1u);
}

TEST(HkxCharacterSaveChain, AMapperOrSkinTargetThatIsItsOwnSourceIsRefusedWithoutOverwriteWhateverTheSpelling) {
  fo4::test::TempDataDir dir;
  const auto source = dir.path() / "char.hkx";
  const auto body = dir.path() / "body.nif";
  put(source, character_file_bytes());
  put(body, {std::byte{1}, std::byte{2}});
  const auto original = take(source);

  CharacterSaveRequest mapper;
  mapper.stages = {stage(CharacterDocumentKind::mapper, source, source, rewriting())};
  const auto same = save_character(mapper);
  EXPECT_FALSE(same.saved);
  EXPECT_TRUE(same.written.empty());
  EXPECT_NE(same.refusal.find("its own source file"), std::string::npos);
  EXPECT_EQ(take(source), original);

  std::string shouted = text(source);
  std::transform(shouted.begin(), shouted.end(), shouted.begin(), [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
  CharacterSaveRequest spelled;
  spelled.stages = {stage(CharacterDocumentKind::mapper, source, std::filesystem::path(shouted), rewriting())};
  EXPECT_FALSE(save_character(spelled).saved);
  EXPECT_EQ(take(source), original);

  CharacterSaveRequest dotted;
  dotted.stages = {stage(CharacterDocumentKind::mapper, source, dir.path() / "sub" / ".." / "char.hkx", rewriting())};
  EXPECT_FALSE(save_character(dotted).saved);
  EXPECT_EQ(take(source), original);

  CharacterSaveRequest skin;
  skin.stages = {stage(CharacterDocumentKind::skin, body, body, rewriting())};
  const auto skin_refused = save_character(skin);
  EXPECT_FALSE(skin_refused.saved);
  EXPECT_TRUE(skin_refused.written.empty());
  EXPECT_NE(skin_refused.refusal.find("its own source file"), std::string::npos);
  EXPECT_EQ(take(body), (std::vector<std::byte>{std::byte{1}, std::byte{2}}));
}

TEST(HkxCharacterSaveChain, AStaleSkinSourceIsRefusedByNameBeforeAnyWrite) {
  fo4::test::TempDataDir dir;
  const auto source = dir.path() / "char.hkx";
  const auto body = dir.path() / "body.nif";
  put(source, character_file_bytes());
  put(body, {std::byte{1}, std::byte{2}});

  CharacterSaveRequest request;
  request.stages = {stage(CharacterDocumentKind::skeleton, source, dir.path() / "s.hkx", skeleton_transform(rename_tip())),
                    stage(CharacterDocumentKind::skin, body, dir.path() / "body2.nif", rewriting())};
  request.stages[1].expected_source_sha256 = fo4::io::sha256_hex(std::vector<std::byte>{std::byte{9}});
  const auto stale = save_character(request);
  EXPECT_FALSE(stale.saved);
  EXPECT_TRUE(stale.written.empty());
  EXPECT_NE(stale.refusal.find("(skin)"), std::string::npos);
  EXPECT_NE(stale.refusal.find(text(body)), std::string::npos);
  EXPECT_NE(stale.refusal.find("changed on disk since it was opened"), std::string::npos);
  EXPECT_FALSE(std::filesystem::exists(dir.path() / "s.hkx"));
  EXPECT_FALSE(std::filesystem::exists(dir.path() / "body2.nif"));
}

TEST(HkxCharacterSaveChain, ATargetPolicyRefusalAndAPlanRefusalWriteNothing) {
  fo4::test::TempDataDir dir;
  const auto source = dir.path() / "char.hkx";
  put(source, character_file_bytes());

  CharacterSaveRequest request;
  request.stages = {stage(CharacterDocumentKind::skeleton, source, dir.path() / "s.hkx", skeleton_transform(rename_tip()))};
  request.target_policy = [](const std::string&, CharacterDocumentKind) { return std::string("protected name"); };
  const auto policy = save_character(request);
  EXPECT_FALSE(policy.saved);
  EXPECT_NE(policy.refusal.find("protected name"), std::string::npos);
  EXPECT_FALSE(std::filesystem::exists(dir.path() / "s.hkx"));

  CharacterSaveRequest planned;
  planned.stages = {stage(CharacterDocumentKind::ragdoll, source, dir.path() / "CHAR.hkx", unchanged())};
  const auto refused = save_character(planned);
  EXPECT_FALSE(refused.saved);
  EXPECT_NE(refused.refusal.find("the plan was refused"), std::string::npos);
  EXPECT_TRUE(refused.written.empty());

  EXPECT_FALSE(save_character(CharacterSaveRequest{}).saved);
}

TEST(HkxCharacterSaveParse, ARagdollEditObjectParsesAndRefusesBadRows) {
  HkxRagdollEdits edits;
  EXPECT_EQ(parse_ragdoll_edits(nlohmann::json::parse(R"({"bodies":[{"body":1,"mass":2.5,"position":[1,2,3]}],"bone_map":[{"bone":0,"body":1}]})"), edits), "");
  ASSERT_EQ(edits.bodies.size(), 1u);
  EXPECT_EQ(edits.bodies[0].body, 1);
  ASSERT_TRUE(edits.bodies[0].mass.has_value());
  EXPECT_FLOAT_EQ(*edits.bodies[0].mass, 2.5f);
  ASSERT_EQ(edits.bone_map.size(), 1u);

  EXPECT_EQ(parse_ragdoll_edits(nlohmann::json::parse("[]"), edits), "edits must be a JSON object");
  EXPECT_TRUE(edits.bodies.empty());
  EXPECT_EQ(parse_ragdoll_edits(nlohmann::json::parse(R"({"bodies":[{"body":-1}]})"), edits), "body must be between 0 and 2147483647");
  EXPECT_EQ(parse_ragdoll_edits(nlohmann::json::parse(R"({"bodies":[{"body":0,"position":[1,2]}]})"), edits), "position must hold 3 numbers");
  EXPECT_EQ(parse_ragdoll_edits(nlohmann::json::parse(R"({"materials":[{"material":0,"allow_shared":"yes"}]})"), edits),
            "allow_shared must be true or false");
  EXPECT_EQ(parse_ragdoll_edits(nlohmann::json::parse(R"({"shapes":{}})"), edits), "shapes must be an array");
  EXPECT_EQ(parse_ragdoll_edits(nlohmann::json::parse("{}"), edits), "");
}

TEST(HkxCharacterSaveChain, AStageWithNoTransformIsRefusedBeforeAnyFileIsWritten) {
  fo4::test::TempDataDir dir;
  const auto source = dir.path() / "char.hkx";
  put(source, character_file_bytes());

  CharacterSaveRequest request;
  request.stages = {stage(CharacterDocumentKind::skeleton, source, dir.path() / "s.hkx", skeleton_transform(rename_tip())),
                    stage(CharacterDocumentKind::ragdoll, source, dir.path() / "r.hkx", CharacterTransform{})};
  const auto refused = save_character(request);
  EXPECT_FALSE(refused.saved);
  EXPECT_FALSE(refused.partial);
  EXPECT_NE(refused.refusal.find("step 2 (ragdoll) has no edits to apply"), std::string::npos);
  EXPECT_TRUE(refused.written.empty());
  EXPECT_TRUE(refused.steps.empty());
  EXPECT_FALSE(std::filesystem::exists(dir.path() / "s.hkx"));
  EXPECT_FALSE(std::filesystem::exists(dir.path() / "r.hkx"));

  const auto empty = save_character(CharacterSaveRequest{});
  EXPECT_FALSE(empty.saved);
  EXPECT_NE(empty.refusal.find("there is nothing to save: no document was given"), std::string::npos);
}
