// List every function whose entry point falls within [startRVA, endRVA), sorted by address.
// Usage: -postScript ListFuncsInRange.java startRVAhex endRVAhex
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.FunctionManager;

public class ListFuncsInRange extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        long imageBase = currentProgram.getImageBase().getOffset();
        long start = Long.parseLong(args[0].replace("0x", "").replace("0X", ""), 16);
        long end   = Long.parseLong(args[1].replace("0x", "").replace("0X", ""), 16);
        FunctionManager fm = currentProgram.getFunctionManager();
        FunctionIterator it = fm.getFunctions(toAddr(imageBase + start), true);
        int count = 0;
        while (it.hasNext()) {
            Function f = it.next();
            long ep = f.getEntryPoint().getOffset();
            long rva = ep - imageBase;
            if (rva >= end) break;
            println("  " + f.getName(true) + "  @ 0x" + Long.toHexString(ep)
                    + "  (RVA 0x" + Long.toHexString(rva) + ")  size=" + f.getBody().getNumAddresses()
                    + "  :: " + f.getSignature().getPrototypeString());
            if (++count >= 500) { println("  ... (truncated at 500)"); break; }
        }
        println("### TOTAL: " + count);
    }
}
