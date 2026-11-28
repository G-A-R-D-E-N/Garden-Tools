#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>

#include <fo4/f4se/binding_model.hpp>
#include <fo4/f4se/cross_check.hpp>
#include <fo4/f4se/emitter.hpp>

namespace fo4::f4se::bindingJson {

struct PluginParse {
    std::optional<PluginBinding> plugin;
    std::string error;
};

struct ExistingParse {
    bool ok{true};
    std::map<std::string, std::string, CiLess> files;
    std::string error;
};

PluginParse parsePlugin(std::string_view json);

ExistingParse parseExisting(std::string_view json);

std::string serializeEmit(const F4SEEmitResult& result, const CrossCheckResult* roundTrip = nullptr);

}
