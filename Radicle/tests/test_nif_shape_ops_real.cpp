#include "fo4/archive/assets.hpp"
#include "fo4/nif/nif.hpp"
#include "fo4/nif/shape_join.hpp"
#include "fo4/nif/shape_ops.hpp"
#include "real_data_plugins.hpp"
#include "real_data.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using fo4::nif::Shape;
using fo4::nif::ShapeOp;
using fo4::nif::ShapeOpReport;
using fo4::nif::ShapeOpRequest;
using fo4::nif::apply_shape_op;
using fo4::nif::read_nif;
using fo4::nif::read_shapes;

std::vector<std::byte> run(const std::vector<std::byte>& bytes, ShapeOp op, std::vector<std::uint32_t> blocks,
                           float crease, ShapeOpReport* report) {
  ShapeOpRequest request;
  request.op = op;
  request.blocks = std::move(blocks);
  request.crease_degrees = crease;
  return apply_shape_op(read_nif(bytes), request, report);
}

bool is_changed(const ShapeOpReport& report, std::uint32_t block) {
  return std::find(report.changed.begin(), report.changed.end(), block) != report.changed.end();
}

bool is_nif(const std::string& path) {
  auto extension = std::filesystem::path(path).extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return extension == ".nif";
}

struct Agreement {
  double sum{0.0};
  std::size_t count{0};
};

double cosine_at(const std::vector<float>& a, std::size_t i, const std::vector<float>& b, std::size_t j) {
  double dot = 0.0;
  double a_length = 0.0;
  double b_length = 0.0;
  for (std::size_t k = 0; k < 3; ++k) {
    dot += static_cast<double>(a[3 * i + k]) * b[3 * j + k];
    a_length += static_cast<double>(a[3 * i + k]) * a[3 * i + k];
    b_length += static_cast<double>(b[3 * j + k]) * b[3 * j + k];
  }
  if (a_length <= 0.0 || b_length <= 0.0) return std::nan("");
  return dot / std::sqrt(a_length * b_length);
}

std::vector<bool> agreeing_vertices(const Shape& shape) {
  std::map<std::array<float, 3>, std::vector<std::size_t>> by_position;
  const auto count = shape.verts.size() / 3;
  for (std::size_t v = 0; v < count; ++v) {
    by_position[{shape.verts[3 * v], shape.verts[3 * v + 1], shape.verts[3 * v + 2]}].push_back(v);
  }
  std::vector<bool> keep(count, false);
  for (const auto& group : by_position) {
    const auto& vertices = group.second;
    const bool agree = std::all_of(vertices.begin(), vertices.end(), [&](std::size_t w) {
      return cosine_at(shape.normals, w, shape.normals, vertices.front()) >= 0.99;
    });
    for (const auto v : vertices) keep[v] = agree;
  }
  return keep;
}

void measure_normals(const std::vector<float>& fresh, const std::vector<float>& authored,
                     const std::vector<bool>& keep, Agreement& total) {
  for (std::size_t v = 0; v < keep.size(); ++v) {
    if (!keep[v]) continue;
    const auto cosine = cosine_at(fresh, v, authored, v);
    if (std::isnan(cosine)) continue;
    total.sum += cosine;
    ++total.count;
  }
}

void measure_slot(const std::vector<float>& fresh, const std::vector<float>& authored, Agreement& total) {
  const auto count = std::min(fresh.size(), authored.size()) / 3;
  for (std::size_t v = 0; v < count; ++v) {
    const auto cosine = cosine_at(fresh, v, authored, v);
    if (std::isnan(cosine)) continue;
    total.sum += cosine;
    ++total.count;
  }
}

}  // namespace

