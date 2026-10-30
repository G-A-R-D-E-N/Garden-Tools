#include "fo4/archive/ba2_reader.hpp"
#include "fo4/environment/environment.hpp"
#include "fo4/environment/placed.hpp"
#include "fo4/environment/strings.hpp"
#include "fo4/environment/terrain.hpp"
#include "fo4/environment/world.hpp"
#include "fo4/schema/vmad.hpp"
#include "parity/common.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using fo4::FormKey;
using fo4::env::Environment;
using fo4::env::RecordRef;
using radicle_parity::dds_payload;
using radicle_parity::fnv;
using radicle_parity::utf8_path;
using fo4::io::utf8;

std::string lower(std::string_view text) {
  std::string out(text);
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return out;
}

std::string key(const FormKey& k) {
  if (k.owner().filename().empty()) return "-";
  return std::format("{:06X}:{}", k.local().value(), lower(k.owner().filename()));
}

std::string key(const std::optional<FormKey>& k) { return k ? key(*k) : "-"; }

std::string hexf(float v) { return std::format("{:08X}", std::bit_cast<std::uint32_t>(v)); }

std::string hexf(const std::optional<float>& v) { return v ? hexf(*v) : "-"; }

std::string text(const std::optional<std::string_view>& v) { return v && !v->empty() ? lower(*v) : "-"; }

void write(const std::filesystem::path& dir, const char* name, std::vector<std::string> lines) {
  std::filesystem::create_directories(dir);
  std::sort(lines.begin(), lines.end());
  std::ofstream out(dir / name, std::ios::binary);
  if (!out) throw std::runtime_error("cannot open measurement output: " + std::string{name});
  for (const auto& line : lines) out << line << '\n';
  out.close();
  if (!out) throw std::runtime_error("cannot save measurement output: " + std::string{name});
}

std::set<FormKey> keys_of(const Environment& env, std::string_view signature) {
  std::set<FormKey> out;
  for (std::size_t i = 0; i < env.plugins().size(); ++i)
    for (const auto* ref : env.plugin_records(i))
      if (signature.empty() || ref->record->header.signature.view() == signature) out.insert(ref->record->form_key);
  return out;
}

void dump_versions(const Environment& env, const std::filesystem::path& out) {
  std::vector<std::string> plugins;
  for (std::size_t i = 0; i < env.plugins().size(); ++i) {
    const auto& p = env.plugins()[i];
    plugins.push_back(std::format("{:04}\t{}\t{}", i, lower(p.key.filename()), p.kind == fo4::PluginKind::light ? 1 : 0));
  }
  write(out, "plugins.tsv", std::move(plugins));
  std::vector<std::string> versions;
  for (const auto& k : keys_of(env, "")) {
    std::string chain;
    for (const auto& v : env.versions(k)) chain += (chain.empty() ? "" : ",") + lower(env.plugins()[v.plugin].key.filename());
    versions.push_back(key(k) + "\t" + chain);
  }
  write(out, "versions.tsv", std::move(versions));
}

const std::set<std::string_view> k_placed{"REFR", "ACHR", "PGRE", "PMIS", "PARW", "PBAR", "PBEA", "PCON", "PFLA", "PHZD"};

void dump_cells(const Environment& env, const std::filesystem::path& out) {
  std::vector<std::string> cells;
  std::vector<std::string> refs;
  for (const auto& ck : keys_of(env, "CELL")) {
    const auto* winner = env.winner(ck);
    const auto cell = fo4::env::decode_cell(env, *winner);
    const bool interior = cell.flags && (*cell.flags & 1u) != 0;
    const auto world = interior ? std::nullopt : env.world_of(*winner);
    const auto* top = world ? env.worldspace_top_cell(*world) : nullptr;
    const bool persistent = top != nullptr && top->record->form_key == ck;
    std::vector<const RecordRef*> children;
    for (const auto& c : env.cell_children(ck))
      if (k_placed.contains(c.ref->record->header.signature.view())) children.push_back(c.ref);
    cells.push_back(std::format("{}\t{}\t{}\t{}\t{}\t{}", key(ck), interior ? 1 : 0,
                                cell.grid ? std::format("{},{}", cell.grid->x, cell.grid->y) : "-", key(world),
                                persistent ? 1 : 0, children.size()));
    for (const auto* ref : children) {
      fo4::env::PlacedReferenceData placed;
      try {
        placed = fo4::env::decode_placed_reference(env, *ref);
      } catch (const std::exception& error) {
        refs.push_back(std::format("{}\t{}\tERROR {}", key(ck), key(ref->record->form_key), error.what()));
        continue;
      }
      std::string model = "-";
      if (placed.base)
        if (const auto* base = env.winner(*placed.base)) model = text(fo4::env::primary_model_path(*base));
      const auto p = placed.position.value_or(fo4::env::Vec3{});
      const auto r = placed.rotation.value_or(fo4::env::Vec3{});
      const auto ep = placed.enable_parent ? std::format("{}/{}", key(placed.enable_parent->reference),
                                                         placed.enable_parent->flags.value_or(0) & 1u)
                                           : std::string{"-"};
      refs.push_back(std::format("{}\t{}\t{}\t{},{},{}\t{},{},{}\t{}\t{:08X}\t{}\t{}", key(ck), key(ref->record->form_key),
                                 key(placed.base), hexf(p[0]), hexf(p[1]), hexf(p[2]), hexf(r[0]), hexf(r[1]), hexf(r[2]),
                                 hexf(placed.scale.value_or(1.0f)), ref->record->header.flags.value(), ep, model));
    }
  }
  write(out, "cells.tsv", std::move(cells));
  write(out, "refs.tsv", std::move(refs));
}

