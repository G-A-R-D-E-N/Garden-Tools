#include <fo4/pex_analysis/disasm.hpp>

#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace fo4::pex_analysis {

namespace {

std::string indent_text(int depth) {
  return std::string(static_cast<std::size_t>(depth) * 2, ' ');
}

std::string instruction_line(const fo4::pex::PexInstruction& instruction, std::size_t index) {
  char buffer[24];
  std::snprintf(buffer, sizeof(buffer), "%4zu", index);
  std::string text = buffer;
  text += ": ";
  text += pex_op_name(instruction.opCode);
  for (const auto& argument : instruction.args) {
    text += " ";
    text += pex_value_text(argument);
  }
  return text;
}

std::string label_line(std::size_t index) { return "L" + std::to_string(index) + ":"; }

class StructuredRenderer {
 public:
  StructuredRenderer(const PexFunctionCfg& cfg, const fo4::pex::PexFunction& function)
      : cfg_(cfg), function_(function), emitted_(cfg.blocks.size(), false) {}

  bool render(std::string& out) {
    if (!cfg_.unreachableBlocks.empty()) return false;
    const auto ended = emit_range(0, cfg_.exitBlock, 0, out);
    if (bailed_ || ended != cfg_.exitBlock) return false;
    for (std::size_t b = 0; b < cfg_.blocks.size(); ++b) {
      if (!emitted_[b] && b != cfg_.exitBlock) return false;
    }
    return true;
  }

 private:
  static constexpr int kMaxNestingDepth = 64;

  std::size_t terminator_of(std::size_t block) const {
    const auto& b = cfg_.blocks[block];
    return b.first + b.count - 1;
  }

  std::size_t jump_target_of(std::size_t instructionIndex) const {
    std::int64_t offset = 0;
    if (!pex_jump_offset(function_.instructions[instructionIndex], &offset)) return kNoIndex;
    const auto target = static_cast<std::int64_t>(instructionIndex) + offset;
    return cfg_block_of(cfg_, static_cast<std::size_t>(target));
  }

  std::string condition_text(std::size_t instructionIndex) const {
    const auto& instruction = function_.instructions[instructionIndex];
    if (instruction.args.empty()) return "?";
    return pex_value_text(instruction.args.front());
  }

  std::size_t loop_exit(std::size_t block, const PexLoop& loop) const {
    const auto& header = cfg_.blocks[block];
    if (header.count == 0) return kNoIndex;
    const auto terminator = terminator_of(block);
    const auto& instruction = function_.instructions[terminator];
    if (instruction.opCode != static_cast<std::uint8_t>(PexOp::JmpF)) return kNoIndex;
    const auto target = jump_target_of(terminator);
    if (target == kNoIndex || target <= block + 1) return kNoIndex;
    if (loop.latchBlock != target - 1) return kNoIndex;
    const auto& latch = cfg_.blocks[loop.latchBlock];
    if (latch.successors.size() != 1 || latch.successors.front() != block) return kNoIndex;
    return target;
  }

  std::size_t conditional_join(std::size_t block, int depth) const {
    if (depth > kMaxNestingDepth) return kNoIndex;
    const auto terminator = terminator_of(block);
    const auto& instruction = function_.instructions[terminator];
    if (instruction.opCode != static_cast<std::uint8_t>(PexOp::JmpF)) return kNoIndex;
    const auto elseStart = jump_target_of(terminator);
    if (elseStart == kNoIndex || elseStart <= block + 1) return kNoIndex;
    std::size_t thenExit = kNoIndex;
    if (!region_exit(block + 1, elseStart, depth, thenExit)) return kNoIndex;
    if (thenExit == kNoIndex || thenExit == elseStart) return elseStart;
    std::size_t elseExit = kNoIndex;
    if (!region_exit(elseStart, thenExit, depth, elseExit)) return kNoIndex;
    return elseExit == thenExit ? thenExit : kNoIndex;
  }

