#include "c_api_env_internal.hpp"
#include "fo4/archive/assets.hpp"
#include "fo4/archive/ba2.hpp"
#include "fo4/archive/ba2_hash.hpp"
#include "fo4/environment/strings.hpp"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

struct radicle_asset_list {
  std::vector<fo4::archive::AssetListing> items;
};

namespace {

using namespace radicle_c_detail;
using namespace radicle_c_env_detail;

const fo4::archive::Ba2Reader* to_reader(const radicle_ba2* ba2) {
  return reinterpret_cast<const fo4::archive::Ba2Reader*>(ba2);
}

const radicle_ba2* to_ba2(const fo4::archive::Ba2Reader* reader) {
  return reinterpret_cast<const radicle_ba2*>(reader);
}

radicle_status copy_bytes(const std::vector<std::byte>& bytes, uint8_t** out_bytes, size_t* out_size) {
  if (bytes.empty()) return RADICLE_STATUS_OK;
  auto* result = static_cast<uint8_t*>(std::malloc(bytes.size()));
  if (result == nullptr) return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate asset bytes");
  std::memcpy(result, bytes.data(), bytes.size());
  *out_bytes = result;
  *out_size = bytes.size();
  return RADICLE_STATUS_OK;
}

radicle_asset_location to_c_location(const fo4::archive::AssetLocation& location) {
  if (location.origin == fo4::archive::AssetOrigin::loose) return {RADICLE_ASSET_LOOSE, 0, 0};
  return {RADICLE_ASSET_ARCHIVE, location.archive, location.entry};
}

bool valid_list(const char* const* values, size_t count) {
  if (values == nullptr) return count == 0;
  for (size_t i = 0; i < count; ++i) {
    if (values[i] == nullptr) return false;
  }
  return true;
}

}  // namespace

extern "C" {

radicle_status radicle_ba2_open(const char* path, radicle_ba2** out_ba2) {
  clear_error();
  if (path == nullptr || out_ba2 == nullptr) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "path and out_ba2 are required");
  *out_ba2 = nullptr;
  return guarded("unknown exception while opening BA2", [&] {
    auto reader = std::make_unique<fo4::archive::Ba2Reader>(fo4::archive::Ba2Reader::open(utf8_path(path)));
    *out_ba2 = const_cast<radicle_ba2*>(to_ba2(reader.release()));
    return RADICLE_STATUS_OK;
  });
}

void radicle_ba2_close(radicle_ba2* ba2) {
  clear_error();
  delete to_reader(ba2);
}

radicle_status radicle_ba2_get_info(const radicle_ba2* ba2, radicle_ba2_info* out_info) {
  clear_error();
  if (ba2 == nullptr || out_info == nullptr) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "ba2 and out_info are required");
  const auto& reader = *to_reader(ba2);
  *out_info = {reader.kind() == fo4::archive::Ba2Kind::texture ? RADICLE_BA2_TEXTURE : RADICLE_BA2_GENERAL,
               reader.version(), reader.has_name_table() ? 1 : 0, reader.entries().size()};
  return RADICLE_STATUS_OK;
}

radicle_status radicle_ba2_get_entry(const radicle_ba2* ba2, size_t index, radicle_ba2_entry_info* out_entry) {
  clear_error();
  if (ba2 == nullptr || out_entry == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "ba2 and out_entry are required");
  }
  const auto& reader = *to_reader(ba2);
  if (index >= reader.entries().size()) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "entry index is out of range");
  const auto& entry = reader.entries()[index];
  *out_entry = {};
  out_entry->path = entry.path.c_str();
  out_entry->name_hash = entry.name_hash;
  out_entry->extension_hash = entry.extension_hash;
  out_entry->directory_hash = entry.directory_hash;
  out_entry->unpacked_size = entry.unpacked_size();
  out_entry->chunk_count = entry.chunks.size();
  if (reader.kind() == fo4::archive::Ba2Kind::texture) {
    out_entry->width = entry.texture.width;
    out_entry->height = entry.texture.height;
    out_entry->mip_count = entry.texture.mip_count;
    out_entry->dxgi_format = entry.texture.dxgi_format;
    out_entry->texture_flags = entry.texture.flags;
    out_entry->tile_mode = entry.texture.tile_mode;
  }
  return RADICLE_STATUS_OK;
}

