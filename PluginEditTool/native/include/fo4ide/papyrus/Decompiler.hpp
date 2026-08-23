#pragma once

#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>

#include <fo4ide/papyrus/Pex.hpp>

namespace fo4ide::papyrus {

// Port of PapyrusDecompiler: turns a PexFile back into Papyrus source
// (ToSource) or a textual assembly dump (Disassemble), mirroring the C#
// oracle's temp inlining, jump folding, and local hoisting.
class Decompiler {
public:
    static std::string toSource(const PexFile& pex);
    static std::string disassemble(const PexFile& pex);

private:
    static void emitFunction(std::ostringstream& sb, const PexObject& obj,
                             const PexFunction& fn,
                             const std::unordered_map<int, std::string>& flagNames,
                             std::string_view indent,
                             std::optional<std::string> nameOverride = std::nullopt);
    static std::string decompileBody(const PexObject& obj, const PexFunction& fn,
                                     std::string indent, bool inlineTemps);
    static void dumpFn(std::ostringstream& sb, const PexFunction& fn, std::string_view indent);
    static std::string asmBody(const PexFunction& fn, std::string_view indent);
};

}  // namespace fo4ide::papyrus
