// Search DEFINED STRINGS (not function/symbol names -- FW_Query.java only searches those) for a
// substring, and for each hit, print its address + the functions that reference it (a string used
// only inside one function is a strong clue that function is the real target, e.g. a Scaleform font
// name like "$ControllerButtonsInverted" referenced only by whatever code selects glyph characters
// for that font).
// Usage: -postScript SearchStrings.java <substring> [<substring2> ...]
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;

public class SearchStrings extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        long imageBase = currentProgram.getImageBase().getOffset();

        for (String needle : args) {
            String lower = needle.toLowerCase();
            println("### STRING SEARCH: " + needle);
            int count = 0;
            DataIterator it = currentProgram.getListing().getDefinedData(true);
            while (it.hasNext()) {
                Data d = it.next();
                if (!d.hasStringValue()) continue;
                Object val = d.getValue();
                if (val == null) continue;
                String s = val.toString();
                if (!s.toLowerCase().contains(lower)) continue;
                Address addr = d.getAddress();
                println("  \"" + s + "\"  @ 0x" + Long.toHexString(addr.getOffset())
                        + "  (RVA 0x" + Long.toHexString(addr.getOffset() - imageBase) + ")");
                Reference[] refs = getReferencesTo(addr);
                int refCount = 0;
                for (Reference r : refs) {
                    if (refCount >= 10) break;
                    Address from = r.getFromAddress();
                    Function f = getFunctionContaining(from);
                    println("      xref from 0x" + Long.toHexString(from.getOffset())
                            + (f != null ? "  in " + f.getName(true)
                                           + "  (fn RVA 0x" + Long.toHexString(f.getEntryPoint().getOffset() - imageBase) + ")"
                                         : "  (no containing function)"));
                    refCount++;
                }
                if (++count >= 40) { println("  ... (truncated at 40)"); break; }
            }
            println("### MATCHES(" + needle + "): " + count);
        }
    }
}
