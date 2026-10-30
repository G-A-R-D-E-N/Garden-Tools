#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/signature.hpp"
#include "real_data_build.hpp"
#include "real_data_plugins.hpp"

#include <gtest/gtest.h>

#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

const char* const kOfficialMasters[] = {
    "Fallout4.esm", "DLCRobot.esm", "DLCworkshop01.esm", "DLCCoast.esm",
    "DLCworkshop02.esm", "DLCworkshop03.esm", "DLCNukaWorld.esm"};

std::vector<std::byte> file_bytes(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  std::vector<std::byte> out(static_cast<std::size_t>(in.tellg()));
  in.seekg(0);
  in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
  return out;
}

std::string property_key(const std::string& name) {
  std::string key;
  for (const char ch : name) {
    key += std::isalnum(static_cast<unsigned char>(ch)) ? ch : '_';
  }
  return key;
}

}  // namespace

TEST(HeaderVersionsReal, ReadsEveryOfficialMasterAndEslHeaderAndRewritesItIdentically) {
  const auto* data_dir = std::getenv("FO4_DATA_DIR");
  if (data_dir == nullptr) GTEST_SKIP() << "set FO4_DATA_DIR to installed Fallout 4 data";
  const std::filesystem::path dir{data_dir};
  record_game_build(detect_game_build(dir));
  for (const auto* master : kOfficialMasters) {
    if (!std::filesystem::exists(dir / master)) {
      GTEST_SKIP() << "missing official master: " << (dir / master);
    }
  }

  for (const auto& name : installed_master_and_cc_plugins(dir)) {
    SCOPED_TRACE(name);
    const auto path = dir / name;
    auto plugin = fo4::read_plugin_file(path);

    EXPECT_EQ(plugin.tes4.header.signature, fo4::sig_tes4);
    EXPECT_TRUE(std::isfinite(plugin.header.version));

    const auto key = property_key(name);
    RecordProperty(key + "_form_version", std::to_string(plugin.tes4.header.form_version));
    RecordProperty(key + "_hedr_version_x100",
                   std::to_string(std::lround(plugin.header.version * 100.0f)));
    RecordProperty(key + "_flags", std::to_string(plugin.header.tes4_flags.value()));
    RecordProperty(key + "_next_object_id", std::to_string(plugin.header.next_form_id));

    const auto original = file_bytes(path);
    const auto bytes = fo4::write_plugin(plugin);
    ASSERT_EQ(bytes.size(), original.size());
    EXPECT_EQ(std::memcmp(bytes.data(), original.data(), bytes.size()), 0);
  }
}
