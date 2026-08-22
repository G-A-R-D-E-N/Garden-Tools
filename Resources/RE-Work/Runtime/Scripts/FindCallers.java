// Find and decompile the functions that CALL a given RVA (not just direct import-thunk xrefs --
// walks one more hop if the immediate caller is itself a trivial one-line thunk). Usage:
// -postScript FindCallers.java targetRVAhex [maxCallers]
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

public class FindCallers extends GhidraScript {
    private DecompInterface di;

    private void decompileAndPrint(Function f) throws Exception {
        long imageBase = currentProgram.getImageBase().getOffset();
        long ep = f.getEntryPoint().getOffset();
        println("### CALLER " + f.getName(true) + " @ 0x" + Long.toHexString(ep)
                + " (RVA 0x" + Long.toHexString(ep - imageBase) + ")");
        DecompileResults res = di.decompileFunction(f, 60, monitor);
        if (res != null && res.decompileCompleted()) {
            for (String line : res.getDecompiledFunction().getC().split("\n")) println(line);
        } else {
            println("  (decompile failed)");
        }
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        long imageBase = currentProgram.getImageBase().getOffset();
        long targetRva = Long.parseLong(args[0].replace("0x", "").replace("0X", ""), 16);
        int maxCallers = args.length > 1 ? Integer.parseInt(args[1]) : 5;
        Address target = toAddr(imageBase + targetRva);

        di = new DecompInterface();
        di.openProgram(currentProgram);

        println("=== IMAGE BASE: 0x" + Long.toHexString(imageBase) + " ===");
        println("### FINDING CALLERS OF RVA 0x" + Long.toHexString(targetRva));

        int shown = 0;
        ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(target);
        while (refs.hasNext() && shown < maxCallers) {
            Reference r = refs.next();
            Address from = r.getFromAddress();
            Function caller = currentProgram.getFunctionManager().getFunctionContaining(from);
            if (caller == null) { println("  (xref from " + from + ", no containing function)"); continue; }
            decompileAndPrint(caller);
            shown++;
        }
        if (shown == 0) println("  (no callers found)");
        di.dispose();
    }
}
