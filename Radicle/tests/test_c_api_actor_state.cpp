#include "fo4/c_api.h"
#include "fo4/c_api_actor_state.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

struct BytesDeleter {
  void operator()(std::uint8_t* bytes) const { radicle_free_bytes(bytes); }
};

struct DecodedDeleter {
  void operator()(radicle_actor_state_decoded* decoded) const { radicle_actor_state_free(decoded); }
};

using BytesPtr = std::unique_ptr<std::uint8_t, BytesDeleter>;
using DecodedPtr = std::unique_ptr<radicle_actor_state_decoded, DecodedDeleter>;

radicle_actor_form_key key(const char* plugin, std::uint32_t local_id) {
  return {plugin, plugin == nullptr ? 0 : std::strlen(plugin), local_id};
}

std::string text(const char* data, std::size_t size) { return std::string(data, size); }

struct Fixture {
  std::vector<radicle_actor_inventory_delta> inventory;
  std::vector<radicle_actor_value_modifier> values;
  std::vector<radicle_actor_faction_rank> factions;
  std::vector<std::uint8_t> tail;
  radicle_actor_state state{};

  Fixture() {
    inventory = {{key("Fallout4.esm", 0x0Fu), 3}, {key("DLC01.esm", 0x10u), -1}};
    values = {{"Health", 6, 25.0f}, {"Armor", 5, -4.5f}};
    factions = {{key("Fallout4.esm", 0x0001C1F1u), 2}};
    state.ref.form = key("Fallout4.esm", 0xABCDu);
    state.flags = RADICLE_ACTOR_FLAG_ENABLED | RADICLE_ACTOR_FLAG_DEAD;
    state.cell = key("Fallout4.esm", 0x00012345u);
    state.position[0] = 1.5f;
    state.position[1] = -2.25f;
    state.position[2] = 3.0f;
    state.rotation[1] = 90.0f;
    state.scale = 1.25f;
    state.forced_package = key("Fallout4.esm", 0x0002B1E1u);
    state.has_combat_target = 1;
    state.combat_target.kind = RADICLE_ACTOR_REF_CREATED;
    state.combat_target.created_id = 77;
    state.combat_target.base = key("Fallout4.esm", 0xBEEFu);
    state.combat_target.spawn_seed = 0x1122334455667788ull;
    state.bounty = 500;
    state.spawn_level = 42;
    bind();
  }

  void bind() {
    state.inventory = inventory.data();
    state.inventory_count = inventory.size();
    state.actor_values = values.data();
    state.actor_value_count = values.size();
    state.factions = factions.data();
    state.faction_count = factions.size();
    state.unknown_tail = tail.empty() ? nullptr : tail.data();
    state.unknown_tail_size = tail.size();
  }
};

std::vector<std::uint8_t> encode_ok(const radicle_actor_state& state) {
  std::uint8_t* raw = nullptr;
  std::size_t size = 0;
  EXPECT_EQ(radicle_actor_state_encode(&state, &raw, &size), RADICLE_STATUS_OK);
  const BytesPtr owned(raw);
  return raw == nullptr ? std::vector<std::uint8_t>() : std::vector<std::uint8_t>(raw, raw + size);
}

DecodedPtr decode_ok(const std::vector<std::uint8_t>& bytes) {
  radicle_actor_state_decoded* raw = nullptr;
  EXPECT_EQ(radicle_actor_state_decode(bytes.data(), bytes.size(), &raw), RADICLE_STATUS_OK);
  return DecodedPtr(raw);
}

radicle_actor_state view_of(const DecodedPtr& decoded) {
  radicle_actor_state view{};
  EXPECT_EQ(radicle_actor_state_view(decoded.get(), &view), RADICLE_STATUS_OK);
  return view;
}

void put_le32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
  for (std::size_t i = 0; i < 4; ++i) bytes[offset + i] = static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFu);
}

}  // namespace

