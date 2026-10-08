#pragma once

#include "water_record_fixture.hpp"

#include <gtest/gtest.h>

namespace fo4::test::water {

inline fo4::ScalarMutationStatus scalar(fo4::Plugin& plugin, std::uint32_t id, const char* path, const char* value) {
  const auto field = fo4::schema::scalar_field_descriptor(path);
  EXPECT_TRUE(field.has_value()) << path;
  return fo4::set_record_scalar(plugin, key(id), *field, 0, value);
}

inline fo4::FormLinkFieldMutationStatus link(fo4::Plugin& plugin, std::uint32_t id, const char* path, const fo4::FormKey& target) {
  const auto field = fo4::schema::formlink_field_descriptor(path);
  EXPECT_TRUE(field.has_value()) << path;
  return fo4::set_record_formlink_field(plugin, key(id), *field, 0, target);
}

template <typename Edit>
bool refused(fo4::Plugin& plugin, Edit edit) {
  const auto before = fo4::write_plugin(plugin);
  bool threw = false;
  try {
    edit();
  } catch (const std::exception&) {
    threw = true;
  }
  return threw && fo4::write_plugin(plugin) == before;
}

inline bool authoring_clean(const fo4::Plugin& plugin, const fo4::FormKey& record) {
  for (const auto& issue : fo4::schema::preflight_authoring_record(plugin, record)) {
    if (issue.severity == fo4::schema::PreflightSeverity::error) { return false; }
  }
  return true;
}

}
