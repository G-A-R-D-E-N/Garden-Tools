// Lists imported (external) symbols whose name contains any of the given substrings, and for each
// match, lists the internal functions that call it (xrefs to the import thunk), decompiling the first
// few callers. Usage: substrings to match against import names, e.g. "RegisterRawInputDevices" "ClipCursor".
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;
import ghidra.program.model.symbol.SymbolTable;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.address.Address;

public class FW_Imports extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        long imageBase = currentProgram.getImageBase().getOffset();
        SymbolTable st = currentProgram.getSymbolTable();
        println("=== IMAGE BASE: 0x" + Long.toHexString(imageBase) + " ===");

        for (String term : args) {
            String lt = term.toLowerCase();
            println("### IMPORT SEARCH: " + term);
            int count = 0;
            SymbolIterator it = st.getSymbolIterator();
            while (it.hasNext()) {
                Symbol sym = it.next();
                if (!sym.isExternal() && !sym.getName().toLowerCase().contains(lt)) continue;
                if (!sym.getName().toLowerCase().contains(lt)) continue;
                if (!sym.isExternal()) continue;
                count++;
                Address addr = sym.getAddress();
                println("  IMPORT: " + sym.getName() + " @ " + addr);
                ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(addr);
                int callerCount = 0;
                while (refs.hasNext()) {
                    Reference r = refs.next();
                    Address from = r.getFromAddress();
                    Function caller = currentProgram.getFunctionManager().getFunctionContaining(from);
                    String callerName = caller != null ? caller.getName() : "(no function)";
                    long callerEp = caller != null ? caller.getEntryPoint().getOffset() : 0;
                    println("    called from " + callerName + " @ 0x" + Long.toHexString(from.getOffset())
                            + " (fn RVA 0x" + Long.toHexString(callerEp - imageBase) + ")");
                    callerCount++;
                    if (callerCount <= 3 && caller != null) {
                        DecompInterface di = new DecompInterface();
                        di.openProgram(currentProgram);
                        DecompileResults res = di.decompileFunction(caller, 60, monitor);
                        if (res != null && res.decompileCompleted()) {
                            String c = res.getDecompiledFunction().getC();
                            for (String line : c.split("\n")) println("      " + line);
                        }
                        di.dispose();
                    }
                    if (callerCount >= 8) { println("    ... (truncated at 8 callers)"); break; }
                }
            }
            if (count == 0) println("  (no external/import symbol matched)");
            println("### MATCHES(" + term + "): " + count);
        }
    }
}
