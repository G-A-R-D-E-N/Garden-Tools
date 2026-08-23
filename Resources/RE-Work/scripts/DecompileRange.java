// Decompile every function whose entry point lies within an RVA range.
// Usage args (exactly one expected):
//   range:0xSTART:0xEND   -> decompile all functions with entry RVA in [START, END)
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.FunctionManager;

public class DecompileRange extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 1 || !args[0].startsWith("range:")) {
            println("### usage: range:0xSTART:0xEND");
            return;
        }
        String[] parts = args[0].substring("range:".length()).split(":");
        long start = Long.parseLong(parts[0].replace("0x", ""), 16);
        long end = Long.parseLong(parts[1].replace("0x", ""), 16);
        long imageBase = currentProgram.getImageBase().getOffset();
        FunctionManager fm = currentProgram.getFunctionManager();

        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);

        int n = 0;
        FunctionIterator it = fm.getFunctions(true);
        while (it.hasNext()) {
            Function f = it.next();
            long rva = f.getEntryPoint().getOffset() - imageBase;
            if (rva < start || rva >= end) continue;
            ++n;
            println("### FUNC " + f.getName() + " RVA 0x" + Long.toHexString(rva)
                    + " size=" + f.getBody().getNumAddresses()
                    + " :: " + f.getSignature().getPrototypeString());
            DecompileResults res = di.decompileFunction(f, 120, monitor);
            if (res != null && res.decompileCompleted()) {
                println(res.getDecompiledFunction().getC());
            } else {
                println("### decompile failed: "
                        + (res == null ? "null" : res.getErrorMessage()));
            }
            println("");
        }
        println("=== RANGE 0x" + Long.toHexString(start) + "-0x" + Long.toHexString(end)
                + ": " + n + " function(s) ===");
    }
}
