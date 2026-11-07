#include "fo4/abc/abc.hpp"
#include "fo4/abc/edit.hpp"
#include "fo4/abc/flow.hpp"
#include "fo4/abc/opcodes.hpp"
#include "fo4/abc/summary.hpp"
#include "fo4/archive/ba2.hpp"
#include "fo4/swf/swf.hpp"
#include "real_data_build.hpp"
#include "real_data_movies.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <system_error>
#include <vector>

namespace {

std::string bucket(std::size_t count) {
  if (count == 0) {
    return "0";
  }
  if (count <= 4) {
    return "1-4";
  }
  if (count <= 16) {
    return "5-16";
  }
  if (count <= 64) {
    return "17-64";
  }
  if (count <= 256) {
    return "65-256";
  }
  return "257+";
}

struct AbcStats {
  std::size_t total_movies{0};
  std::size_t movies{0};
  std::size_t lzma{0};
  std::size_t unreadable{0};
  std::size_t blocks{0};
  std::size_t parsed{0};
  std::size_t trailing_bytes_blocks{0};
  std::size_t serialize_mismatches{0};
  std::size_t bodies{0};
  std::size_t instructions{0};
  std::size_t reencode_mismatches{0};
  std::size_t edit_roundtrips{0};
  std::size_t edit_mismatches{0};
  std::size_t verified_flows{0};
  std::size_t basic_blocks{0};
  std::size_t unreachable_blocks{0};
  std::size_t observed_max_stack{0};
  std::size_t classes{0};
  std::size_t methods{0};
  std::size_t scripts{0};
  std::map<std::string, std::size_t> unsupported;
  std::map<std::string, std::size_t> flow_refusals;
  std::map<std::string, std::size_t> class_histogram;
  std::map<std::string, std::size_t> method_histogram;
  std::map<std::string, std::size_t> script_histogram;

