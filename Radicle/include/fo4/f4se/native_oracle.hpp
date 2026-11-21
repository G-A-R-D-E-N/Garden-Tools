#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <fo4/f4se/binding_model.hpp>

namespace fo4::f4se {

struct OracleNative {
    std::string script;
    std::string name;
    PapyrusTypeText returnType;
    std::vector<PapyrusTypeText> parameterTypes;
    bool isGlobal{false};

    int arity() const { return static_cast<int>(parameterTypes.size()); }

    std::string toString() const {
        return returnType.toString() + " " + script + "." + name + "/" +
               std::to_string(arity());
    }
};

class NativeOracle {
public:
    NativeOracle(std::vector<OracleNative> natives, int scriptsRead, int fragmentsRepaired,
                 std::vector<std::string> problems)
        : natives_(std::move(natives)), scriptsRead_(scriptsRead),
          fragmentsRepaired_(fragmentsRepaired), problems_(std::move(problems)) {}

    const std::vector<OracleNative>& natives() const { return natives_; }
    int scriptsRead() const { return scriptsRead_; }
    int fragmentsRepaired() const { return fragmentsRepaired_; }
    const std::vector<std::string>& problems() const { return problems_; }

    const OracleNative* find(const std::string& script, const std::string& name) const;

    static NativeOracle build(const std::filesystem::path& mergedRoot,
                              const std::filesystem::path* vanillaRoot = nullptr);

private:
    std::vector<OracleNative> natives_;
    int scriptsRead_{0};
    int fragmentsRepaired_{0};
    std::vector<std::string> problems_;
};

}