  bool region_exit(std::size_t start, std::size_t limit, int depth,
                   std::size_t& exitBlock) const {
    if (start >= limit || limit > cfg_.blocks.size()) return false;
    auto cur = start;
    while (true) {
      const auto& block = cfg_.blocks[cur];
      if (block.count == 0) return false;
      const auto terminator = terminator_of(cur);
      const auto& instruction = function_.instructions[terminator];
      if (instruction.opCode == static_cast<std::uint8_t>(PexOp::Return)) {
        if (cur + 1 != limit) return false;
        exitBlock = kNoIndex;
        return true;
      }
      std::size_t next = kNoIndex;
      const auto* loop = loop_at(cur);
      if (loop != nullptr) {
        next = loop_exit(cur, *loop);
      } else if (pex_is_conditional_jump(instruction.opCode)) {
        next = conditional_join(cur, depth + 1);
      } else if (pex_is_jump(instruction.opCode)) {
        const auto target = jump_target_of(terminator);
        if (target == kNoIndex) return false;
        if (target != cur + 1) {
          if (cur + 1 != limit) return false;
          exitBlock = target;
          return true;
        }
        next = cur + 1;
      } else {
        if (block.successors.size() != 1 || block.successors.front() != cur + 1) return false;
        next = cur + 1;
      }
      if (next == kNoIndex || next <= cur || next > limit) return false;
      if (next == limit) {
        exitBlock = limit;
        return true;
      }
      cur = next;
    }
  }

  const PexLoop* loop_at(std::size_t block) const {
    for (const auto& loop : cfg_.loops) {
      if (loop.headerBlock == block) return &loop;
    }
    return nullptr;
  }

  void emit_block_body(std::size_t block, int depth, bool withTerminator,
                       std::string& out) const {
    const auto& b = cfg_.blocks[block];
    if (b.count == 0) return;
    const auto end = b.first + b.count - (withTerminator ? std::size_t{0} : std::size_t{1});
    for (auto i = b.first; i < end; ++i) {
      out += indent_text(depth);
      out += instruction_line(function_.instructions[i], i);
      out += "\n";
    }
  }

  bool emit_loop(std::size_t block, const PexLoop& loop, int depth, std::string& out) {
    const auto target = loop_exit(block, loop);
    if (target == kNoIndex) return false;
    const auto terminator = terminator_of(block);
    if (cfg_.blocks[block].count > 1) {
      out += indent_text(depth) + "while (true) {\n";
      emit_block_body(block, depth + 1, false, out);
      out += indent_text(depth + 1) + "if (!(" + condition_text(terminator) + ")) break;\n";
    } else {
      emit_block_body(block, depth, false, out);
      out += indent_text(depth) + "while (" + condition_text(terminator) + ") {\n";
    }
    const auto ended = emit_range(block + 1, target, depth + 1, out);
    if (bailed_ || ended != target) return false;
    out += indent_text(depth) + "}\n";
    return true;
  }

  bool emit_conditional(std::size_t block, int depth, std::size_t& next, std::string& out) {
    const auto terminator = terminator_of(block);
    const auto& instruction = function_.instructions[terminator];
    if (instruction.opCode != static_cast<std::uint8_t>(PexOp::JmpF)) return false;
    const auto elseStart = jump_target_of(terminator);
    if (elseStart == kNoIndex || elseStart <= block + 1) return false;
    const auto join = conditional_join(block, depth);
    if (join == kNoIndex) return false;

    emit_block_body(block, depth, false, out);
    out += indent_text(depth) + "if (" + condition_text(terminator) + ") {\n";
    const auto endedThen = emit_range(block + 1, elseStart, depth + 1, out);
    if (bailed_ || endedThen != elseStart) return false;
    if (join == elseStart) {
      out += indent_text(depth) + "}\n";
      next = elseStart;
      return true;
    }
    out += indent_text(depth) + "} else {\n";
    const auto endedElse = emit_range(elseStart, join, depth + 1, out);
    if (bailed_ || endedElse != join) return false;
    out += indent_text(depth) + "}\n";
    next = join;
    return true;
  }