void dump_world(const Environment& env, const std::filesystem::path& out) {
  std::vector<std::string> worlds;
  std::vector<std::string> grid;
  for (const auto& wk : keys_of(env, "WRLD")) {
    const auto* winner = env.winner(wk);
    const auto ws = fo4::env::decode_worldspace(env, *winner);
    const auto* top = env.worldspace_top_cell(wk);
    std::map<std::pair<std::int32_t, std::int32_t>, FormKey> by_grid;
    for (const auto* c : env.worldspace_cells(wk))
      if (const auto g = fo4::env::decode_cell(env, *c).grid) by_grid[{g->x, g->y}] = c->record->form_key;
    const auto pair = [](const std::optional<std::array<float, 2>>& v) {
      return v ? std::format("{},{}", hexf((*v)[0] / 4096.0f), hexf((*v)[1] / 4096.0f)) : std::string{"-"};
    };
    worlds.push_back(std::format("{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}", key(wk), text(env.metadata(*winner).editor_id),
                                 key(ws.parent), hexf(ws.default_land_height), hexf(ws.default_water_height),
                                 pair(ws.bounds_min), pair(ws.bounds_max), top ? key(top->record->form_key) : "-"));
    for (const auto& [g, ck] : by_grid)
      if (top == nullptr || !(ck == top->record->form_key))
        grid.push_back(std::format("{}\t{},{}\t{}", key(wk), g.first, g.second, key(ck)));
  }
  write(out, "worlds.tsv", std::move(worlds));
  write(out, "grid.tsv", std::move(grid));

  std::vector<std::string> land;
  for (const auto& ck : keys_of(env, "CELL")) {
    const auto* rec = env.cell_landscape(ck);
    if (rec == nullptr) continue;
    const auto l = fo4::env::decode_landscape(env, *rec);
    std::string layers;
    for (const auto& layer : l.layers) {
      const auto diffuse = layer.texture.owner().filename().empty()
                               ? std::nullopt
                               : fo4::env::landscape_texture_diffuse(env, layer.texture);
      layers += std::format("{}{}/{}/{}/{}/{}", layers.empty() ? "" : " ", layer.quadrant, layer.layer, key(layer.texture),
                            text(diffuse), fnv(layer.alpha, !layer.base));
    }
    land.push_back(std::format("{}\t{}\t{}\t{}\t{}\t{}\t{}", key(ck), key(rec->record->form_key), hexf(l.height_offset),
                               fnv(l.heights, !l.heights.empty()), fnv(l.normals, !l.normals.empty()),
                               fnv(l.colors, !l.colors.empty()), layers));
  }
  write(out, "land.tsv", std::move(land));
}

