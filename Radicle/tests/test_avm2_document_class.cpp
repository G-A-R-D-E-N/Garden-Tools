#include "fo4/abc/abc.hpp"
#include "fo4/avm2/display.hpp"
#include "fo4/avm2/interpreter.hpp"
#include "fo4/avm2/name.hpp"
#include "fo4/avm2/object.hpp"
#include "fo4/avm2/runtime.hpp"
#include "fo4/avm2/swf_runtime.hpp"
#include "fo4/avm2/value.hpp"
#include "fo4/swf/swf.hpp"
#include "abc_test_support.hpp"
#include "swf_movie_builder.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace {

namespace abc = fo4::abc;

using fo4::test::put_string;
using fo4::test::put_u16;
using fo4::test::put_u30;
using fo4::test::put_u8;
using swf_test::SwfBuilder;
using swf_test::wrap_body;

/* A document-class ABC must extend MovieClip and its iinit must record that it ran. */
std::vector<std::uint8_t> document_class_abc() {
  std::vector<std::uint8_t> out;
  put_u16(out, 16);
  put_u16(out, 46);
  put_u30(out, 0);
  put_u30(out, 0);
  put_u30(out, 0);
  put_u30(out, 6);
  put_string(out, "DocumentClass");
  put_string(out, "flash.display");
  put_string(out, "MovieClip");
  put_string(out, "documentReady");
  put_string(out, "yes");
  put_u30(out, 3);
  put_u8(out, 0x16);
  put_u30(out, 0);
  put_u8(out, 0x16);
  put_u30(out, 2);
  put_u30(out, 0);
  put_u30(out, 4);
  put_u8(out, 0x07);
  put_u30(out, 1);
  put_u30(out, 1);
  put_u8(out, 0x07);
  put_u30(out, 2);
  put_u30(out, 3);
  put_u8(out, 0x07);
  put_u30(out, 1);
  put_u30(out, 4);
  put_u30(out, 2);
  for (int method = 0; method < 2; ++method) {
    put_u30(out, 0);
    put_u30(out, 0);
    put_u30(out, 0);
    put_u8(out, 0);
  }
  put_u30(out, 0);
  put_u30(out, 1);
  put_u30(out, 1);
  put_u30(out, 2);
  put_u8(out, 0);
  put_u30(out, 0);
  put_u30(out, 1);
  put_u30(out, 0);
  put_u30(out, 0);
  put_u30(out, 0);
  put_u30(out, 0);
  put_u30(out, 2);

  std::vector<std::uint8_t> class_init;
  class_init.push_back(0x47);
  put_u30(out, 0);
  put_u30(out, 1);
  put_u30(out, 0);
  put_u30(out, 0);
  put_u30(out, 0);
  put_u30(out, static_cast<std::uint32_t>(class_init.size()));
  out.insert(out.end(), class_init.begin(), class_init.end());
  put_u30(out, 0);
  put_u30(out, 0);

  std::vector<std::uint8_t> constructor_code;
  constructor_code.push_back(0x62);
  put_u30(constructor_code, 0);
  constructor_code.push_back(0x30);
  constructor_code.push_back(0x5D);
  put_u30(constructor_code, 3);
  constructor_code.push_back(0x2C);
  put_u30(constructor_code, 5);
  constructor_code.push_back(0x68);
  put_u30(constructor_code, 3);
  constructor_code.push_back(0x47);
  put_u30(out, 1);
  put_u30(out, 2);
  put_u30(out, 1);
  put_u30(out, 0);
  put_u30(out, 1);
  put_u30(out, static_cast<std::uint32_t>(constructor_code.size()));
  out.insert(out.end(), constructor_code.begin(), constructor_code.end());
  put_u30(out, 0);
  put_u30(out, 0);
  return out;
}

std::vector<std::byte> document_class_movie() {
  const std::vector<std::uint8_t> abc_bytes = document_class_abc();
  SwfBuilder abc_payload;
  abc_payload.u32(0);
  abc_payload.text("document");
  abc_payload.bytes(std::span<const std::byte>(
      reinterpret_cast<const std::byte*>(abc_bytes.data()), abc_bytes.size()));

  SwfBuilder symbols;
  symbols.u16(1);
  symbols.u16(0);
  symbols.text("DocumentClass");

  SwfBuilder builder;
  builder.rect(0, 400, 0, 200, 9);
  builder.u16(24u * 256u);
  builder.u16(1);
  builder.tag(fo4::swf::tag::kDoAbc2, abc_payload.body, true);
  builder.tag(fo4::swf::tag::kSymbolClass, symbols.body, false);
  builder.tag(fo4::swf::tag::kShowFrame);
  builder.tag(fo4::swf::tag::kEnd);
  return wrap_body(builder.body, 10, false, false);
}

std::string global_string(fo4::avm2::Runtime& runtime, const std::string& name) {
  fo4::avm2::Multiname multiname;
  multiname.name = name;
  multiname.namespaces.push_back(fo4::avm2::Namespace{});
  fo4::avm2::Value value;
  if (!runtime.global_object()->read_value(multiname, value) || !value.is_string()) {
    return {};
  }
  return value.as_string();
}

}  // namespace

TEST(Avm2DocumentClass, ConstructorRunsOnTheRootClipBeforeFrameOne) {
  const std::vector<std::byte> bytes = document_class_movie();
  const fo4::swf::SwfReadResult read = fo4::swf::read_swf(bytes);
  ASSERT_TRUE(read.ok()) << fo4::swf::status_name(read.status);

  fo4::avm2::Runtime runtime;
  fo4::avm2::Interpreter interpreter{runtime};
  ASSERT_TRUE(runtime.link().ok());
  const std::shared_ptr<fo4::avm2::SwfRuntime> movie =
      fo4::avm2::SwfRuntime::create(runtime, interpreter, read.file);
  ASSERT_NE(movie, nullptr) << fo4::avm2::SwfRuntime::last_error();
  ASSERT_NE(movie->root(), nullptr);
  ASSERT_NE(movie->root()->display_class(), nullptr);
  EXPECT_EQ(movie->root()->display_class()->name, "DocumentClass");
  EXPECT_EQ(global_string(runtime, "documentReady"), "yes");
}
