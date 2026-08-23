// FindCallers.java — list functions calling a target function (by RVA).
// Usage: analyzeHeadless ... -postScript FindCallers.java 0xRVA
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.address.Address;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

public class FindCallers extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) { println("usage: FindCallers 0xRVA"); return; }
        String hex = args[0].replace("0x", "").replace("0X", "");
        long imageBase = currentProgram.getImageBase().getOffset();
        long target = Long.parseLong(hex, 16) + imageBase;
        FunctionManager fm = currentProgram.getFunctionManager();

        Function tf = fm.getFunctionContaining(toAddr(target));
        if (tf == null) { println("no function at 0x" + Long.toHexString(target)); return; }
        println("TARGET " + tf.getName() + " @ 0x" + Long.toHexString(tf.getEntryPoint().getOffset() - imageBase));

        int n = 0;
        ReferenceIterator ri = currentProgram.getReferenceManager().getReferencesTo(tf.getEntryPoint());
        while (ri.hasNext()) {
            Reference r = ri.next();
            Address from = r.getFromAddress();
            Function f = fm.getFunctionContaining(from);
            if (f != null) {
                println("  CALLER 0x" + Long.toHexString(f.getEntryPoint().getOffset() - imageBase) + " " + f.getName()
                        + "  (+0x" + Long.toHexString(from.getOffset() - f.getEntryPoint().getOffset()) + ")");
                n++;
            } else {
                println("  REF (data/unknown) 0x" + Long.toHexString(from.getOffset() - imageBase));
                n++;
            }
            if (n > 120) { println("(truncated)"); break; }
        }
    }
}
