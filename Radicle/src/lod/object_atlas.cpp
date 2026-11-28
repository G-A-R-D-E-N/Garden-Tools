#include "fo4/lod/object_atlas.hpp"

#include "fo4/lod/terrain_texture.hpp"

#include "fo4/hash.hpp"
#include "fo4/texture/dds.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <map>
#include <memory>
#include <new>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace fo4::lod {
namespace {

constexpr std::size_t k_pixel_bytes = 4;
// Design choice, unmeasured: the per-source cap, gutter and repeat cap keep a world's textures in one 4096 atlas.
constexpr int k_max_entry_side = 512;
constexpr int k_min_entry_side = 4;
constexpr int k_gutter = 4;
constexpr int k_fallback_side = 8;
constexpr int k_max_shrink = 8;
constexpr std::array<std::uint8_t, 4> k_flat_normal{128, 128, 255, 255};
// Design choice, unmeasured: a source without a specular map gets a dull surface instead of a mirror.
constexpr std::array<std::uint8_t, 4> k_flat_specular{40, 40, 40, 255};

std::string_view map_suffix(ObjectAtlasMap map) {
  switch (map) {
    case ObjectAtlasMap::diffuse:
      return ".DDS";
    case ObjectAtlasMap::normal:
      return "_n.DDS";
    case ObjectAtlasMap::specular:
      return "_s.DDS";
  }
  return ".DDS";
}

std::string_view map_stem(ObjectAtlasMap map) {
  switch (map) {
    case ObjectAtlasMap::diffuse:
      return "diffuse";
    case ObjectAtlasMap::normal:
      return "normal";
    case ObjectAtlasMap::specular:
      return "specular";
  }
  return "diffuse";
}

std::uint64_t hash_text(std::string_view text) {
  return fnv1a64(text);
}

std::uint8_t channel(std::uint64_t hash, int shift, std::uint8_t base, std::uint8_t span) {
  return static_cast<std::uint8_t>(base + static_cast<std::uint8_t>(((hash >> shift) & 0xFFU) % span));
}

std::string lower_copy(std::string_view text) {
  std::string out{text};
  for (auto& ch : out) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return out;
}

std::string texture_asset_path(std::string_view name) { return lod_data_folder_path(name, "Textures"); }

struct SourceImage {
  int width{0};
  int height{0};
  std::vector<std::uint8_t> rgba;
};

// Invariant: a source texture is decoded once per bake; an unreadable one is remembered and counted once.
struct TextureCache {
  const archive::AssetResolver& assets;
  std::map<std::string, std::optional<SourceImage>> images;
  std::size_t missing{0};
};

const std::optional<SourceImage>& load_texture(TextureCache& cache, std::string_view name, ObjectAtlasMap map) {
  const auto path = texture_asset_path(name);
  const auto key = lower_copy(path);
  const auto found = cache.images.find(key);
  if (found != cache.images.end()) return found->second;
  std::optional<SourceImage> loaded;
  try {
    if (const auto location = cache.assets.locate(path)) {
      const auto bytes = cache.assets.read(*location);
      std::vector<std::uint8_t> raw(bytes.size());
      for (std::size_t index = 0; index < bytes.size(); ++index) {
        raw[index] = std::to_integer<std::uint8_t>(bytes[index]);
      }
      const auto info = texture::read_dds(raw);
      // Constraint: the first mip at or under the entry cap is decoded, so a 4K source never fills memory.
      std::uint32_t mip = 0;
      while (static_cast<int>(mip) + 1 < info.mip_count &&
             std::max(info.width >> mip, info.height >> mip) > k_max_entry_side) {
        ++mip;
      }
      std::uint32_t width = 0;
      std::uint32_t height = 0;
      SourceImage image;
      image.rgba = texture::decode_dds_rgba(raw, mip, map == ObjectAtlasMap::normal && texture::is_bc5(raw), width,
                                            height);
      image.width = static_cast<int>(width);
      image.height = static_cast<int>(height);
      if (image.width > 0 && image.height > 0 &&
          image.rgba.size() == static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) *
                                   k_pixel_bytes) {
        loaded = std::move(image);
      }
    }
  } catch (const std::bad_alloc&) {
    throw;
  } catch (const std::exception&) {
    loaded.reset();
  }
  if (!loaded) ++cache.missing;
  return cache.images.emplace(key, std::move(loaded)).first->second;
}

