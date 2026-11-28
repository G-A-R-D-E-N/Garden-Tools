#pragma once

#include "fo4/pex/pex.hpp"
#include "fo4/pex_analysis/callgraph.hpp"

#include <cstddef>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace fo4::pex_analysis {

struct PexNativeEntry {
  std::string script;
  std::string function;
  std::string state;
  std::string returnType;
  bool global{false};
  std::vector<fo4::pex::PexTypedName> params;
  std::size_t fileIndex{kNoIndex};
  std::size_t callSites{0};
};

struct PexNativeCatalogue {
  std::vector<PexNativeEntry> entries;  // invariant: sorted by lower-cased script then function, one entry per pair
  std::size_t calledEntries{0};
};

// Invariant: callSites counts only call sites the graph resolved to the declaring script; an inherited native called through a child script is not counted.
PexNativeCatalogue build_native_catalogue(std::span<const fo4::pex::PexFile* const> files,
                                          const PexCallGraph* graph = nullptr);

struct PexNativeBindingReport {
  std::vector<PexNativeEntry> bound;
  std::vector<PexNativeEntry> unbound;
  std::size_t unboundCalled{0};
};

// Requires isBound to answer for the declaring script and function name as the catalogue spells them.
PexNativeBindingReport report_native_binding(
    const PexNativeCatalogue& catalogue,
    const std::function<bool(const std::string& script, const std::string& function)>& isBound);

std::string format_native_binding_report(const PexNativeBindingReport& report);

}  // namespace fo4::pex_analysis