radicle_status radicle_ba2_find(const radicle_ba2* ba2, const char* path, size_t* out_index) {
  clear_error();
  if (ba2 == nullptr || path == nullptr || out_index == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "ba2, path and out_index are required");
  }
  *out_index = 0;
  return guarded("unknown exception while searching BA2", [&] {
    const auto index = to_reader(ba2)->find(path);
    if (!index) return fail(RADICLE_STATUS_NOT_FOUND, "path is not in the archive");
    *out_index = *index;
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_ba2_extract(const radicle_ba2* ba2, size_t index, uint8_t** out_bytes, size_t* out_size) {
  clear_error();
  if (ba2 == nullptr || out_bytes == nullptr || out_size == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "ba2, out_bytes and out_size are required");
  }
  *out_bytes = nullptr;
  *out_size = 0;
  if (index >= to_reader(ba2)->entries().size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "entry index is out of range");
  }
  return guarded("unknown exception while extracting BA2 entry",
                 [&] { return copy_bytes(to_reader(ba2)->extract(index), out_bytes, out_size); });
}

radicle_status radicle_ba2_write_general(const radicle_ba2_file* files,
                                         size_t file_count,
                                         uint32_t version,
                                         int32_t compress,
                                         uint8_t** out_bytes,
                                         size_t* out_size) {
  clear_error();
  if ((files == nullptr && file_count != 0) || out_bytes == nullptr || out_size == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "files, out_bytes and out_size are required");
  }
  *out_bytes = nullptr;
  *out_size = 0;
  for (size_t i = 0; i < file_count; ++i) {
    if (files[i].path == nullptr || (files[i].data == nullptr && files[i].size != 0)) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "every file needs a path and data");
    }
  }
  return guarded("unknown exception while writing BA2", [&] {
    fo4::archive::Ba2GeneralArchive archive;
    archive.version = version;
    archive.entries.reserve(file_count);
    for (size_t i = 0; i < file_count; ++i) {
      fo4::archive::Ba2GeneralEntry entry;
      entry.path = files[i].path;
      const auto hashes = fo4::archive::compute_ba2_hashes(entry.path);
      entry.name_hash = hashes.name;
      entry.extension_hash = hashes.extension;
      entry.directory_hash = hashes.directory;
      const std::span<const std::byte> data(reinterpret_cast<const std::byte*>(files[i].data), files[i].size);
      entry.chunks.push_back(compress != 0 ? fo4::archive::make_compressed_ba2_chunk(data)
                                           : fo4::archive::make_stored_ba2_chunk(data));
      archive.entries.push_back(std::move(entry));
    }
    return copy_bytes(fo4::archive::write_ba2_general(archive), out_bytes, out_size);
  });
}

radicle_status radicle_archive_load_order(const char* data_dir,
                                          const char* const* ini_archives,
                                          size_t ini_count,
                                          const char* const* plugins,
                                          size_t plugin_count,
                                          char*** out_paths,
                                          size_t* out_count) {
  clear_error();
  if (data_dir == nullptr || !valid_list(ini_archives, ini_count) || !valid_list(plugins, plugin_count) ||
      out_paths == nullptr || out_count == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "data_dir, non-NULL name lists and outputs are required");
  }
  *out_paths = nullptr;
  *out_count = 0;
  return guarded("unknown exception while ordering archives", [&] {
    const std::vector<std::string> ini(ini_archives, ini_archives + ini_count);
    const std::vector<std::string> names(plugins, plugins + plugin_count);
    std::vector<std::string> paths;
    for (const auto& path : fo4::archive::archive_load_order(utf8_path(data_dir), ini, names)) {
      const auto utf8 = path.u8string();
      paths.emplace_back(utf8.begin(), utf8.end());
    }
    return copy_strings(paths, out_paths, out_count);
  });
}