// Invariant: shapes share an entry only when they would sample the same texels: same sources, same repeat window.
struct EntryKey {
  std::array<std::string, k_object_atlas_maps> textures;
  // Invariant: the material only tells entries apart when no diffuse is named, where it picks the stand-in colour.
  std::string material;
  int repeat_u{1};
  int repeat_v{1};
  friend bool operator<(const EntryKey& left, const EntryKey& right) {
    return std::tie(left.textures, left.material, left.repeat_u, left.repeat_v) <
           std::tie(right.textures, right.material, right.repeat_u, right.repeat_v);
  }
};

EntryKey key_of(const ObjectTileShape& shape) {
  EntryKey key;
  for (std::size_t index = 0; index < k_object_atlas_maps; ++index) {
    key.textures[index] =
        shape.textures[index].empty() ? std::string{} : lower_copy(texture_asset_path(shape.textures[index]));
  }
  if (key.textures[0].empty()) key.material = lower_copy(shape.material);
  key.repeat_u = std::clamp(shape.uv_repeat[0], 1, k_object_uv_max_repeat);
  key.repeat_v = std::clamp(shape.uv_repeat[1], 1, k_object_uv_max_repeat);
  return key;
}

struct Entry {
  std::array<const SourceImage*, k_object_atlas_maps> images{};
  std::array<std::uint8_t, 4> stand_in{0, 0, 0, 255};
  int base_width{k_fallback_side};
  int base_height{k_fallback_side};
  // Invariant: width and height are the interior texels; the cell adds a gutter on every side.
  int width{0};
  int height{0};
  int x{0};
  int y{0};
};

int entry_side(int base, int repeat, int shrink) {
  auto side = static_cast<std::int64_t>(base) * repeat;
  while (side > k_max_entry_side) side >>= 1;
  side >>= shrink;
  side = std::max<std::int64_t>(side, k_min_entry_side);
  // Constraint: BC3 blocks are 4x4, so a whole-block interior keeps neighbours out of each other's blocks.
  return static_cast<int>((side + 3) / 4 * 4);
}

// Invariant: entries are placed tallest first on shelves, in key order within one height, so the layout is stable.
bool place_entries(std::vector<Entry*>& order, int side) {
  int x = 0;
  int y = 0;
  int shelf = 0;
  for (auto* entry : order) {
    const int cell_width = entry->width + 2 * k_gutter;
    const int cell_height = entry->height + 2 * k_gutter;
    if (cell_width > side || cell_height > side) return false;
    if (x + cell_width > side) {
      y += shelf;
      x = 0;
      shelf = 0;
    }
    if (y + cell_height > side) return false;
    entry->x = x;
    entry->y = y;
    x += cell_width;
    shelf = std::max(shelf, cell_height);
  }
  return true;
}

