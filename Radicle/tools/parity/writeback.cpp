#include "fo4/c_api.h"
#include "fo4/c_api_env.h"
#include "fo4/environment/environment.hpp"
#include "fo4/environment/placed.hpp"
#include "parity/common.hpp"

#include <bit>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using fo4::FormKey;
using fo4::env::Environment;
using fo4::io::utf8;

constexpr std::uint32_t k_player_faction = 0x01C21C;

struct Edit {
  FormKey ref;
  std::string signature;
  FormKey partner;
};

std::string hexf(float v) { return std::format("{:08X}", std::bit_cast<std::uint32_t>(v)); }

std::string key(const FormKey& k) {
  return k.owner().filename().empty() ? "-" : std::format("{:06X}:{}", k.local().value(), k.owner().filename());
}

std::string key(const std::optional<FormKey>& k) { return k ? key(*k) : "-"; }

std::string vec(const fo4::env::Vec3& v) { return std::format("{},{},{}", hexf(v[0]), hexf(v[1]), hexf(v[2])); }

std::vector<Edit> read_edits(const std::filesystem::path& file) {
  std::vector<Edit> edits;
  std::ifstream in(file);
  for (std::string line; std::getline(in, line);) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const auto a = line.find('\t');
    const auto b = line.find('\t', a + 1);
    edits.push_back({FormKey::parse(line.substr(0, a)), line.substr(a + 1, b - a - 1), FormKey::parse(line.substr(b + 1))});
  }
  return edits;
}

int pick(const Environment& env, const std::filesystem::path& out_file) {
  std::map<std::string, std::vector<FormKey>> buckets;
  for (const auto* cell : env.plugin_records(0)) {
    if (cell->record->header.signature.view() != "CELL") continue;
    for (const auto& child : env.cell_children(cell->record->form_key)) {
      const auto sig = child.ref->record->header.signature.view();
      if ((sig != "REFR" && sig != "ACHR") || child.ref->plugin != 0) continue;
      const auto p = fo4::env::decode_placed_reference(env, *child.ref);
      const auto& k = child.ref->record->form_key;
      if (sig == "ACHR") buckets["achr"].push_back(k);
      if (p.teleport) buckets["teleport"].push_back(k);
      if (p.primitive) buckets["primitive"].push_back(k);
      if (!p.activate_parents.empty()) buckets["activate"].push_back(k);
      if (p.ownership) buckets["owned"].push_back(k);
      if (!p.linked_references.empty()) buckets["linked"].push_back(k);
      if (p.enable_parent) buckets["enable"].push_back(k);
    }
  }
  std::vector<std::pair<FormKey, std::string>> chosen;
  std::set<std::string> seen;
  for (const auto& [kind, keys] : buckets)
    for (std::size_t i = 0; i < keys.size() && i < 2; ++i)
      if (seen.insert(key(keys[i])).second)
        chosen.emplace_back(keys[i], std::string(env.winner(keys[i])->record->header.signature.view()));
  std::ofstream out(out_file, std::ios::binary);
  for (std::size_t i = 0; i < chosen.size(); ++i)
    out << key(chosen[i].first) << '\t' << chosen[i].second << '\t' << key(chosen[(i + 1) % chosen.size()].first) << '\n';
  std::cout << "picked " << chosen.size() << " references\n";
  return 0;
}

void check(radicle_status status, const char* what) {
  if (status != RADICLE_STATUS_OK) throw std::runtime_error(std::format("{}: {}", what, radicle_last_error()));
}

