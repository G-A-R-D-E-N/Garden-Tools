#include "fo4/abc/abc.hpp"
#include "fo4/c_api.h"
#include "fo4/c_api_runtime.h"
#include "abc_test_support.hpp"
#include "avm2_test_support.hpp"
#include "fo4/swf/swf.hpp"
#include "swf_movie_builder.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace {

namespace abc = fo4::abc;

using swf_test::SwfBuilder;
using swf_test::wrap_body;

struct RuntimeDeleter {
  void operator()(radicle_runtime* runtime) const { radicle_runtime_close(runtime); }
};

struct MovieDeleter {
  void operator()(radicle_movie* movie) const { radicle_movie_close(movie); }
};

struct SnapshotDeleter {
  void operator()(radicle_runtime_snapshot* snapshot) const {
    radicle_runtime_snapshot_destroy(snapshot);
  }
};

using RuntimeHandle = std::unique_ptr<radicle_runtime, RuntimeDeleter>;
using MovieHandle = std::unique_ptr<radicle_movie, MovieDeleter>;
using SnapshotHandle = std::unique_ptr<radicle_runtime_snapshot, SnapshotDeleter>;

std::vector<std::byte> sprite_movie() {
  SwfBuilder sprite;
  sprite.u16(1);
  sprite.tag(fo4::swf::tag::kShowFrame);
  SwfBuilder builder;
  builder.rect(0, 200, 0, 100, 9);
  builder.u16(24u * 256u);
  builder.u16(2);
  builder.tag(fo4::swf::tag::kDefineSprite, sprite.body, true);
  SwfBuilder place;
  place.u8(1);
  place.u16(7);
  place.u16(1);
  builder.tag(fo4::swf::tag::kPlaceObject2, place.body, false);
  builder.tag(fo4::swf::tag::kShowFrame);
  SwfBuilder second;
  second.u8(0x06);
  second.u16(2);
  second.u16(7);
  second.push_bits(0, 1);
  second.push_bits(0, 1);
  second.push_bits(10, 5);
  second.push_bits(200, 10);
  second.push_bits(0, 10);
  second.align();
  builder.tag(fo4::swf::tag::kPlaceObject2, second.body, false);
  builder.tag(fo4::swf::tag::kShowFrame);
  builder.tag(fo4::swf::tag::kEnd);
  return swf_test::wrap_body(builder.body, 10, false, false);
}

std::vector<std::byte> external_interface_movie(std::size_t* out_calls) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  const std::uint32_t external = script.multiname("ExternalInterface", "flash.external");
  const std::uint32_t call_name = script.multiname("call");
  code.getlex(external);
  code.pushstring(script.string_of("Ping"));
  code.callproperty(call_name, 1);
  code.pop();
  code.returnvoid();
  const std::uint32_t method = script.method("init", code.bytes(), 0);
  abc::AbcFile file = script.finish(method);
  std::vector<std::uint8_t> bytes;
  EXPECT_EQ(file.serialize(bytes), abc::AbcStatus::Ok);
  *out_calls = 0;

  SwfBuilder builder;
  builder.rect(0, 200, 0, 100, 9);
  builder.u16(24u * 256u);
  builder.u16(1);
  builder.tag(fo4::swf::tag::kShowFrame);
  SwfBuilder payload;
  payload.u32(0);
  payload.text("boot");
  payload.bytes(std::span<const std::byte>(reinterpret_cast<const std::byte*>(bytes.data()),
                                           bytes.size()));
  builder.tag(fo4::swf::tag::kDoAbc2, payload.body, true);
  builder.tag(fo4::swf::tag::kEnd);
  return swf_test::wrap_body(builder.body, 10, false, false);
}

struct Host {
  std::size_t calls = 0;
  std::vector<std::string> methods;
};

radicle_status host_call(void* user, const char* method, const char* const*, size_t,
                         uint8_t** out_text, size_t* out_size) {
  auto* host = static_cast<Host*>(user);
  host->calls += 1;
  host->methods.push_back(method);
  const std::string answer = "pong";
  auto* buffer = static_cast<uint8_t*>(std::malloc(answer.size() + 1));
  if (buffer == nullptr) return RADICLE_STATUS_OUT_OF_MEMORY;
  std::memcpy(buffer, answer.data(), answer.size());
  buffer[answer.size()] = 0;
  *out_text = buffer;
  *out_size = answer.size();
  return RADICLE_STATUS_OK;
}