std::array<std::uint8_t, 4> sample(const SourceImage& image, int repeat_u, int repeat_v, int column, int row, int width,
                                   int height) {
  // Invariant: one atlas texel averages the source texels it covers, so a shrunk entry does not alias.
  const auto span_u = static_cast<std::int64_t>(image.width) * repeat_u;
  const auto span_v = static_cast<std::int64_t>(image.height) * repeat_v;
  const auto first_u = span_u * column / width;
  const auto first_v = span_v * row / height;
  const auto step_u = std::max<std::int64_t>(1, span_u / width);
  const auto step_v = std::max<std::int64_t>(1, span_v / height);
  std::array<std::uint32_t, 4> sum{};
  for (std::int64_t dv = 0; dv < step_v; ++dv) {
    for (std::int64_t du = 0; du < step_u; ++du) {
      const auto texel_u = static_cast<std::size_t>((first_u + du) % image.width);
      const auto texel_v = static_cast<std::size_t>((first_v + dv) % image.height);
      const auto index = (texel_v * static_cast<std::size_t>(image.width) + texel_u) * k_pixel_bytes;
      for (std::size_t part = 0; part < 4; ++part) sum[part] += image.rgba[index + part];
    }
  }
  const auto count = static_cast<std::uint32_t>(step_u * step_v);
  std::array<std::uint8_t, 4> out{};
  for (std::size_t part = 0; part < 4; ++part) out[part] = static_cast<std::uint8_t>((sum[part] + count / 2) / count);
  return out;
}

void blit_entry(TerrainImage& target, const Entry& entry, const EntryKey& key, std::size_t map) {
  const auto* image = entry.images[map];
  for (int row = -k_gutter; row < entry.height + k_gutter; ++row) {
    for (int column = -k_gutter; column < entry.width + k_gutter; ++column) {
      // Invariant: the gutter repeats the edge texel, so a mip never blends a neighbouring entry in.
      const int inner_column = std::clamp(column, 0, entry.width - 1);
      const int inner_row = std::clamp(row, 0, entry.height - 1);
      std::array<std::uint8_t, 4> texel = entry.stand_in;
      if (image != nullptr) {
        texel = sample(*image, key.repeat_u, key.repeat_v, inner_column, inner_row, entry.width, entry.height);
      } else if (map == static_cast<std::size_t>(ObjectAtlasMap::normal)) {
        texel = k_flat_normal;
      } else if (map == static_cast<std::size_t>(ObjectAtlasMap::specular)) {
        texel = k_flat_specular;
      }
      const auto index = (static_cast<std::size_t>(entry.y + k_gutter + row) * static_cast<std::size_t>(target.width) +
                          static_cast<std::size_t>(entry.x + k_gutter + column)) *
                         k_pixel_bytes;
      for (std::size_t part = 0; part < 4; ++part) target.rgba[index + part] = texel[part];
    }
  }
}

void clamp_atlas_cells(std::vector<std::uint8_t>& level, int width, int height, int mip, const void* context) {
  const auto* cells = static_cast<const std::vector<ObjectAtlas::AtlasCell>*>(context);
  if (cells == nullptr || mip <= 0 || width <= 0 || height <= 0) return;
  for (const auto& cell : *cells) {
    const int x0 = cell.x >> mip;
    const int y0 = cell.y >> mip;
    const int x1 = (cell.x + cell.width) >> mip;
    const int y1 = (cell.y + cell.height) >> mip;
    const int ix0 = std::clamp((cell.x + k_gutter) >> mip, x0, x1);
    const int iy0 = std::clamp((cell.y + k_gutter) >> mip, y0, y1);
    const int ix1 = std::clamp((cell.x + cell.width - k_gutter) >> mip, x0, x1);
    const int iy1 = std::clamp((cell.y + cell.height - k_gutter) >> mip, y0, y1);
    if (ix1 <= ix0 || iy1 <= iy0) continue;
    for (int row = std::max(0, y0); row < std::min(y1, height); ++row) {
      for (int column = std::max(0, x0); column < std::min(x1, width); ++column) {
        if (column >= ix0 && column < ix1 && row >= iy0 && row < iy1) continue;
        const int source_column = std::clamp(column, ix0, ix1 - 1);
        const int source_row = std::clamp(row, iy0, iy1 - 1);
        const auto from = (static_cast<std::size_t>(source_row) * static_cast<std::size_t>(width) +
                           static_cast<std::size_t>(source_column)) *
                          k_pixel_bytes;
        const auto to = (static_cast<std::size_t>(row) * static_cast<std::size_t>(width) +
                         static_cast<std::size_t>(column)) *
                        k_pixel_bytes;
        for (std::size_t part = 0; part < k_pixel_bytes; ++part) level[to + part] = level[from + part];
      }
    }
  }
}

