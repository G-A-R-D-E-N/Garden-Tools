#include <fo4/pex_analysis/native_catalogue.hpp>

#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <utility>

namespace fo4::pex_analysis {

namespace {

std::string lowered(const std::string& text) {
  std::string out = text;
  for (char& ch : out) {
    if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch + 32);
  }
  return out;
}

using Slot = std::array<std::size_t, 4>;

}  // namespace

PexNativeCatalogue build_native_catalogue(std::span<const fo4::pex::PexFile* const> files,
                                          const PexCallGraph* graph) {
  PexNativeCatalogue catalogue;
  std::set<std::pair<std::string, std::string>> seen;
  std::map<Slot, std::size_t> slots;

  for (std::size_t f = 0; f < files.size(); ++f) {
    const auto* file = files[f];
    if (file == nullptr) continue;
    for (std::size_t o = 0; o < file->objects.size(); ++o) {
      const auto& object = file->objects[o];
      for (std::size_t s = 0; s < object.states.size(); ++s) {
        const auto& state = object.states[s];
        for (std::size_t k = 0; k < state.functions.size(); ++k) {
          const auto& function = state.functions[k];
          if (!function.isNative) continue;
          if (!seen.emplace(lowered(object.name), lowered(function.name)).second) continue;
          PexNativeEntry entry;
          entry.script = object.name;
          entry.function = function.name;
          entry.state = state.name;
          entry.returnType = function.returnType;
          entry.global = function.isGlobal;
          entry.params = function.params;
          entry.fileIndex = f;
          slots.emplace(Slot{f, o, s, k}, catalogue.entries.size());
          catalogue.entries.push_back(std::move(entry));
        }
      }
    }
  }

  if (graph != nullptr) {
    for (const auto& site : graph->calls) {
      if (!site.resolved || !site.nativeTarget) continue;
      const auto found = slots.find(
          Slot{site.target.fileIndex, site.target.objectIndex, site.target.stateIndex, site.target.functionIndex});
      if (found != slots.end()) ++catalogue.entries[found->second].callSites;
    }
  }

  std::sort(catalogue.entries.begin(), catalogue.entries.end(),
            [](const PexNativeEntry& left, const PexNativeEntry& right) {
              const auto leftScript = lowered(left.script);
              const auto rightScript = lowered(right.script);
              if (leftScript != rightScript) return leftScript < rightScript;
              return lowered(left.function) < lowered(right.function);
            });
  catalogue.calledEntries = static_cast<std::size_t>(std::count_if(
      catalogue.entries.begin(), catalogue.entries.end(), [](const PexNativeEntry& e) { return e.callSites > 0; }));
  return catalogue;
}

PexNativeBindingReport report_native_binding(
    const PexNativeCatalogue& catalogue,
    const std::function<bool(const std::string& script, const std::string& function)>& isBound) {
  PexNativeBindingReport report;
  for (const auto& entry : catalogue.entries) {
    if (isBound && isBound(entry.script, entry.function)) {
      report.bound.push_back(entry);
    } else {
      if (entry.callSites > 0) ++report.unboundCalled;
      report.unbound.push_back(entry);
    }
  }
  return report;
}

std::string format_native_binding_report(const PexNativeBindingReport& report) {
  std::string text = "natives bound " + std::to_string(report.bound.size()) + ", unbound " +
                     std::to_string(report.unbound.size()) + " (called " + std::to_string(report.unboundCalled) +
                     ")\n";
  for (const auto& entry : report.unbound) {
    text += "unbound " + entry.script + "." + entry.function + " calls " + std::to_string(entry.callSites) + "\n";
  }
  return text;
}

}  // namespace fo4::pex_analysis
