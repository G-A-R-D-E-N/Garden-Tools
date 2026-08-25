#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fo4ide::f4se {

// Case-insensitive string comparison helpers mirroring the C#
// StringComparer.OrdinalIgnoreCase usages in the F4SE binding model.
struct CiLess {
    bool operator()(std::string_view a, std::string_view b) const {
        return icompare(a, b) < 0;
    }
    static int icompare(std::string_view a, std::string_view b);
};
struct CiEqual {
    bool operator()(std::string_view a, std::string_view b) const {
        return CiLess::icompare(a, b) == 0;
    }
};

// Port of PapyrusTypeText: a Papyrus type name plus an array flag, with the
// []-suffixed ToString.
struct PapyrusTypeText {
    std::string name;
    bool isArray{false};

    std::string toString() const { return isArray ? name + "[]" : name; }
};

// Port of CppTypeRef: a C++ signature type (VMArray<...> nests, trailing '*'
// becomes isPointer).
struct CppTypeRef {
    std::string name;
    bool isPointer{false};
    std::unique_ptr<CppTypeRef> elementType;

    static constexpr const char* kArrayTemplate = "VMArray";

    CppTypeRef() = default;
    CppTypeRef(std::string name_, bool pointer = false)
        : name(std::move(name_)), isPointer(pointer) {}
    CppTypeRef(std::string name_, bool pointer, std::unique_ptr<CppTypeRef> element)
        : name(std::move(name_)), isPointer(pointer), elementType(std::move(element)) {}

    CppTypeRef(const CppTypeRef& other)
        : name(other.name), isPointer(other.isPointer),
          elementType(other.elementType ? std::make_unique<CppTypeRef>(*other.elementType)
                                        : nullptr) {}
    CppTypeRef& operator=(const CppTypeRef& other) {
        if (this != &other) {
            name = other.name;
            isPointer = other.isPointer;
            elementType = other.elementType ? std::make_unique<CppTypeRef>(*other.elementType)
                                            : nullptr;
        }
        return *this;
    }
    CppTypeRef(CppTypeRef&&) noexcept = default;
    CppTypeRef& operator=(CppTypeRef&&) noexcept = default;

    bool isArray() const { return elementType != nullptr; }

    std::string format() const {
        return isArray() ? std::string(kArrayTemplate) + "<" + elementType->format() + ">"
                         : name + (isPointer ? "*" : "");
    }

    static CppTypeRef arrayOf(CppTypeRef element) {
        return CppTypeRef(kArrayTemplate, false,
                          std::make_unique<CppTypeRef>(std::move(element)));
    }

    // nullopt when the text is not a signature type (mirrors the C# bool
    // TryParse out-parameter).
    static std::optional<CppTypeRef> tryParse(std::string_view text);

    // Throws nothing: mirrors C# Parse which throws FormatException on bad
    // text; here the caller gets an empty optional via tryParse instead.
    static CppTypeRef parse(std::string_view text);
};

// Port of F4SETypeMap: the Papyrus<->C++ marshalling table and the
// include / template-instantiation helpers the emitters need.
class F4SETypeMap {
public:
    static constexpr const char* kStaticFunctionTag = "StaticFunctionTag";

    explicit F4SETypeMap(std::vector<std::string> structNames);

    const std::vector<std::string>& structNames() const { return structNames_; }

    // Papyrus name -> C++ name for every form-table row (OrdinalIgnoreCase).
    static std::map<std::string, std::string, CiLess> formTypes();

    static const std::vector<std::string>& coreIncludes();
    static const std::vector<std::string>& gameIncludes();

    // Mirrors TryToCpp(PapyrusTypeText, bool, out CppTypeRef?, out string?).
    // On failure outCpp is reset and refusal (when non-null) receives the
    // reason text.
    bool tryToCpp(const PapyrusTypeText& papyrus, bool isUnsigned,
                  std::optional<CppTypeRef>& outCpp, std::string* refusal) const;

    // Mirrors TryToPapyrus(CppTypeRef?, out PapyrusTypeText, out string?);
    // a null cpp is "no type given".
    bool tryToPapyrus(const CppTypeRef* cpp, PapyrusTypeText& outPapyrus,
                      std::string* refusal) const;

    std::vector<std::string> requiredIncludesFor(
        const std::vector<CppTypeRef>& types) const;

    static std::string templateInstantiation(bool latent, int arity,
                                             std::string_view cppBaseType,
                                             const CppTypeRef& returnType,
                                             const std::vector<CppTypeRef>& parameters);

private:
    std::vector<std::string> structNames_;
};

}  // namespace fo4ide::f4se
