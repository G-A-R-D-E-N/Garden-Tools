#include "fo4/pex_analysis/parent_chain.hpp"

#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace fo4::pex_analysis {
namespace {

std::string fold(std::string_view text) {
  std::string out(text);
  for (char& ch : out) {
    if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch + 32);
  }
  return out;
}

std::unordered_map<std::string, const fo4::pex::PexObject*> index_objects(
    std::span<const fo4::pex::PexFile* const> files) {
  std::unordered_map<std::string, const fo4::pex::PexObject*> by_name;
  for (const auto* file : files) {
    if (file == nullptr) continue;
    for (const auto& object : file->objects) by_name.try_emplace(fold(object.name), &object);
  }
  return by_name;
}

std::unordered_set<std::string> fold_seeds(std::span<const std::string> seedTypes) {
  std::unordered_set<std::string> seeds;
  seeds.reserve(seedTypes.size());
  for (const auto& seed : seedTypes) seeds.insert(fold(seed));
  return seeds;
}

}  // namespace

PexChainReport validate_parent_chains(std::span<const fo4::pex::PexFile* const> files,
                                      std::span<const std::string> seedTypes,
                                      std::size_t depthLimit) {
  PexChainReport report;
  const auto by_name = index_objects(files);
  const auto seeds = fold_seeds(seedTypes);

  for (std::size_t f = 0; f < files.size(); ++f) {
    const auto* file = files[f];
    if (file == nullptr) continue;
    for (std::size_t o = 0; o < file->objects.size(); ++o) {
      const auto& object = file->objects[o];
      ++report.checkedObjects;
      PexChainFinding finding;
      finding.fileIndex = f;
      finding.objectIndex = o;
      finding.objectName = object.name;
      const auto finish = [&](PexChainProblem problem) {
        finding.problem = problem;
        finding.depth = finding.chain.size();
        report.findings.push_back(finding);
      };
      std::unordered_set<std::string> seen;
      std::string current = object.parentClassName;
      while (!current.empty() && !seeds.contains(fold(current))) {
        if (finding.chain.size() >= depthLimit) {
          finish(PexChainProblem::TooDeep);
          break;
        }
        finding.chain.push_back(current);
        if (!seen.insert(fold(current)).second) {
          finish(PexChainProblem::Cycle);
          break;
        }
        const auto next = by_name.find(fold(current));
        if (next == by_name.end()) {
          finish(PexChainProblem::MissingParent);
          break;
        }
        current = next->second->parentClassName;
      }
    }
  }
  return report;
}

}  // namespace fo4::pex_analysis