  std::size_t emit_range(std::size_t block, std::size_t stop, int depth, std::string& out) {
    if (depth > kMaxNestingDepth) {
      bailed_ = true;
      return kNoIndex;
    }
    while (block != stop) {
      if (block >= cfg_.blocks.size() || emitted_[block]) {
        bailed_ = true;
        return kNoIndex;
      }
      emitted_[block] = true;
      const auto& current = cfg_.blocks[block];
      if (current.count == 0) {
        bailed_ = true;
        return kNoIndex;
      }

      const auto terminator = terminator_of(block);
      const auto* loop = loop_at(block);
      if (loop != nullptr) {
        const auto exitBlock = loop_exit(block, *loop);
        if (exitBlock == kNoIndex || !emit_loop(block, *loop, depth, out)) {
          bailed_ = true;
          return kNoIndex;
        }
        block = exitBlock;
        continue;
      }

      if (pex_is_conditional_jump(function_.instructions[terminator].opCode)) {
        std::size_t next = kNoIndex;
        if (!emit_conditional(block, depth, next, out)) {
          bailed_ = true;
          return kNoIndex;
        }
        block = next;
        continue;
      }

      const auto& successors = current.successors;
      if (block == stop - 1) {
        if (successors.size() > 1) {
          bailed_ = true;
          return kNoIndex;
        }
        const bool isJump = pex_is_jump(function_.instructions[terminator].opCode);
        if (depth == 0 && isJump && jump_target_of(terminator) != stop) {
          bailed_ = true;
          return kNoIndex;
        }
        emit_block_body(block, depth, !isJump, out);
        block = stop;
        continue;
      }

      if (successors.size() != 1 || successors.front() != block + 1) {
        bailed_ = true;
        return kNoIndex;
      }
      emit_block_body(block, depth, true, out);
      block = successors.front();
    }
    return block;
  }

  const PexFunctionCfg& cfg_;
  const fo4::pex::PexFunction& function_;
  std::vector<bool> emitted_;
  bool bailed_{false};
};

std::string plain_listing(const PexFunctionCfg& cfg, const fo4::pex::PexFunction& function) {
  const auto count = function.instructions.size();
  std::vector<bool> isTarget(count + 1, false);
  for (std::size_t i = 0; i < count; ++i) {
    std::int64_t offset = 0;
    if (!pex_jump_offset(function.instructions[i], &offset)) continue;
    const auto target = static_cast<std::int64_t>(i) + offset;
    if (target >= 0 && target <= static_cast<std::int64_t>(count)) {
      isTarget[static_cast<std::size_t>(target)] = true;
    }
  }

  std::string out;
  for (std::size_t i = 0; i < count; ++i) {
    if (isTarget[i]) {
      out += label_line(i);
      out += "\n";
    }
    const auto block = cfg_block_of(cfg, i);
    if (block != kNoIndex && !cfg.blocks[block].reachable && cfg.blocks[block].first == i) {
      out += "; unreachable\n";
    }
    out += instruction_line(function.instructions[i], i);
    out += "\n";
  }
  if (isTarget[count]) {
    out += label_line(count);
    out += "\n";
  }
  return out;
}

}

PexDisasmResult disassemble_function(const fo4::pex::PexFunction& function,
                                     const PexDisasmOptions& options) {
  PexDisasmResult result;
  const auto cfg = build_cfg(function);
  result.status = cfg.status;
  result.errorInstruction = cfg.errorInstruction;
  result.errorTarget = cfg.errorTarget;
  if (cfg.status != PexAnalysisStatus::Ok) {
    result.text = "error: " + pex_analysis_status_text(cfg.status) + " at instruction " +
                  std::to_string(cfg.errorInstruction);
    if (cfg.status == PexAnalysisStatus::JumpTargetOutOfRange) {
      result.text += " (target " + std::to_string(cfg.errorTarget) + ")";
    }
    result.text += "\n";
    return result;
  }

  if (options.structured) {
    std::string structured;
    StructuredRenderer renderer(cfg, function);
    if (renderer.render(structured)) {
      result.structured = true;
      result.text = std::move(structured);
      return result;
    }
  }
  result.text = plain_listing(cfg, function);
  return result;
}

}