// Invariant: a UV lands on texel centres inside the interior, so bilinear sampling never reaches the gutter's far side.
void remap_shape(ObjectTileShape& shape, const EntryKey& key, const Entry& entry, int side) {
  const auto scale = 1.0f / static_cast<float>(side);
  for (auto& vertex : shape.vertices) {
    const float local_u =
        std::clamp(vertex.u / static_cast<float>(key.repeat_u), 0.0f, 1.0f);
    const float local_v =
        std::clamp(vertex.v / static_cast<float>(key.repeat_v), 0.0f, 1.0f);
    vertex.u = (static_cast<float>(entry.x + k_gutter) + 0.5f + local_u * static_cast<float>(entry.width - 1)) * scale;
    vertex.v = (static_cast<float>(entry.y + k_gutter) + 0.5f + local_v * static_cast<float>(entry.height - 1)) * scale;
  }
}

}

std::string lod_data_folder_path(std::string_view name, std::string_view folder) {
  std::string path{name};
  for (auto& ch : path) {
    if (ch == '/') ch = '\\';
  }
  const auto lower = lower_copy(path);
  const auto prefix = lower_copy(folder) + '\\';
  if (lower.rfind(prefix, 0) == 0) return path;
  // Constraint: only a Data folder marks where the game path starts; a deeper folder of that name stays.
  const auto rooted = "data\\" + prefix;
  for (auto at = lower.find(rooted); at != std::string::npos; at = lower.find(rooted, at + 1)) {
    if (at == 0 || lower[at - 1] == '\\') return path.substr(at + 5);
  }
  return std::string{folder} + '\\' + path;
}

std::string_view object_atlas_map_name(ObjectAtlasMap map) { return map_stem(map); }

std::string object_atlas_texture_path(std::string_view worldspace, ObjectAtlasMap map) {
  std::string path{"Data\\Textures\\Terrain\\"};
  path += worldspace;
  path += "\\Objects\\";
  path += worldspace;
  path += ".Objects";
  path += map_suffix(map);
  return path;
}

std::string object_atlas_content_ledger_key(std::string_view worldspace) {
  return lower_copy(worldspace) + "|atlas|content";
}

std::string object_atlas_ledger_key(std::string_view worldspace, ObjectAtlasMap map) {
  std::string key;
  for (const char raw : worldspace) key.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(raw))));
  key += "|texture|";
  key += map_stem(map);
  return key;
}

std::filesystem::path object_atlas_local_path(const std::filesystem::path& data_dir, std::string_view worldspace,
                                              ObjectAtlasMap map) {
  std::filesystem::path path = data_dir;
  path /= "Textures";
  path /= "Terrain";
  path /= std::string{worldspace};
  path /= "Objects";
  std::string name{worldspace};
  name += ".Objects";
  name += map_suffix(map);
  path /= name;
  return path;
}

struct ObjectAtlasPlan::State {
  // Invariant: keys keep first-seen order, so textures load in the order the all-at-once bake loaded them.
  std::vector<EntryKey> order;
  std::set<EntryKey> seen;
  std::size_t shapes{0};
  std::size_t clamped_shapes{0};
  std::map<EntryKey, Entry> entries;
  int side{0};
};

ObjectAtlasPlan::ObjectAtlasPlan() : state_(std::make_unique<State>()) {}

ObjectAtlasPlan::~ObjectAtlasPlan() = default;

void ObjectAtlasPlan::add(const ObjectTile& tile) {
  auto& state = *state_;
  for (const auto& shape : tile.shapes) {
    if (shape.triangles.size() < 3 || shape.vertices.empty()) continue;
    ++state.shapes;
    if (shape.uv_clamped) ++state.clamped_shapes;
    auto key = key_of(shape);
    if (state.seen.insert(key).second) state.order.push_back(std::move(key));
  }
}

