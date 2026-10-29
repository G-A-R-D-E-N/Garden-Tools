#pragma once

#include <string>
#include <vector>

#include <fo4/f4se/binding_model.hpp>
#include <fo4/f4se/native_oracle.hpp>

namespace fo4::f4se {

struct CrossCheckMismatch {
    std::string cls;
    std::string function;
    std::string what;
    std::string cpp;
    std::string psc;

    std::string toString() const {
        return cls + "." + function + " " + what + ": cpp=" + cpp + " psc=" + psc;
    }
};

struct CrossCheckResult {
    int recovered{0};
    int declared{0};
    int matched{0};
    std::vector<NativeBinding> cppOnly;
    std::vector<OracleNative> pscOnly;
    std::vector<CrossCheckMismatch> mismatches;
    int latentOnlyInCpp{0};
    int noWaitOnlyInCpp{0};

    bool agrees() const {
        return cppOnly.empty() && pscOnly.empty() && mismatches.empty();
    }
};

class CrossCheck {
public:
    static CrossCheckResult compare(const std::vector<NativeBinding>& recovered,
                                    const NativeOracle& oracle) {
        return compare(recovered, oracle.natives());
    }

    static CrossCheckResult compare(const std::vector<NativeBinding>& recovered,
                                    const std::vector<OracleNative>& declarations);
};

}
