#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <fo4ide/f4se/BindingModel.hpp>

namespace fo4ide::f4se {

// Port of F4SEModuleSchema: what one .cpp file registers.
struct F4SEModuleSchema {
    std::string sourcePath;
    std::vector<NativeBinding> natives;
    std::vector<StructBinding> structs;
    std::vector<std::string> problems;
};

// Port of F4SERegistrationExtractor: recovers the NativeFunction registrations
// and DECLARE_STRUCT declarations a generated .cpp carries, so the emitter can
// cross-check its own output and the corpus sweeps can scan real F4SE trees.
class RegistrationExtractor {
public:
    static constexpr const char* kUnknownTypeName = "<unmapped>";

    F4SEModuleSchema extract(std::string_view source, std::string sourcePath = "",
                             std::vector<std::string> knownStructs = {}) const;

    F4SEModuleSchema extractFile(const std::filesystem::path& path,
                                 const std::vector<std::string>* knownStructs = nullptr) const;

    // All Papyrus*.cpp files in `directory` (ordinal-sorted), with struct
    // names collected across the directory before any signature is mapped.
    std::vector<F4SEModuleSchema> extractDirectory(const std::filesystem::path& directory,
                                                   std::string_view pattern =
                                                       "Papyrus*.cpp") const;

private:
    static F4SEModuleSchema extractBlanked(const std::string& text, std::string sourcePath,
                                           const std::vector<std::string>& structNames);
    static std::vector<NativeBinding> readRegistrations(
        const std::string& text, const std::string& sourcePath,
        const std::vector<std::string>& structNames, std::vector<std::string>& problems);
    static void applyFunctionFlags(const std::string& text,
                                   std::vector<NativeBinding>& natives,
                                   const std::string& sourcePath,
                                   std::vector<std::string>& problems);
    static void collectStructNames(const std::string& blankedText,
                                   std::vector<std::string>& into);
};

}  // namespace fo4ide::f4se
