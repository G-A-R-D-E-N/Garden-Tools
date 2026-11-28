#include <fo4/abc/decompile.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fo4::abc {

const char* decompile_status_name(AbcDecompileStatus status) noexcept {
  switch (status) {
    case AbcDecompileStatus::Ok:
      return "Ok";
    case AbcDecompileStatus::BodyNotFound:
      return "BodyNotFound";
    case AbcDecompileStatus::FlowRefused:
      return "FlowRefused";
    case AbcDecompileStatus::StackShape:
      return "StackShape";
    case AbcDecompileStatus::IrreducibleFlow:
      return "IrreducibleFlow";
    case AbcDecompileStatus::UnstructuredFlow:
      return "UnstructuredFlow";
    case AbcDecompileStatus::UnsupportedCondition:
      return "UnsupportedCondition";
    case AbcDecompileStatus::UnsupportedValueRegion:
      return "UnsupportedValueRegion";
    case AbcDecompileStatus::UnsupportedSwitch:
      return "UnsupportedSwitch";
    case AbcDecompileStatus::UnsupportedTry:
      return "UnsupportedTry";
    case AbcDecompileStatus::UnsupportedOpcode:
      return "UnsupportedOpcode";
    case AbcDecompileStatus::UnsupportedExpression:
      return "UnsupportedExpression";
    case AbcDecompileStatus::UnsupportedMember:
      return "UnsupportedMember";
    case AbcDecompileStatus::UnmappedName:
      return "UnmappedName";
  }
  return "Unknown";
}

namespace {

void dump_nodes(const std::vector<AbcNode>& nodes, std::string& out);

void dump_braced(const std::vector<AbcNode>& nodes, std::string& out) {
  out += '{';
  dump_nodes(nodes, out);
  out += '}';
}

std::string block_name(std::uint32_t block) { return "B" + std::to_string(block); }

std::string chain_name(const std::vector<AbcCondStep>& cond) {
  std::string text;
  for (const AbcCondStep& step : cond) {
    if (!text.empty()) {
      text += '+';
    }
    text += block_name(step.block);
  }
  return text;
}

void dump_node(const AbcNode& node, std::string& out) {
  switch (node.kind) {
    case AbcNodeKind::Block:
      out += block_name(node.block);
      if (node.skip != 0) {
        out += '/' + std::to_string(node.skip);
      }
      return;
    case AbcNodeKind::Break:
      out += "break";
      return;
    case AbcNodeKind::Continue:
      out += "continue";
      return;
    case AbcNodeKind::If:
      out += "if(" + chain_name(node.cond) + ")";
      dump_braced(node.body, out);
      if (!node.other.empty()) {
        out += "else";
        dump_braced(node.other, out);
      }
      return;
    case AbcNodeKind::Loop:
      if (node.loop_form == AbcLoopForm::While) {
        out += "while(" + chain_name(node.cond) + ")";
      } else if (node.loop_form == AbcLoopForm::DoWhile) {
        out += "dowhile(" + chain_name(node.cond) + ")";
      } else {
        out += "loop";
      }
      dump_braced(node.body, out);
      return;
    case AbcNodeKind::Switch:
      out += "switch(" + block_name(node.block) + "){";
      for (std::size_t i = 0; i < node.cases.size(); ++i) {
        const AbcSwitchCase& one = node.cases[i];
        if (i != 0) {
          out += ' ';
        }
        if (!one.values.empty()) {
          out += "case[";
          for (std::size_t v = 0; v < one.values.size(); ++v) {
            if (v != 0) {
              out += ',';
            }
            out += std::to_string(one.values[v]);
          }
          out += ']';
          if (one.is_default) {
            out += '+';
          }
        }
        if (one.is_default) {
          out += "default";
        }
        dump_braced(one.body, out);
      }
      out += '}';
      return;
    case AbcNodeKind::Try:
      out += "try";
      dump_braced(node.body, out);
      for (const AbcCatch& one : node.catches) {
        out += "catch" + std::to_string(one.exception);
        dump_braced(one.body, out);
      }
      return;
    case AbcNodeKind::ShortCircuit:
      out += (node.is_and ? "and(" : "or(") + block_name(node.block) + ")";
      dump_braced(node.body, out);
      return;
    case AbcNodeKind::Ternary:
      out += "tern(" + chain_name(node.cond) + ")";
      dump_braced(node.body, out);
      out += "else";
      dump_braced(node.other, out);
      return;
  }
}

void dump_nodes(const std::vector<AbcNode>& nodes, std::string& out) {
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    if (i != 0) {
      out += ' ';
    }
    dump_node(nodes[i], out);
  }
}

}  // namespace

std::string dump_structure(const AbcStructure& structure) {
  std::string out;
  dump_nodes(structure.root, out);
  return out;
}

}  // namespace fo4::abc
