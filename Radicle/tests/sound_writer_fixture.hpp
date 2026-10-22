#pragma once

#include "sound_record_fixture.hpp"

#include <gtest/gtest.h>

namespace fo4::test::sound {

inline fo4::ScalarMutationStatus scalar(fo4::Plugin& plugin, std::uint32_t id, const char* path, std::size_t index, const char* value) {
  const auto field = fo4::schema::scalar_field_descriptor(path);
  EXPECT_TRUE(field.has_value()) << path;
  return fo4::set_record_scalar(plugin, key(id), field.value(), index, value);
}

inline fo4::ScalarMutationStatus text(fo4::Plugin& plugin, std::uint32_t id, const char* path, std::size_t index, const char* value) {
  return fo4::set_record_string(plugin, key(id), path, index, value);
}

inline fo4::FormLinkFieldMutationStatus link(fo4::Plugin& plugin, std::uint32_t id, const char* path, std::size_t index, const fo4::FormKey& target) {
  const auto field = fo4::schema::formlink_field_descriptor(path);
  EXPECT_TRUE(field.has_value()) << path;
  return fo4::set_record_formlink_field(plugin, key(id), field.value(), index, target);
}

inline fo4::schema::FormLinkListMutationStatus list_link(fo4::Plugin& plugin, std::uint32_t id, const char* path, std::size_t index, const fo4::FormKey& target) {
  const auto list = fo4::schema::formlink_list_descriptor(sig(path), path);
  EXPECT_TRUE(list.has_value()) << path;
  return fo4::schema::set_record_formlink_list_element(plugin, key(id), list.value(), index, target);
}

inline std::size_t count(const fo4::Plugin& plugin, std::uint32_t id, const char* path) {
  return fo4::record_field_count(plugin, key(id), path).value_or(9999);
}

template <typename Edit>
bool refused(fo4::Plugin& plugin, Edit&& edit) {
  const auto before = fo4::write_plugin(plugin);
  bool threw = false;
  try {
    edit();
  } catch (const std::exception&) {
    threw = true;
  }
  return threw && fo4::write_plugin(plugin) == before;
}

inline bool authoring_clean(const fo4::Plugin& plugin, std::uint32_t id) {
  for (const auto& issue : fo4::schema::preflight_authoring_record(plugin, key(id))) {
    if (issue.severity == fo4::schema::PreflightSeverity::error) { return false; }
  }
  return true;
}

inline std::filesystem::path saved_root() {
  return std::filesystem::temp_directory_path() /
         ("radicle_sound_writer_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
}

inline fo4::env::Environment load_saved(const fo4::Plugin& plugin, const std::filesystem::path& root) {
  std::filesystem::create_directories(root);
  fo4::write_plugin_file(plugin, root / k_plugin);
  const std::filesystem::path paths[] = {root / k_plugin};
  return fo4::env::Environment::load(paths);
}

struct TempRoot {
  std::filesystem::path path{saved_root()};
  TempRoot() = default;
  TempRoot(const TempRoot&) = delete;
  TempRoot& operator=(const TempRoot&) = delete;
  ~TempRoot() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
};

class SavedEnvironment {
public:
  explicit SavedEnvironment(const fo4::Plugin& plugin) : env_(load_saved(plugin, root_.path)) {}
  const fo4::env::Environment& env() const { return env_; }
  const fo4::env::RecordRef& record(std::uint32_t id) const { return *env_.winner(key(id)); }
  std::filesystem::path path() const { return root_.path / k_plugin; }

private:
  TempRoot root_;
  fo4::env::Environment env_;
};

}