  std::size_t unsupported_total() const {
    std::size_t total = 0;
    for (const auto& [status, count] : unsupported) {
      total += count;
    }
    return total;
  }
};

std::string histogram_text(const std::map<std::string, std::size_t>& histogram) {
  std::string text;
  for (const auto& [bucket_name, count] : histogram) {
    if (!text.empty()) {
      text += ' ';
    }
    text += bucket_name + "=" + std::to_string(count);
  }
  return text;
}

std::size_t first_difference(std::span<const std::byte> expected,
                             const std::vector<std::uint8_t>& actual) {
  for (std::size_t i = 0; i < expected.size(); ++i) {
    if (i >= actual.size() || static_cast<std::uint8_t>(expected[i]) != actual[i]) {
      return i;
    }
  }
  return expected.size();
}

void check_block(const std::string& name, std::span<const std::byte> payload, AbcStats& stats,
                 std::vector<std::uint8_t>& rebuilt) {
  ++stats.blocks;
  fo4::abc::AbcFile file;
  const auto parse_status = fo4::abc::AbcFile::parse(
      reinterpret_cast<const std::uint8_t*>(payload.data()), payload.size(), file);
  if (parse_status != fo4::abc::AbcStatus::Ok) {
    ++stats.unsupported[fo4::abc::status_name(parse_status)];
    if (parse_status == fo4::abc::AbcStatus::TrailingBytes) {
      ++stats.trailing_bytes_blocks;
    }
    return;
  }
  ++stats.parsed;

  const auto serialize_status = file.serialize(rebuilt);
  const std::size_t difference = first_difference(payload, rebuilt);
  const bool identical = serialize_status == fo4::abc::AbcStatus::Ok &&
                         rebuilt.size() == payload.size() && difference == payload.size();
  if (!identical) {
    ++stats.serialize_mismatches;
  }
  EXPECT_TRUE(identical) << name << ": serialize " << fo4::abc::status_name(serialize_status)
                         << ", first differing offset " << difference << " of "
                         << payload.size();

  for (std::size_t body_index = 0; body_index < file.bodies.size(); ++body_index) {
    const fo4::abc::AbcMethodBody& body = file.bodies[body_index];
    ++stats.bodies;
    std::vector<fo4::abc::AbcInstruction> instructions;
    const auto decode_status = fo4::abc::decode_code(body.code, instructions, body.exceptions);
    EXPECT_EQ(decode_status, fo4::abc::AbcStatus::Ok)
        << name << ": " << fo4::abc::status_name(decode_status);
    if (decode_status != fo4::abc::AbcStatus::Ok) {
      continue;
    }
    const auto operand_status = fo4::abc::validate_operand_pools(file, instructions);
    EXPECT_EQ(operand_status, fo4::abc::AbcStatus::Ok)
        << name << ": " << fo4::abc::status_name(operand_status);
    stats.instructions += instructions.size();

    if (body_index == 0) {
      fo4::abc::AbcFile edited = file;
      const std::array<std::uint8_t, 1> nop{0x02};
      std::string edit_message;
      const bool inserted =
          fo4::abc::insert_instruction(edited, 0, 0, nop, edit_message) ==
          fo4::abc::AbcEditStatus::Ok;
      const bool reverted =
          inserted && fo4::abc::delete_instruction(edited, 0, 0, edit_message) ==
                           fo4::abc::AbcEditStatus::Ok;
      if (reverted) {
        ++stats.edit_roundtrips;
        const auto& back = edited.bodies[0];
        bool same = back.code == body.code && back.max_stack == body.max_stack &&
                    back.local_count == body.local_count &&
                    back.exceptions.size() == body.exceptions.size();
        if (same) {
          for (std::size_t entry = 0; entry < back.exceptions.size(); ++entry) {
            const auto& left = back.exceptions[entry];
            const auto& right = body.exceptions[entry];
            if (left.from != right.from || left.to != right.to ||
                left.target != right.target || left.exc_type != right.exc_type ||
                left.var_name != right.var_name) {
              same = false;
              break;
            }
          }
        }
        if (!same) {
          ++stats.edit_mismatches;
          EXPECT_EQ(back.code, body.code) << name << ": body 0 did not revert to its own bytes";
        }
      }
    }

    std::vector<std::uint8_t> reencoded;
    const auto encode_status = fo4::abc::encode_code(instructions, reencoded);
    EXPECT_EQ(encode_status, fo4::abc::AbcStatus::Ok)
        << name << ": body " << body_index << ": " << fo4::abc::status_name(encode_status);
    if (encode_status != fo4::abc::AbcStatus::Ok || reencoded != body.code) {
      ++stats.reencode_mismatches;
      EXPECT_TRUE(reencoded == body.code)
          << name << ": body " << body_index << " did not re-encode to its own bytes"
          << "; first difference at byte "
          << first_difference(std::as_bytes(std::span<const std::uint8_t>(body.code)), reencoded)
          << " of " << body.code.size();
    }

    fo4::abc::AbcMethodFlow flow;
    fo4::abc::AbcFlowIssue issue;
    const auto flow_status = fo4::abc::verify_method_flow(
        file, static_cast<std::uint32_t>(body_index), flow, issue);
    EXPECT_EQ(flow_status, fo4::abc::AbcFlowStatus::Ok)
        << name << ": body " << body_index << " method " << body.method << " at offset "
        << issue.offset << " (" << fo4::abc::flow_status_name(flow_status)
        << "): " << issue.message;
    if (flow_status != fo4::abc::AbcFlowStatus::Ok) {
      ++stats.flow_refusals[fo4::abc::flow_status_name(flow_status)];
    } else {
      ++stats.verified_flows;
      stats.basic_blocks += flow.graph.blocks.size();
      stats.unreachable_blocks += flow.graph.unreachable_block_count();
      if (flow.observed_max_stack > stats.observed_max_stack) {
        stats.observed_max_stack = flow.observed_max_stack;
      }
    }

    std::string listing;
    const auto listing_status = fo4::abc::disassemble(file, body.code, listing, body.exceptions);
    EXPECT_EQ(listing_status, fo4::abc::AbcStatus::Ok)
        << name << ": " << fo4::abc::status_name(listing_status);
  }

  fo4::abc::AbcSummary summary;
  const auto summary_status = fo4::abc::summarize(file, summary);
  ASSERT_EQ(summary_status, fo4::abc::AbcStatus::Ok)
      << name << ": " << fo4::abc::status_name(summary_status);
  EXPECT_EQ(summary.classes.size(), file.instances.size()) << name;

  std::size_t methods = 0;
  for (const fo4::abc::AbcClassSummary& klass : summary.classes) {
    methods += klass.methods.size();
  }
  stats.classes += summary.classes.size();
  stats.methods += methods;
  stats.scripts += summary.scripts.size();
  ++stats.class_histogram[bucket(summary.classes.size())];
  ++stats.method_histogram[bucket(methods)];
  ++stats.script_histogram[bucket(summary.scripts.size())];
}

void check_movie(const std::string& name, std::span<const std::byte> bytes, AbcStats& stats) {
  ++stats.total_movies;
  const auto result = fo4::swf::read_swf(bytes);
  if (result.status == fo4::swf::SwfStatus::unsupported_compression) {
    ++stats.lzma;
    return;
  }
  EXPECT_TRUE(result.ok()) << name << ": " << fo4::swf::status_name(result.status) << " "
                           << result.message;
  if (!result.ok()) {
    ++stats.unreadable;
    return;
  }
  ++stats.movies;
  const auto blocks = fo4::swf::abc_blocks(result.file);
  fo4::swf::SwfFile rebuilt = result.file;
  for (const fo4::swf::SwfAbcBlock& block : blocks) {
    const std::string label = name + ":tag" + std::to_string(block.tag_index) +
                              (block.name.empty() ? std::string{} : ":" + block.name);
    std::vector<std::uint8_t> serialized;
    check_block(label, block.payload, stats, serialized);
    if (serialized.empty()) {
      continue;
    }
    const fo4::swf::SwfTag& tag = result.file.tags[block.tag_index];
    const auto prefix_end =
        tag.payload.begin() + static_cast<std::ptrdiff_t>(tag.abc->payload_offset);
    std::vector<std::byte> payload(tag.payload.begin(), prefix_end);
    for (const std::uint8_t byte : serialized) {
      payload.push_back(static_cast<std::byte>(byte));
    }
    EXPECT_TRUE(fo4::swf::replace_tag_payload(rebuilt, block.tag_index, payload)) << label;
  }
  if (!blocks.empty()) {
    EXPECT_TRUE(fo4::swf::write_swf(rebuilt, fo4::swf::SwfCompression::none) ==
                fo4::swf::write_swf(result.file, fo4::swf::SwfCompression::none))
        << name;
  }
}

}  // namespace

