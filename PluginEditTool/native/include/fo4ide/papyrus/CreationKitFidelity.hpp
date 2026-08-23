#pragma once

#include <fo4ide/papyrus/Pex.hpp>
#include <fo4ide/papyrus/ScriptIndex.hpp>

namespace fo4ide::papyrus {

// Port of PapyrusCreationKitFidelity: post-generation touches that make the
// emitted PEX match the Creation Kit where the compiler is free to choose.
// GotoState string operands are canonicalized to the declared state name
// (when the receiver is this script), and negated literals fold into the
// assign form.
class CreationKitFidelity {
public:
    static void normalize(PexFile& pex, ScriptIndex& index);
};

}  // namespace fo4ide::papyrus
