#pragma once

#include <string>

namespace fo4::papyrus {

class DecompileService {
public:
    static std::string decompile(const std::string& source, const std::string& output,
                                 bool assembly, bool write);
};

}
