#include "fo4/c_api_nif.h"

#include "nif_fixture.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace {

using nif_fixture::Block;
using nif_fixture::Writer;

class Nif {
public:
  explicit Nif(const std::vector<std::byte>& bytes) {
    const auto status = radicle_nif_read(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), &handle_);
    EXPECT_EQ(status, RADICLE_STATUS_OK) << radicle_last_error();
  }
  ~Nif() { radicle_nif_destroy(handle_); }
  Nif(const Nif&) = delete;
  Nif& operator=(const Nif&) = delete;

  radicle_nif* get() const { return handle_; }

private:
  radicle_nif* handle_{nullptr};
};

Block collision_block(const std::string& type, const std::vector<std::uint8_t>& payload) {
  Writer w;
  w.u32(static_cast<std::uint32_t>(payload.size()));
  for (const auto value : payload) w.u8(value);
  return Block{type, w.bytes()};
}

Block collision_object_block(std::int32_t target,
                             std::int32_t data,
                             std::uint32_t body_id,
                             std::uint16_t flags = 0) {
  Writer w;
  w.i32(target);
  w.u16(flags);
  w.i32(data);
  w.u32(body_id);
  return Block{"bhkNPCollisionObject", w.bytes()};
}

std::vector<std::uint8_t> take_bytes(uint8_t* bytes, size_t size) {
  std::vector<std::uint8_t> result(bytes, bytes + size);
  radicle_free_bytes(bytes);
  return result;
}

}  // namespace

TEST(CapiNifCollision, ReadsTheBlobBlockTypeAndPayload) {
  const std::vector<std::byte> bytes = nif_fixture::build_nif(
      {}, {collision_block("bhkPhysicsSystem", {0x57, 0xE0, 0xE0, 0x57, 0x10, 0xC0, 0xC0, 0x10})}, {});
  Nif nif(bytes);
  ASSERT_NE(nif.get(), nullptr);

  size_t count = 99;
  ASSERT_EQ(radicle_nif_collision_count(nif.get(), &count), RADICLE_STATUS_OK);
  ASSERT_EQ(count, 1U);

  uint32_t block = 99;
  ASSERT_EQ(radicle_nif_collision_block(nif.get(), 0, &block), RADICLE_STATUS_OK);
  EXPECT_EQ(block, 0U);

  uint8_t* type = nullptr;
  size_t type_size = 0;
  ASSERT_EQ(radicle_nif_collision_type(nif.get(), 0, &type, &type_size), RADICLE_STATUS_OK);
  EXPECT_EQ(std::string(reinterpret_cast<const char*>(type), type_size), "bhkPhysicsSystem");
  radicle_free_bytes(type);

  uint8_t* payload = nullptr;
  size_t payload_size = 0;
  ASSERT_EQ(radicle_nif_collision_bytes(nif.get(), 0, &payload, &payload_size), RADICLE_STATUS_OK);
  EXPECT_EQ(take_bytes(payload, payload_size),
            (std::vector<std::uint8_t>{0x57, 0xE0, 0xE0, 0x57, 0x10, 0xC0, 0xC0, 0x10}));
}

TEST(CapiNifCollision, ReportsAnEmptyCollisionListAsZeroAndAnEmptyPayloadAsNull) {
  const std::vector<std::byte> none = nif_fixture::build_nif({}, {}, {});
  Nif plain(none);
  size_t count = 99;
  ASSERT_EQ(radicle_nif_collision_count(plain.get(), &count), RADICLE_STATUS_OK);
  EXPECT_EQ(count, 0U);

  const std::vector<std::byte> empty =
      nif_fixture::build_nif({}, {collision_block("bhkRagdollSystem", {})}, {});
  Nif held(empty);
  uint8_t* payload = reinterpret_cast<uint8_t*>(0x1);
  size_t payload_size = 99;
  ASSERT_EQ(radicle_nif_collision_bytes(held.get(), 0, &payload, &payload_size), RADICLE_STATUS_OK);
  EXPECT_EQ(payload, nullptr);
  EXPECT_EQ(payload_size, 0U);
}

