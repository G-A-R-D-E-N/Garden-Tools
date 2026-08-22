// Show which instructions reference given record addresses (RVAs).
// usage: CheckRefs.java 0x3b9d8c8 0x3b9d8e0 ...
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.address.Address;
import ghidra.program.model.symbol.Reference;

public class CheckRefs extends GhidraScript {
    @Override
    public void run() throws Exception {
        long base = currentProgram.getImageBase().getOffset();
        for (String arg : getScriptArgs()) {
            long rva = Long.parseLong(arg.replace("0x", ""), 16);
            Address a = toAddr(base + rva);
            println("=== 0x" + Long.toHexString(rva) + " ===");
            int n = 0;
            for (Reference r : currentProgram.getReferenceManager().getReferencesTo(a)) {
                Address f = r.getFromAddress();
                Instruction ins = currentProgram.getListing().getInstructionAt(f);
                if (ins != null) {
                    println("  insn 0x" + Long.toHexString(f.getOffset() - base) + ": " + ins.toString()
                            + "  type=" + r.getReferenceType());
                    if (++n >= 6) break;
                }
            }
            if (n == 0) println("  (no instruction refs)");
        }
    }
}