TEST(AbcCorpusReal, DecodesEveryShippedInterfaceAbcBlock) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder";
  }
  const auto build = detect_game_build(dir);
  record_game_build(build);
  const std::filesystem::path data_dir(dir);

  AbcStats stats;
  const auto interface_dir = data_dir / "Interface";
  std::error_code error;
  if (std::filesystem::is_directory(interface_dir, error)) {
    for (const auto& entry : std::filesystem::directory_iterator(interface_dir, error)) {
      if (!entry.is_regular_file(error)) {
        continue;
      }
      const auto name = entry.path().filename().string();
      if (!is_movie(name)) {
        continue;
      }
      check_movie(name, read_file(entry.path()), stats);
    }
  }

  if (stats.blocks == 0) {
    const auto archive_path = data_dir / "Fallout4 - Interface.ba2";
    if (!std::filesystem::exists(archive_path)) {
      GTEST_SKIP() << "no Interface movies with bytecode and no Fallout4 - Interface.ba2 in "
                      "FO4_DATA_DIR";
    }
    const auto archive = fo4::archive::read_ba2_general_file(archive_path);
    for (const auto& entry : archive.entries) {
      if (!is_movie(entry.path)) {
        continue;
      }
      check_movie(entry.path, fo4::archive::extract_ba2_general_entry(entry), stats);
    }
  }

  EXPECT_EQ(stats.total_movies, stats.movies + stats.lzma + stats.unreadable);
  EXPECT_EQ(stats.blocks, stats.parsed + stats.unsupported_total());
  EXPECT_EQ(stats.trailing_bytes_blocks, 0u);
  EXPECT_EQ(stats.serialize_mismatches, 0u);
  EXPECT_EQ(stats.reencode_mismatches, 0u)
      << "bodies whose instruction stream did not re-encode to its own bytes";
  EXPECT_EQ(stats.edit_mismatches, 0u)
      << "bodies whose nop insert/delete did not revert to their own bytes";
  EXPECT_EQ(stats.verified_flows, stats.bodies)
      << "flow_refusal_histogram: " << histogram_text(stats.flow_refusals);
  EXPECT_EQ(stats.unsupported_total(), 0u)
      << "unsupported_status_histogram: " << histogram_text(stats.unsupported);
  if (exact_counts_known(build)) {
    EXPECT_EQ(stats.lzma, 0u);
  }
  EXPECT_GT(stats.parsed, 0u);
  EXPECT_GT(stats.bodies, 0u);

  RecordProperty("movies", std::to_string(stats.movies));
  RecordProperty("lzma_movies", std::to_string(stats.lzma));
  RecordProperty("unreadable_movies", std::to_string(stats.unreadable));
  RecordProperty("abc_blocks", std::to_string(stats.blocks));
  RecordProperty("parsed_blocks", std::to_string(stats.parsed));
  RecordProperty("unsupported_blocks", std::to_string(stats.unsupported_total()));
  RecordProperty("unsupported_status_histogram", histogram_text(stats.unsupported));
  RecordProperty("bodies", std::to_string(stats.bodies));
  RecordProperty("instructions", std::to_string(stats.instructions));
  RecordProperty("reencode_mismatches", std::to_string(stats.reencode_mismatches));
  RecordProperty("edit_roundtrips", std::to_string(stats.edit_roundtrips));
  RecordProperty("edit_mismatches", std::to_string(stats.edit_mismatches));
  RecordProperty("verified_flows", std::to_string(stats.verified_flows));
  RecordProperty("basic_blocks", std::to_string(stats.basic_blocks));
  RecordProperty("unreachable_blocks", std::to_string(stats.unreachable_blocks));
  RecordProperty("observed_max_stack", std::to_string(stats.observed_max_stack));
  RecordProperty("flow_refusal_histogram", histogram_text(stats.flow_refusals));
  RecordProperty("total_classes", std::to_string(stats.classes));
  RecordProperty("total_methods", std::to_string(stats.methods));
  RecordProperty("total_scripts", std::to_string(stats.scripts));
  RecordProperty("class_histogram", histogram_text(stats.class_histogram));
  RecordProperty("method_histogram", histogram_text(stats.method_histogram));
  RecordProperty("script_histogram", histogram_text(stats.script_histogram));
}
