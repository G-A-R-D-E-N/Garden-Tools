#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string_view>
#include <vector>

namespace {

void write_u64(fo4::test::ByteWriter& out, std::uint64_t value) {
  out.u32(static_cast<std::uint32_t>(value));
  out.u32(static_cast<std::uint32_t>(value >> 32));
}

std::vector<std::byte> make_plugin() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);

  fo4::test::ByteWriter first_data;
  write_u64(first_data, 0x1122334455667788ull);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"First.esm\0", 10});
  fo4::test::write_subrecord(out, "DATA", first_data.bytes);

  fo4::test::ByteWriter second_data;
  write_u64(second_data, 0x0102030405060708ull);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Second.esp\0", 11});
  fo4::test::write_subrecord(out, "DATA", second_data.bytes);
  fo4::test::end_record(out, tes4);
  return out.bytes;
}

}  // namespace

TEST(MasterMetadata, ReorderPreservesMatchingDataSizes) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  ASSERT_EQ(plugin.header.masters.size(), 2u);
  ASSERT_EQ(plugin.header.masters[0].data_size, 0x1122334455667788ull);
  ASSERT_EQ(plugin.header.masters[1].data_size, 0x0102030405060708ull);

  std::vector<fo4::MasterDescriptor> reordered{
      {fo4::PluginKey::from_filename("Second.esp"), fo4::PluginKind::full},
      {fo4::PluginKey::from_filename("First.esm"), fo4::PluginKind::full},
  };
  plugin = fo4::remap_plugin_masters(std::move(plugin), std::move(reordered));

  const auto reloaded = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  ASSERT_EQ(reloaded.header.masters.size(), 2u);
  EXPECT_EQ(reloaded.header.masters[0].key.filename(), "Second.esp");
  EXPECT_EQ(reloaded.header.masters[0].data_size, 0x0102030405060708ull);
  EXPECT_EQ(reloaded.header.masters[1].key.filename(), "First.esm");
  EXPECT_EQ(reloaded.header.masters[1].data_size, 0x1122334455667788ull);
}
