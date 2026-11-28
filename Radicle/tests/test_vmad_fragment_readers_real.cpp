#include "fo4/plugin/reader.hpp"
#include "fo4/schema/vmad.hpp"
#include "plugin_bytes.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>

namespace {

struct SignatureTally {
  std::size_t records{0};
  std::size_t decoded{0};
  std::size_t failed{0};
  std::size_t script_unsupported{0};
  std::size_t fragments{0};
};

enum class Kind { perk, term, pack };

void tally_record(const fo4::Record& record, Kind kind, SignatureTally& tally) {
  for (const auto& sub : record.subrecords) {
    if (sub.signature.to_string() != "VMAD") continue;
    const auto scripts = fo4::read_vmad_scripts(sub.data);
    if (scripts.unsupported) {
      ++tally.script_unsupported;
      continue;
    }
    if (scripts.trailing_bytes == 0) continue;
    ++tally.records;
    const auto extras = std::span<const std::byte>(sub.data).subspan(sub.data.size() - scripts.trailing_bytes);
    const auto coverage = fo4::vmad_coverage(sub.data, record.header.signature);

    std::size_t fragments = 0;
    bool ok = false;
    if (kind == Kind::pack) {
      const auto decoded = fo4::read_pack_vmad_extras(extras, scripts.object_format);
      fragments = decoded.fragments.size();
      ok = !decoded.unsupported && decoded.fully_consumed;
    } else {
      const auto decoded = kind == Kind::perk
                               ? fo4::read_perk_vmad_extras(extras, scripts.object_format)
                               : fo4::read_term_vmad_extras(extras, scripts.object_format);
      fragments = decoded.fragments.size();
      ok = !decoded.unsupported && decoded.fully_consumed;
    }
    ok = ok && coverage.complete() && coverage.fragment_count == fragments;
    if (ok) {
      ++tally.decoded;
      tally.fragments += fragments;
    } else {
      ++tally.failed;
    }
    return;
  }
}

void print_tally(const std::string& name, const SignatureTally& tally) {
  std::cout << "[vmad-fragments] " << name << " records=" << tally.records
            << " decoded=" << tally.decoded << " failed=" << tally.failed
            << " fragments=" << tally.fragments
            << " script_unsupported=" << tally.script_unsupported << "\n";
}

}  // namespace

TEST(VmadFragmentReadersReal, PerkTermPackFragmentsDecodeFullyOnFallout4Esm) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) GTEST_SKIP() << "set FO4_DATA_DIR for read-only official coverage";
  record_game_build(detect_game_build(dir));
  const auto path = std::filesystem::path(dir) / "Fallout4.esm";
  ASSERT_TRUE(std::filesystem::exists(path));

  SignatureTally perk;
  SignatureTally term;
  SignatureTally pack;
  {
    const auto plugin = fo4::read_plugin_file(path);
    fo4::visit_records(plugin, [&](const fo4::Record& record) {
      if (&record == &plugin.tes4) return;
      if (record.header.signature == fo4::sig_perk) {
        tally_record(record, Kind::perk, perk);
      } else if (record.header.signature == fo4::sig_term) {
        tally_record(record, Kind::term, term);
      } else if (record.header.signature == fo4::sig_pack) {
        tally_record(record, Kind::pack, pack);
      }
    });
  }

  print_tally("PERK", perk);
  print_tally("TERM", term);
  print_tally("PACK", pack);
  for (const auto* tally : {&perk, &term, &pack}) {
    EXPECT_GT(tally->records, 0u);
    EXPECT_EQ(tally->failed, 0u);
    EXPECT_EQ(tally->decoded, tally->records);
    EXPECT_EQ(tally->script_unsupported, 0u);
  }
}
