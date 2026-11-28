#pragma once

#include <string>

#include <fo4/graph/graph_ir.hpp>

namespace fo4::graph::irJson {

std::string serialize(const ir::IrScript& script, bool indented = true);

}
