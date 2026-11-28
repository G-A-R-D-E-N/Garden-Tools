#pragma once

#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>

#include <fo4/pex/pex.hpp>

namespace fo4::papyrus {

class Decompiler {
public:
    static std::string toSource(const fo4::pex::PexFile& pex);
    static std::string disassemble(const fo4::pex::PexFile& pex);

private:
    static void emitFunction(std::ostringstream& sb, const fo4::pex::PexObject& obj,
                             const fo4::pex::PexFunction& fn,
                             const std::unordered_map<int, std::string>& flagNames,
                             std::string_view indent,
                             std::optional<std::string> nameOverride = std::nullopt);
    static std::string decompileBody(const fo4::pex::PexObject& obj, const fo4::pex::PexFunction& fn,
                                     std::string indent, bool inlineTemps);
    static void dumpFn(std::ostringstream& sb, const fo4::pex::PexFunction& fn, std::string_view indent);
    static std::string asmBody(const fo4::pex::PexFunction& fn, std::string_view indent);
};

}  // namespace fo4::papyrus