TEST(CApiActorStateTest, PopulatedStateSurvivesEncodeDecodeViewAndReencodeByteForByte) {
  Fixture fixture;
  const std::vector<std::uint8_t> bytes = encode_ok(fixture.state);
  ASSERT_GT(bytes.size(), 12u);
  EXPECT_EQ(std::string(bytes.begin(), bytes.begin() + 4), "RASV");

  const DecodedPtr decoded = decode_ok(bytes);
  ASSERT_NE(decoded.get(), nullptr);
  const radicle_actor_state view = view_of(decoded);
  EXPECT_EQ(text(view.ref.form.plugin, view.ref.form.plugin_size), "Fallout4.esm");
  EXPECT_EQ(view.ref.form.local_id, 0xABCDu);
  EXPECT_EQ(view.flags, RADICLE_ACTOR_FLAG_ENABLED | RADICLE_ACTOR_FLAG_DEAD);
  EXPECT_EQ(view.position[1], -2.25f);
  EXPECT_EQ(view.rotation[1], 90.0f);
  EXPECT_EQ(view.scale, 1.25f);
  ASSERT_EQ(view.inventory_count, 2u);
  EXPECT_EQ(view.inventory[1].count, -1);
  EXPECT_EQ(text(view.inventory[1].item.plugin, view.inventory[1].item.plugin_size), "DLC01.esm");
  ASSERT_EQ(view.actor_value_count, 2u);
  EXPECT_EQ(text(view.actor_values[1].actor_value, view.actor_values[1].actor_value_size), "Armor");
  EXPECT_EQ(view.actor_values[1].value, -4.5f);
  ASSERT_EQ(view.faction_count, 1u);
  EXPECT_EQ(view.factions[0].rank, 2);
  EXPECT_EQ(view.forced_package.local_id, 0x0002B1E1u);
  EXPECT_EQ(view.current_package.plugin_size, 0u);
  EXPECT_EQ(view.has_combat_target, 1);
  EXPECT_EQ(view.combat_target.kind, RADICLE_ACTOR_REF_CREATED);
  EXPECT_EQ(view.combat_target.created_id, 77u);
  EXPECT_EQ(view.combat_target.spawn_seed, 0x1122334455667788ull);
  EXPECT_EQ(view.bounty, 500);
  EXPECT_EQ(view.spawn_level, 42);
  EXPECT_EQ(view.unknown_tail, nullptr);
  EXPECT_EQ(view.unknown_tail_size, 0u);

  EXPECT_EQ(encode_ok(view), bytes);
}

TEST(CApiActorStateTest, ZeroedStateWithNullListsRoundTripsAndDecodesToNullLists) {
  radicle_actor_state state{};
  const std::vector<std::uint8_t> bytes = encode_ok(state);
  const DecodedPtr decoded = decode_ok(bytes);
  ASSERT_NE(decoded.get(), nullptr);
  const radicle_actor_state view = view_of(decoded);
  EXPECT_EQ(view.inventory, nullptr);
  EXPECT_EQ(view.inventory_count, 0u);
  EXPECT_EQ(view.actor_values, nullptr);
  EXPECT_EQ(view.factions, nullptr);
  EXPECT_EQ(view.has_combat_target, 0);
  EXPECT_EQ(view.ref.form.plugin_size, 0u);
  EXPECT_EQ(encode_ok(view), bytes);
}

