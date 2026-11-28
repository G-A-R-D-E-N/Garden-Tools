#pragma once

#include <fo4/papyrus/script_index.hpp>
#include <fo4/pex/pex.hpp>

namespace fo4::papyrus {

class CreationKitFidelity {
public:
    static void normalize(fo4::pex::PexFile& pex, ScriptIndex& index);
};

}  // namespace fo4::papyrus
