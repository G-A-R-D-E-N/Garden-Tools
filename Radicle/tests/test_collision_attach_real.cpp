#include "fo4/archive/assets.hpp"
#include "fo4/error.hpp"
#include "fo4/nif/collision.hpp"
#include "fo4/nif/collision_attach.hpp"
#include "fo4/nif/collision_lint.hpp"
#include "fo4/nif/nif.hpp"
#include "fo4/nif/nif_write.hpp"
#include "real_data_build.hpp"
#include "real_data_plugins.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::size_t k_max_rebuilds = 40;
constexpr std::size_t k_entry_stride = 37;
constexpr std::string_view k_layout_refusal = "reference layout";

std::string lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return text;
}

bool is_nif(const std::string& path) {
  const auto text = lower(path);
  return text.size() >= 4 && text.substr(text.size() - 4) == ".nif";
}

enum class Outcome { rebuilt, refused, failed, unchanged };

Outcome strip_and_reattach(const fo4::nif::NifFile& nif, const fo4::nif::CollisionBlob& blob, std::string& message) {
  const auto name = nif.nodes()[static_cast<std::size_t>(blob.target_node)].name;
  try {
    const auto stripped_bytes = fo4::nif::remove_node_collision(nif, name);
    if (stripped_bytes == fo4::nif::write_nif(nif)) return Outcome::unchanged;
    const auto stripped = fo4::nif::read_nif(stripped_bytes);
    const auto removed = nif.blocks().size() - stripped.blocks().size();
    if (removed < 1 || removed > 2) {
      message = "removed " + std::to_string(removed) + " blocks for one collision";
      return Outcome::failed;
    }
    const auto rebuilt = fo4::nif::read_nif(fo4::nif::set_node_collision(stripped, name, blob.bytes, blob.body_id));
    if (rebuilt.blocks().size() != nif.blocks().size() + 2 - removed) {
      message = "block count after re-attach is " + std::to_string(rebuilt.blocks().size());
      return Outcome::failed;
    }
    if (rebuilt.roots() != nif.roots() || rebuilt.nodes().size() != nif.nodes().size()) {
      message = "root or node list changed";
      return Outcome::failed;
    }
    const auto blobs = fo4::nif::read_collision_blobs(rebuilt);
    const auto found = std::find_if(blobs.begin(), blobs.end(), [&name, &rebuilt](const fo4::nif::CollisionBlob& other) {
      return other.target_node >= 0 && rebuilt.nodes()[static_cast<std::size_t>(other.target_node)].name == name;
    });
    if (found == blobs.end() || found->bytes != blob.bytes || found->flags != blob.flags ||
        found->body_id != blob.body_id) {
      message = "collision on " + name + " does not read back equal";
      return Outcome::failed;
    }
    fo4::nif::read_shapes(rebuilt);
    const auto original_findings = fo4::nif::lint_collision(nif);
    for (const auto& finding : fo4::nif::lint_collision(rebuilt)) {
      const auto known = std::any_of(original_findings.begin(), original_findings.end(),
                                     [&finding](const fo4::nif::CollisionFinding& kept) { return kept.issue == finding.issue; });
      if (!known) {
        message = "lint finding kind added on " + name;
        return Outcome::failed;
      }
    }
    return Outcome::rebuilt;
  } catch (const fo4::parse_error& error) {
    message = error.parse_message();
    return message.find(k_layout_refusal) != std::string::npos ? Outcome::refused : Outcome::failed;
  }
}

}  // namespace

// Detects: a vanilla collision strip plus re-attach that changes the blob, body, flags, target node, root list or lint findings, or that corrupts a reference it cannot read.
TEST(CollisionAttachRealData, VanillaStripAndReattachReadsBackEqual) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives";
  }
  record_game_build(detect_game_build(dir));
  const auto archives = fo4::archive::archive_load_order(dir, {}, installed_master_and_cc_plugins(dir));
  if (archives.empty()) {
    GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives";
  }

  const auto assets = fo4::archive::AssetResolver::open({}, archives);
  std::size_t nifs = 0;
  std::size_t rebuilt = 0;
  std::size_t refused = 0;
  std::size_t failed = 0;
  std::string first_failure;
  std::string first_refusal;
  for (const auto& reader : assets.archives()) {
    if (reader.kind() != fo4::archive::Ba2Kind::general) continue;
    for (std::size_t i = 0; i < reader.entries().size() && rebuilt + refused < k_max_rebuilds; i += k_entry_stride) {
      const auto& path = reader.entries()[i].path;
      if (!is_nif(path)) continue;
      ++nifs;
      try {
        const auto nif = fo4::nif::read_nif(reader.extract(i));
        const auto blobs = fo4::nif::read_collision_blobs(nif);
        const auto first = std::find_if(blobs.begin(), blobs.end(), [](const fo4::nif::CollisionBlob& blob) {
          return blob.owner >= 0 && blob.target_node >= 0;
        });
        if (first == blobs.end()) continue;
        std::string message;
        switch (strip_and_reattach(nif, *first, message)) {
          case Outcome::rebuilt:
            ++rebuilt;
            break;
          case Outcome::refused:
            ++refused;
            if (first_refusal.empty()) first_refusal = path + ": " + message;
            break;
          case Outcome::unchanged:
            break;
          case Outcome::failed:
            ++failed;
            if (first_failure.empty()) first_failure = path + ": " + message;
            break;
        }
      } catch (const fo4::parse_error& error) {
        ++failed;
        if (first_failure.empty()) first_failure = path + ": " + error.parse_message();
      }
    }
  }

  std::cout << "nifs " << nifs << ", strip and re-attach rebuilt " << rebuilt << ", refused for a missing layout "
            << refused << ", failed " << failed << '\n';
  if (!first_refusal.empty()) std::cout << "first refusal: " << first_refusal << '\n';
  if (!first_failure.empty()) std::cout << "first failure: " << first_failure << '\n';
  RecordProperty("rebuilt_count", std::to_string(rebuilt));
  RecordProperty("refused_count", std::to_string(refused));
  EXPECT_EQ(failed, 0U) << first_failure;
  EXPECT_GT(rebuilt, 0U);
}

