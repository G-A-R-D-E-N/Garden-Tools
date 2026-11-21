#include "abc_decompile_fixtures.hpp"
#include "fo4/abc/abc.hpp"
#include "fo4/abc/decompile.hpp"
#include "fo4/c_api.h"
#include "fo4/c_api_swf.h"
#include "fo4/c_api_swf_as3.h"
#include "fo4/swf/swf.hpp"
#include "swf_movie_builder.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace {

using fo4::abc::AbcFile;
using fo4::test::CodeBuilder;
using fo4::test::decompile_fixture_file;
using swf_test::SwfBuilder;

const char* const kFullClass =
    "class Test extends Object implements click {\n"
    "  function Test() {\n"
    "  }\n"
    "  function onClick(click:void = 7):void {\n"
    "    addEventListener.addEventListener(\"click\", handler);\n"
    "  }\n"
    "  var counter:void;\n"
    "  const counter:void = 7;\n"
    "  /* class Inner -> Test */\n"
    "  function helper():void {\n"
    "  }\n"
    "}\n"
    "\n";

const char* const kClassCs =
    "public class Test : click {\n"
    "  public Test() {\n"
    "  }\n"
    "  public virtual void onClick(void click = 7) {\n"
    "    AddEventListener.AddEventListener(\"click\", handler);\n"
    "  }\n"
    "  public void counter;\n"
    "  public readonly void counter = 7;\n"
    "  /* class Inner -> Test */\n"
    "  public virtual void helper() {\n"
    "  }\n"
    "}\n";

struct SwfDeleter {
  void operator()(radicle_swf* swf) const { radicle_swf_destroy(swf); }
};

struct As3Deleter {
  void operator()(radicle_swf_as3* as3) const { radicle_swf_as3_destroy(as3); }
};

using SwfHandle = std::unique_ptr<radicle_swf, SwfDeleter>;
using As3Handle = std::unique_ptr<radicle_swf_as3, As3Deleter>;

AbcFile class_file() {
  AbcFile file = decompile_fixture_file(CodeBuilder{}.op(fo4::test::kOpReturnVoid).bytes());
  file.instances[0].flags = 0x01;
  file.bodies[2].exceptions.clear();
  return file;
}

std::vector<std::byte> movie_with_abc(const std::vector<std::uint8_t>& abc) {
  SwfBuilder payload;
  payload.u32(0);
  payload.text("as3");
  payload.bytes(std::span<const std::byte>(reinterpret_cast<const std::byte*>(abc.data()), abc.size()));
  SwfBuilder body;
  body.rect(0, 200, 0, 100, 9);
  body.u16(24u * 256u);
  body.u16(1);
  body.tag(fo4::swf::tag::kDoAbc2, payload.body, true);
  body.tag(fo4::swf::tag::kEnd);
  return swf_test::wrap_body(body.body, 10, false, false);
}

std::vector<std::byte> movie_with(const AbcFile& file) {
  std::vector<std::uint8_t> abc;
  EXPECT_EQ(file.serialize(abc), fo4::abc::AbcStatus::Ok);
  return movie_with_abc(abc);
}

