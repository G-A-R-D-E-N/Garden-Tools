// Full function export for the named FO4 1.10.163 Ghidra DB (fwgh_proj_1.10.163_unpacked).
// Dumps every function's RVA, byte size, and prototype string to a flat text file in a format
// close enough to ReverseEngineering\Symbols\Fallout4_functions.txt (the 1.10.155 PDB dump) that the same
// subsystem-bucketing script can parse both. Pure symbol-table/listing walk -- no decompile calls,
// no re-analysis -- so this should run in a couple of minutes even at ~210k functions.
//
// Usage: -postScript FW_ExportAll.java [outPath]
//   outPath defaults to E:\F4SE OG\ReverseEngineering\Symbols\1.10.163\Fallout4_163_functions.txt
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.FunctionManager;
import java.io.BufferedWriter;
import java.io.FileWriter;

public class FW_ExportAll extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        String outPath = (args.length > 0 && !args[0].isEmpty())
            ? args[0]
            : "E:\\F4SE OG\\ReverseEngineering\\Symbols\\1.10.163\\Fallout4_163_functions.txt";

        FunctionManager fm = currentProgram.getFunctionManager();
        long imageBase = currentProgram.getImageBase().getOffset();

        try (BufferedWriter w = new BufferedWriter(new FileWriter(outPath))) {
            w.write("# Fallout4.exe.unpacked.exe 1.10.163 OG -- full function export from fwgh_proj_1.10.163_unpacked\n");
            w.write("# image base 0x" + Long.toHexString(imageBase) + "\n");
            w.write("# format: func [0xRVA | sizeof=N] <prototype string>\n");
            w.write("---GLOBALS---\n");

            FunctionIterator it = fm.getFunctions(true);
            long count = 0;
            while (it.hasNext()) {
                Function f = it.next();
                long ep = f.getEntryPoint().getOffset();
                long rva = ep - imageBase;
                long size = f.getBody().getNumAddresses();
                String proto;
                try {
                    proto = f.getSignature().getPrototypeString();
                } catch (Exception e) {
                    proto = f.getName(true) + "(...)";
                }
                // getSignature().getPrototypeString() does NOT qualify member-function names with
                // their class (e.g. "void GetHeight(BGSAIWorldLocation * this)", not
                // "BGSAIWorldLocation::GetHeight"), unlike the 1.10.155 PDB dump's demangled
                // signatures which always are. Emit f.getName(true) explicitly so downstream
                // subsystem-bucketing has one reliable "Class::Method" field to key off, matching
                // the 1.10.155 format.
                w.write("  func [0x" + Long.toHexString(rva) + " | sizeof=" + size + "] " + f.getName(true)
                        + " :: " + proto + "\n");
                count++;
                if (count % 25000 == 0) {
                    w.flush();
                    println("... exported " + count);
                }
            }
            w.write("# TOTAL: " + count + " functions\n");
            println("Exported " + count + " functions to " + outPath);
        }
    }
}