namespace {

bool contains_text(const std::vector<std::byte>& bytes, std::string_view text) {
  const std::string_view view(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  return view.find(text) != std::string_view::npos;
}

std::string refused_block_type(const std::string& message) {
  constexpr std::string_view k_prefix = "block type ";
  const auto start = message.find(k_prefix);
  if (start == std::string::npos) return message;
  const auto begin = start + k_prefix.size();
  const auto end = message.find(' ', begin);
  return message.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
}

}  // namespace

// Detects: a vanilla collision NIF holding a segmented BSSubIndexTriShape that strip and re-attach refuses for that block, which would leave its collision unreplaceable; the refusal count by block type shows what still has no layout.
TEST(CollisionAttachRealData, VanillaSegmentedShapeCollisionsRebuildWithoutARefusal) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives";
  }
  record_game_build(detect_game_build(dir));
  const auto archives = fo4::archive::archive_load_order(dir, {}, installed_master_and_cc_plugins(dir));
  if (archives.empty()) {
    GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives";
  }

  const auto assets = fo4::archive::AssetResolver::open({}, archives);
  std::size_t candidates = 0;
  std::size_t rebuilt = 0;
  std::size_t unchanged = 0;
  std::size_t segmented_refusals = 0;
  std::size_t failed = 0;
  std::map<std::string, std::size_t> refused_types;
  std::string first_segmented;
  std::string first_failure;
  for (const auto& reader : assets.archives()) {
    if (reader.kind() != fo4::archive::Ba2Kind::general) continue;
    for (std::size_t i = 0; i < reader.entries().size(); ++i) {
      const auto& path = reader.entries()[i].path;
      if (!is_nif(path)) continue;
      const auto bytes = reader.extract(i);
      if (!contains_text(bytes, "bhkNPCollisionObject")) continue;
      if (!contains_text(bytes, "BSSubIndexTriShape") && !contains_text(bytes, "BSConnectPoint")) continue;
      ++candidates;
      try {
        const auto nif = fo4::nif::read_nif(bytes);
        const auto blobs = fo4::nif::read_collision_blobs(nif);
        const auto first = std::find_if(blobs.begin(), blobs.end(), [](const fo4::nif::CollisionBlob& blob) {
          return blob.owner >= 0 && blob.target_node >= 0;
        });
        if (first == blobs.end()) continue;
        const auto name = nif.nodes()[static_cast<std::size_t>(first->target_node)].name;
        try {
          fo4::nif::remove_node_collision(nif, name);
        } catch (const fo4::parse_error& error) {
          const auto message = error.parse_message();
          if (message.find(k_layout_refusal) == std::string::npos) {
            ++failed;
            if (first_failure.empty()) first_failure = path + ": " + message;
            continue;
          }
          const auto type = refused_block_type(message);
          ++refused_types[type];
          if (type == "BSSubIndexTriShape") {
            ++segmented_refusals;
            if (first_segmented.empty()) first_segmented = path + ": " + message;
          }
          continue;
        }
        std::string message;
        switch (strip_and_reattach(nif, *first, message)) {
          case Outcome::rebuilt:
            ++rebuilt;
            break;
          case Outcome::unchanged:
            ++unchanged;
            break;
          case Outcome::refused:
            ++failed;
            if (first_failure.empty()) first_failure = path + ": " + message;
            break;
          case Outcome::failed:
            ++failed;
            if (first_failure.empty()) first_failure = path + ": " + message;
            break;
        }
      } catch (const fo4::parse_error& error) {
        ++failed;
        if (first_failure.empty()) first_failure = path + ": " + error.parse_message();
      }
    }
  }

  std::cout << "collision files with a segmented or connect-point block " << candidates << ", strip and re-attach rebuilt "
            << rebuilt << ", unchanged " << unchanged << ", failed " << failed << ", segmented refusals "
            << segmented_refusals << '\n';
  for (const auto& [type, count] : refused_types) std::cout << "refused for block type " << type << ": " << count << '\n';
  if (!first_segmented.empty()) std::cout << "first segmented refusal: " << first_segmented << '\n';
  if (!first_failure.empty()) std::cout << "first failure: " << first_failure << '\n';
  RecordProperty("segmented_candidate_count", std::to_string(candidates));
  RecordProperty("segmented_rebuilt_count", std::to_string(rebuilt));
  EXPECT_EQ(failed, 0U) << first_failure;
  EXPECT_EQ(segmented_refusals, 0U) << first_segmented;
  EXPECT_GT(rebuilt, 0U);
}
