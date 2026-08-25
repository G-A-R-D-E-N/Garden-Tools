#pragma once

#include <string>
#include <vector>

#include <fo4ide/f4se/BindingModel.hpp>
#include <fo4ide/f4se/NativeOracle.hpp>

namespace fo4ide::f4se {

// Port of CrossCheckMismatch.
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

// Port of CrossCheckResult.
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

// Port of F4SECrossCheck: compares recovered registrations against oracle
// declarations by (class, function), arity, global flag, return type and
// parameter types.
class CrossCheck {
public:
    static CrossCheckResult compare(const std::vector<NativeBinding>& recovered,
                                    const NativeOracle& oracle) {
        return compare(recovered, oracle.natives());
    }

    static CrossCheckResult compare(const std::vector<NativeBinding>& recovered,
                                    const std::vector<OracleNative>& declarations);
};

}  // namespace fo4ide::f4se
