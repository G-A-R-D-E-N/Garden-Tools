#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <fo4ide/f4se/BindingModel.hpp>
#include <fo4ide/f4se/CrossCheck.hpp>
#include <fo4ide/graph/GraphDiagnostic.hpp>

namespace fo4ide::f4se {

// Port of F4SEEmitOptions: the previous emit's files (for body preservation)
// and the escape hatch that emits despite validation errors.
struct F4SEEmitOptions {
    std::map<std::string, std::string, CiLess> existing;
    bool emitDespiteErrors{false};
};

// Port of F4SEEmitResult.
struct F4SEEmitResult {
    std::vector<EmittedFile> files;
    std::vector<graph::GraphDiagnostic> diagnostics;
    std::vector<std::string> orphanedBodies;

    std::vector<graph::GraphDiagnostic> errors() const {
        std::vector<graph::GraphDiagnostic> out;
        for (const auto& diagnostic : diagnostics) {
            if (diagnostic.severity == graph::GraphSeverity::Error) out.push_back(diagnostic);
        }
        return out;
    }

    bool success() const {
        return !files.empty() && errors().empty();
    }

    // Case-insensitive path lookup, mirroring the C# File(relativePath).
    const EmittedFile* file(std::string_view relativePath) const {
        for (const auto& file : files) {
            if (CiEqual{}(file.relativePath, relativePath)) return &file;
        }
        return nullptr;
    }
};

// Port of F4SEEmitter: validate -> emit every module's .h/.cpp/.psc, the
// registrations aggregator, the shell files, then merge preserved bodies.
class F4SEEmitter {
public:
    F4SEEmitResult emit(const PluginBinding& plugin,
                        const F4SEEmitOptions* options = nullptr) const;

    // Recovers the emitted .cpp registrations and cross checks them against
    // the emitted .psc declarations.
    static CrossCheckResult roundTrip(const PluginBinding& plugin,
                                      const F4SEEmitResult& result);
};

}  // namespace fo4ide::f4se
