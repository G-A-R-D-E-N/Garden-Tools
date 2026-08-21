// Disassemble a function (or raw range) and print the listing.
// usage: DisasmAt.java 0x4870f0 [0x4885fd]   (RVAs; range optional)
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;

public class DisasmAt extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        long base = currentProgram.getImageBase().getOffset();
        long startRva = Long.parseLong(args[0].replace("0x", ""), 16);
        long endRva = args.length > 1 ? Long.parseLong(args[1].replace("0x", ""), 16) : -1;
        Address start = toAddr(base + startRva);
        Address end = endRva >= 0 ? toAddr(base + endRva) : null;
        InstructionIterator it = currentProgram.getListing().getInstructions(start, end != null);
        int n = 0;
        while (it.hasNext()) {
            Instruction ins = it.next();
            if (end != null && ins.getAddress().getOffset() > end.getOffset()) break;
            String refs = "";
            ghidra.program.model.symbol.Reference[] ri =
                    currentProgram.getReferenceManager().getReferencesFrom(ins.getAddress());
            if (ri.length > 0) {
                long t = ri[0].getToAddress().getOffset();
                if (t > 0x140000000L) {
                    refs = "  ; -> 0x" + Long.toHexString(t - base);
                }
            }
            println(String.format("0x%08x  %s%s", ins.getAddress().getOffset() - base,
                    ins.toString(), refs));
            if (++n > 2000) { println("(truncated)"); break; }
        }
        println("instructions: " + n);
    }
}