ObjectAtlas ObjectAtlasPlan::bake(const archive::AssetResolver& assets) {
  auto& state = *state_;
  auto& entries = state.entries;
  ObjectAtlas atlas;
  atlas.shapes = state.shapes;
  atlas.clamped_shapes = state.clamped_shapes;
  TextureCache cache{assets, {}, 0};
  entries.clear();
  for (const auto& key : state.order) {
    Entry entry;
    for (std::size_t map = 0; map < k_object_atlas_maps; ++map) {
      if (key.textures[map].empty()) continue;
      const auto& image = load_texture(cache, key.textures[map], static_cast<ObjectAtlasMap>(map));
      if (image) entry.images[map] = &*image;
    }
    if (entry.images[0] != nullptr) {
      entry.base_width = entry.images[0]->width;
      entry.base_height = entry.images[0]->height;
    } else {
      // Invariant: a source with no readable diffuse still gets a stable, distinguishable colour, never a hole.
      const auto hash = hash_text(key.textures[0].empty() ? key.material : key.textures[0]);
      entry.stand_in = {channel(hash, 0, 48, 160), channel(hash, 8, 48, 160), channel(hash, 16, 48, 160), 255};
    }
    entries.emplace(key, entry);
  }
  atlas.entries = entries.size();
  atlas.missing_textures = cache.missing;
  if (entries.empty()) return atlas;

  std::vector<Entry*> order;
  order.reserve(entries.size());
  bool placed = false;
  for (int shrink = 0; shrink <= k_max_shrink && !placed; ++shrink) {
    order.clear();
    for (auto& [key, entry] : entries) {
      entry.width = entry_side(entry.base_width, key.repeat_u, shrink);
      entry.height = entry_side(entry.base_height, key.repeat_v, shrink);
      order.push_back(&entry);
    }
    std::stable_sort(order.begin(), order.end(),
                     [](const Entry* left, const Entry* right) { return left->height > right->height; });
    for (int side = k_object_atlas_min_side; side <= k_object_atlas_max_side; side <<= 1) {
      if (!place_entries(order, side)) continue;
      atlas.side = side;
      placed = true;
      break;
    }
  }
  if (!placed) throw LodError("object atlas cannot hold the world's LOD texture set");

  const auto pixels = static_cast<std::size_t>(atlas.side) * static_cast<std::size_t>(atlas.side);
  for (std::size_t map = 0; map < k_object_atlas_maps; ++map) {
    auto& image = atlas.maps[map];
    image.width = atlas.side;
    image.height = atlas.side;
    image.rgba.assign(pixels * k_pixel_bytes, 0);
    // Constraint: the normal and specular maps start filled, so an unused texel still holds a valid surface value.
    if (map == static_cast<std::size_t>(ObjectAtlasMap::diffuse)) continue;
    const auto& fill = map == static_cast<std::size_t>(ObjectAtlasMap::normal) ? k_flat_normal : k_flat_specular;
    for (std::size_t pixel = 0; pixel < pixels; ++pixel) {
      for (std::size_t part = 0; part < 4; ++part) image.rgba[pixel * k_pixel_bytes + part] = fill[part];
    }
  }
  for (const auto& [key, entry] : entries) {
    atlas.cells.push_back(ObjectAtlas::AtlasCell{entry.x, entry.y, entry.width + 2 * k_gutter,
                                                 entry.height + 2 * k_gutter});
    for (std::size_t map = 0; map < k_object_atlas_maps; ++map) blit_entry(atlas.maps[map], entry, key, map);
  }

  std::uint64_t hash = k_fnv64_offset;
  const auto mix = [&hash](std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) {
      hash ^= (value >> shift) & 0xFFU;
      hash *= k_fnv64_prime;
    }
  };
  mix(static_cast<std::uint64_t>(atlas.side));
  for (const auto& [key, entry] : entries) {
    for (const auto& texture : key.textures) mix(hash_text(texture));
    mix(hash_text(key.material));
    for (const int value : {key.repeat_u, key.repeat_v, entry.x, entry.y, entry.width, entry.height}) {
      mix(static_cast<std::uint64_t>(value));
    }
  }
  for (const auto& image : atlas.maps) {
    for (const auto byte : image.rgba) {
      hash ^= byte;
      hash *= k_fnv64_prime;
    }
  }
  atlas.content_hash = hash;
  state.side = atlas.side;
  // Invariant: the texture cache dies with this call, so no entry may keep a pointer into it.
  for (auto& [key, entry] : entries) entry.images = {};
  return atlas;
}