std::vector<std::uint64_t> run_replay(const std::vector<std::byte>& movie_bytes,
                                      const std::vector<radicle_runtime_input>& inputs,
                                      bool* created, std::size_t* out_host_calls) {
  radicle_runtime_host host{};
  Host calls;
  host.user = &calls;
  host.host_call = host_call;
  radicle_runtime* raw_runtime = nullptr;
  EXPECT_EQ(radicle_runtime_open(&host, &raw_runtime), RADICLE_STATUS_OK) << radicle_last_error();
  RuntimeHandle runtime(raw_runtime);
  radicle_movie* raw_movie = nullptr;
  const radicle_status status = radicle_runtime_open_movie(
      runtime.get(), reinterpret_cast<const std::uint8_t*>(movie_bytes.data()), movie_bytes.size(),
      &raw_movie);
  *created = status == RADICLE_STATUS_OK;
  if (!*created) return {};
  MovieHandle movie(raw_movie);
  std::vector<std::uint64_t> hashes(8, 0);
  size_t count = 0;
  EXPECT_EQ(radicle_movie_replay(movie.get(), inputs.data(), inputs.size(), 1.0 / 24.0, 8,
                                 hashes.data(), hashes.size(), &count),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(count, 8u);
  *out_host_calls = calls.calls;
  return hashes;
}

abc::AbcTrait method_trait(std::uint32_t name, std::uint32_t method) {
  abc::AbcTrait trait;
  trait.name = name;
  trait.kind = abc::AbcTraitKind::Method;
  trait.method_index = method;
  return trait;
}

abc::AbcFile member_root_file() {
  avm2_test::ScriptBuilder script;
  const std::uint32_t root = script.multiname("MemberRoot");
  const std::uint32_t movie_clip = script.multiname("MovieClip", "flash.display");
  const std::uint32_t string_type = script.multiname("String");
  const std::uint32_t number_type = script.multiname("Number");
  const std::uint32_t plain = script.multiname("Plain");
  const std::uint32_t ping_name = script.multiname("Ping");
  const std::uint32_t add_name = script.multiname("Add");
  avm2_test::CodeBuilder constructor;
  constructor.getlocal0();
  constructor.pushscope();
  constructor.getlocal0();
  constructor.pushstring(script.string_of("text"));
  constructor.initproperty(plain);
  constructor.returnvoid();
  const std::uint32_t constructor_method = script.method("MemberRoot", constructor.bytes(), 1);
  avm2_test::CodeBuilder ping;
  ping.getlocal(1);
  ping.returnvalue();
  const std::uint32_t ping_method = script.method("Ping", ping.bytes(), 2);
  avm2_test::CodeBuilder add;
  add.getlocal(1);
  add.getlocal(2);
  add.add();
  add.returnvalue();
  const std::uint32_t add_method = script.method("Add", add.bytes(), 3);
  const std::uint32_t class_init = script.method("cinit", {0x47}, 1);
  const std::uint32_t script_init = script.method("init", {0x47}, 1);
  abc::AbcFile file = script.finish(script_init);
  file.methods[ping_method].param_count = 1;
  file.methods[ping_method].param_types = {string_type};
  file.methods[ping_method].return_type = string_type;
  file.methods[add_method].param_count = 2;
  file.methods[add_method].param_types = {number_type, number_type};
  file.methods[add_method].return_type = number_type;

  abc::AbcInstanceInfo instance;
  instance.name = root;
  instance.super_index = movie_clip;
  instance.iinit = constructor_method;
  instance.traits = {method_trait(ping_name, ping_method), method_trait(add_name, add_method)};
  file.instances.push_back(instance);
  abc::AbcClassInfo klass;
  klass.cinit = class_init;
  file.classes.push_back(klass);
  abc::AbcTrait class_trait;
  class_trait.name = root;
  class_trait.kind = abc::AbcTraitKind::Class;
  class_trait.class_index = 0;
  file.scripts.front().traits.push_back(class_trait);
  return file;
}

std::vector<std::byte> root_class_movie(abc::AbcFile file, const std::string& root_name) {
  std::vector<std::uint8_t> abc_bytes;
  EXPECT_EQ(file.serialize(abc_bytes), abc::AbcStatus::Ok);
  SwfBuilder abc_payload;
  abc_payload.u32(0);
  abc_payload.text(root_name);
  abc_payload.bytes(std::span<const std::byte>(reinterpret_cast<const std::byte*>(abc_bytes.data()),
                                               abc_bytes.size()));
  SwfBuilder symbols;
  symbols.u16(1);
  symbols.u16(0);
  symbols.text(root_name);
  SwfBuilder builder;
  builder.rect(0, 200, 0, 100, 9);
  builder.u16(24u * 256u);
  builder.u16(1);
  builder.tag(fo4::swf::tag::kDoAbc2, abc_payload.body, true);
  builder.tag(fo4::swf::tag::kSymbolClass, symbols.body, false);
  builder.tag(fo4::swf::tag::kShowFrame);
  builder.tag(fo4::swf::tag::kEnd);
  return swf_test::wrap_body(builder.body, 10, false, false);
}

std::vector<std::byte> member_root_movie() {
  return root_class_movie(member_root_file(), "MemberRoot");
}

abc::AbcFile bgs_root_file() {
  avm2_test::ScriptBuilder script;
  const std::uint32_t root = script.multiname("BgsRoot");
  const std::uint32_t movie_clip = script.multiname("MovieClip", "flash.display");
  const std::uint32_t code_object = script.multiname("BGSCodeObj");
  const std::uint32_t play_sound = script.multiname("playSound");
  const std::uint32_t apply = script.multiname("apply");
  const std::uint32_t play_name = script.multiname("Play");
  const std::uint32_t argument = script.string_of("x");
  avm2_test::CodeBuilder constructor;
  constructor.getlocal0();
  constructor.pushscope();
  constructor.getlocal0();
  constructor.newobject(0);
  constructor.initproperty(code_object);
  constructor.returnvoid();
  const std::uint32_t constructor_method = script.method("BgsRoot", constructor.bytes(), 1);
  avm2_test::CodeBuilder play;
  play.getlocal0();
  play.getproperty(code_object);
  play.getproperty(play_sound);
  play.pushnull();
  play.pushstring(argument);
  play.newarray(1);
  play.callproperty(apply, 2);
  play.returnvalue();
  const std::uint32_t play_method = script.method("Play", play.bytes(), 1);
  const std::uint32_t class_init = script.method("cinit", {0x47}, 1);
  const std::uint32_t script_init = script.method("init", {0x47}, 1);
  abc::AbcFile file = script.finish(script_init);

  abc::AbcInstanceInfo instance;
  instance.name = root;
  instance.super_index = movie_clip;
  instance.iinit = constructor_method;
  instance.traits = {method_trait(play_name, play_method)};
  file.instances.push_back(instance);
  abc::AbcClassInfo klass;
  klass.cinit = class_init;
  file.classes.push_back(klass);
  abc::AbcTrait class_trait;
  class_trait.name = root;
  class_trait.kind = abc::AbcTraitKind::Class;
  class_trait.class_index = 0;
  file.scripts.front().traits.push_back(class_trait);
  return file;
}

std::vector<std::byte> bgs_root_movie() { return root_class_movie(bgs_root_file(), "BgsRoot"); }

radicle_status play_through_bgs(int bgs_host, Host& calls) {
  radicle_runtime_host host{};
  host.user = &calls;
  host.host_call = host_call;
  radicle_runtime* raw_runtime = nullptr;
  EXPECT_EQ(radicle_runtime_open(&host, &raw_runtime), RADICLE_STATUS_OK) << radicle_last_error();
  RuntimeHandle runtime(raw_runtime);
  const std::vector<std::byte> bytes = bgs_root_movie();
  radicle_movie* raw_movie = nullptr;
  EXPECT_EQ(radicle_runtime_open_movie_with_bgs(
                runtime.get(), reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size(),
                bgs_host, &raw_movie),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  MovieHandle movie(raw_movie);
  uint8_t* out_text = nullptr;
  size_t out_size = 0;
  const radicle_status called =
      radicle_movie_call(movie.get(), "Play", nullptr, 0, &out_text, &out_size);
  radicle_free_bytes(out_text);
  return called;
}

MovieHandle open_member_movie(radicle_runtime* runtime, const std::vector<std::byte>& bytes) {
  radicle_movie* raw_movie = nullptr;
  EXPECT_EQ(radicle_runtime_open_movie(runtime, reinterpret_cast<const std::uint8_t*>(bytes.data()),
                                       bytes.size(), &raw_movie),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  return MovieHandle(raw_movie);
}

}  // namespace

TEST(CApiRuntime, AdvancingProducesAStableSnapshotSequence) {
  radicle_runtime_host host{};
  radicle_runtime* raw_runtime = nullptr;
  ASSERT_EQ(radicle_runtime_open(&host, &raw_runtime), RADICLE_STATUS_OK) << radicle_last_error();
  RuntimeHandle runtime(raw_runtime);
  const std::vector<std::byte> bytes = sprite_movie();
  radicle_movie* raw_movie = nullptr;
  ASSERT_EQ(radicle_runtime_open_movie(runtime.get(),
                                      reinterpret_cast<const std::uint8_t*>(bytes.data()),
                                      bytes.size(), &raw_movie),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  MovieHandle movie(raw_movie);

  radicle_runtime_snapshot* raw_snapshot = nullptr;
  ASSERT_EQ(radicle_movie_snapshot(movie.get(), &raw_snapshot), RADICLE_STATUS_OK);
  SnapshotHandle first(raw_snapshot);
  EXPECT_GT(radicle_runtime_snapshot_entry_count(first.get()), 0u);
  radicle_runtime_entry entry{};
  ASSERT_EQ(radicle_runtime_snapshot_entry_at(first.get(), 0, &entry), RADICLE_STATUS_OK);
  EXPECT_NE(entry.path, nullptr);
  const std::uint64_t first_hash = radicle_runtime_snapshot_hash(first.get());
  EXPECT_NE(first_hash, 0u);

  ASSERT_EQ(radicle_movie_advance(movie.get(), 1.0 / 24.0), RADICLE_STATUS_OK);
  radicle_runtime_snapshot* moved_raw = nullptr;
  ASSERT_EQ(radicle_movie_snapshot(movie.get(), &moved_raw), RADICLE_STATUS_OK);
  SnapshotHandle moved(moved_raw);
  EXPECT_NE(radicle_runtime_snapshot_hash(moved.get()), first_hash);
}

TEST(CApiRuntime, ReplayIsDeterministicAndForwardsHostCalls) {
  std::size_t boot_calls = 0;
  const std::vector<std::byte> bytes = external_interface_movie(&boot_calls);
  std::vector<radicle_runtime_input> inputs(3);
  inputs[0].time_ms = 0.0;
  inputs[0].kind = RADICLE_INPUT_MOUSE_MOVE;
  inputs[0].x = 10.0;
  inputs[0].y = 20.0;
  inputs[1].time_ms = 20.0;
  inputs[1].kind = RADICLE_INPUT_MOUSE_DOWN;
  inputs[1].button = 0;
  inputs[2].time_ms = 40.0;
  inputs[2].kind = RADICLE_INPUT_KEY_DOWN;
  inputs[2].key_code = 32;

  std::size_t first_calls = 0;
  bool first_created = false;
  const std::vector<std::uint64_t> first = run_replay(bytes, inputs, &first_created, &first_calls);
  ASSERT_TRUE(first_created) << radicle_last_error();
  EXPECT_EQ(first_calls, 1u);
  std::size_t second_calls = 0;
  bool second_created = false;
  const std::vector<std::uint64_t> second = run_replay(bytes, inputs, &second_created, &second_calls);
  ASSERT_TRUE(second_created);
  EXPECT_EQ(first, second);
}

TEST(CApiRuntime, RejectsAnOutOfRangeInputKind) {
  radicle_runtime_host host{};
  radicle_runtime* raw_runtime = nullptr;
  ASSERT_EQ(radicle_runtime_open(&host, &raw_runtime), RADICLE_STATUS_OK);
  RuntimeHandle runtime(raw_runtime);
  const std::vector<std::byte> bytes = sprite_movie();
  radicle_movie* raw_movie = nullptr;
  ASSERT_EQ(radicle_runtime_open_movie(runtime.get(),
                                      reinterpret_cast<const std::uint8_t*>(bytes.data()),
                                      bytes.size(), &raw_movie),
            RADICLE_STATUS_OK);
  MovieHandle movie(raw_movie);
  radicle_runtime_input input{};
  input.kind = 200;
  EXPECT_EQ(radicle_movie_input(movie.get(), &input), RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST(CApiRuntime, MovieCallReturnsTypedResultsFromTheRootMethods) {
  radicle_runtime_host host{};
  radicle_runtime* raw_runtime = nullptr;
  ASSERT_EQ(radicle_runtime_open(&host, &raw_runtime), RADICLE_STATUS_OK) << radicle_last_error();
  RuntimeHandle runtime(raw_runtime);
  const std::vector<std::byte> bytes = member_root_movie();
  MovieHandle movie = open_member_movie(runtime.get(), bytes);
  ASSERT_NE(movie.get(), nullptr) << radicle_last_error();

  radicle_call_arg text{};
  text.kind = RADICLE_CALL_ARG_STRING;
  text.text = "hello";
  uint8_t* out_text = nullptr;
  size_t out_size = 0;
  ASSERT_EQ(radicle_movie_call(movie.get(), "Ping", &text, 1, &out_text, &out_size),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_NE(out_text, nullptr);
  EXPECT_EQ(std::string(reinterpret_cast<const char*>(out_text), out_size), "hello");
  radicle_free_bytes(out_text);

  radicle_call_arg numbers[2]{};
  numbers[0].kind = RADICLE_CALL_ARG_NUMBER;
  numbers[0].number = 2.0;
  numbers[1].kind = RADICLE_CALL_ARG_NUMBER;
  numbers[1].number = 3.0;
  out_text = nullptr;
  ASSERT_EQ(radicle_movie_call(movie.get(), "Add", numbers, 2, &out_text, &out_size),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_NE(out_text, nullptr);
  EXPECT_EQ(std::string(reinterpret_cast<const char*>(out_text), out_size), "5");
  radicle_free_bytes(out_text);

  radicle_call_arg nothing{};
  nothing.kind = RADICLE_CALL_ARG_NULL;
  out_text = nullptr;
  out_size = 99;
  EXPECT_EQ(radicle_movie_call(movie.get(), "Ping", &nothing, 1, &out_text, &out_size),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(out_text, nullptr);
  EXPECT_EQ(out_size, 0u);
}

TEST(CApiRuntime, MovieCallFailsCleanlyForAbsentOrNonMethodMembers) {
  radicle_runtime_host host{};
  radicle_runtime* raw_runtime = nullptr;
  ASSERT_EQ(radicle_runtime_open(&host, &raw_runtime), RADICLE_STATUS_OK) << radicle_last_error();
  RuntimeHandle runtime(raw_runtime);
  const std::vector<std::byte> bytes = member_root_movie();
  MovieHandle movie = open_member_movie(runtime.get(), bytes);
  ASSERT_NE(movie.get(), nullptr) << radicle_last_error();

  uint8_t* out_text = nullptr;
  size_t out_size = 0;
  EXPECT_EQ(radicle_movie_call(movie.get(), "NoSuchMember", nullptr, 0, &out_text, &out_size),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_NE(std::string(radicle_last_error()).find("NoSuchMember"), std::string::npos);
  EXPECT_EQ(out_text, nullptr);

  EXPECT_EQ(radicle_movie_call(movie.get(), "Plain", nullptr, 0, &out_text, &out_size),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_NE(std::string(radicle_last_error()).find("not a method"), std::string::npos);
  EXPECT_EQ(out_text, nullptr);

  radicle_call_arg bad{};
  bad.kind = 200;
  EXPECT_EQ(radicle_movie_call(movie.get(), "Ping", &bad, 1, &out_text, &out_size),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_movie_call(movie.get(), nullptr, nullptr, 0, &out_text, &out_size),
            RADICLE_STATUS_INVALID_ARGUMENT);
}

// Must reach the host only when bgs_host is set; detects an open that drops the flag or ignores it.
TEST(CApiRuntime, BgsOpenInstallsTheCodeObjectFunctionsOnlyWhenRequested) {
  Host off_host;
  play_through_bgs(0, off_host);
  EXPECT_EQ(off_host.calls, 0u);

  Host on_host;
  EXPECT_EQ(play_through_bgs(1, on_host), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(on_host.methods, std::vector<std::string>{"bgs.playSound"});
}

// Must leave *out_movie NULL on every refusal; detects a refusal that keeps a stale handle.
TEST(CApiRuntime, BgsOpenClearsItsMovieOutputOnEveryRefusal) {
  radicle_runtime_host host{};
  radicle_runtime* raw_runtime = nullptr;
  ASSERT_EQ(radicle_runtime_open(&host, &raw_runtime), RADICLE_STATUS_OK) << radicle_last_error();
  RuntimeHandle runtime(raw_runtime);
  const std::vector<std::byte> bytes = bgs_root_movie();
  const auto* data = reinterpret_cast<const std::uint8_t*>(bytes.data());
  const std::vector<std::uint8_t> garbage(16, 0xFF);
  radicle_movie* const sentinel = reinterpret_cast<radicle_movie*>(&host);
  radicle_movie* out = sentinel;

  EXPECT_EQ(radicle_runtime_open_movie_with_bgs(nullptr, data, bytes.size(), 1, &out),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(out, nullptr);

  out = sentinel;
  EXPECT_EQ(radicle_runtime_open_movie_with_bgs(runtime.get(), data, bytes.size(), 1, nullptr),
            RADICLE_STATUS_INVALID_ARGUMENT);

  out = sentinel;
  EXPECT_EQ(radicle_runtime_open_movie_with_bgs(runtime.get(), nullptr, 0, 1, &out),
            RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(out, nullptr);

  out = sentinel;
  EXPECT_EQ(radicle_runtime_open_movie_with_bgs(runtime.get(), garbage.data(), garbage.size(), 1,
                                                &out),
            RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(out, nullptr);
}