TEST(CapiNifCollision, RejectsBadArgumentsAndIndexes) {
  const std::vector<std::byte> bytes =
      nif_fixture::build_nif({}, {collision_block("bhkPhysicsSystem", {0x01})}, {});
  Nif nif(bytes);

  size_t count = 0;
  EXPECT_EQ(radicle_nif_collision_count(nullptr, &count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_nif_collision_count(nif.get(), nullptr), RADICLE_STATUS_INVALID_ARGUMENT);

  uint32_t block = 0;
  EXPECT_EQ(radicle_nif_collision_block(nif.get(), 3, &block), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_nif_collision_block(nif.get(), 0, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);

  EXPECT_EQ(radicle_nif_collision_type(nif.get(), 3, nullptr, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_nif_collision_type(nif.get(), 0, nullptr, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);

  uint8_t* payload = nullptr;
  size_t payload_size = 0;
  EXPECT_EQ(radicle_nif_collision_bytes(nif.get(), 3, &payload, &payload_size),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_nif_collision_bytes(nif.get(), 0, nullptr, &payload_size),
            RADICLE_STATUS_INVALID_ARGUMENT);

  int32_t owner = 0;
  EXPECT_EQ(radicle_nif_collision_owner_block(nif.get(), 3, &owner), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_nif_collision_owner_block(nif.get(), 0, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_nif_collision_flags(nif.get(), 3, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_nif_collision_flags(nif.get(), 0, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_nif_collision_system_ref(nif.get(), 0, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_nif_collision_body_id(nif.get(), 0, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_nif_collision_target_block(nif.get(), 0, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_nif_collision_target_node(nif.get(), 0, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST(CapiNifCollision, ReadsTheOwnerFlagsSystemBodyAndTargetOfASystem) {
  const std::vector<std::byte> bytes = nif_fixture::build_nif(
      {"collision"},
      {nif_fixture::node_block("NiNode", 0, {}, {}), collision_block("bhkPhysicsSystem", {0x57, 0xE0, 0xE0, 0x57}),
       collision_object_block(0, 1, 0x77, 0x0002)},
      {0});
  Nif nif(bytes);
  ASSERT_NE(nif.get(), nullptr);

  int32_t owner = -2;
  ASSERT_EQ(radicle_nif_collision_owner_block(nif.get(), 0, &owner), RADICLE_STATUS_OK);
  EXPECT_EQ(owner, 2);

  uint16_t flags = 0;
  ASSERT_EQ(radicle_nif_collision_flags(nif.get(), 0, &flags), RADICLE_STATUS_OK);
  EXPECT_EQ(flags, std::uint16_t{0x0002});

  int32_t system_ref = -2;
  ASSERT_EQ(radicle_nif_collision_system_ref(nif.get(), 0, &system_ref), RADICLE_STATUS_OK);
  EXPECT_EQ(system_ref, 1);

  uint32_t body = 0;
  ASSERT_EQ(radicle_nif_collision_body_id(nif.get(), 0, &body), RADICLE_STATUS_OK);
  EXPECT_EQ(body, 0x77U);

  int32_t target = -2;
  ASSERT_EQ(radicle_nif_collision_target_block(nif.get(), 0, &target), RADICLE_STATUS_OK);
  EXPECT_EQ(target, 0);

  int32_t node = -2;
  ASSERT_EQ(radicle_nif_collision_target_node(nif.get(), 0, &node), RADICLE_STATUS_OK);
  EXPECT_EQ(node, 0);
}

TEST(CapiNifCollision, RefusesAMalformedCollisionBlockWithoutACrash) {
  Writer w;
  w.u32(64);
  w.u8(0x01);
  const std::vector<std::byte> bytes =
      nif_fixture::build_nif({}, {Block{"bhkPhysicsSystem", w.bytes()}}, {});
  radicle_nif* handle = nullptr;
  EXPECT_EQ(radicle_nif_read(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), &handle),
            RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(handle, nullptr);
  EXPECT_NE(std::string(radicle_last_error()).find("collision blob length runs past the end of its block"),
            std::string::npos)
      << radicle_last_error();
}
