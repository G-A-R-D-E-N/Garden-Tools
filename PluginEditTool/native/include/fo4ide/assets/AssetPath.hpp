#pragma once

#include <initializer_list>
#include <string>
#include <string_view>

namespace fo4ide::assets {

bool isAbsoluteOnAnyHost(std::string_view path);

std::string stripDevAbsolutePrefix(std::string_view path,
                                   std::initializer_list<std::string_view> markers);

}  // namespace fo4ide::assets