SwfHandle open(const std::vector<std::byte>& bytes) {
  radicle_swf* raw = nullptr;
  EXPECT_EQ(radicle_swf_open_bytes(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size(), &raw),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  return SwfHandle(raw);
}

As3Handle decompile(const radicle_swf* swf) {
  radicle_swf_as3* raw = nullptr;
  EXPECT_EQ(radicle_swf_abc_decompile_ex(swf, 0, RADICLE_SWF_AS3_LANGUAGE_AS3, RADICLE_SWF_AS3_SCOPE_FILE, 0, &raw), RADICLE_STATUS_OK) << radicle_last_error();
  return As3Handle(raw);
}

As3Handle decompile_ex(const radicle_swf* swf, radicle_swf_as3_language language, radicle_swf_as3_scope scope,
                       std::uint32_t index) {
  radicle_swf_as3* raw = nullptr;
  EXPECT_EQ(radicle_swf_abc_decompile_ex(swf, 0, language, scope, index, &raw), RADICLE_STATUS_OK)
      << radicle_last_error();
  return As3Handle(raw);
}

std::string text_of(const radicle_swf_as3* as3) {
  const char* text = nullptr;
  std::size_t size = 0;
  EXPECT_EQ(radicle_swf_as3_text(as3, &text, &size), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_NE(text, nullptr);
  return text == nullptr ? std::string() : std::string(text, size);
}

}  // namespace

TEST(CApiSwfAs3, DecompilesABlockToItsTextHashAndCounts) {
  SwfHandle swf = open(movie_with(class_file()));
  ASSERT_NE(swf, nullptr);
  As3Handle as3 = decompile(swf.get());
  ASSERT_NE(as3, nullptr);

  radicle_swf_as3_info info{};
  ASSERT_EQ(radicle_swf_as3_get_info(as3.get(), &info), RADICLE_STATUS_OK) << radicle_last_error();
  const std::string expected = kFullClass;
  EXPECT_EQ(info.text_size, expected.size());
  EXPECT_EQ(info.hash, fo4::abc::as3_text_hash(expected));
  EXPECT_EQ(info.class_count, 1u);
  EXPECT_EQ(info.function_count, 4u);
  EXPECT_EQ(info.refused_count, 0u);

  const char* text = nullptr;
  std::size_t size = 0;
  ASSERT_EQ(radicle_swf_as3_text(as3.get(), &text, &size), RADICLE_STATUS_OK);
  EXPECT_EQ(std::string(text, size), expected);
  EXPECT_EQ(text[size], '\0');
}

TEST(CApiSwfAs3, MatchesTheInMemoryWriterAndIsDeterministic) {
  const AbcFile file = class_file();
  const fo4::abc::AbcAs3Output direct = fo4::abc::write_as3(file);
  SwfHandle swf = open(movie_with(file));
  ASSERT_NE(swf, nullptr);
  As3Handle first = decompile(swf.get());
  As3Handle second = decompile(swf.get());
  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);
  EXPECT_EQ(text_of(first.get()), direct.text);
  EXPECT_EQ(text_of(second.get()), direct.text);
  radicle_swf_as3_info one{};
  radicle_swf_as3_info two{};
  ASSERT_EQ(radicle_swf_as3_get_info(first.get(), &one), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_swf_as3_get_info(second.get(), &two), RADICLE_STATUS_OK);
  EXPECT_EQ(one.hash, direct.hash);
  EXPECT_EQ(two.hash, direct.hash);
}

