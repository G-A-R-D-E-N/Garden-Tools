#include "fo4/loadorder/load_order_files.hpp"

#include "fo4/formid.hpp"
#include "fo4/headers.hpp"
#include "fo4/io/path_text.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin_key.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <exception>
#include <fstream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// Stage and checked rules: RE-Work/subsystems/radicle-re-garden-tools/light-plugin-index-389-28/README.md (RE repo commit 6916afb).
namespace fo4::loadorder {
namespace {

constexpr float k_max_checked_version = 1.0f;
constexpr std::size_t k_official_og_ng_count = 7;
constexpr std::uint32_t k_max_tes4_data_size = 16u * 1024u * 1024u;
constexpr std::array<std::string_view, 8> k_official_ae{
    "DLCRobot.esm",
    "DLCworkshop01.esm",
    "DLCCoast.esm",
    "DLCworkshop02.esm",
    "DLCworkshop03.esm",
    "DLCNukaWorld.esm",
    "DLCUltraHighResolution.esm",
    "DLCDummy10thAnniversary.esm"};

struct Candidate {
  std::string name;
  PluginKey key;
  std::filesystem::path path;
  std::filesystem::file_time_type time;
  bool master_flag = false;
  bool light = false;
  float version = 0.0f;
  std::vector<PluginKey> masters;
};

using FileIndex = std::map<PluginKey, std::size_t>;

std::span<const std::string_view> official_names(OfficialTable table) {
  const std::span<const std::string_view> names{k_official_ae};
  return names.first(table == OfficialTable::ae ? k_official_ae.size() : k_official_og_ng_count);
}

bool is_plugin_file(const std::filesystem::path& path) {
  auto extension = io::utf8(path.extension());
  for (auto& ch : extension) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return extension == ".esm" || extension == ".esl" || extension == ".esp";
}

std::map<PluginKey, Candidate> discover(std::span<const std::filesystem::path> plugin_files) {
  std::map<PluginKey, Candidate> found;
  for (const auto& path : plugin_files) {
    if (!is_plugin_file(path) || !std::filesystem::is_regular_file(path) || std::filesystem::file_size(path) == 0) {
      continue;
    }
    Candidate candidate;
    candidate.name = io::utf8(path.filename());
    candidate.key = PluginKey::from_filename(candidate.name);
    candidate.path = path;
    candidate.time = std::filesystem::last_write_time(path);
    const auto key = candidate.key;
    found.try_emplace(key, std::move(candidate));
  }
  return found;
}

PluginHeader read_tes4_header(const std::filesystem::path& path) {
  std::ifstream in{path, std::ios::binary};
  std::vector<std::byte> bytes(k_record_header_size);
  if (!in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
    throw std::runtime_error("unable to read the TES4 record header");
  }
  std::uint32_t data_size = 0;
  std::memcpy(&data_size, bytes.data() + 4, sizeof data_size);
  if (data_size > k_max_tes4_data_size) {
    throw std::runtime_error("TES4 record data exceeds 16 MiB");
  }
  if (data_size > std::filesystem::file_size(path) - k_record_header_size) {
    throw std::runtime_error("TES4 record overruns the file");
  }
  bytes.resize(k_record_header_size + data_size);
  if (!in.read(reinterpret_cast<char*>(bytes.data() + k_record_header_size),
               static_cast<std::streamsize>(data_size))) {
    throw std::runtime_error("unable to read the TES4 record data");
  }
  return read_plugin(bytes).header;
}

void read_header(Candidate& candidate) {
  const auto header = read_tes4_header(candidate.path);
  candidate.master_flag = is_master_plugin(header.tes4_flags, candidate.key);
  candidate.light = classify_plugin_kind(header.tes4_flags, candidate.key) == PluginKind::light;
  candidate.version = header.version;
  for (const auto& master : header.masters) {
    if (!master.key.empty()) candidate.masters.push_back(master.key);
  }
}

std::optional<std::size_t> find_file(const FileIndex& index, const PluginKey& key) {
  const auto found = index.find(key);
  if (found == index.end()) return std::nullopt;
  return found->second;
}

enum class Visit { unvisited, active, done };

bool find_master_cycle(std::size_t file, const std::vector<Candidate>& files, const FileIndex& index,
                       std::vector<Visit>& visits, std::vector<std::size_t>& path,
                       std::vector<std::size_t>& cycle) {
  visits[file] = Visit::active;
  path.push_back(file);
  for (const auto& master : files[file].masters) {
    const auto found = find_file(index, master);
    if (!found || !files[*found].master_flag) continue;
    if (visits[*found] == Visit::active) {
      cycle.assign(std::find(path.begin(), path.end(), *found), path.end());
      return true;
    }
    if (visits[*found] == Visit::unvisited && find_master_cycle(*found, files, index, visits, path, cycle)) {
      return true;
    }
  }
  path.pop_back();
  visits[file] = Visit::done;
  return false;
}

std::vector<std::size_t> master_cycle(const std::vector<Candidate>& files, const FileIndex& index) {
  std::vector<Visit> visits(files.size(), Visit::unvisited);
  for (std::size_t file = 0; file < files.size(); ++file) {
    if (!files[file].master_flag || visits[file] != Visit::unvisited) continue;
    std::vector<std::size_t> path;
    std::vector<std::size_t> cycle;
    if (find_master_cycle(file, files, index, visits, path, cycle)) return cycle;
  }
  return {};
}

bool hoist_masters(std::vector<std::size_t>& order, const std::vector<Candidate>& files,
                   const FileIndex& index, AssembledLoadOrder& result) {
  const std::size_t budget = order.size() * order.size() + order.size();
  std::size_t moves = 0;
  std::size_t pos = 0;
  while (pos < order.size()) {
    const auto dependent = order[pos];
    bool moved = false;
    if (files[dependent].master_flag) {
      for (const auto& master : files[dependent].masters) {
        const auto file = find_file(index, master);
        if (!file) continue;
        const auto at =
            static_cast<std::size_t>(std::find(order.begin(), order.end(), *file) - order.begin());
        if (at < pos) continue;
        if (moves == budget) {
          result.findings.push_back({"hoist_cycle", files[dependent].name,
                                     "master hoist exceeded " + std::to_string(budget) + " moves"});
          result.failed = true;
          return false;
        }
        ++moves;
        order.erase(order.begin() + at);
        order.insert(order.begin() + pos, *file);
        moved = true;
      }
    }
    if (!moved) ++pos;
  }
  return true;
}

bool settle_checked(std::size_t file, const std::vector<Candidate>& files, const FileIndex& index,
                    std::vector<bool>& checked, AssembledLoadOrder& result) {
  const auto& node = files[file];
  if (node.version > k_max_checked_version) {
    checked[file] = false;
    result.findings.push_back({"version_unchecked", node.name, "HEDR version is above 1.0; unchecked"});
    return true;
  }
  for (const auto& master : node.masters) {
    const auto found = find_file(index, master);
    if (!found) {
      result.findings.push_back(
          {"missing_master", node.name, "master " + std::string{master.filename()} + " is not in Data"});
      result.failed = true;
      return false;
    }
    if (files[*found].version > k_max_checked_version) {
      result.findings.push_back(
          {"master_too_new", node.name, "master " + files[*found].name + " has HEDR version above 1.0"});
      result.failed = true;
      return false;
    }
    if (checked[*found]) continue;
    checked[*found] = true;
    if (!settle_checked(*found, files, index, checked, result)) return false;
  }
  return true;
}

}  // namespace

AssembledLoadOrder assemble_load_order(const std::filesystem::path& data_dir,
                                       const PluginList& plugin_list,
                                       std::span<const std::string> ccc_names,
                                       OfficialTable table) {
  std::vector<std::filesystem::path> plugin_files;
  for (const auto& entry : std::filesystem::directory_iterator{data_dir}) plugin_files.push_back(entry.path());
  return assemble_load_order(std::span<const std::filesystem::path>{plugin_files}, plugin_list, ccc_names, table);
}

AssembledLoadOrder assemble_load_order(std::span<const std::filesystem::path> plugin_files,
                                       const PluginList& plugin_list,
                                       std::span<const std::string> ccc_names,
                                       OfficialTable table) {
  AssembledLoadOrder result;
  auto discovered = discover(plugin_files);
  std::vector<Candidate> files;
  for (auto& entry : discovered) {
    auto& candidate = entry.second;
    try {
      read_header(candidate);
    } catch (const std::exception& error) {
      result.findings.push_back({"unreadable_header", candidate.name, error.what()});
      continue;
    }
    files.push_back(std::move(candidate));
  }
  std::sort(files.begin(), files.end(), [](const Candidate& a, const Candidate& b) {
    if (a.master_flag != b.master_flag) return a.master_flag;
    if (a.time != b.time) return a.time < b.time;
    return a.key < b.key;
  });
  FileIndex index;
  for (std::size_t file = 0; file < files.size(); ++file) index.emplace(files[file].key, file);

  for (const auto& duplicate : plugin_list.duplicates) {
    result.findings.push_back({"duplicate_entry", duplicate.name,
                               "line " + std::to_string(duplicate.line) + " repeats an earlier entry"});
  }
  for (const auto line : plugin_list.invalid_lines) {
    result.findings.push_back({"invalid_name", "",
                               "line " + std::to_string(line) + " has a control character in its name"});
  }
  for (const auto& entry : plugin_list.entries) {
    if (!discovered.contains(PluginKey::from_filename(entry.name))) {
      result.findings.push_back({"missing_file", entry.name,
                                 "listed in the plugin list at line " + std::to_string(entry.line) +
                                     " but not in Data"});
    }
  }
  for (const auto& name : ccc_names) {
    if (!discovered.contains(PluginKey::from_filename(name))) {
      result.findings.push_back({"missing_file", name, "listed in Fallout4.ccc but not in Data"});
    }
  }

  std::vector<std::size_t> order;
  std::vector<bool> placed(files.size(), false);
  std::vector<bool> checked(files.size(), false);
  const auto place = [&](std::size_t file) {
    if (placed[file]) return;
    placed[file] = true;
    order.push_back(file);
  };
  for (const auto name : official_names(table)) {
    if (const auto file = find_file(index, PluginKey::from_filename(name))) {
      place(*file);
      checked[*file] = true;
    }
  }
  for (const auto& name : ccc_names) {
    if (const auto file = find_file(index, PluginKey::from_filename(name))) {
      place(*file);
      checked[*file] = true;
    }
  }

  struct Listed {
    std::size_t file;
    bool active;
  };
  std::vector<Listed> local;
  for (const auto& entry : plugin_list.entries) {
    const auto file = find_file(index, PluginKey::from_filename(entry.name));
    if (!file) continue;
    const auto& masters = files[*file].masters;
    const bool masters_present = std::all_of(masters.begin(), masters.end(), [&](const PluginKey& master) {
      return find_file(index, master).has_value();
    });
    if (masters_present) local.push_back({*file, entry.active});
  }
  for (const auto& listed : local) {
    if (files[listed.file].master_flag) place(listed.file);
  }
  for (std::size_t file = 0; file < files.size(); ++file) {
    if (files[file].master_flag) place(file);
  }
  for (const auto& listed : local) {
    if (!files[listed.file].master_flag) place(listed.file);
  }
  for (std::size_t file = 0; file < files.size(); ++file) place(file);
  for (const auto& listed : local) {
    if (listed.active) checked[listed.file] = true;
  }
  if (const auto fallout = find_file(index, PluginKey::from_filename("Fallout4.esm"))) {
    checked[*fallout] = true;
  }

  const auto cycle = master_cycle(files, index);
  if (!cycle.empty()) {
    std::string names;
    for (const auto file : cycle) {
      if (!names.empty()) names += ", ";
      names += files[file].name;
    }
    result.findings.push_back({"hoist_cycle", files[cycle.front()].name, "masters form a cycle: " + names});
    result.failed = true;
  } else if (hoist_masters(order, files, index, result)) {
    for (const auto file : order) {
      if (checked[file] && !settle_checked(file, files, index, checked, result)) break;
    }
  }

  for (const auto file : order) {
    result.entries.push_back({files[file].name, checked[file], files[file].master_flag, files[file].light});
  }
  return result;
}

}  // namespace fo4::loadorder
