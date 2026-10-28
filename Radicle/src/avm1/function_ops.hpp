#pragma once

#include "state.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace fo4::avm1::function_ops {

Avm1RunStatus get_variable(const Avm1Block& block, Avm1State& state, std::string& message);
Avm1RunStatus set_variable(const Avm1Block& block, Avm1State& state, std::string& message);
Avm1RunStatus define_local(const Avm1Block& block, Avm1State& state, std::string& message);
Avm1RunStatus define_local2(const Avm1Block& block, Avm1State& state, std::string& message);
Avm1RunStatus define_function(Avm1Run& run, const Avm1Block& block, const Avm1Action& action,
                              std::size_t& next);
Avm1RunStatus call_function(Avm1Run& run, const Avm1Block& block, std::string& message);
Avm1RunStatus call_method(Avm1Run& run, std::string& message);
Avm1RunStatus new_method(Avm1Run& run, std::string& message);
Avm1RunStatus store_register(const Avm1Block& block, Avm1State& state, std::uint8_t index,
                             std::string& message);
Avm1RunStatus push_register(const Avm1Block& block, Avm1State& state, std::uint16_t index,
                            std::string& message);
Avm1RunStatus return_value(const Avm1Block& block, Avm1State& state, std::size_t& next,
                           std::string& message);

}  // namespace fo4::avm1::function_ops