TEST(CApiSwfAs3, ListsARefusedMethodAndKeepsItsSignature) {
  AbcFile file = class_file();
  file.bodies[1].code = CodeBuilder{}
                            .op_u30(0x40, 1)
                            .op(fo4::test::kOpPop)
                            .op(fo4::test::kOpReturnVoid)
                            .bytes();
  SwfHandle swf = open(movie_with(file));
  ASSERT_NE(swf, nullptr);
  As3Handle as3 = decompile(swf.get());
  ASSERT_NE(as3, nullptr);

  radicle_swf_as3_info info{};
  ASSERT_EQ(radicle_swf_as3_get_info(as3.get(), &info), RADICLE_STATUS_OK);
  EXPECT_EQ(info.refused_count, 1u);
  EXPECT_EQ(info.function_count, 4u);
  EXPECT_NE(text_of(as3.get()).find("  function onClick(click:void = 7):void {\n    /* refused: UnsupportedOpcode at offset 0: "),
            std::string::npos);

  radicle_swf_as3_issue issue{};
  ASSERT_EQ(radicle_swf_as3_issue_at(as3.get(), 0, &issue), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(issue.status, static_cast<std::uint8_t>(fo4::abc::AbcDecompileStatus::UnsupportedOpcode));
  EXPECT_STREQ(issue.status_name, "UnsupportedOpcode");
  EXPECT_EQ(issue.method, 1u);
  EXPECT_EQ(issue.offset, 0u);
  ASSERT_NE(issue.message, nullptr);
  EXPECT_GT(std::strlen(issue.message), 0u);

  radicle_swf_as3_issue past{};
  EXPECT_EQ(radicle_swf_as3_issue_at(as3.get(), 1, &past), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(past.status_name, nullptr);
}

TEST(CApiSwfAs3, OutlivesTheMovieItWasBuiltFrom) {
  SwfHandle swf = open(movie_with(class_file()));
  ASSERT_NE(swf, nullptr);
  As3Handle as3 = decompile(swf.get());
  ASSERT_NE(as3, nullptr);
  swf.reset();
  EXPECT_EQ(text_of(as3.get()), kFullClass);
}

TEST(CApiSwfAs3, RefusesMissingArgumentsAnIndexPastTheBlocksAndAnUnparsedBlock) {
  SwfHandle swf = open(movie_with(class_file()));
  ASSERT_NE(swf, nullptr);
  radicle_swf_as3* out = nullptr;
  EXPECT_EQ(radicle_swf_abc_decompile_ex(nullptr, 0, RADICLE_SWF_AS3_LANGUAGE_AS3, RADICLE_SWF_AS3_SCOPE_FILE, 0, &out), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_swf_abc_decompile_ex(swf.get(), 0, RADICLE_SWF_AS3_LANGUAGE_AS3, RADICLE_SWF_AS3_SCOPE_FILE, 0, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_swf_abc_decompile_ex(swf.get(), radicle_swf_abc_count(swf.get()), RADICLE_SWF_AS3_LANGUAGE_AS3, RADICLE_SWF_AS3_SCOPE_FILE, 0, &out),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(out, nullptr);

  radicle_swf_as3_info info{};
  EXPECT_EQ(radicle_swf_as3_get_info(nullptr, &info), RADICLE_STATUS_INVALID_ARGUMENT);
  const char* text = nullptr;
  std::size_t size = 0;
  EXPECT_EQ(radicle_swf_as3_text(nullptr, &text, &size), RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_swf_as3_destroy(nullptr);

  SwfHandle broken = open(movie_with_abc({0x01, 0x02, 0x03}));
  ASSERT_NE(broken, nullptr);
  ASSERT_EQ(radicle_swf_abc_count(broken.get()), 1u);
  EXPECT_EQ(radicle_swf_abc_decompile_ex(broken.get(), 0, RADICLE_SWF_AS3_LANGUAGE_AS3, RADICLE_SWF_AS3_SCOPE_FILE, 0, &out), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(out, nullptr);
}

TEST(CApiSwfAs3, DecompilesToCSharpAndMatchesTheInMemoryWriter) {
  const AbcFile file = class_file();
  SwfHandle swf = open(movie_with(file));
  ASSERT_NE(swf, nullptr);
  As3Handle as3 = decompile_ex(swf.get(), RADICLE_SWF_AS3_LANGUAGE_CSHARP, RADICLE_SWF_AS3_SCOPE_FILE, 0);
  ASSERT_NE(as3, nullptr);
  EXPECT_EQ(text_of(as3.get()), std::string(kClassCs) + "\n");
  const fo4::abc::AbcCsOutput direct = fo4::abc::write_cs(file);
  radicle_swf_as3_info info{};
  ASSERT_EQ(radicle_swf_as3_get_info(as3.get(), &info), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(info.hash, direct.hash);
  EXPECT_EQ(info.function_count, 4u);
  EXPECT_EQ(info.class_count, 1u);
  EXPECT_EQ(info.refused_count, 0u);

  As3Handle as3_ex = decompile_ex(swf.get(), RADICLE_SWF_AS3_LANGUAGE_AS3, RADICLE_SWF_AS3_SCOPE_FILE, 0);
  As3Handle plain = decompile(swf.get());
  ASSERT_NE(as3_ex, nullptr);
  ASSERT_NE(plain, nullptr);
  EXPECT_EQ(text_of(as3_ex.get()), text_of(plain.get()));
}

TEST(CApiSwfAs3, DecompilesOneClassOrOneMethodInEitherLanguage) {
  SwfHandle swf = open(movie_with(class_file()));
  ASSERT_NE(swf, nullptr);
  As3Handle cs_class = decompile_ex(swf.get(), RADICLE_SWF_AS3_LANGUAGE_CSHARP, RADICLE_SWF_AS3_SCOPE_CLASS, 0);
  ASSERT_NE(cs_class, nullptr);
  EXPECT_EQ(text_of(cs_class.get()), kClassCs);

  As3Handle as3_class = decompile_ex(swf.get(), RADICLE_SWF_AS3_LANGUAGE_AS3, RADICLE_SWF_AS3_SCOPE_CLASS, 0);
  ASSERT_NE(as3_class, nullptr);
  const std::string whole = kFullClass;
  EXPECT_EQ(text_of(as3_class.get()), whole.substr(0, whole.size() - 1));

  As3Handle cs_method = decompile_ex(swf.get(), RADICLE_SWF_AS3_LANGUAGE_CSHARP, RADICLE_SWF_AS3_SCOPE_METHOD, 2);
  ASSERT_NE(cs_method, nullptr);
  EXPECT_EQ(text_of(cs_method.get()), "public virtual void helper() {\n}\n");
  radicle_swf_as3_info info{};
  ASSERT_EQ(radicle_swf_as3_get_info(cs_method.get(), &info), RADICLE_STATUS_OK);
  EXPECT_EQ(info.function_count, 1u);
  EXPECT_EQ(info.class_count, 1u);

  As3Handle as3_method = decompile_ex(swf.get(), RADICLE_SWF_AS3_LANGUAGE_AS3, RADICLE_SWF_AS3_SCOPE_METHOD, 2);
  ASSERT_NE(as3_method, nullptr);
  EXPECT_EQ(text_of(as3_method.get()), "function helper():void {\n}\n");
}

TEST(CApiSwfAs3, RefusesAnUnknownScopeOrAnIndexPastItsTable) {
  SwfHandle swf = open(movie_with(class_file()));
  ASSERT_NE(swf, nullptr);
  radicle_swf_as3* out = nullptr;
  const auto call = [&](radicle_swf_as3_language language, radicle_swf_as3_scope scope, std::uint32_t index) {
    return radicle_swf_abc_decompile_ex(swf.get(), 0, language, scope, index, &out);
  };
  EXPECT_EQ(call(RADICLE_SWF_AS3_LANGUAGE_AS3, static_cast<radicle_swf_as3_scope>(3), 0),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(call(RADICLE_SWF_AS3_LANGUAGE_CSHARP, RADICLE_SWF_AS3_SCOPE_CLASS, 1), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(call(RADICLE_SWF_AS3_LANGUAGE_CSHARP, RADICLE_SWF_AS3_SCOPE_METHOD, 3), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(call(RADICLE_SWF_AS3_LANGUAGE_AS3, RADICLE_SWF_AS3_SCOPE_METHOD, 0xFFFFFFFFu),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(out, nullptr);
  EXPECT_EQ(radicle_swf_abc_decompile_ex(nullptr, 0, RADICLE_SWF_AS3_LANGUAGE_AS3, RADICLE_SWF_AS3_SCOPE_FILE, 0, &out),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_swf_abc_decompile_ex(swf.get(), 1, RADICLE_SWF_AS3_LANGUAGE_AS3, RADICLE_SWF_AS3_SCOPE_FILE, 0, &out),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_swf_abc_decompile_ex(swf.get(), 0, RADICLE_SWF_AS3_LANGUAGE_AS3, RADICLE_SWF_AS3_SCOPE_FILE, 0, nullptr),
            RADICLE_STATUS_INVALID_ARGUMENT);
}
