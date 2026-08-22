// Headless query script for the PDB-named FO4 1.10.155 Ghidra DB (see ../ANALYSIS.md).
// Identical logic to ReverseEngineering/Scripts/FW_Query.java (the 1.10.163 pipeline's query
// script) - kept as its own copy here since this is a separate project/binary version and the two
// pipelines are intentionally not cross-wired.
//
// Usage args: a list of search terms. Each plain term => substring search over function names.
// A term prefixed "decompile:" => decompile that exact function name (full match) and print C.
// A term prefixed "addr:0xRVA" (optionally ",decompile") => resolve an RVA to a function.
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.FunctionManager;

public class PDB_Query extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        FunctionManager fm = currentProgram.getFunctionManager();
        long imageBase = currentProgram.getImageBase().getOffset();
        println("=== IMAGE BASE: 0x" + Long.toHexString(imageBase) + " ===");

        for (String term : args) {
            if (term.startsWith("addr:")) {
                String rest = term.substring("addr:".length());
                boolean wantDecompile = rest.endsWith(",decompile");
                String hex = wantDecompile ? rest.substring(0, rest.length() - ",decompile".length()) : rest;
                hex = hex.replace("0x", "").replace("0X", "");
                long rva = Long.parseLong(hex, 16);
                long addr = imageBase + rva;
                Function f = fm.getFunctionContaining(toAddr(addr));
                if (f == null) {
                    println("### ADDR 0x" + Long.toHexString(rva) + ": no function contains 0x" + Long.toHexString(addr));
                    continue;
                }
                long ep = f.getEntryPoint().getOffset();
                println("### ADDR 0x" + Long.toHexString(rva) + " -> " + f.getName()
                        + "  @ 0x" + Long.toHexString(ep) + "  (fn RVA 0x" + Long.toHexString(ep - imageBase) + ")"
                        + "  +0x" + Long.toHexString(addr - ep) + "  :: " + f.getSignature().getPrototypeString());
                if (wantDecompile) {
                    DecompInterface di = new DecompInterface();
                    di.openProgram(currentProgram);
                    DecompileResults res = di.decompileFunction(f, 60, monitor);
                    if (res != null && res.decompileCompleted()) {
                        String c = res.getDecompiledFunction().getC();
                        for (String line : c.split("\n")) println(line);
                    } else {
                        println("  (decompile failed)");
                    }
                    di.dispose();
                }
                continue;
            }
            if (term.startsWith("decompile:")) {
                // Match against either the short name ("CheckBorderRegion") or the fully-qualified
                // name ("PlayerCharacter::CheckBorderRegion") - this PDB-named DB uses C++ namespaces
                // heavily, so f.getName() alone (no "::") almost never matches a qualified query.
                String want = term.substring("decompile:".length());
                Function target = null;
                FunctionIterator it = fm.getFunctions(true);
                while (it.hasNext()) {
                    Function f = it.next();
                    if (f.getName().equals(want) || f.getName(true).equals(want)) { target = f; break; }
                }
                if (target == null) { println("### DECOMPILE: no exact match for " + want); continue; }
                long ep = target.getEntryPoint().getOffset();
                println("### DECOMPILE: " + target.getName()
                        + "  @ 0x" + Long.toHexString(ep)
                        + "  (RVA 0x" + Long.toHexString(ep - imageBase) + ")");
                DecompInterface di = new DecompInterface();
                di.openProgram(currentProgram);
                DecompileResults res = di.decompileFunction(target, 60, monitor);
                if (res != null && res.decompileCompleted()) {
                    String c = res.getDecompiledFunction().getC();
                    for (String line : c.split("\n")) println(line);
                } else {
                    println("  (decompile failed)");
                }
                di.dispose();
                continue;
            }

            println("### SEARCH: " + term);
            String lt = term.toLowerCase();
            int count = 0;
            FunctionIterator it = fm.getFunctions(true);
            while (it.hasNext()) {
                Function f = it.next();
                // Match against the fully-qualified name so "PlayerCharacter::Check" finds
                // CheckBorderRegion even though f.getName() alone drops the namespace.
                if (f.getName(true).toLowerCase().contains(lt)) {
                    long ep = f.getEntryPoint().getOffset();
                    println("  " + f.getName(true)
                            + "  @ 0x" + Long.toHexString(ep)
                            + "  (RVA 0x" + Long.toHexString(ep - imageBase) + ")  :: "
                            + f.getSignature().getPrototypeString());
                    if (++count >= 60) { println("  ... (truncated at 60)"); break; }
                }
            }
            println("### MATCHES(" + term + "): " + count);
        }
    }
}
