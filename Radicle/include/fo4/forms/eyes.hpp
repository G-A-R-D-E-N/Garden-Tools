#pragma once

#include "fo4/flags.hpp"
#include "fo4/formid.hpp"
#include "fo4/plugin/container.hpp"

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::forms {

class EyesError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

struct Eyes {
  FormKey key{};
  RecordFlags record_flags{};
  std::optional<std::string> editor_id;
  std::optional<std::string> full_text;
  std::vector<std::byte> full_raw;
  std::string icon;
  std::uint8_t data_flags{0};
};

Eyes decode_eyes(const Plugin& plugin, const FormKey& key);

FormKey create_eyes(Plugin& plugin,
                    std::optional<std::string_view> editor_id,
                    std::string_view full_text,
                    std::string_view icon,
                    std::uint8_t data_flags,
                    RecordFlags record_flags = {});

bool set_eyes_text_fields(Plugin& plugin,
                          const FormKey& key,
                          std::optional<std::string_view> editor_id,
                          std::optional<std::string_view> full_text,
                          std::optional<std::string_view> icon);

bool set_eyes_data_flags(Plugin& plugin,
                         const FormKey& key,
                         std::uint8_t flags);

}  // namespace fo4::forms
