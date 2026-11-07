#include <fo4/f4se/registration_extractor.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

#include <fo4/f4se/cpp_scanner.hpp>
#include <fo4/f4se/type_map.hpp>

namespace fo4::f4se {

namespace {

struct Opening {
    std::size_t index{0};
    bool latent{false};
    int arity{0};
    std::size_t angle{0};
};

std::vector<Opening> findOpenings(std::string_view text) {
    std::vector<Opening> out;
    constexpr std::string_view needle = "new";
    std::size_t i = 0;
    while ((i = text.find(needle, i)) != std::string_view::npos) {
        std::size_t j = i + needle.size();
        const auto wsStart = j;
        while (j < text.size() && std::isspace(static_cast<unsigned char>(text[j]))) j++;
        if (j == wsStart) {
            i += needle.size();
            continue;
        }
        bool latent = false;
        if (text.substr(j, 6) == "Latent") {
            latent = true;
            j += 6;
        }
        if (text.substr(j, 14) != "NativeFunction") {
            i += needle.size();
            continue;
        }
        j += 14;
        const auto digitsStart = j;
        int arity = 0;
        while (j < text.size() && std::isdigit(static_cast<unsigned char>(text[j]))) {
            arity = arity * 10 + (text[j] - '0');
            j++;
        }
        if (j == digitsStart) {
            i += needle.size();
            continue;
        }
        while (j < text.size() && std::isspace(static_cast<unsigned char>(text[j]))) j++;
        if (j >= text.size() || text[j] != '<') {
            i += needle.size();
            continue;
        }
        out.push_back(Opening{i, latent, arity, j});
        i += needle.size();
    }
    return out;
}

struct ConstructorMatch {
    std::string fn;
    std::string cls;
    std::string ptr;
};

std::optional<ConstructorMatch> parseConstructor(std::string_view text, std::size_t start) {
    std::size_t i = start;
    const auto skipWs = [&]() {
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) i++;
    };

    skipWs();
    if (i >= text.size() || text[i] != '(') return std::nullopt;
    i++;
    skipWs();
    if (i >= text.size() || text[i] != '"') return std::nullopt;
    i++;
    std::string fn;
    while (i < text.size() && text[i] != '"') fn.push_back(text[i++]);
    if (i >= text.size()) return std::nullopt;
    i++;
    skipWs();
    if (i >= text.size() || text[i] != ',') return std::nullopt;
    i++;
    skipWs();
    if (i >= text.size() || text[i] != '"') return std::nullopt;
    i++;
    std::string cls;
    while (i < text.size() && text[i] != '"') cls.push_back(text[i++]);
    if (i >= text.size()) return std::nullopt;
    i++;
    skipWs();
    if (i >= text.size() || text[i] != ',') return std::nullopt;
    i++;
    skipWs();
    if (i >= text.size()) return std::nullopt;
    const auto first = static_cast<unsigned char>(text[i]);
    if (!std::isalpha(first) && text[i] != '_') return std::nullopt;
    std::string ptr;
    while (i < text.size()) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (std::isalnum(c) || text[i] == '_' || text[i] == ':') {
            ptr.push_back(text[i]);
            i++;
        } else {
            break;
        }
    }
    return ConstructorMatch{std::move(fn), std::move(cls), std::move(ptr)};
}

struct FlagMatch {
    std::size_t index{0};
    std::string cls;
    std::string fn;
    std::string flag;
};

