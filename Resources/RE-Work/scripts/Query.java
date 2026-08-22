// Headless query script for the CreationKit.exe Ghidra project.
// Usage args (repeatable, space separated):
//   <plain term>            => substring search over function names + defined strings
//   addr:0xRVA              => print function containing the RVA (offset from image base)
//   addr:0xRVA,decompile    => also decompile that function
//   decompile:<name>        => full-name match decompile
//   strings                 => dump every defined string with address
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolTable;
import ghidra.program.model.symbol.SymbolIterator;

public class Query extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        FunctionManager fm = currentProgram.getFunctionManager();
        long imageBase = currentProgram.getImageBase().getOffset();
        println("=== IMAGE BASE: 0x" + Long.toHexString(imageBase) + " ===");

        for (String term : args) {
            if (term.equals("strings")) {
                int n = 0;
                DataIterator di = currentProgram.getListing().getDefinedData(true);
                while (di.hasNext()) {
                    Data d = di.next();
                    if (d.hasStringValue()) {
                        long rva = d.getAddress().getOffset() - imageBase;
                        println("STRING 0x" + Long.toHexString(rva) + " = " + d.getDefaultValueRepresentation());
                        if (++n > 2000) { println("(truncated at 2000)"); break; }
                    }
                }
                continue;
            }
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
                        for (String line : res.getDecompiledFunction().getC().split("\n")) println(line);
                    } else {
                        println("  (decompile failed)");
                    }
                    di.dispose();
                }
                continue;
            }
            if (term.startsWith("decompile:")) {
                String want = term.substring("decompile:".length());
                FunctionIterator it = fm.getFunctions(true);
                while (it.hasNext()) {
                    Function f = it.next();
                    if (f.getName().equals(want)) {
                        DecompInterface di = new DecompInterface();
                        di.openProgram(currentProgram);
                        DecompileResults res = di.decompileFunction(f, 60, monitor);
                        if (res != null && res.decompileCompleted()) {
                            for (String line : res.getDecompiledFunction().getC().split("\n")) println(line);
                        } else {
                            println("  (decompile failed)");
                        }
                        di.dispose();
                        break;
                    }
                }
                continue;
            }
            // substring search: function names then defined strings
            int hits = 0;
            FunctionIterator it = fm.getFunctions(true);
            while (it.hasNext()) {
                Function f = it.next();
                if (f.getName().toLowerCase().contains(term.toLowerCase())) {
                    long rva = f.getEntryPoint().getOffset() - imageBase;
                    println("FN 0x" + Long.toHexString(rva) + " " + f.getName() + " :: " + f.getSignature().getPrototypeString());
                    if (++hits > 400) { println("(truncated)"); break; }
                }
            }
            if (hits == 0) {
                DataIterator ddi = currentProgram.getListing().getDefinedData(true);
                while (ddi.hasNext()) {
                    Data d = ddi.next();
                    if (d.hasStringValue()) {
                        String s = d.getDefaultValueRepresentation();
                        if (s.toLowerCase().contains(term.toLowerCase())) {
                            long rva = d.getAddress().getOffset() - imageBase;
                            println("STR 0x" + Long.toHexString(rva) + " = " + s);
                            if (++hits > 100) break;
                        }
                    }
                }
            }
        }
    }
}
