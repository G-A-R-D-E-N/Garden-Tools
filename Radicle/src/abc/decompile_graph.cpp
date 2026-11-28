#include "decompile_internal.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace fo4::abc::detail {

namespace {

using Adjacency = std::vector<std::vector<std::uint32_t>>;

void add_unique(std::vector<std::uint32_t>& list, std::uint32_t value) {
  if (std::find(list.begin(), list.end(), value) == list.end()) {
    list.push_back(value);
  }
}

AbcDecompileStatus refuse(AbcDecompileIssue& issue, AbcDecompileStatus status, std::uint32_t offset,
                          std::string message) {
  issue.status = status;
  issue.offset = offset;
  issue.message = std::move(message);
  return status;
}

std::uint32_t intersect(const std::vector<std::uint32_t>& idom, const std::vector<std::uint32_t>& order,
                        std::uint32_t a, std::uint32_t b) {
  while (a != b) {
    while (order[a] > order[b]) {
      a = idom[a];
    }
    while (order[b] > order[a]) {
      b = idom[b];
    }
  }
  return a;
}

/* Constraint: cooper-harvey-kennedy over reverse postorder; unreached nodes keep kNone. */
void compute_idoms(std::uint32_t nodes, std::uint32_t root, const Adjacency& succ,
                   const Adjacency& pred, std::vector<std::uint32_t>& idom,
                   std::vector<std::uint32_t>& rpo) {
  idom.assign(nodes, kNone);
  rpo.assign(nodes, kNone);
  std::vector<std::uint32_t> post;
  post.reserve(nodes);
  std::vector<bool> seen(nodes, false);
  std::vector<std::pair<std::uint32_t, std::size_t>> stack;
  stack.emplace_back(root, 0);
  seen[root] = true;
  while (!stack.empty()) {
    const std::uint32_t top = stack.back().first;
    if (stack.back().second < succ[top].size()) {
      const std::uint32_t next = succ[top][stack.back().second];
      ++stack.back().second;
      if (!seen[next]) {
        seen[next] = true;
        stack.emplace_back(next, 0);
      }
    } else {
      post.push_back(top);
      stack.pop_back();
    }
  }
  const std::size_t total = post.size();
  for (std::size_t i = 0; i < total; ++i) {
    rpo[post[i]] = static_cast<std::uint32_t>(total - 1 - i);
  }
  idom[root] = root;
  bool changed = true;
  while (changed) {
    changed = false;
    for (std::size_t i = total; i-- > 0;) {
      const std::uint32_t node = post[i];
      if (node == root) {
        continue;
      }
      std::uint32_t candidate = kNone;
      for (const std::uint32_t p : pred[node]) {
        if (rpo[p] == kNone || idom[p] == kNone) {
          continue;
        }
        candidate = candidate == kNone ? p : intersect(idom, rpo, p, candidate);
      }
      if (candidate != kNone && idom[node] != candidate) {
        idom[node] = candidate;
        changed = true;
      }
    }
  }
}

}  // namespace

bool FlowFacts::dominates(std::uint32_t a, std::uint32_t b) const {
  if (b >= idom.size() || idom[b] == kNone) {
    return false;
  }
  for (;;) {
    if (b == a) {
      return true;
    }
    if (idom[b] == b || idom[b] == kNone) {
      return false;
    }
    b = idom[b];
  }
}

bool FlowFacts::in_region(std::uint32_t header, std::uint32_t block) const {
  if (header >= region.size()) {
    return false;
  }
  return std::binary_search(region[header].begin(), region[header].end(), block);
}

bool is_conditional_branch(std::uint8_t opcode) {
  return opcode >= 0x0C && opcode <= 0x1A && opcode != 0x10;
}

bool is_noise_opcode(std::uint8_t opcode) {
  switch (opcode) {
    case 0x02:
    case 0x09:
    case 0xEF:
    case 0xF0:
    case 0xF1:
    case 0xF2:
    case 0xF3:
      return true;
    default:
      return false;
  }
}

bool is_get_local(std::uint8_t opcode) { return opcode == 0x62 || (opcode >= 0xD0 && opcode <= 0xD3); }

bool is_set_local(std::uint8_t opcode) { return opcode == 0x63 || (opcode >= 0xD4 && opcode <= 0xD7); }