std::vector<FlagMatch> findFunctionFlags(std::string_view text) {
    std::vector<FlagMatch> out;
    constexpr std::string_view needle = "SetFunctionFlags";
    std::size_t i = 0;
    while ((i = text.find(needle, i)) != std::string_view::npos) {
        const auto matchIndex = i;
        std::size_t j = i + needle.size();
        if (text.substr(j, 2) == "Ex") j += 2;
        const auto skipWs = [&]() {
            while (j < text.size() && std::isspace(static_cast<unsigned char>(text[j]))) j++;
        };
        skipWs();
        if (j >= text.size() || text[j] != '(') {
            i += needle.size();
            continue;
        }
        j++;
        skipWs();
        if (j >= text.size() || text[j] != '"') {
            i += needle.size();
            continue;
        }
        j++;
        std::string cls;
        while (j < text.size() && text[j] != '"') cls.push_back(text[j++]);
        if (j >= text.size()) {
            i += needle.size();
            continue;
        }
        j++;
        skipWs();
        if (j >= text.size() || text[j] != ',') {
            i += needle.size();
            continue;
        }
        j++;
        skipWs();
        if (j < text.size() && text[j] != '"') {
            while (j < text.size() && text[j] != ',') j++;
            if (j >= text.size()) {
                i += needle.size();
                continue;
            }
            j++;
            skipWs();
        }
        if (j >= text.size() || text[j] != '"') {
            i += needle.size();
            continue;
        }
        j++;
        std::string fn;
        while (j < text.size() && text[j] != '"') fn.push_back(text[j++]);
        if (j >= text.size()) {
            i += needle.size();
            continue;
        }
        j++;
        skipWs();
        if (j >= text.size() || text[j] != ',') {
            i += needle.size();
            continue;
        }
        j++;
        skipWs();
        if (text.substr(j, 9) == "IFunction") {
            std::size_t k = j + 9;
            while (k < text.size() && std::isspace(static_cast<unsigned char>(text[k]))) k++;
            if (text.substr(k, 2) == "::") {
                j = k + 2;
                skipWs();
            }
        }
        const auto flagStart = j;
        while (j < text.size()) {
            const auto c = static_cast<unsigned char>(text[j]);
            if (std::isalnum(c) || text[j] == '_') {
                j++;
            } else {
                break;
            }
        }
        if (j == flagStart) {
            i += needle.size();
            continue;
        }
        const auto flag = std::string(text.substr(flagStart, j - flagStart));
        skipWs();
        if (j >= text.size() || text[j] != ')') {
            i += needle.size();
            continue;
        }
        out.push_back(FlagMatch{matchIndex, std::move(cls), std::move(fn), flag});
        i += needle.size();
    }
    return out;
}

std::vector<std::pair<std::string, std::string>> findStructDeclarations(std::string_view text) {
    std::vector<std::pair<std::string, std::string>> out;
    constexpr std::string_view needle = "DECLARE_STRUCT";
    std::size_t i = 0;
    while ((i = text.find(needle, i)) != std::string_view::npos) {
        if (i > 0) {
            const auto before = static_cast<unsigned char>(text[i - 1]);
            if (std::isalnum(before) || text[i - 1] == '_') {
                i += needle.size();
                continue;
            }
        }
        std::size_t j = i + needle.size();
        const auto skipWs = [&]() {
            while (j < text.size() && std::isspace(static_cast<unsigned char>(text[j]))) j++;
        };
        skipWs();
        if (j >= text.size() || text[j] != '(') {
            i += needle.size();
            continue;
        }
        j++;
        skipWs();
        const auto nameStart = j;
        while (j < text.size() &&
               (std::isalnum(static_cast<unsigned char>(text[j])) || text[j] == '_')) {
            j++;
        }
        if (j == nameStart) {
            i += needle.size();
            continue;
        }
        const auto name = std::string(text.substr(nameStart, j - nameStart));
        skipWs();
        if (j >= text.size() || text[j] != ',') {
            i += needle.size();
            continue;
        }
        j++;
        skipWs();
        if (j >= text.size() || text[j] != '"') {
            i += needle.size();
            continue;
        }
        j++;
        const auto ownerStart = j;
        while (j < text.size() && text[j] != '"') j++;
        if (j >= text.size()) {
            i += needle.size();
            continue;
        }
        const auto owner = std::string(text.substr(ownerStart, j - ownerStart));
        j++;
        skipWs();
        if (j >= text.size() || text[j] != ')') {
            i += needle.size();
            continue;
        }
        out.emplace_back(name, owner);
        i += needle.size();
    }
    return out;
}

