#include "fo4/abc/abc.hpp"
#include "fo4/c_api.h"
#include "fo4/c_api_runtime.h"
#include "fo4/c_api_runtime_art.h"
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
#include <string_view>
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

struct ArtDeleter {
  void operator()(radicle_art* art) const { radicle_art_destroy(art); }
};

using RuntimeHandle = std::unique_ptr<radicle_runtime, RuntimeDeleter>;
using MovieHandle = std::unique_ptr<radicle_movie, MovieDeleter>;
using SnapshotHandle = std::unique_ptr<radicle_runtime_snapshot, SnapshotDeleter>;
using ArtHandle = std::unique_ptr<radicle_art, ArtDeleter>;

RuntimeHandle open_runtime() {
  radicle_runtime_host host{};
  radicle_runtime* raw = nullptr;
  EXPECT_EQ(radicle_runtime_open(&host, &raw), RADICLE_STATUS_OK) << radicle_last_error();
  return RuntimeHandle(raw);
}

MovieHandle open_movie(radicle_runtime* runtime, const std::vector<std::byte>& bytes) {
  radicle_movie* raw = nullptr;
  EXPECT_EQ(radicle_runtime_open_movie_with_bgs(runtime, reinterpret_cast<const std::uint8_t*>(bytes.data()),
                                       bytes.size(), 0, &raw),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  return MovieHandle(raw);
}

ArtHandle open_art(radicle_movie* movie) {
  radicle_art* raw = nullptr;
  EXPECT_EQ(radicle_movie_art_open(movie, &raw), RADICLE_STATUS_OK) << radicle_last_error();
  return ArtHandle(raw);
}

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

std::vector<std::byte> styled_placement_movie() {
  SwfBuilder place;
  place.u8(0x12);
  place.u8(0x26);
  place.u16(1);
  place.u16(20);
  place.u16(3);
  place.u8(8);
  place.u8(1);
  place.u8(0);
  SwfBuilder builder;
  builder.rect(0, 200, 0, 100, 9);
  builder.u16(24u * 256u);
  builder.u16(1);
  builder.tag(fo4::swf::tag::kPlaceObject3, place.body, false);
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
  radicle_status status = RADICLE_STATUS_OK;
};

radicle_status host_call(void* user, const char* method, const char* const*, size_t,
                         uint8_t** out_text, size_t* out_size) {
  auto* host = static_cast<Host*>(user);
  host->calls += 1;
  host->methods.push_back(method);
  const std::string answer = "pong";
  auto* buffer = radicle_alloc_bytes(answer.size() + 1);
  if (buffer == nullptr) return RADICLE_STATUS_OUT_OF_MEMORY;
  std::memcpy(buffer, answer.data(), answer.size());
  buffer[answer.size()] = 0;
  *out_text = buffer;
  *out_size = answer.size();
  return host->status;
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
  const radicle_status status = radicle_runtime_open_movie_with_bgs(
      runtime.get(), reinterpret_cast<const std::uint8_t*>(movie_bytes.data()), movie_bytes.size(), 0,
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

abc::AbcFile member_root_file(bool frame_two_writes_a_missing_super_property = false) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t root = script.multiname("MemberRoot");
  const std::uint32_t movie_clip = script.multiname("MovieClip", "flash.display");
  const std::uint32_t string_type = script.multiname("String");
  const std::uint32_t number_type = script.multiname("Number");
  const std::uint32_t plain = script.multiname("Plain");
  const std::uint32_t ping_name = script.multiname("Ping");
  const std::uint32_t add_name = script.multiname("Add");
  const std::uint32_t frame_two_name = script.multiname("frame2");
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
  avm2_test::CodeBuilder frame_two;
  if (frame_two_writes_a_missing_super_property) {
    frame_two.getlocal0();
    frame_two.pushbyte(1);
    frame_two.setsuper(script.multiname("NoSuchSuperMember"));
    frame_two.returnvoid();
  } else {
    frame_two.pushbyte(1);
    frame_two.throw_op();
  }
  const std::uint32_t frame_two_method = script.method("frame2", frame_two.bytes(), 1);
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
  instance.traits = {method_trait(ping_name, ping_method), method_trait(add_name, add_method),
                     method_trait(frame_two_name, frame_two_method)};
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

std::vector<std::byte> root_class_movie(abc::AbcFile file, const std::string& root_name,
                                        std::uint16_t frames = 1,
                                        std::string_view last_frame_label = {}) {
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
  builder.u16(frames);
  builder.tag(fo4::swf::tag::kDoAbc2, abc_payload.body, true);
  builder.tag(fo4::swf::tag::kSymbolClass, symbols.body, false);
  for (std::uint16_t frame = 0; frame < frames; ++frame) {
    if (!last_frame_label.empty() && frame + 1 == frames) {
      builder.tag(fo4::swf::tag::kFrameLabel, swf_test::frame_label(last_frame_label), false);
    }
    builder.tag(fo4::swf::tag::kShowFrame);
  }
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

std::vector<std::byte> labelled_frames_movie() {
  std::vector<swf_test::TagPayload> tags;
  for (std::uint16_t id = 21; id <= 23; ++id) {
    tags.push_back({fo4::swf::tag::kDefineSprite,
                    swf_test::sprite_tag(id, 1, {{fo4::swf::tag::kShowFrame, {}}})});
  }
  const char* const labels[] = {"Open", "Mid", "Close"};
  const char* const names[] = {"A", "B", "C"};
  for (std::uint16_t frame = 0; frame < 3; ++frame) {
    tags.push_back({fo4::swf::tag::kFrameLabel, swf_test::frame_label(labels[frame])});
    SwfBuilder place;
    place.u8(0x22);
    place.u16(1);
    place.u16(21 + frame);
    place.text(names[frame]);
    tags.push_back({fo4::swf::tag::kPlaceObject2, place.body});
    tags.push_back({fo4::swf::tag::kShowFrame, {}});
  }
  return swf_test::movie_of(3, tags);
}

struct Shown {
  std::uint64_t hash{0};
  std::string names;
};

Shown shown(radicle_movie* movie) {
  radicle_runtime_snapshot* raw = nullptr;
  EXPECT_EQ(radicle_movie_snapshot(movie, &raw), RADICLE_STATUS_OK);
  SnapshotHandle snapshot(raw);
  Shown result;
  result.hash = radicle_runtime_snapshot_hash(snapshot.get());
  for (size_t index = 0; index < radicle_runtime_snapshot_entry_count(snapshot.get()); ++index) {
    radicle_runtime_entry entry{};
    EXPECT_EQ(radicle_runtime_snapshot_entry_at(snapshot.get(), index, &entry), RADICLE_STATUS_OK);
    result.names += std::string(entry.name) + ",";
  }
  return result;
}

struct RootState {
  uint32_t frame{0};
  uint32_t total{0};
  int32_t playing{0};
  std::string label;
};

RootState root_state(radicle_movie* movie) {
  RootState state;
  const char* label = nullptr;
  EXPECT_EQ(radicle_movie_root_state(movie, &state.frame, &state.total, &state.playing, &label),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  if (label != nullptr) state.label = label;
  return state;
}

}  // namespace

TEST(CApiRuntime, AdvancingProducesAStableSnapshotSequence) {
  radicle_runtime_host host{};
  radicle_runtime* raw_runtime = nullptr;
  ASSERT_EQ(radicle_runtime_open(&host, &raw_runtime), RADICLE_STATUS_OK) << radicle_last_error();
  RuntimeHandle runtime(raw_runtime);
  const std::vector<std::byte> bytes = sprite_movie();
  radicle_movie* raw_movie = nullptr;
  ASSERT_EQ(radicle_runtime_open_movie_with_bgs(runtime.get(),
                                      reinterpret_cast<const std::uint8_t*>(bytes.data()),
                                      bytes.size(), 0, &raw_movie),
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

TEST(CApiRuntime, SnapshotEntriesCarryAPlacementsRatioAndCacheFlag) {
  // Detects: a PlaceObject3 ratio or cache flag that the C entry drops or defaults, so a host draws the wrong morph frame or bitmap cache; the placed values must come through and the root must keep its defaults.
  radicle_runtime_host host{};
  radicle_runtime* raw_runtime = nullptr;
  ASSERT_EQ(radicle_runtime_open(&host, &raw_runtime), RADICLE_STATUS_OK) << radicle_last_error();
  RuntimeHandle runtime(raw_runtime);
  const std::vector<std::byte> bytes = styled_placement_movie();
  MovieHandle movie = open_movie(runtime.get(), bytes);
  ASSERT_NE(movie.get(), nullptr);

  radicle_runtime_snapshot* raw_snapshot = nullptr;
  ASSERT_EQ(radicle_movie_snapshot(movie.get(), &raw_snapshot), RADICLE_STATUS_OK);
  SnapshotHandle snapshot(raw_snapshot);
  ASSERT_EQ(radicle_runtime_snapshot_entry_count(snapshot.get()), 2u);

  radicle_runtime_entry root{};
  ASSERT_EQ(radicle_runtime_snapshot_entry_at(snapshot.get(), 0, &root), RADICLE_STATUS_OK);
  EXPECT_EQ(root.visible, std::uint8_t{1});
  EXPECT_EQ(root.ratio, std::uint16_t{0});
  EXPECT_EQ(root.cache_as_bitmap, std::uint8_t{0});

  radicle_runtime_entry placed{};
  ASSERT_EQ(radicle_runtime_snapshot_entry_at(snapshot.get(), 1, &placed), RADICLE_STATUS_OK);
  EXPECT_EQ(placed.character_id, 20u);
  EXPECT_EQ(placed.blend_mode, std::uint8_t{8});
  EXPECT_EQ(placed.visible, std::uint8_t{0});
  EXPECT_EQ(placed.ratio, std::uint16_t{3});
  EXPECT_EQ(placed.cache_as_bitmap, std::uint8_t{1});
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

TEST(CApiRuntime, HostCallbackBuffersSurviveSuccessAndErrorResults) {
  for (const radicle_status result : {RADICLE_STATUS_OK, RADICLE_STATUS_INTERNAL_ERROR}) {
    Host calls;
    calls.status = result;
    radicle_runtime_host host{};
    host.user = &calls;
    host.host_call = host_call;
    radicle_runtime* raw_runtime = nullptr;
    ASSERT_EQ(radicle_runtime_open(&host, &raw_runtime), RADICLE_STATUS_OK);
    RuntimeHandle runtime(raw_runtime);
    std::size_t boot_calls = 0;
    const auto bytes = external_interface_movie(&boot_calls);
    MovieHandle movie = open_movie(runtime.get(), bytes);
    ASSERT_NE(movie.get(), nullptr);
    EXPECT_EQ(calls.calls, 1u);
    EXPECT_EQ(calls.methods, std::vector<std::string>{"Ping"});
  }
}

TEST(CApiRuntime, RejectsAnOutOfRangeInputKind) {
  radicle_runtime_host host{};
  radicle_runtime* raw_runtime = nullptr;
  ASSERT_EQ(radicle_runtime_open(&host, &raw_runtime), RADICLE_STATUS_OK);
  RuntimeHandle runtime(raw_runtime);
  const std::vector<std::byte> bytes = sprite_movie();
  radicle_movie* raw_movie = nullptr;
  ASSERT_EQ(radicle_runtime_open_movie_with_bgs(runtime.get(),
                                      reinterpret_cast<const std::uint8_t*>(bytes.data()),
                                      bytes.size(), 0, &raw_movie),
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
  MovieHandle movie = open_movie(runtime.get(), bytes);
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
  MovieHandle movie = open_movie(runtime.get(), bytes);
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

  out = sentinel;
  EXPECT_EQ(radicle_runtime_open_movie_from_ba2_with_bgs(runtime.get(), nullptr, "Programs/x.swf", 1,
                                                         &out),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(out, nullptr);
}

// Detects: a static shape whose gradient stops, gradient matrix, stroke width or stroke colour is dropped from the art table, so GE draws the shape flat or unstroked; the table must carry each of them.
TEST(CApiRuntime, ArtTableCarriesGradientFillsAndStrokesOfEachShape) {
  RuntimeHandle runtime = open_runtime();
  const std::vector<std::byte> gradient =
      swf_test::linear_gradient_fill(swf_test::matrix_full(65536, 65536, 0, 0, 0, 0));
  MovieHandle movie = open_movie(
      runtime.get(), swf_test::movie_of(1, {{fo4::swf::tag::kDefineShape3,
                                             swf_test::square_with_fill(7, 200, gradient, true)}}));
  ASSERT_NE(movie.get(), nullptr);
  ArtHandle art = open_art(movie.get());
  ASSERT_NE(art.get(), nullptr);
  ASSERT_EQ(radicle_art_shape_count(art.get()), 1u);

  radicle_art_shape shape{};
  ASSERT_EQ(radicle_art_shape_at(art.get(), 0, &shape), RADICLE_STATUS_OK);
  EXPECT_EQ(shape.character_id, std::uint16_t{7});
  ASSERT_EQ(shape.fill_count, 1u);
  const radicle_art_fill& fill = shape.fills[0];
  EXPECT_EQ(fill.kind, std::uint8_t{0x10});
  ASSERT_EQ(fill.stop_count, 2u);
  EXPECT_EQ(fill.stops[0].color, 0xFFFF0000u);
  EXPECT_EQ(fill.stops[1].ratio, std::uint8_t{255});
  EXPECT_EQ(fill.stops[1].color, 0xFF0000FFu);
  EXPECT_DOUBLE_EQ(fill.matrix[0], 1.0);
  EXPECT_DOUBLE_EQ(fill.matrix[3], 1.0);
  ASSERT_EQ(fill.contour_count, 1u);
  EXPECT_TRUE(fill.contours[0].closed);
  EXPECT_EQ(fill.contours[0].point_count, 4u);
  ASSERT_EQ(shape.line_count, 1u);
  EXPECT_DOUBLE_EQ(shape.lines[0].width, 1.0);
  EXPECT_EQ(shape.lines[0].color, 0xFF00FF00u);
  ASSERT_EQ(shape.lines[0].contour_count, 1u);
  EXPECT_TRUE(shape.lines[0].contours[0].closed);
  EXPECT_EQ(shape.lines[0].contours[0].point_count, 4u);
}

// Detects: a lossless bitmap the art table skips or exports without a PNG signature, a glyph outline dropped or indexed off by one, or a JPEG bitmap fill that is not flagged as missing; the table must keep all of them.
TEST(CApiRuntime, ArtTableCarriesBitmapsAsPngAndGlyphOutlinesOfEmbeddedFonts) {
  RuntimeHandle runtime = open_runtime();
  const auto bitmap_fill = [](std::uint16_t id) {
    SwfBuilder fill;
    fill.u8(0x40);
    fill.u16(id);
    fill.bytes(swf_test::matrix_full(65536, 65536, 0, 0, 0, 0));
    return fill.body;
  };
  const std::vector<std::byte> jpeg = {std::byte{0x0C}, std::byte{0x00}, std::byte{0xFF},
                                       std::byte{0xD8}, std::byte{0xFF}, std::byte{0xD9}};
  MovieHandle movie = open_movie(
      runtime.get(),
      swf_test::movie_of(
          1, {{fo4::swf::tag::kDefineBitsLossless, swf_test::lossless_bitmap(9, 2, 2)},
              {fo4::swf::tag::kDefineBitsJpeg2, jpeg},
              {fo4::swf::tag::kDefineFont, swf_test::one_glyph_font()},
              {fo4::swf::tag::kDefineShape3, swf_test::square_with_fill(8, 200, bitmap_fill(12), false)},
              {fo4::swf::tag::kDefineShape3, swf_test::square_with_fill(10, 200, bitmap_fill(9), false)}}));
  ASSERT_NE(movie.get(), nullptr);
  ArtHandle art = open_art(movie.get());
  ASSERT_NE(art.get(), nullptr);
  ASSERT_EQ(radicle_art_bitmap_count(art.get()), 1u);

  radicle_art_bitmap bitmap{};
  ASSERT_EQ(radicle_art_bitmap_at(art.get(), 0, &bitmap), RADICLE_STATUS_OK);
  EXPECT_EQ(bitmap.character_id, std::uint16_t{9});
  EXPECT_EQ(bitmap.width, std::uint16_t{2});
  ASSERT_GT(bitmap.png_size, 8u);
  EXPECT_EQ(std::vector<std::uint8_t>(bitmap.png, bitmap.png + 8),
            (std::vector<std::uint8_t>{137, 80, 78, 71, 13, 10, 26, 10}));

  ASSERT_EQ(radicle_art_font_count(art.get()), 1u);
  radicle_art_font font{};
  ASSERT_EQ(radicle_art_font_at(art.get(), 0, &font), RADICLE_STATUS_OK);
  EXPECT_EQ(font.font_id, std::uint16_t{7});
  ASSERT_EQ(font.glyph_count, 1u);
  const radicle_art_shape& glyph = font.glyphs[0];
  EXPECT_EQ(glyph.character_id, std::uint16_t{0});
  ASSERT_EQ(glyph.fill_count, 1u);
  ASSERT_EQ(glyph.fills[0].contour_count, 1u);
  EXPECT_TRUE(glyph.fills[0].contours[0].closed);
  EXPECT_EQ(glyph.fills[0].contours[0].point_count, 3u);

  ASSERT_EQ(radicle_art_shape_count(art.get()), 2u);
  radicle_art_shape jpeg_shape{};
  ASSERT_EQ(radicle_art_shape_at(art.get(), 0, &jpeg_shape), RADICLE_STATUS_OK);
  ASSERT_EQ(jpeg_shape.fill_count, 1u);
  EXPECT_EQ(jpeg_shape.fills[0].bitmap_id, std::uint16_t{12});
  EXPECT_EQ(jpeg_shape.fills[0].bitmap_missing, std::uint8_t{1});
  radicle_art_shape png_shape{};
  ASSERT_EQ(radicle_art_shape_at(art.get(), 1, &png_shape), RADICLE_STATUS_OK);
  ASSERT_EQ(png_shape.fill_count, 1u);
  EXPECT_EQ(png_shape.fills[0].bitmap_id, std::uint16_t{9});
  EXPECT_EQ(png_shape.fills[0].bitmap_missing, std::uint8_t{0});
}

// Detects: a placed filter whose kind, colour, blur, distance, strength, flags or pass count is dropped on the way to the C entry, or a plain placed shape that reports a drawn shape, so GE would upload art for an object that never drew; plain entries must report no drawn shape.
TEST(CApiRuntime, PlacedFilterListReachesTheEntryExtraAndPlainEntriesHaveNoDrawnShape) {
  RuntimeHandle runtime = open_runtime();
  MovieHandle movie = open_movie(runtime.get(), swf_test::filtered_placement_movie());
  ASSERT_NE(movie.get(), nullptr);
  radicle_runtime_snapshot* raw_snapshot = nullptr;
  ASSERT_EQ(radicle_movie_snapshot(movie.get(), &raw_snapshot), RADICLE_STATUS_OK);
  SnapshotHandle snapshot(raw_snapshot);
  ASSERT_EQ(radicle_runtime_snapshot_entry_count(snapshot.get()), 2u);

  radicle_snapshot_entry_extra root{};
  ASSERT_EQ(radicle_runtime_snapshot_entry_extra_at(snapshot.get(), 0, &root), RADICLE_STATUS_OK);
  EXPECT_EQ(root.placed_filter_count, 0u);
  radicle_snapshot_entry_extra placed{};
  ASSERT_EQ(radicle_runtime_snapshot_entry_extra_at(snapshot.get(), 1, &placed), RADICLE_STATUS_OK);
  ASSERT_EQ(placed.placed_filter_count, 2u);
  const radicle_snapshot_filter& shadow = placed.placed_filters[0];
  EXPECT_EQ(shadow.kind, std::uint8_t{0});
  EXPECT_EQ(shadow.color, 0x40102030u);
  EXPECT_DOUBLE_EQ(shadow.blur_x, 2.0);
  EXPECT_DOUBLE_EQ(shadow.blur_y, 3.0);
  EXPECT_DOUBLE_EQ(shadow.angle, 1.0);
  EXPECT_DOUBLE_EQ(shadow.distance, 4.0);
  EXPECT_DOUBLE_EQ(shadow.strength, 1.5);
  EXPECT_EQ(shadow.composite_source, std::uint8_t{1});
  EXPECT_EQ(shadow.passes, std::uint8_t{1});
  const radicle_snapshot_filter& blur = placed.placed_filters[1];
  EXPECT_EQ(blur.kind, std::uint8_t{1});
  EXPECT_DOUBLE_EQ(blur.blur_x, 5.0);
  EXPECT_EQ(blur.passes, std::uint8_t{2});

  radicle_art_shape plain{};
  EXPECT_EQ(radicle_runtime_snapshot_drawn_at(snapshot.get(), 1, &plain), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(plain.fill_count, 0u);
}

// Detects: a frame script that fails after an advance, which radicle_movie_advance still reports as OK, so a host that never reads the frame error sees a movie that silently stops; the frame error must report it.
TEST(CApiRuntime, MovieFrameErrorReportsAFailedFrameScriptAfterAdvance) {
  RuntimeHandle runtime = open_runtime();
  MovieHandle movie =
      open_movie(runtime.get(), root_class_movie(member_root_file(), "MemberRoot", 2));
  ASSERT_NE(movie.get(), nullptr) << radicle_last_error();

  uint8_t* out_text = nullptr;
  size_t out_size = 0;
  ASSERT_EQ(radicle_movie_frame_error(movie.get(), &out_text, &out_size), RADICLE_STATUS_OK);
  EXPECT_EQ(out_text, nullptr);
  ASSERT_EQ(radicle_movie_advance(movie.get(), 1.0), RADICLE_STATUS_OK) << radicle_last_error();
  ASSERT_EQ(radicle_movie_frame_error(movie.get(), &out_text, &out_size), RADICLE_STATUS_OK);
  ASSERT_NE(out_text, nullptr);
  EXPECT_EQ(std::string(reinterpret_cast<const char*>(out_text), out_size), "uncaught exception: 1");
  radicle_free_bytes(out_text);
}

// Detects: a jump that leaves a UI unable to show a named state, or that sends an unknown label to frame 1 (what gotoAndPlay does) instead of refusing; a known label must give the frame's own display list, play must decide whether the root keeps stepping, and an unknown label must change nothing.
TEST(CApiRuntime, MovieGotoJumpsToAFrameLabelAndRefusesAnUnknownOne) {
  RuntimeHandle runtime = open_runtime();
  MovieHandle movie = open_movie(runtime.get(), labelled_frames_movie());
  ASSERT_NE(movie.get(), nullptr) << radicle_last_error();
  const RootState start = root_state(movie.get());
  EXPECT_EQ(start.frame, 1u);
  EXPECT_EQ(start.total, 3u);
  EXPECT_EQ(start.playing, 1);
  EXPECT_EQ(start.label, "Open");
  const Shown first = shown(movie.get());
  EXPECT_EQ(first.names, ",A,");

  ASSERT_EQ(radicle_movie_goto(movie.get(), "Close", 0), RADICLE_STATUS_OK) << radicle_last_error();
  const RootState closed = root_state(movie.get());
  EXPECT_EQ(closed.frame, 3u);
  EXPECT_EQ(closed.playing, 0);
  EXPECT_EQ(closed.label, "Close");
  const Shown at_close = shown(movie.get());
  EXPECT_EQ(at_close.names, ",C,");
  EXPECT_NE(at_close.hash, first.hash);
  ASSERT_EQ(radicle_movie_advance(movie.get(), 1.0), RADICLE_STATUS_OK);
  EXPECT_EQ(root_state(movie.get()).frame, 3u);
  EXPECT_EQ(shown(movie.get()).hash, at_close.hash);

  RuntimeHandle other_runtime = open_runtime();
  MovieHandle played = open_movie(other_runtime.get(), labelled_frames_movie());
  ASSERT_NE(played.get(), nullptr) << radicle_last_error();
  ASSERT_EQ(radicle_movie_advance(played.get(), 2.5 / 12.0), RADICLE_STATUS_OK);
  EXPECT_EQ(root_state(played.get()).frame, 3u);
  EXPECT_EQ(shown(played.get()).hash, at_close.hash);

  ASSERT_EQ(radicle_movie_goto(movie.get(), "Mid", 1), RADICLE_STATUS_OK) << radicle_last_error();
  const RootState mid = root_state(movie.get());
  EXPECT_EQ(mid.frame, 2u);
  EXPECT_EQ(mid.playing, 1);
  EXPECT_EQ(mid.label, "Mid");
  ASSERT_EQ(radicle_movie_advance(movie.get(), 1.5 / 12.0), RADICLE_STATUS_OK);
  EXPECT_EQ(root_state(movie.get()).frame, 3u);

  const Shown before = shown(movie.get());
  EXPECT_EQ(radicle_movie_goto(movie.get(), "Nope", 0), RADICLE_STATUS_NOT_FOUND);
  EXPECT_NE(std::string(radicle_last_error()).find("frame label"), std::string::npos);
  EXPECT_EQ(radicle_movie_goto(movie.get(), "", 0), RADICLE_STATUS_NOT_FOUND);
  const RootState after = root_state(movie.get());
  EXPECT_EQ(after.frame, 3u);
  EXPECT_EQ(after.playing, 1);
  EXPECT_EQ(shown(movie.get()).hash, before.hash);
}

// Detects: a jump whose target frame script fails and reports NOT_FOUND (an unknown label, so a host retries a label that exists) or OK, or a failure that leaves the root state stale; the label must be found, the playhead must sit on that frame and the failure must be INTERNAL_ERROR.
TEST(CApiRuntime, MovieGotoReportsAFailingFrameScriptAsInternalErrorAfterMovingThePlayhead) {
  RuntimeHandle runtime = open_runtime();
  MovieHandle movie = open_movie(
      runtime.get(), root_class_movie(member_root_file(true), "MemberRoot", 2, "Boom"));
  ASSERT_NE(movie.get(), nullptr) << radicle_last_error();
  EXPECT_EQ(root_state(movie.get()).frame, 1u);

  EXPECT_EQ(radicle_movie_goto(movie.get(), "Boom", 0), RADICLE_STATUS_INTERNAL_ERROR);
  const RootState moved = root_state(movie.get());
  EXPECT_EQ(moved.frame, 2u);
  EXPECT_EQ(moved.playing, 0);
  EXPECT_EQ(moved.label, "Boom");
}

// Detects: a null handle or output that crashes or leaves stale values in the caller's variables.
TEST(CApiRuntime, MovieGotoAndRootStateRefuseNullArguments) {
  RuntimeHandle runtime = open_runtime();
  MovieHandle movie = open_movie(runtime.get(), labelled_frames_movie());
  ASSERT_NE(movie.get(), nullptr) << radicle_last_error();
  EXPECT_EQ(radicle_movie_goto(nullptr, "Open", 0), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_movie_goto(movie.get(), nullptr, 0), RADICLE_STATUS_INVALID_ARGUMENT);

  uint32_t frame = 9;
  uint32_t total = 9;
  int32_t playing = 9;
  const char* label = "x";
  EXPECT_EQ(radicle_movie_root_state(nullptr, &frame, &total, &playing, &label),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(frame, 0u);
  EXPECT_EQ(total, 0u);
  EXPECT_EQ(playing, 0);
  EXPECT_EQ(label, nullptr);
  EXPECT_EQ(radicle_movie_root_state(movie.get(), nullptr, &total, &playing, &label),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_movie_root_state(movie.get(), &frame, nullptr, &playing, &label),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_movie_root_state(movie.get(), &frame, &total, nullptr, &label),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_movie_root_state(movie.get(), &frame, &total, &playing, nullptr),
            RADICLE_STATUS_INVALID_ARGUMENT);
}