/* Constraint: a condition-only block holds none of these, so merging it into a condition drops no statement. */
bool is_condition_hostile(std::uint8_t opcode) {
  if (is_set_local(opcode)) {
    return true;
  }
  switch (opcode) {
    case 0x03:
    case 0x05:
    case 0x06:
    case 0x07:
    case 0x08:
    case 0x1C:
    case 0x1D:
    case 0x29:
    case 0x30:
    case 0x32:
    case 0x3A:
    case 0x3B:
    case 0x3C:
    case 0x3D:
    case 0x3E:
    case 0x47:
    case 0x48:
    case 0x49:
    case 0x4E:
    case 0x4F:
    case 0x5A:
    case 0x61:
    case 0x68:
    case 0x69:
    case 0x6A:
    case 0x6B:
    case 0x6D:
    case 0x6F:
    case 0x92:
    case 0x94:
    case 0xC2:
    case 0xC3:
      return true;
    default:
      return false;
  }
}

AbcDecompileStatus compute_depths(const AbcFile& file, const AbcStructure& structure,
                                  const std::vector<HandlerEdge>& handlers, FlowFacts& facts,
                                  AbcDecompileIssue& issue) {
  const std::uint32_t count = static_cast<std::uint32_t>(structure.graph.blocks.size());
  facts.count = count;
  facts.depth.assign(count, kNone);
  facts.exit_depth.assign(count, kNone);
  if (count == 0) {
    return AbcDecompileStatus::Ok;
  }
  std::vector<std::uint32_t> pending;
  facts.depth[0] = 0;
  pending.push_back(0);
  while (!pending.empty()) {
    const std::uint32_t index = pending.back();
    pending.pop_back();
    const AbcBasicBlock& block = structure.graph.blocks[index];
    std::uint64_t depth = facts.depth[index];
    for (std::uint32_t i = 0; i < block.instruction_count; ++i) {
      const AbcInstruction& instruction = structure.instructions[block.first_instruction + i];
      std::uint64_t pops = 0;
      std::uint64_t pushes = 0;
      AbcFlowIssue flow_issue;
      if (stack_effect(file, instruction, pops, pushes, flow_issue) != AbcFlowStatus::Ok) {
        return refuse(issue, AbcDecompileStatus::StackShape,
                      static_cast<std::uint32_t>(instruction.offset), flow_issue.message);
      }
      if (pops > depth) {
        return refuse(issue, AbcDecompileStatus::StackShape,
                      static_cast<std::uint32_t>(instruction.offset),
                      std::string(mnemonic_of(instruction)) + " pops below the stack floor");
      }
      depth = depth - pops + pushes;
    }
    facts.exit_depth[index] = static_cast<std::uint32_t>(depth);
    const auto reach = [&](std::uint32_t target, std::uint64_t at) {
      if (target == kNone) {
        return true;
      }
      if (facts.depth[target] == kNone) {
        facts.depth[target] = static_cast<std::uint32_t>(at);
        pending.push_back(target);
        return true;
      }
      return static_cast<std::uint64_t>(facts.depth[target]) == at;
    };
    for (const AbcControlFlowEdge& edge : block.successors) {
      if (!reach(edge.block, depth)) {
        return refuse(issue, AbcDecompileStatus::StackShape, edge.target,
                      "stack depth differs between two paths into one block");
      }
    }
    for (const HandlerEdge& handler : handlers) {
      if (handler.from_block == index && !reach(handler.handler_block, 1)) {
        return refuse(issue, AbcDecompileStatus::StackShape, block.start_offset,
                      "an exception handler is also entered with another stack depth");
      }
    }
  }
  return AbcDecompileStatus::Ok;
}