std::vector<std::string> findExternStructDeclarations(std::string_view text) {
    std::vector<std::string> out;
    constexpr std::string_view needle = "DECLARE_EXTERN_STRUCT";
    std::size_t i = 0;
    while ((i = text.find(needle, i)) != std::string_view::npos) {
        if (i > 0) {
            const auto before = static_cast<unsigned char>(text[i - 1]);
            if (std::isalnum(before) || text[i - 1] == '_') {
                i += needle.size();
                continue;
            }
        }
        std::size_t j = i + needle.size();
        const auto skipWs = [&]() {
            while (j < text.size() && std::isspace(static_cast<unsigned char>(text[j]))) j++;
        };
        skipWs();
        if (j >= text.size() || text[j] != '(') {
            i += needle.size();
            continue;
        }
        j++;
        skipWs();
        const auto nameStart = j;
        while (j < text.size() &&
               (std::isalnum(static_cast<unsigned char>(text[j])) || text[j] == '_')) {
            j++;
        }
        if (j == nameStart) {
            i += needle.size();
            continue;
        }
        const auto name = std::string(text.substr(nameStart, j - nameStart));
        skipWs();
        if (j >= text.size() || text[j] != ')') {
            i += needle.size();
            continue;
        }
        out.push_back(name);
        i += needle.size();
    }
    return out;
}

bool matchesPattern(const std::string& name, std::string_view pattern) {
    const auto star = pattern.find('*');
    if (star == std::string_view::npos) return name == pattern;
    const auto prefix = pattern.substr(0, star);
    const auto suffix = pattern.substr(star + 1);
    return name.size() >= prefix.size() + suffix.size() &&
           name.compare(0, prefix.size(), prefix) == 0 &&
           name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool readFile(const std::filesystem::path& path, std::string& out) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return false;
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    out = buffer.str();
    return true;
}

}

F4SEModuleSchema RegistrationExtractor::extract(std::string_view source,
                                                std::string sourcePath,
                                                std::vector<std::string> knownStructs) const {
    const auto text = CppScanner::blankComments(source);
    collectStructNames(text, knownStructs);
    return extractBlanked(text, std::move(sourcePath), knownStructs);
}

F4SEModuleSchema RegistrationExtractor::extractFile(
    const std::filesystem::path& path,
    const std::vector<std::string>* knownStructs) const {
    std::string content;
    if (!readFile(path, content)) return F4SEModuleSchema{};
    return extract(content, path.string(),
                   knownStructs ? *knownStructs : std::vector<std::string>{});
}

std::vector<F4SEModuleSchema> RegistrationExtractor::extractDirectory(
    const std::filesystem::path& directory, std::string_view pattern) const {
    std::vector<F4SEModuleSchema> out;
    if (!std::filesystem::exists(directory)) return out;

    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file()) continue;
        if (matchesPattern(entry.path().filename().string(), pattern)) {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());

    std::vector<std::pair<std::filesystem::path, std::string>> texts;
    std::vector<std::string> structNames;
    for (const auto& file : files) {
        std::string content;
        if (!readFile(file, content)) continue;
        auto blanked = CppScanner::blankComments(content);
        collectStructNames(blanked, structNames);
        texts.emplace_back(file, std::move(blanked));
    }

    for (const auto& [file, blanked] : texts) {
        out.push_back(extractBlanked(blanked, file.string(), structNames));
    }
    return out;
}

void RegistrationExtractor::collectStructNames(const std::string& blankedText,
                                               std::vector<std::string>& into) {
    for (const auto& [name, owner] : findStructDeclarations(blankedText)) {
        (void)owner;
        if (std::find(into.begin(), into.end(), name) == into.end()) into.push_back(name);
    }
    for (const auto& name : findExternStructDeclarations(blankedText)) {
        if (std::find(into.begin(), into.end(), name) == into.end()) into.push_back(name);
    }
}

F4SEModuleSchema RegistrationExtractor::extractBlanked(
    const std::string& text, std::string sourcePath,
    const std::vector<std::string>& structNames) {
    std::vector<std::string> problems;
    auto natives = readRegistrations(text, sourcePath, structNames, problems);
    applyFunctionFlags(text, natives, sourcePath, problems);

    std::vector<StructBinding> structs;
    for (const auto& [name, owner] : findStructDeclarations(text)) {
        StructBinding declared;
        declared.name = name;
        declared.ownerScript = owner;
        structs.push_back(std::move(declared));
    }

    F4SEModuleSchema schema;
    schema.sourcePath = std::move(sourcePath);
    schema.natives = std::move(natives);
    schema.structs = std::move(structs);
    schema.problems = std::move(problems);
    return schema;
}