bool ObjectAtlasPlan::remap(ObjectTile& tile) const {
  const auto& state = *state_;
  struct Use {
    ObjectTileShape* shape{nullptr};
    const EntryKey* key{nullptr};
    const Entry* entry{nullptr};
  };
  std::vector<Use> uses;
  for (auto& shape : tile.shapes) {
    if (shape.triangles.size() < 3 || shape.vertices.empty()) continue;
    const auto found = state.entries.find(key_of(shape));
    if (found == state.entries.end()) return false;
    uses.push_back(Use{&shape, &found->first, &found->second});
  }
  for (const auto& use : uses) remap_shape(*use.shape, *use.key, *use.entry, state.side);
  return true;
}

ObjectAtlas bake_object_atlas(const archive::AssetResolver& assets, std::vector<ObjectTile>& tiles) {
  ObjectAtlasPlan plan;
  for (const auto& tile : tiles) plan.add(tile);
  auto atlas = plan.bake(assets);
  for (auto& tile : tiles) plan.remap(tile);
  return atlas;
}

std::vector<std::byte> write_object_atlas_dds(const ObjectAtlas& atlas, ObjectAtlasMap map) {
  const auto index = static_cast<std::size_t>(map);
  if (index >= atlas.maps.size()) throw LodError("object atlas map index is out of range");
  if (atlas.side <= 0) throw LodError("object atlas has no baked side");
  // Invariant: only the diffuse map keeps its alpha, because the alpha-tested shapes cut out against it.
  return write_terrain_dds(atlas.maps[index], TerrainDdsFormat::bc3,
                           map == ObjectAtlasMap::diffuse ? TerrainDdsAlpha::keep : TerrainDdsAlpha::opaque,
                           clamp_atlas_cells, &atlas.cells);
}

std::uint64_t object_atlas_inputs_hash(std::string_view worldspace, ObjectAtlasMap map,
                                       std::span<const LodInput> inputs) {
  // Constraint: the identity level is a constant, so verify can recompute it without knowing the baked side.
  const auto identity = LodTile{std::string{worldspace}, 1, static_cast<std::int32_t>(map), 0, LodTileKind::object};
  return lod_tile_inputs_hash(identity, inputs);
}

bool object_atlas_dds_well_formed(std::span<const std::byte> bytes) {
  if (bytes.size() < 128) return false;
  std::vector<std::uint8_t> raw(bytes.size());
  for (std::size_t index = 0; index < bytes.size(); ++index) raw[index] = std::to_integer<std::uint8_t>(bytes[index]);
  try {
    const auto info = texture::read_dds(raw);
    if (info.dxgi_format() != static_cast<std::uint8_t>(TerrainDdsFormat::bc3)) return false;
    if (info.width != info.height) return false;
    if (info.width < k_object_atlas_min_side || info.width > k_object_atlas_max_side) return false;
    if ((info.width & (info.width - 1)) != 0) return false;
    int expected_mips = 1;
    for (int side = info.width; side > 1; side >>= 1) ++expected_mips;
    if (info.mip_count != expected_mips) return false;
    return static_cast<std::int64_t>(raw.size()) ==
           static_cast<std::int64_t>(info.data_offset) + info.payload_size();
  } catch (const std::exception&) {
    return false;
  }
}

}
