// Find defined strings whose content matches a substring, and list the functions that reference them.
// Usage: -postScript FindStringXrefs.java "[...]" [more...]
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

public class FindStringXrefs extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        long base = currentProgram.getImageBase().getOffset();
        DataIterator it = currentProgram.getListing().getDefinedData(true);
        while (it.hasNext()) {
            Data d = it.next();
            Object v = d.getValue();
            if (v == null) continue;
            String s = v.toString();
            for (String needle : args) {
                if (!s.contains(needle)) continue;
                Address a = d.getAddress();
                println("STRING @ RVA 0x" + Long.toHexString(a.getOffset() - base) + " : \"" + s + "\"");
                ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(a);
                while (refs.hasNext()) {
                    Reference r = refs.next();
                    Function f = getFunctionContaining(r.getFromAddress());
                    println("   xref from 0x" + Long.toHexString(r.getFromAddress().getOffset() - base)
                            + (f != null ? "  in  " + f.getName() + " (RVA 0x"
                                    + Long.toHexString(f.getEntryPoint().getOffset() - base) + ")" : ""));
                }
            }
        }
        println("done");
    }
}
