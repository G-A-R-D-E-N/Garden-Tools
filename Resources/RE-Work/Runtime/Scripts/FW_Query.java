// Headless query script for the named FO4 1.10.163 Ghidra DB.
// Usage args: a list of search terms. Each plain term => substring search over function names.
// A term prefixed "decompile:" => decompile that exact function name (full match) and print C.
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.FunctionManager;

public class FW_Query extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        FunctionManager fm = currentProgram.getFunctionManager();
        long imageBase = currentProgram.getImageBase().getOffset();
        println("=== IMAGE BASE: 0x" + Long.toHexString(imageBase) + " ===");

        for (String term : args) {
            // "addrdc:0xRVA" == "addr:0xRVA,decompile" but with no comma, since the batch-file
            // wrapper (analyzeHeadless.bat) re-splits args on commas via a FOR loop's default
            // delimiter set before this script ever sees them. See crash-investigations.md
            // 2026-07-07 pt1 toolchain notes for the history of that gotcha.
            if (term.startsWith("addrdc:")) {
                term = "addr:" + term.substring("addrdc:".length()) + ",decompile";
            }
            if (term.startsWith("addr:")) {
                // term = "addr:0xRVA" (offset from image base) or "addr:0xRVA,decompile"
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
                String want = term.substring("decompile:".length());
                Function target = null;
                FunctionIterator it = fm.getFunctions(true);
                while (it.hasNext()) {
                    Function f = it.next();
                    if (f.getName().equals(want)) { target = f; break; }
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
                    // Print line-by-line so each line gets the script log prefix (a single
                    // multi-line println only prefixes its first line, which breaks log filters).
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
                if (f.getName().toLowerCase().contains(lt)) {
                    long ep = f.getEntryPoint().getOffset();
                    println("  " + f.getName()
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
