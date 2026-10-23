#include "fo4/loadorder/mo2_profile.hpp"

#include "fo4/io/path_text.hpp"
#include "fo4/text/encoding.hpp"

#include <cstddef>
#include <fstream>
#include <map>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace fo4::loadorder {
namespace {

constexpr std::string_view k_base_token = "%BASE_DIR%";

bool starts_with_ci(std::string_view source, std::string_view prefix) {
  return source.size() >= prefix.size() && text::equals_folded(source.substr(0, prefix.size()), prefix);
}

bool ends_with_ci(std::string_view source, std::string_view suffix) {
  return source.size() >= suffix.size() && text::equals_folded(source.substr(source.size() - suffix.size()), suffix);
}

std::string_view trim(std::string_view text) {
  constexpr std::string_view spaces = " \t\r\n\v\f";
  const auto first = text.find_first_not_of(spaces);
  if (first == std::string_view::npos) return {};
  const auto last = text.find_last_not_of(spaces);
  return text.substr(first, last - first + 1);
}

void replace_all(std::string& text, std::string_view from, std::string_view to) {
  for (std::size_t at = 0; (at = text.find(from, at)) != std::string::npos; at += to.size()) {
    text.replace(at, from.size(), to);
  }
}

std::vector<std::string> read_lines(const std::filesystem::path& path) {
  std::vector<std::string> lines;
  std::ifstream in{path, std::ios::binary};
  for (std::string line; std::getline(in, line);) lines.push_back(std::move(line));
  return lines;
}

std::string unwrap(std::string_view raw) {
  constexpr std::string_view prefix = "@ByteArray(";
  auto value = trim(raw);
  if (value.size() > prefix.size() && starts_with_ci(value, prefix) && value.back() == ')') {
    value = value.substr(prefix.size(), value.size() - prefix.size() - 1);
  }
  std::string result{value};
  replace_all(result, "\\\\", "\\");
  return result;
}

std::map<std::string, std::string> read_ini(const std::filesystem::path& path) {
  std::map<std::string, std::string> values;
  for (const auto& raw : read_lines(path)) {
    const auto line = trim(raw);
    if (line.empty() || line.front() == ';' || line.front() == '#') continue;
    const auto split = line.find('=');
    if (split == std::string::npos) continue;
    values.insert_or_assign(text::fold(trim(line.substr(0, split))), unwrap(line.substr(split + 1)));
  }
  return values;
}

std::filesystem::path to_path(std::string value, const Mo2PathTranslator& translate) {
  return translate ? translate(std::move(value)) : io::from_utf8(value);
}

std::filesystem::path expand_dir(std::string value, const std::filesystem::path& base,
                                 const Mo2PathTranslator& translate) {
  replace_all(value, k_base_token, io::utf8(base));
  auto result = to_path(std::move(value), translate);
  if (result.is_relative()) result = base / result;
  std::error_code ec;
  auto absolute = std::filesystem::absolute(result, ec);
  return (ec ? result : absolute).lexically_normal();
}

bool is_plugin_file(const std::filesystem::path& file) {
  const auto ext = text::fold(io::utf8(file.extension()));
  return ext == ".esp" || ext == ".esm" || ext == ".esl";
}

}  // namespace

Mo2Instance read_mo2_instance(const std::filesystem::path& instance_dir, const Mo2PathTranslator& translate) {
  const auto values = read_ini(instance_dir / "ModOrganizer.ini");
  const auto value_or = [&values](const char* key, const std::string& fallback) {
    const auto found = values.find(key);
    return found == values.end() || found->second.empty() ? fallback : found->second;
  };

  Mo2Instance instance;
  instance.instance_dir = instance_dir;
  instance.selected_profile = value_or("selected_profile", "Default");
  instance.base_dir = expand_dir(value_or("base_directory", io::utf8(instance_dir)), instance_dir, translate);
  instance.mods_dir = expand_dir(value_or("mod_directory", "%BASE_DIR%/mods"), instance.base_dir, translate);
  instance.overwrite_dir =
      expand_dir(value_or("overwrite_directory", "%BASE_DIR%/overwrite"), instance.base_dir, translate);
  instance.profiles_dir =
      expand_dir(value_or("profiles_directory", "%BASE_DIR%/profiles"), instance.base_dir, translate);

  const auto game = value_or("gamepath", "");
  if (!game.empty()) instance.game_path = to_path(game, translate);
  if (!instance.game_path.empty()) instance.data_dir = instance.game_path / "Data";
  std::error_code ec;
  if (!std::filesystem::is_directory(instance.data_dir, ec)) {
    const auto portable = instance_dir / "Stock Folder" / "Data";
    if (std::filesystem::is_directory(portable, ec)) instance.data_dir = portable;
  }
  return instance;
}

Mo2Profile read_mo2_profile(const Mo2Instance& instance, std::string_view profile) {
  Mo2Profile result;
  result.name = std::string{profile};
  const auto dir = instance.profiles_dir / io::from_utf8(profile);
  for (const auto& raw : read_lines(dir / "modlist.txt")) {
    const auto line = trim(raw);
    if (line.empty() || (line.front() != '+' && line.front() != '-' && line.front() != '*')) continue;
    const auto name = trim(line.substr(1));
    if (name.empty()) continue;
    result.mods_high_to_low.push_back(Mo2Mod{std::string{name}, line.front() == '+', ends_with_ci(name, "_separator")});
  }
  result.plugins = read_plugin_list(dir / "plugins.txt");
  return result;
}

std::vector<std::filesystem::path> mo2_asset_roots_low_to_high(const Mo2Instance& instance, const Mo2Profile& profile) {
  std::vector<std::filesystem::path> roots;
  if (!instance.data_dir.empty()) roots.push_back(instance.data_dir);
  for (auto mod = profile.mods_high_to_low.rbegin(); mod != profile.mods_high_to_low.rend(); ++mod) {
    if (mod->enabled && !mod->is_separator) roots.push_back(instance.mods_dir / io::from_utf8(mod->name));
  }
  roots.push_back(instance.overwrite_dir);
  return roots;
}

std::map<std::string, std::filesystem::path> mo2_resolve_plugin_paths(const Mo2Instance& instance,
                                                                      const Mo2Profile& profile) {
  std::map<std::string, std::filesystem::path> resolved;
  for (const auto& root : mo2_asset_roots_low_to_high(instance, profile)) {
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec)) continue;
    for (std::filesystem::directory_iterator it{root, ec}, end; !ec && it != end; it.increment(ec)) {
      if (!it->is_regular_file(ec) || !is_plugin_file(it->path())) continue;
      resolved.insert_or_assign(text::fold(io::utf8(it->path().filename())), it->path());
    }
  }
  return resolved;
}

}  // namespace fo4::loadorder
