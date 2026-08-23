// preScript: force the PDB Universal analyzer to use our sibling Fallout4.pdb instead of relying
// on auto-discovery / symbol-server search (headless has no "Edit -> Symbol Server Config" UI).
// This is the officially documented headless hook - see
// ghidra.app.plugin.core.analysis.PdbAnalyzerCommon#findPdb(), which explicitly tells you to call
// PdbUniversalAnalyzer.setPdbFileOption(currentProgram, new File(...)) from a pre-script when
// running headless. Must run as -preScript (before -analyze / the default post-import analysis).
import ghidra.app.script.GhidraScript;
import ghidra.app.plugin.core.analysis.PdbUniversalAnalyzer;
import java.io.File;

public class SetPdbFile extends GhidraScript {
    @Override
    public void run() throws Exception {
        File pdb = new File("E:\\F4SE OG\\ReverseEngineering\\Symbols\\Fallout4.pdb");
        if (!pdb.isFile()) {
            println("!!! PDB file not found at " + pdb.getAbsolutePath());
            return;
        }
        PdbUniversalAnalyzer.setPdbFileOption(currentProgram, pdb);
        println("SetPdbFile: PDB Universal analyzer will use " + pdb.getAbsolutePath()
            + " (" + pdb.length() + " bytes)");
    }
}