void dump_names(const Environment& env, const std::filesystem::path& out, const std::filesystem::path& data_dir) {
  std::vector<std::string> plugins;
  for (const auto& p : env.plugins()) plugins.emplace_back(p.key.filename());
  const auto archives = fo4::archive::AssetResolver::open(
      data_dir, fo4::archive::archive_load_order(data_dir, std::span<const std::string>{}, plugins));
  const auto tables = fo4::env::StringTables::load(env, data_dir / "Strings", "en", &archives);
  std::vector<std::string> names;
  std::vector<std::string> unresolved;
  std::vector<std::string> signatures;
  for (const auto& k : keys_of(env, "")) {
    const auto* winner = env.winner(k);
    signatures.push_back(std::format("{}\t{}", key(k), winner->record->header.signature.view()));
    const auto name = tables.resolve_name(env, *winner);
    if (!name || name->empty()) {
      const auto raw = fo4::env::decode_record_name(env.plugin(winner->plugin), *winner->record);
      if (raw.kind != fo4::env::NameKind::absent)
        unresolved.push_back(std::format("{}\t{}\t{}\t{:08X}", key(k), winner->record->header.signature.view(),
                                         static_cast<int>(raw.kind), raw.string_id));
      continue;
    }
    names.push_back(std::format("{}\t{}:{}", key(k), name->size(), fnv(std::as_bytes(std::span{*name}), true)));
  }
  write(out, "names.tsv", std::move(names));
  write(out, "names_unresolved.tsv", std::move(unresolved));
  write(out, "signatures.tsv", std::move(signatures));
}

std::string archive_entry_path(std::string_view path, bool lowercase = true) {
  std::string out = lowercase ? lower(path) : std::string(path);
  std::replace(out.begin(), out.end(), '\\', '/');
  return out;
}

// Entry paths come from the archive: they must stay inside the output directory.
bool is_relative_inside(std::string_view path) {
  if (path.empty() || path.front() == '/') return false;
  if (path.find(':') != std::string_view::npos) return false;
  for (std::size_t start = 0; start <= path.size();) {
    const auto end = path.find('/', start);
    const auto part = path.substr(start, end == std::string_view::npos ? path.size() - start : end - start);
    if (part == "..") return false;
    if (end == std::string_view::npos) break;
    start = end + 1;
  }
  return true;
}

int extract_ba2_pex(const std::filesystem::path& archive, const std::filesystem::path& out) {
  std::error_code error;
  if (!std::filesystem::is_regular_file(archive, error)) {
    throw std::runtime_error("no archive at " + utf8(archive));
  }
  const auto reader = fo4::archive::Ba2Reader::open(archive);
  std::filesystem::create_directories(out);
  std::size_t extracted = 0;
  for (std::size_t index = 0; index < reader.entries().size(); ++index) {
    // Must keep the entry's case: the gates pair <root>/Source/<name>.psc by exact name.
    const auto relative = archive_entry_path(reader.entries()[index].path, false);
    if (!relative.ends_with(".pex")) continue;
    if (!is_relative_inside(relative)) {
      throw std::runtime_error("refusing BA2 entry path outside the output directory: " + relative);
    }
    const auto bytes = reader.extract(index);
    const auto target = out / std::filesystem::path(relative);
    std::filesystem::create_directories(target.parent_path(), error);
    std::ofstream stream(target, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    stream.close();
    if (!stream) throw std::runtime_error("cannot write " + utf8(target));
    ++extracted;
  }
  if (extracted == 0) {
    throw std::runtime_error("no .pex entries in " + utf8(archive) + "; the corpus would be empty");
  }
  std::cout << "ba2pex archive=" << utf8(archive) << " out=" << utf8(out)
            << " extracted=" << extracted << '\n';
  return 0;
}

int dump_ba2(const std::filesystem::path& list, const std::filesystem::path& out, std::uint64_t max_bytes) {
  std::vector<std::string> index;
  std::vector<std::string> content;
  std::ifstream in(list);
  for (std::string line; std::getline(in, line);) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const auto tab = line.find('\t');
    const auto file = line.substr(0, tab);
    const auto name = archive_entry_path(file);
    try {
      const auto reader = fo4::archive::Ba2Reader::open(utf8_path(file));
      const bool hash = std::filesystem::file_size(utf8_path(file)) <= max_bytes;
      for (std::size_t i = 0; i < reader.entries().size(); ++i) {
        const auto& entry = reader.entries()[i];
        index.push_back(std::format("{}\t{}", name, archive_entry_path(entry.path)));
        if (!hash) continue;
        const auto bytes = reader.extract(i);
        content.push_back(std::format("{}\t{}\t{}:{}\t{}", name, archive_entry_path(entry.path), bytes.size(), fnv(bytes, true), dds_payload(bytes)));
      }
    } catch (const std::exception& error) {
      index.push_back(std::format("{}\tERROR {}", name, error.what()));
    }
  }
  write(out, "ba2_index.tsv", std::move(index));
  write(out, "ba2_content.tsv", std::move(content));
  return 0;
}