radicle_status radicle_assets_open(const char* data_dir,
                                   const char* const* archive_paths,
                                   size_t archive_count,
                                   radicle_assets** out_assets) {
  clear_error();
  if (!valid_list(archive_paths, archive_count) || out_assets == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "non-NULL archive paths and out_assets are required");
  }
  *out_assets = nullptr;
  return guarded("unknown exception while opening assets", [&] {
    std::vector<std::filesystem::path> archives;
    for (size_t i = 0; i < archive_count; ++i) archives.push_back(utf8_path(archive_paths[i]));
    const auto dir = data_dir == nullptr ? std::filesystem::path{} : utf8_path(data_dir);
    auto assets = std::make_unique<radicle_assets>(radicle_assets{fo4::archive::AssetResolver::open(dir, archives)});
    *out_assets = assets.release();
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_assets_open_roots(const char* const* root_paths,
                                         size_t root_count,
                                         const char* const* archive_paths,
                                         size_t archive_count,
                                         radicle_assets** out_assets) {
  clear_error();
  if (!valid_list(root_paths, root_count) || !valid_list(archive_paths, archive_count) || out_assets == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "non-NULL root paths, archive paths and out_assets are required");
  }
  *out_assets = nullptr;
  return guarded("unknown exception while opening assets", [&] {
    std::vector<std::filesystem::path> roots;
    for (size_t i = 0; i < root_count; ++i) roots.push_back(utf8_path(root_paths[i]));
    std::vector<std::filesystem::path> archives;
    for (size_t i = 0; i < archive_count; ++i) archives.push_back(utf8_path(archive_paths[i]));
    auto assets = std::make_unique<radicle_assets>(radicle_assets{fo4::archive::AssetResolver::open_roots(roots, archives)});
    *out_assets = assets.release();
    return RADICLE_STATUS_OK;
  });
}

void radicle_assets_destroy(radicle_assets* assets) {
  clear_error();
  delete assets;
}

size_t radicle_assets_archive_count(const radicle_assets* assets) {
  clear_error();
  return assets == nullptr ? 0 : assets->value.archives().size();
}

size_t radicle_assets_root_count(const radicle_assets* assets) {
  clear_error();
  return assets == nullptr ? 0 : assets->value.roots().size();
}

radicle_status radicle_assets_get_archive(const radicle_assets* assets, size_t index, const radicle_ba2** out_ba2) {
  clear_error();
  if (assets == nullptr || out_ba2 == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "assets and out_ba2 are required");
  }
  *out_ba2 = nullptr;
  if (index >= assets->value.archives().size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "archive index is out of range");
  }
  *out_ba2 = to_ba2(&assets->value.archives()[index]);
  return RADICLE_STATUS_OK;
}

radicle_status radicle_assets_locate(const radicle_assets* assets, const char* path, radicle_asset_location* out_location) {
  clear_error();
  if (assets == nullptr || path == nullptr || out_location == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "assets, path and out_location are required");
  }
  *out_location = {};
  return guarded("unknown exception while locating asset", [&] {
    const auto location = assets->value.locate(path);
    if (!location) return fail(RADICLE_STATUS_NOT_FOUND, "no loose file or archive provides this path");
    *out_location = to_c_location(*location);
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_assets_list(const radicle_assets* assets,
                                   const char* prefix,
                                   const char* extension,
                                   int32_t include_shadowed,
                                   radicle_asset_list** out_list) {
  clear_error();
  if (assets == nullptr || out_list == nullptr) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "assets and out_list are required");
  *out_list = nullptr;
  return guarded("unknown exception while listing assets", [&] {
    auto list = std::make_unique<radicle_asset_list>();
    list->items = assets->value.list(prefix == nullptr ? "" : prefix, extension == nullptr ? "" : extension,
                                     include_shadowed != 0);
    *out_list = list.release();
    return RADICLE_STATUS_OK;
  });
}

size_t radicle_asset_list_count(const radicle_asset_list* list) {
  clear_error();
  return list == nullptr ? 0 : list->items.size();
}

radicle_status radicle_asset_list_get(const radicle_asset_list* list, size_t index, radicle_asset_list_entry* out_entry) {
  clear_error();
  if (list == nullptr || out_entry == nullptr) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "list and out_entry are required");
  if (index >= list->items.size()) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "list index is out of range");
  const auto& item = list->items[index];
  *out_entry = {item.path.c_str(), to_c_location(item.location), item.size, item.shadowed ? 1 : 0};
  return RADICLE_STATUS_OK;
}

void radicle_asset_list_destroy(radicle_asset_list* list) {
  clear_error();
  delete list;
}

radicle_status radicle_assets_read(const radicle_assets* assets,
                                   const char* path,
                                   uint8_t** out_bytes,
                                   size_t* out_size,
                                   radicle_asset_location* out_location) {
  clear_error();
  if (assets == nullptr || path == nullptr || out_bytes == nullptr || out_size == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "assets, path, out_bytes and out_size are required");
  }
  *out_bytes = nullptr;
  *out_size = 0;
  if (out_location != nullptr) *out_location = {};
  return guarded("unknown exception while reading asset", [&] {
    const auto location = assets->value.locate(path);
    if (!location) return fail(RADICLE_STATUS_NOT_FOUND, "no loose file or archive provides this path");
    const auto status = copy_bytes(assets->value.read(*location), out_bytes, out_size);
    if (status == RADICLE_STATUS_OK && out_location != nullptr) *out_location = to_c_location(*location);
    return status;
  });
}

radicle_status radicle_strings_load_with_assets(const radicle_env* env,
                                                const char* strings_dir,
                                                const char* language,
                                                const radicle_assets* assets,
                                                radicle_strings** out_strings) {
  return load_strings(env, strings_dir, language, assets == nullptr ? nullptr : &assets->value, out_strings);
}

}  // extern "C"
