#include <fo4/avm2/display.hpp>
#include <fo4/avm2/hit_test.hpp>
#include <fo4/avm2/interpreter.hpp>
#include <fo4/avm2/runtime.hpp>
#include <fo4/avm2/swf_runtime.hpp>
#include <fo4/swf/swf.hpp>

#include "swf_movie_builder.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace {

using fo4::avm2::DisplayObject;
using fo4::avm2::Interpreter;
using fo4::avm2::Runtime;
using fo4::avm2::SwfRuntime;

struct Harness {
  Runtime runtime;
  Interpreter interpreter{runtime};

  Harness() { runtime.link(); }
};

std::vector<std::byte> triangle_shape_bytes(std::uint16_t id, std::int32_t size) {
  // Requires a real DefineShape3 payload, so the parser supplies deltas and style records, not absolutes.
  swf_test::SwfBuilder body;
  body.u16(id);
  body.rect(0, size, 0, size, 12);
  body.u8(1);
  body.u8(0x00);
  body.u8(0xFF);
  body.u8(0x00);
  body.u8(0x00);
  body.u8(0xFF);
  body.u8(0);
  body.u8(0x10);
  body.push_bits(0, 1);
  body.push_bits(0x03, 5);
  body.push_bits(12, 5);
  body.push_bits(0, 12);
  body.push_bits(0, 12);
  body.push_bits(1, 1);
  const std::int32_t deltas[6] = {size, 0, -size, size, 0, -size};
  for (std::size_t index = 0; index < 6; index += 2) {
    body.push_bits(1, 1);
    body.push_bits(1, 1);
    body.push_bits(10, 4);
    body.push_bits(1, 1);
    body.push_bits(static_cast<std::uint32_t>(deltas[index]) & 0xFFFu, 12);
    body.push_bits(static_cast<std::uint32_t>(deltas[index + 1]) & 0xFFFu, 12);
  }
  body.push_bits(0, 6);
  body.align();
  return body.body;
}

TEST(Avm2SwfShapeGeometry, PlacedShapeGeometryDrivesShapeFlagHitTesting) {
  // Requires real DefineShape bytes through the parser: absolute edges with fill flags leave no geometry.
  const std::vector<std::byte> movie_bytes = swf_test::movie_of(
      1, {{32, triangle_shape_bytes(20, 1200)},
           {26, swf_test::place_full(
                   1, 20, swf_test::matrix_full(65536, 65536, 0, 0, 2000, 1000), {})}});
  const fo4::swf::SwfReadResult parsed = fo4::swf::read_swf(movie_bytes);
  ASSERT_TRUE(parsed.ok()) << fo4::swf::status_name(parsed.status);
  Harness harness;
  const std::shared_ptr<SwfRuntime> movie =
      SwfRuntime::create(harness.runtime, harness.interpreter, parsed.file);
  ASSERT_NE(movie, nullptr);
  ASSERT_EQ(movie->root()->children().size(), 1u);
  const DisplayObject& shape = *movie->root()->children()[0];
  ASSERT_NE(shape.shape_geometry(), nullptr);
  EXPECT_TRUE(fo4::avm2::hit_test_point(shape, 110.0, 60.0));
  EXPECT_TRUE(fo4::avm2::hit_test_point(shape, 110.0, 60.0, true));
  EXPECT_TRUE(fo4::avm2::hit_test_point(shape, 150.0, 100.0));
  EXPECT_FALSE(fo4::avm2::hit_test_point(shape, 150.0, 100.0, true));
  EXPECT_EQ(fo4::avm2::mouse_target_at(movie->root(), 150.0, 100.0).get(), nullptr);
}

}  // namespace