int dump_papyrus(const Environment& env, const std::filesystem::path& out) {
  std::vector<std::string> rows;
  std::vector<std::string> fragment_rows;
  std::vector<std::string> failures;
  const auto signature = fo4::Signature::from_string("VMAD");
  for (const auto& form : keys_of(env, "")) {
    const auto* ref = env.winner(form);
    if (env.metadata(*ref).deleted) continue;
    const auto& subrecords = ref->record->subrecords;
    const auto vmad = std::find_if(subrecords.begin(), subrecords.end(),
                                 [&](const auto& sub) { return sub.signature == signature; });
    if (vmad == subrecords.end()) continue;
    const auto id = key(form);
    if (std::count_if(subrecords.begin(), subrecords.end(),
                      [&](const auto& sub) { return sub.signature == signature; }) != 1) {
      failures.push_back(id + "\tduplicate-vmad");
      continue;
    }
    try {
      const auto scripts = fo4::read_vmad_scripts(vmad->data);
      const auto coverage = fo4::vmad_coverage(vmad->data, ref->record->header.signature);
      if (scripts.unsupported || !coverage.complete()) {
        failures.push_back(id + "\tincomplete-vmad");
        continue;
      }
      std::size_t fragments = 0;
      std::size_t aliases = 0;
      std::size_t alias_scripts = 0;
      if (ref->record->header.signature.view() == "QUST" && scripts.trailing_bytes != 0) {
        const auto extras = fo4::read_quest_vmad_extras(
            std::span<const std::byte>(vmad->data).subspan(vmad->data.size() - scripts.trailing_bytes), scripts.object_format);
        if (extras.unsupported || !extras.fully_consumed) {
          failures.push_back(id + "\tincomplete-quest-extras");
          continue;
        }
        fragments = extras.fragments.size();
        aliases = extras.aliases.size();
        for (const auto& alias : extras.aliases) alias_scripts += alias.scripts.size();
      }
      rows.push_back(std::format("{}\t{}\t{}\t{}\t{}\t{}", id, ref->record->header.signature.view(),
                                 scripts.scripts.size(), fragments, aliases, alias_scripts));
      fragment_rows.push_back(std::format("{}\t{}\t{}\t{}", id, ref->record->header.signature.view(),
                                          coverage.fragment_count, coverage.fragment_script_count));
    } catch (const std::exception& error) {
      failures.push_back(id + "\tmalformed-vmad");
      std::cerr << id << ": " << error.what() << '\n';
    }
  }
  const bool complete = failures.empty();
  write(out, "papyrus.tsv", std::move(rows));
  write(out, "papyrus_fragments.tsv", std::move(fragment_rows));
  write(out, "papyrus_failures.tsv", std::move(failures));
  return complete ? 0 : 1;
}

int usage() {
  std::cerr << "usage: radicle_parity_dump <versions|cells|world|papyrus> <order.tsv> <out-dir> | names <order.tsv> <out-dir> <data-dir> | "
               "ba2 <archives.txt> <out-dir> [max-mb] | ba2x <archive.ba2> <entry> <out-file> | "
               "ba2pex <archive.ba2> <out-dir>\n";
  return 2;
}

int run(const radicle_parity::Args& args) {
  if (args.size() != 4 && args.size() != 5) return usage();
  const auto mode = utf8(args[1]);
  if (mode == "ba2pex") {
    if (args.size() != 4) return usage();
    return extract_ba2_pex(args[2], args[3]);
  }
  if (mode == "ba2x") {
    if (args.size() != 5) return usage();
    const auto reader = fo4::archive::Ba2Reader::open(args[2]);
    const auto entry = reader.find(utf8(args[3]));
    if (!entry) throw std::runtime_error(utf8(args[3]) + " is not in " + utf8(args[2]));
    const auto bytes = reader.extract(*entry);
    std::ofstream(args[4], std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return 0;
  }
  if (mode == "ba2") return dump_ba2(args[2], args[3], args.size() == 5 ? std::stoull(utf8(args[4])) << 20 : 0);
  const auto env = Environment::load(radicle_parity::read_order(args[2]));
  if (mode == "papyrus") return dump_papyrus(env, args[3]);
  if (mode == "versions") dump_versions(env, args[3]);
  else if (mode == "cells") dump_cells(env, args[3]);
  else if (mode == "world") dump_world(env, args[3]);
  else if (mode == "names" && args.size() == 5) dump_names(env, args[3], args[4]);
  else return usage();
  return 0;
}

}  // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) { return radicle_parity::guarded_main(argc, argv, run); }
#else
int main(int argc, char** argv) { return radicle_parity::guarded_main(argc, argv, run); }
#endif
