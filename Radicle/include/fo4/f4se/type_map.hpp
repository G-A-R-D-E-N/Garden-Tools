#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::f4se {

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

struct PapyrusTypeText {
    std::string name;
    bool isArray{false};

    std::string toString() const { return isArray ? name + "[]" : name; }
};

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

    static std::optional<CppTypeRef> tryParse(std::string_view text);

    static CppTypeRef parse(std::string_view text);
};

class F4SETypeMap {
public:
    static constexpr const char* kStaticFunctionTag = "StaticFunctionTag";

    explicit F4SETypeMap(std::vector<std::string> structNames);

    const std::vector<std::string>& structNames() const { return structNames_; }

    static std::map<std::string, std::string, CiLess> formTypes();

    static const std::vector<std::string>& coreIncludes();
    static const std::vector<std::string>& gameIncludes();

    bool tryToCpp(const PapyrusTypeText& papyrus, bool isUnsigned,
                  std::optional<CppTypeRef>& outCpp, std::string* refusal) const;

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

}