AbcDecompileStatus analyse_flow(const AbcStructure& structure,
                                const std::vector<HandlerEdge>& handlers, FlowFacts& facts,
                                AbcDecompileIssue& issue) {
  const std::uint32_t count = facts.count;
  const std::vector<AbcBasicBlock>& blocks = structure.graph.blocks;
  facts.live.assign(count, false);
  for (std::uint32_t b = 0; b < count; ++b) {
    facts.live[b] = facts.depth[b] != kNone;
  }
  facts.succ.assign(count, {});
  facts.pred.assign(count, {});
  for (std::uint32_t b = 0; b < count; ++b) {
    if (!facts.live[b]) {
      continue;
    }
    for (const AbcControlFlowEdge& edge : blocks[b].successors) {
      if (edge.block != kNone && facts.live[edge.block]) {
        add_unique(facts.succ[b], edge.block);
      }
    }
  }
  for (std::uint32_t b = 0; b < count; ++b) {
    for (const std::uint32_t target : facts.succ[b]) {
      facts.pred[target].push_back(b);
    }
  }
  Adjacency forward_succ = facts.succ;
  Adjacency forward_pred = facts.pred;
  for (const HandlerEdge& handler : handlers) {
    if (facts.live[handler.from_block] && facts.live[handler.handler_block]) {
      add_unique(forward_succ[handler.from_block], handler.handler_block);
      add_unique(forward_pred[handler.handler_block], handler.from_block);
    }
  }
  compute_idoms(count, 0, forward_succ, forward_pred, facts.idom, facts.rpo);

  facts.is_header.assign(count, false);
  facts.latches.assign(count, {});
  std::vector<std::vector<bool>> natural(count);
  for (std::uint32_t u = 0; u < count; ++u) {
    if (!facts.live[u] || facts.rpo[u] == kNone) {
      continue;
    }
    for (const std::uint32_t v : forward_succ[u]) {
      if (facts.rpo[v] > facts.rpo[u]) {
        continue;
      }
      if (!facts.dominates(v, u)) {
        return refuse(issue, AbcDecompileStatus::IrreducibleFlow, blocks[v].start_offset,
                      "irreducible control flow: a loop is entered at more than one block (back edge " +
                          hex_offset(blocks[u].start_offset) + " to " +
                          hex_offset(blocks[v].start_offset) + ")");
      }
      facts.is_header[v] = true;
      add_unique(facts.latches[v], u);
      if (natural[v].empty()) {
        natural[v].assign(count, false);
      }
      natural[v][v] = true;
      std::vector<std::uint32_t> work;
      if (!natural[v][u]) {
        natural[v][u] = true;
        work.push_back(u);
      }
      while (!work.empty()) {
        const std::uint32_t x = work.back();
        work.pop_back();
        for (const std::uint32_t p : forward_pred[x]) {
          if (!natural[v][p]) {
            natural[v][p] = true;
            work.push_back(p);
          }
        }
      }
    }
  }

  facts.region.assign(count, {});
  for (std::uint32_t h = 0; h < count; ++h) {
    if (!facts.is_header[h]) {
      continue;
    }
    std::uint32_t low = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t high = 0;
    for (std::uint32_t m = 0; m < count; ++m) {
      if (natural[h][m]) {
        low = std::min(low, blocks[m].start_offset);
        high = std::max(high, blocks[m].end_offset);
      }
    }
    for (std::uint32_t b = 0; b < count; ++b) {
      if (!facts.live[b]) {
        continue;
      }
      const bool inside = blocks[b].start_offset >= low && blocks[b].start_offset < high &&
                          facts.dominates(h, b);
      if (natural[h][b] || inside) {
        facts.region[h].push_back(b);
      }
    }
  }

  const std::uint32_t exit_node = count;
  Adjacency reverse_succ(count + 1);
  Adjacency reverse_pred(count + 1);
  for (std::uint32_t b = 0; b < count; ++b) {
    if (!facts.live[b]) {
      continue;
    }
    for (const std::uint32_t p : facts.pred[b]) {
      reverse_succ[b].push_back(p);
    }
    if (facts.succ[b].empty()) {
      reverse_succ[exit_node].push_back(b);
    }
  }
  std::vector<bool> reaches_exit(count + 1, false);
  const auto flood = [&](std::uint32_t start) {
    std::vector<std::uint32_t> work{start};
    reaches_exit[start] = true;
    while (!work.empty()) {
      const std::uint32_t x = work.back();
      work.pop_back();
      for (const std::uint32_t y : reverse_succ[x]) {
        if (!reaches_exit[y]) {
          reaches_exit[y] = true;
          work.push_back(y);
        }
      }
    }
  };
  flood(exit_node);
  for (std::uint32_t b = count; b-- > 0;) {
    if (facts.live[b] && !reaches_exit[b]) {
      reverse_succ[exit_node].push_back(b);
      flood(b);
    }
  }
  for (std::uint32_t from = 0; from <= count; ++from) {
    for (const std::uint32_t to : reverse_succ[from]) {
      reverse_pred[to].push_back(from);
    }
  }
  std::vector<std::uint32_t> post_order;
  compute_idoms(count + 1, exit_node, reverse_succ, reverse_pred, facts.ipdom, post_order);
  return AbcDecompileStatus::Ok;
}

}  // namespace fo4::abc::detail