TEST(CApiActorStateTest, UnknownTrailingBytesSurviveDecodeViewAndReencode) {
  Fixture fixture;
  std::vector<std::uint8_t> bytes = encode_ok(fixture.state);
  const std::vector<std::uint8_t> appended = {0xDE, 0xAD, 0xBE};
  bytes.insert(bytes.end(), appended.begin(), appended.end());
  put_le32(bytes, 8, static_cast<std::uint32_t>(bytes.size() - 12));

  const DecodedPtr decoded = decode_ok(bytes);
  ASSERT_NE(decoded.get(), nullptr);
  const radicle_actor_state view = view_of(decoded);
  ASSERT_EQ(view.unknown_tail_size, appended.size());
  EXPECT_EQ(std::vector<std::uint8_t>(view.unknown_tail, view.unknown_tail + view.unknown_tail_size), appended);
  EXPECT_EQ(encode_ok(view), bytes);

  Fixture with_tail;
  with_tail.tail = appended;
  with_tail.bind();
  EXPECT_EQ(encode_ok(with_tail.state), bytes);
}

TEST(CApiActorStateTest, TextCarriesItsLengthSoAnEmbeddedNulSurvives) {
  Fixture fixture;
  const std::string name("A\0B.esp", 7);
  fixture.state.cell = {name.data(), name.size(), 9};
  const std::vector<std::uint8_t> bytes = encode_ok(fixture.state);
  const DecodedPtr decoded = decode_ok(bytes);
  ASSERT_NE(decoded.get(), nullptr);
  const radicle_actor_state view = view_of(decoded);
  EXPECT_EQ(text(view.cell.plugin, view.cell.plugin_size), name);
  EXPECT_EQ(encode_ok(view), bytes);
}

TEST(CApiActorStateTest, CreatedRefsOfOneBaseKeepDistinctIdentitiesThroughTheCView) {
  radicle_actor_state first{};
  first.ref.kind = RADICLE_ACTOR_REF_CREATED;
  first.ref.created_id = 1001;
  first.ref.base = key("Fallout4.esm", 0xBEEFu);
  first.ref.spawn_seed = 7;
  radicle_actor_state second = first;
  second.ref.created_id = 1002;
  second.ref.spawn_seed = 8;

  const DecodedPtr first_back = decode_ok(encode_ok(first));
  const DecodedPtr second_back = decode_ok(encode_ok(second));
  ASSERT_NE(first_back.get(), nullptr);
  ASSERT_NE(second_back.get(), nullptr);
  const radicle_actor_state a = view_of(first_back);
  const radicle_actor_state b = view_of(second_back);
  EXPECT_EQ(a.ref.kind, RADICLE_ACTOR_REF_CREATED);
  EXPECT_EQ(a.ref.created_id, 1001u);
  EXPECT_EQ(a.ref.spawn_seed, 7u);
  EXPECT_EQ(b.ref.created_id, 1002u);
  EXPECT_EQ(b.ref.spawn_seed, 8u);
  EXPECT_EQ(a.ref.base.local_id, b.ref.base.local_id);
}

