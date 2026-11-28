#pragma once

#include "fo4/c_api_papyrus.h"
#include "fo4/papyrus/status.hpp"
#include "fo4/papyrus/value.hpp"

struct radicle_papyrus_value {
  fo4::papyrus::Value value;
};

struct radicle_papyrus_object {
  fo4::papyrus::ObjectRef ref;
};

namespace radicle_c_papyrus_detail {

radicle_status make_value(fo4::papyrus::Value value, radicle_papyrus_value** out);
radicle_papyrus_status to_c_status(fo4::papyrus::Status status);
fo4::papyrus::Status from_c_status(radicle_papyrus_status status);

}  // namespace radicle_c_papyrus_detail
