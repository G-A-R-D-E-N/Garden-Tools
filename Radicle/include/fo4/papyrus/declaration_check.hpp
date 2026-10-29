#pragma once

#include <vector>

#include <fo4/papyrus/ast.hpp>
#include <fo4/papyrus/diagnostic.hpp>

namespace fo4::papyrus {

std::vector<Diagnostic> declarationCheck(const Script& script);

}