TEST(CApiActorStateTest, EncodeRefusesBadInputWithInvalidArgumentAndClearsOutputs) {
  Fixture fixture;
  std::uint8_t* bytes = nullptr;
  std::size_t size = 0;
  EXPECT_EQ(radicle_actor_state_encode(nullptr, &bytes, &size), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_actor_state_encode(&fixture.state, nullptr, &size), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_actor_state_encode(&fixture.state, &bytes, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);

  const auto refused = [&](const radicle_actor_state& state) {
    bytes = reinterpret_cast<std::uint8_t*>(1);
    size = 99;
    EXPECT_EQ(radicle_actor_state_encode(&state, &bytes, &size), RADICLE_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(bytes, nullptr);
    EXPECT_EQ(size, 0u);
  };

  radicle_actor_state bad_kind = fixture.state;
  bad_kind.ref.kind = static_cast<radicle_actor_ref_kind>(2);
  refused(bad_kind);

  radicle_actor_state bad_presence = fixture.state;
  bad_presence.has_combat_target = 2;
  refused(bad_presence);

  radicle_actor_state bad_target_kind = fixture.state;
  bad_target_kind.combat_target.kind = static_cast<radicle_actor_ref_kind>(-1);
  refused(bad_target_kind);

  radicle_actor_state null_list = fixture.state;
  null_list.inventory = nullptr;
  refused(null_list);

  radicle_actor_state huge_list = fixture.state;
  huge_list.faction_count = RADICLE_ACTOR_STATE_MAX_LIST_ENTRIES + 1;
  refused(huge_list);

  radicle_actor_state null_text = fixture.state;
  null_text.cell = {nullptr, 4, 0};
  refused(null_text);

  const std::string long_name(RADICLE_ACTOR_STATE_MAX_STRING_BYTES + 1, 'p');
  radicle_actor_state long_text = fixture.state;
  long_text.cell = {long_name.data(), long_name.size(), 0};
  refused(long_text);

  radicle_actor_state nameless_with_id = fixture.state;
  nameless_with_id.current_package = {nullptr, 0, 5};
  refused(nameless_with_id);

  radicle_actor_state null_tail = fixture.state;
  null_tail.unknown_tail_size = 3;
  refused(null_tail);
  EXPECT_NE(std::string(radicle_last_error()), "");
}

TEST(CApiActorStateTest, DecodeRefusesBadBlobsWithParseErrorAndLeavesOutNull) {
  Fixture fixture;
  const std::vector<std::uint8_t> good = encode_ok(fixture.state);

  const auto refused = [](const std::vector<std::uint8_t>& blob) {
    radicle_actor_state_decoded* decoded = reinterpret_cast<radicle_actor_state_decoded*>(1);
    EXPECT_EQ(radicle_actor_state_decode(blob.data(), blob.size(), &decoded), RADICLE_STATUS_PARSE_ERROR);
    EXPECT_EQ(decoded, nullptr);
  };

  std::vector<std::uint8_t> bad_magic = good;
  bad_magic[0] = 'X';
  refused(bad_magic);

  std::vector<std::uint8_t> bad_version = good;
  put_le32(bad_version, 4, RADICLE_ACTOR_STATE_FORMAT_VERSION + 1);
  refused(bad_version);

  std::vector<std::uint8_t> size_mismatch = good;
  put_le32(size_mismatch, 8, static_cast<std::uint32_t>(good.size()));
  refused(size_mismatch);

  refused(std::vector<std::uint8_t>());

  for (std::size_t length = 12; length < good.size(); ++length) {
    std::vector<std::uint8_t> cut(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(length));
    put_le32(cut, 8, static_cast<std::uint32_t>(length - 12));
    refused(cut);
  }
}

TEST(CApiActorStateTest, NullArgumentsAreRefusedAndFreeAcceptsNull) {
  radicle_actor_state_decoded* decoded = nullptr;
  EXPECT_EQ(radicle_actor_state_decode(nullptr, 0, &decoded), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(radicle_actor_state_decode(nullptr, 5, &decoded), RADICLE_STATUS_INVALID_ARGUMENT);
  const std::uint8_t byte = 0;
  EXPECT_EQ(radicle_actor_state_decode(&byte, 1, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);

  radicle_actor_state view{};
  view.bounty = 9;
  EXPECT_EQ(radicle_actor_state_view(nullptr, &view), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(view.bounty, 0);
  EXPECT_EQ(radicle_actor_state_view(nullptr, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_actor_state_free(nullptr);
}

TEST(CApiActorStateTest, DecodedViewDoesNotBorrowTheInputBlob) {
  Fixture fixture;
  std::vector<std::uint8_t> bytes = encode_ok(fixture.state);
  const DecodedPtr decoded = decode_ok(bytes);
  ASSERT_NE(decoded.get(), nullptr);
  std::fill(bytes.begin(), bytes.end(), std::uint8_t{0xFF});
  const radicle_actor_state view = view_of(decoded);
  EXPECT_EQ(text(view.actor_values[0].actor_value, view.actor_values[0].actor_value_size), "Health");
  EXPECT_EQ(text(view.cell.plugin, view.cell.plugin_size), "Fallout4.esm");
}