std::vector<NativeBinding> RegistrationExtractor::readRegistrations(
    const std::string& text, const std::string& sourcePath,
    const std::vector<std::string>& structNames, std::vector<std::string>& problems) {
    std::vector<NativeBinding> natives;
    F4SETypeMap map(structNames);

    for (const auto& opening : findOpenings(text)) {
        const auto close = CppScanner::findMatchingAngle(text, static_cast<int>(opening.angle));
        const auto line = CppScanner::lineAt(text, static_cast<int>(opening.index));

        if (close < 0) {
            problems.push_back(sourcePath + "(" + std::to_string(line) +
                               "): template argument list is never closed");
            continue;
        }

        const auto constructor = parseConstructor(text, static_cast<std::size_t>(close) + 1);
        if (!constructor) {
            problems.push_back(sourcePath + "(" + std::to_string(line) +
                               "): registration is not followed by (name, class, function)");
            continue;
        }

        const auto arguments = CppScanner::splitTemplateArguments(
            std::string_view(text).substr(opening.angle + 1,
                                          static_cast<std::size_t>(close) - opening.angle - 1));
        if (arguments.size() < 2) {
            problems.push_back(sourcePath + "(" + std::to_string(line) +
                               "): fewer than the two required template arguments");
            continue;
        }

        const int arity = opening.arity;
        const int declared = static_cast<int>(arguments.size()) - 2;
        if (declared != arity) {
            problems.push_back(sourcePath + "(" + std::to_string(line) +
                               "): NativeFunction" + std::to_string(arity) + " carries " +
                               std::to_string(declared) + " parameter types");
            continue;
        }

        const auto returnType = CppTypeRef::tryParse(arguments[1]);
        if (!returnType) {
            problems.push_back(sourcePath + "(" + std::to_string(line) +
                               "): return type '" + arguments[1] +
                               "' is not a signature type");
            continue;
        }

        std::vector<NativeParameter> parameters;
        bool ok = true;
        for (int i = 0; i < arity; i++) {
            const auto& argumentText = arguments[static_cast<std::size_t>(i) + 2];
            const auto cpp = CppTypeRef::tryParse(argumentText);
            if (!cpp) {
                problems.push_back(sourcePath + "(" + std::to_string(line) +
                                   "): parameter " + std::to_string(i) + " type '" +
                                   argumentText + "' is not a signature type");
                ok = false;
                break;
            }

            NativeParameter parameter;
            parameter.name = "a" + std::to_string(i);
            parameter.isUnsigned = cpp->name == "UInt32";
            if (!map.tryToPapyrus(&*cpp, parameter.type, nullptr)) {
                parameter.type = PapyrusTypeText{kUnknownTypeName, false};
            }
            parameters.push_back(std::move(parameter));
        }
        if (!ok) continue;

        NativeBinding binding;
        binding.functionName = constructor->fn;
        binding.papyrusClass = constructor->cls;
        binding.cppBaseType = arguments[0];
        binding.returnUnsigned = returnType->name == "UInt32";
        if (!map.tryToPapyrus(&*returnType, binding.returnType, nullptr)) {
            binding.returnType = PapyrusTypeText{kUnknownTypeName, false};
        }
        binding.parameters = std::move(parameters);
        binding.isLatent = opening.latent;
        binding.cppFunctionName = constructor->ptr;
        binding.sourceFile = sourcePath;
        binding.sourceLine = line;
        natives.push_back(std::move(binding));
    }

    return natives;
}

void RegistrationExtractor::applyFunctionFlags(const std::string& text,
                                               std::vector<NativeBinding>& natives,
                                               const std::string& sourcePath,
                                               std::vector<std::string>& problems) {
    for (const auto& match : findFunctionFlags(text)) {
        if (match.flag != "kFunctionFlag_NoWait") continue;

        int index = -1;
        for (std::size_t i = 0; i < natives.size(); i++) {
            if (CiEqual{}(natives[i].papyrusClass, match.cls) &&
                CiEqual{}(natives[i].functionName, match.fn)) {
                index = static_cast<int>(i);
                break;
            }
        }

        if (index < 0) {
            problems.push_back(
                sourcePath + "(" +
                std::to_string(CppScanner::lineAt(text, static_cast<int>(match.index))) +
                "): flags set on " + match.cls + "." + match.fn +
                ", which this file does not register");
            continue;
        }

        natives[static_cast<std::size_t>(index)].noWait = true;
    }
}

}
