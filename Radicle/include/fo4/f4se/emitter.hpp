#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <fo4/f4se/binding_model.hpp>
#include <fo4/f4se/cross_check.hpp>
#include <fo4/f4se/diagnostic.hpp>

namespace fo4::f4se {

struct F4SEEmitOptions {
    std::map<std::string, std::string, CiLess> existing;
    bool emitDespiteErrors{false};
};

struct F4SEEmitResult {
    std::vector<EmittedFile> files;
    std::vector<Diagnostic> diagnostics;
    std::vector<std::string> orphanedBodies;

    std::vector<Diagnostic> errors() const {
        std::vector<Diagnostic> out;
        for (const auto& diagnostic : diagnostics) {
            if (diagnostic.severity == DiagnosticSeverity::Error) out.push_back(diagnostic);
        }
        return out;
    }

    bool success() const {
        return !files.empty() && errors().empty();
    }

    const EmittedFile* file(std::string_view relativePath) const {
        for (const auto& file : files) {
            if (CiEqual{}(file.relativePath, relativePath)) return &file;
        }
        return nullptr;
    }
};

class F4SEEmitter {
public:
    F4SEEmitResult emit(const PluginBinding& plugin,
                        const F4SEEmitOptions* options = nullptr) const;

    static CrossCheckResult roundTrip(const PluginBinding& plugin,
                                      const F4SEEmitResult& result);
};

}