int apply(const Environment& env, const std::vector<std::filesystem::path>& order, const std::filesystem::path& edits_file,
          const std::filesystem::path& out_file) {
  std::vector<std::string> path_strings;
  for (const auto& p : order) path_strings.push_back(utf8(p));
  std::vector<const char*> paths;
  for (const auto& p : path_strings) paths.push_back(p.c_str());
  radicle_env* cenv = nullptr;
  check(radicle_env_load(paths.data(), paths.size(), &cenv), "env load");
  radicle_plugin* plugin = nullptr;
  check(radicle_plugin_create("OpenCommonwealth.esp", &plugin), "plugin create");
  const std::string fallout4 = "Fallout4.esm";
  for (const auto& e : read_edits(edits_file)) {
    const std::string owner_name(e.ref.owner().filename());
    const radicle_form_key ref{owner_name.c_str(), e.ref.local().value()};
    const std::string partner_name(e.partner.owner().filename());
    const radicle_form_key partner{partner_name.c_str(), e.partner.local().value()};
    const radicle_record* record = nullptr;
    check(radicle_env_winner(cenv, ref, &record), "winner");
    radicle_placed_copy_status copy{};
    check(radicle_plugin_copy_placed_ref_override(plugin, cenv, record, 0, &copy), "copy override");
    const auto original = fo4::env::decode_placed_reference(env, *env.winner(e.ref));
    const auto p = original.position.value_or(fo4::env::Vec3{});
    const auto r = original.rotation.value_or(fo4::env::Vec3{});
    const float position[3]{p[0] + 1.0f, p[1] + 2.0f, p[2] + 3.0f};
    const float rotation[3]{r[0] + 0.25f, r[1] + 0.5f, r[2] + 0.75f};
    check(radicle_plugin_set_placed_transform(plugin, ref, position, rotation), "transform");
    check(radicle_plugin_set_placed_scale(plugin, ref, 1, 1.25f), "scale");
    check(radicle_plugin_set_placed_editor_id(plugin, ref, std::format("GE{:06X}", e.ref.local().value()).c_str()), "edid");
    check(radicle_plugin_set_placed_record_flags(plugin, ref, env.winner(e.ref)->record->header.flags.value() | 0x800u), "flags");
    if (e.signature != "REFR") continue;
    check(radicle_plugin_set_placed_comments(plugin, ref, "ge note"), "comments");
    check(radicle_plugin_set_placed_location_reference(plugin, cenv, ref, radicle_form_key{nullptr, 0}), "location");
    check(radicle_plugin_set_placed_ownership(plugin, cenv, ref, radicle_form_key{fallout4.c_str(), k_player_faction}, 1, 1, 2),
          "ownership");
    const std::uint8_t ep_flags = original.enable_parent ? original.enable_parent->flags.value_or(0) : 0;
    check(radicle_plugin_set_placed_enable_parent(plugin, cenv, ref, partner, static_cast<std::uint8_t>(ep_flags | 1u)),
          "enable parent");
    const radicle_linked_ref link{radicle_form_key{nullptr, 0}, partner};
    check(radicle_plugin_set_placed_linked_refs(plugin, cenv, ref, &link, 1), "linked refs");
    if (original.teleport) {
      const std::string door_name(original.teleport->door.owner().filename());
      const auto& tp = original.teleport->position;
      const float tpos[3]{tp[0] + 5.0f, tp[1] + 5.0f, tp[2] + 5.0f};
      const float trot[3]{original.teleport->rotation[0], original.teleport->rotation[1], original.teleport->rotation[2]};
      check(radicle_plugin_set_placed_teleport(plugin, cenv, ref, radicle_form_key{door_name.c_str(), original.teleport->door.local().value()},
                                               tpos, trot), "teleport");
    }
    if (original.primitive) {
      const auto& b = original.primitive->bounds;
      const float bounds[3]{b[0] + 1.0f, b[1] + 1.0f, b[2] + 1.0f};
      check(radicle_plugin_set_placed_primitive(plugin, ref, bounds, original.primitive->type), "primitive");
    }
    if (!original.activate_parents.empty()) check(radicle_plugin_set_placed_activate_parent_delay(plugin, ref, 0, 0.5f), "activate");
  }
  check(radicle_plugin_write_file(plugin, utf8(out_file).c_str()), "write");
  radicle_plugin_destroy(plugin);
  radicle_env_destroy(cenv);
  return 0;
}

int dump(const Environment& env, const std::filesystem::path& edits_file, const std::filesystem::path& out_file) {
  std::ofstream out(out_file, std::ios::binary);
  const auto last = env.plugins().size() - 1;
  for (const auto& e : read_edits(edits_file)) {
    const auto* w = env.winner(e.ref);
    if (w == nullptr || w->plugin != last) {
      out << key(e.ref) << "\tNOT OVERRIDDEN\n";
      continue;
    }
    const auto p = fo4::env::decode_placed_reference(env, *w);
    const auto meta = env.metadata(*w);
    std::string links;
    for (const auto& l : p.linked_references) links += key(l.keyword) + ">" + key(l.reference) + ";";
    std::string parents;
    for (const auto& a : p.activate_parents) parents += key(a.reference) + "@" + hexf(a.delay) + ";";
    out << std::format("{}\tflags={:08X}\tedid={}\tbase={}\tpos={}\trot={}\tscale={}\tcomments={}\tloc={}\t", key(e.ref),
                       w->record->header.flags.value(), meta.editor_id.value_or("-"), key(p.base),
                       vec(p.position.value_or(fo4::env::Vec3{})), vec(p.rotation.value_or(fo4::env::Vec3{})),
                       p.scale ? hexf(*p.scale) : "-", p.comments.value_or("-"), key(p.location_reference));
    out << std::format("owner={}/{}/{}\tep={}/{}\tlinks={}\t", p.ownership ? key(p.ownership->owner) : "-",
                       p.ownership && p.ownership->no_crime ? static_cast<int>(*p.ownership->no_crime) : -1,
                       p.faction_rank.value_or(-1), p.enable_parent ? key(p.enable_parent->reference) : "-",
                       p.enable_parent ? p.enable_parent->flags.value_or(0) : 0, links);
    out << std::format("tele={}\tprim={}\tactivate={}\n",
                       p.teleport ? key(p.teleport->door) + "@" + vec(p.teleport->position) + "@" + vec(p.teleport->rotation) : "-",
                       p.primitive ? vec(p.primitive->bounds) + "/" + std::to_string(p.primitive->type) : "-", parents);
  }
  return 0;
}

int run(const radicle_parity::Args& args) {
  if (args.size() != 4 && args.size() != 5) {
    std::cerr << "usage: radicle_parity_writeback pick <order> <edits.tsv> | apply <order> <edits.tsv> <out.esp> | "
                 "dump <order> <edits.tsv> <out.tsv>\n";
    return 2;
  }
  const auto mode = utf8(args[1]);
  const auto order = radicle_parity::read_order(args[2]);
  const auto env = Environment::load(order);
  if (mode == "pick") return pick(env, args[3]);
  if (mode == "apply" && args.size() == 5) return apply(env, order, args[3], args[4]);
  if (mode == "dump" && args.size() == 5) return dump(env, args[3], args[4]);
  return 2;
}

}  // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) { return radicle_parity::guarded_main(argc, argv, run); }
#else
int main(int argc, char** argv) { return radicle_parity::guarded_main(argc, argv, run); }
#endif
