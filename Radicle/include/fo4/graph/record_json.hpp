#pragma once

#include "fo4/plugin/container.hpp"

#include <string>

namespace fo4 {

struct RecordJsonOptions {
  bool include_raw{true};
};

std::string record_to_json(const Plugin& plugin, const FormKey& key, const RecordJsonOptions& options = {});

}