TEST(ShapeOpsCorpus, SmoothNormalsAndTangentsTrackTheAuthoredFrame) {
  FO4_REQUIRE_DATA_DIR(dir, "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives");
  const auto archives = fo4::archive::archive_load_order(dir, {}, installed_master_and_cc_plugins(dir));
  if (archives.empty()) {
    GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives";
  }

  const auto assets = fo4::archive::AssetResolver::open({}, archives);
  Agreement normals;
  Agreement tangents;
  Agreement bitangents;
  std::size_t nifs = 0;
  for (const auto& reader : assets.archives()) {
    if (reader.kind() != fo4::archive::Ba2Kind::general) continue;
    for (std::size_t i = 0; i < reader.entries().size(); ++i) {
      if (!is_nif(reader.entries()[i].path)) continue;
      const auto bytes = reader.extract(i);
      const auto authored = read_shapes(read_nif(bytes));
      std::vector<std::uint32_t> blocks;
      for (const auto& shape : authored) {
        if (!shape.normals.empty() && !shape.tris.empty()) blocks.push_back(shape.block);
      }
      if (blocks.empty()) continue;
      ++nifs;
      ShapeOpReport smooth_report;
      const auto smooth = run(bytes, ShapeOp::recalc_normals, blocks, 180.0f, &smooth_report);
      ShapeOpReport tangent_report;
      const auto framed = run(bytes, ShapeOp::recalc_tangents, blocks, 180.0f, &tangent_report);
      const auto smooth_shapes = read_shapes(read_nif(smooth));
      const auto framed_shapes = read_shapes(read_nif(framed));
      for (std::size_t k = 0; k < authored.size(); ++k) {
        if (is_changed(smooth_report, authored[k].block)) {
          measure_normals(smooth_shapes[k].normals, authored[k].normals, agreeing_vertices(authored[k]), normals);
        }
        if (is_changed(tangent_report, authored[k].block)) {
          measure_slot(framed_shapes[k].tangents, authored[k].tangents, tangents);
          measure_slot(framed_shapes[k].bitangents, authored[k].bitangents, bitangents);
        }
      }
    }
  }
  const auto normal_mean = normals.count == 0 ? 0.0 : normals.sum / static_cast<double>(normals.count);
  const auto tangent_mean = tangents.count == 0 ? 0.0 : tangents.sum / static_cast<double>(tangents.count);
  const auto bitangent_mean = bitangents.count == 0 ? 0.0 : bitangents.sum / static_cast<double>(bitangents.count);
  std::cout << "shape ops corpus: nifs " << nifs << ", smoothed vertices " << normals.count
            << ", mean dot with authored normals " << normal_mean << ", tangent vertices " << tangents.count
            << ", mean dot with authored tangents " << tangent_mean << ", bitangent vertices " << bitangents.count
            << ", mean dot with authored bitangents " << bitangent_mean << '\n';
  EXPECT_GT(normals.count, 0U);
  EXPECT_GT(normal_mean, 0.9);
  EXPECT_GT(tangents.count, 0U);
  EXPECT_GT(tangent_mean, 0.8);
  EXPECT_GT(bitangents.count, 0U);
  EXPECT_GT(bitangent_mean, 0.8);
}

// Invariant: a vanilla shape survives separate then join with every triangle and bone name kept.
TEST(NifShapeOpsRealData, SeparateThenJoinKeepsEveryTriangleOnAVanillaShape) {
  FO4_REQUIRE_DATA_DIR(dir, "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives");
  const auto archives = fo4::archive::archive_load_order(dir, {}, installed_master_and_cc_plugins(dir));
  if (archives.empty()) {
    GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives";
  }
  const auto assets = fo4::archive::AssetResolver::open({}, archives);
  std::size_t tried = 0;
  std::string first_failure;
  bool done = false;
  for (const auto& reader : assets.archives()) {
    if (reader.kind() != fo4::archive::Ba2Kind::general) continue;
    for (std::size_t i = 0; i < reader.entries().size() && !done; ++i) {
      if (!is_nif(reader.entries()[i].path)) continue;
      const auto bytes = reader.extract(i);
      std::optional<fo4::nif::NifFile> nif;
      try {
        nif = read_nif(bytes);
      } catch (const std::exception&) {
        continue;
      }
      for (const auto& shape : read_shapes(*nif)) {
        if (shape.tris.size() < 12) continue;
        std::vector<std::uint32_t> first;
        std::vector<std::uint32_t> second;
        for (std::uint32_t t = 0; t < shape.tris.size() / 3; ++t) {
          (t % 2 == 0 ? first : second).push_back(t);
        }
        if (second.empty()) continue;
        ++tried;
        try {
          fo4::nif::SeparateRequest request;
          request.block = shape.block;
          request.groups = {first, second};
          fo4::nif::SeparateReport separate_report;
          const auto split = fo4::nif::separate_shape(*nif, request, &separate_report);
          fo4::nif::JoinRequest join;
          join.blocks = separate_report.blocks;
          fo4::nif::JoinReport join_report;
          const auto joined = fo4::nif::join_shapes(read_nif(split), join, &join_report);
          const auto check = read_nif(joined);
          const auto after = read_shape(check, join_report.target);
          EXPECT_EQ(after.tris.size(), shape.tris.size());
          EXPECT_EQ(join_report.triangles_after, shape.tris.size() / 3);
          ASSERT_EQ(after.bones.size(), shape.bones.size());
          for (std::size_t b = 0; b < shape.bones.size(); ++b) EXPECT_EQ(after.bones[b].name, shape.bones[b].name);
          std::cout << "separate then join: " << reader.entries()[i].path << " triangles "
                    << shape.tris.size() / 3 << ", bones " << shape.bones.size() << '\n';
          done = true;
          break;
        } catch (const std::exception& error) {
          if (first_failure.empty()) {
            first_failure = std::string(reader.entries()[i].path) + ": " + error.what();
          }
        }
      }
    }
    if (done) break;
  }
  EXPECT_TRUE(done) << "no vanilla shape survived separate then join; first failure: " << first_failure;
  EXPECT_GT(tried, 0U);
}
