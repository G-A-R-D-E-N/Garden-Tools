// FindStringXrefs.java — find a string and list code xrefs to it.
// Usage: analyzeHeadless ... -postScript FindStringXrefs.java "<exact string>"
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.address.Address;

public class FindStringXrefs extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) { println("usage: FindStringXrefs <exact string>"); return; }
        String want = args[0];
        long imageBase = currentProgram.getImageBase().getOffset();
        FunctionManager fm = currentProgram.getFunctionManager();

        Data target = null;
        DataIterator ddi = currentProgram.getListing().getDefinedData(true);
        while (ddi.hasNext()) {
            Data d = ddi.next();
            if (d.hasStringValue()) {
                String s = d.getDefaultValueRepresentation().replace("\"", "");
                if (s.equals(want)) { target = d; break; }
            }
        }
        if (target == null) { println("string not found: " + want); return; }
        println("STRING @" + target.getAddress() + " (RVA 0x" + Long.toHexString(target.getAddress().getOffset() - imageBase) + ")");

        int n = 0;
        ReferenceIterator ri = currentProgram.getReferenceManager().getReferencesTo(target.getAddress());
        while (ri.hasNext()) {
            Reference r = ri.next();
            Address from = r.getFromAddress();
            Function f = fm.getFunctionContaining(from);
            if (f != null) {
                long rva = f.getEntryPoint().getOffset() - imageBase;
                println("  XREF 0x" + Long.toHexString(rva) + " " + f.getName() + "  (+0x" + Long.toHexString(from.getOffset() - f.getEntryPoint().getOffset()) + ")");
                n++;
            } else {
                println("  XREF (data) 0x" + Long.toHexString(from.getOffset() - imageBase));
                n++;
            }
            if (n > 100) { println("(truncated)"); break; }
        }
    }
}
