// Debug: inspect the RTTI string at a given address and its references.
// usage: RefDebug.java 0x3c72490
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Data;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.address.Address;

public class RefDebug extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        long imageBase = currentProgram.getImageBase().getOffset();
        for (String arg : args) {
            long rva = Long.parseLong(arg.replace("0x", ""), 16);
            Address a = toAddr(imageBase + rva);
            println("=== addr 0x" + Long.toHexString(rva));
            Data d = currentProgram.getListing().getDataAt(a);
            println("data: " + (d == null ? "none" : d.getDataType().getName() + " = " + d.getDefaultValueRepresentation()));
            ReferenceIterator ri = currentProgram.getReferenceManager().getReferencesTo(a);
            int cnt = 0;
            while (ri.hasNext()) {
                Reference r = ri.next();
                println("  ref from 0x" + Long.toHexString(r.getFromAddress().getOffset() - imageBase)
                        + " type=" + r.getReferenceType() + " isData=" + r.getReferenceType().isData());
                if (++cnt > 20) break;
            }
            if (cnt == 0) println("  (no references)");
            // what does the RTTI analyzer call this area?
            Symbol s = currentProgram.getSymbolTable().getPrimarySymbol(a);
            println("primary symbol: " + (s == null ? "none" : s.getName()));
        }
    }
}
