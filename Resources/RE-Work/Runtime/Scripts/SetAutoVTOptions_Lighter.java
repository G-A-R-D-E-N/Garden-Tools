// Lighter AutoVersionTracking options for this workspace: the default duplicate-function and
// exact-function-instruction correlators OOM'd (java.lang.OutOfMemoryError: Java heap space in
// FunctionMatchProgramCorrelator.doCorrelate) correlating 1.11.191.0 -> 1.11.221.0 on this machine.
// Disables those two; keeps exact symbol/data/function-bytes + ref correlators, which are cheaper
// and sufficient for our actual targets (exact RTTI-named functions/vtables, not fuzzy duplicates).
//@category Examples.Version Tracking
import ghidra.app.script.GhidraScript;
import ghidra.feature.vt.gui.util.VTOptionDefines;
import ghidra.features.base.values.GhidraValuesMap;

public class SetAutoVTOptions_Lighter extends GhidraScript {

    @Override
    public void run() throws Exception {
        GhidraValuesMap optionsMap = getOptions();
        state.addEnvironmentVar("autoVTOptionsMap", optionsMap);
    }

    private GhidraValuesMap getOptions() {
        GhidraValuesMap optionsValues = new GhidraValuesMap();

        optionsValues.defineBoolean(VTOptionDefines.CREATE_IMPLIED_MATCHES_OPTION_TEXT, true);
        optionsValues.defineBoolean(VTOptionDefines.RUN_EXACT_SYMBOL_OPTION_TEXT, true);
        optionsValues.defineBoolean(VTOptionDefines.RUN_EXACT_DATA_OPTION_TEXT, true);
        optionsValues.defineBoolean(VTOptionDefines.RUN_EXACT_FUNCTION_BYTES_OPTION_TEXT, true);

        // Disabled: OOM'd last run.
        optionsValues.defineBoolean(VTOptionDefines.RUN_EXACT_FUNCTION_INST_OPTION_TEXT, false);
        optionsValues.defineBoolean(VTOptionDefines.RUN_DUPE_FUNCTION_OPTION_TEXT, false);

        optionsValues.defineBoolean(VTOptionDefines.RUN_REF_CORRELATORS_OPTION_TEXT, true);

        optionsValues.defineInt(VTOptionDefines.DATA_CORRELATOR_MIN_LEN_OPTION_TEXT, 5);
        optionsValues.defineInt(VTOptionDefines.SYMBOL_CORRELATOR_MIN_LEN_OPTION_TEXT, 3);
        optionsValues.defineInt(VTOptionDefines.FUNCTION_CORRELATOR_MIN_LEN_OPTION_TEXT, 10);
        optionsValues.defineInt(VTOptionDefines.DUPE_FUNCTION_CORRELATOR_MIN_LEN_OPTION_TEXT, 10);

        optionsValues.defineBoolean(VTOptionDefines.APPLY_IMPLIED_MATCHES_OPTION_TEXT, true);
        optionsValues.defineInt(VTOptionDefines.MIN_VOTES_OPTION_TEXT, 2);
        optionsValues.defineInt(VTOptionDefines.MAX_CONFLICTS_OPTION_TEXT, 0);
        optionsValues.defineDouble(VTOptionDefines.REF_CORRELATOR_MIN_SCORE_OPTION_TEXT, 0.95);
        optionsValues.defineDouble(VTOptionDefines.REF_CORRELATOR_MIN_CONF_OPTION_TEXT, 10.0);

        return optionsValues;
    }
}
