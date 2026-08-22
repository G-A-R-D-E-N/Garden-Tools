// Reverse xrefs: which functions reference a given ADDRESS (data global or function).
// Usage: -postScript RefsTo.java aRVAhex [anotherRVAhex ...]
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

public class RefsTo extends GhidraScript {
    @Override
    public void run() throws Exception {
        long imageBase = currentProgram.getImageBase().getOffset();
        for (String arg : getScriptArgs()) {
            long rva = Long.parseLong(arg.replace("0x", "").replace("0X", ""), 16);
            if (rva > imageBase) rva -= imageBase;
            Address target = currentProgram.getImageBase().add(rva);
            println("### REFS TO " + target + " (RVA 0x" + Long.toHexString(rva) + ")");
            ReferenceIterator it = currentProgram.getReferenceManager().getReferencesTo(target);
            int n = 0;
            while (it.hasNext()) {
                Reference r = it.next();
                Address from = r.getFromAddress();
                Function f = getFunctionContaining(from);
                println("  " + from + "  " + r.getReferenceType() + "  in "
                        + (f != null ? f.getName() + " @ RVA 0x"
                                       + Long.toHexString(f.getEntryPoint().getOffset() - imageBase)
                                     : "<no function>"));
                ++n;
            }
            println("### COUNT: " + n);
        }
    }
}
