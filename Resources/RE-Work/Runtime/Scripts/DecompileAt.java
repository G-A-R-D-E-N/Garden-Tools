// Decompile the function containing a given RVA (hex, no 0x needed). No comma-splitting issues.
// Usage: -postScript DecompileAt.java aRVAhex [anotherRVAhex ...]
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.listing.Function;

public class DecompileAt extends GhidraScript {
    @Override
    public void run() throws Exception {
        long imageBase = currentProgram.getImageBase().getOffset();
        for (String arg : getScriptArgs()) {
            String hex = arg.replace("0x", "").replace("0X", "");
            long rva = Long.parseLong(hex, 16);
            long addr = imageBase + rva;
            Function f = currentProgram.getFunctionManager().getFunctionContaining(toAddr(addr));
            if (f == null) { println("### 0x" + hex + ": no function here"); continue; }
            long ep = f.getEntryPoint().getOffset();
            println("### DECOMPILE " + f.getName(true) + " @ 0x" + Long.toHexString(ep)
                    + " (RVA 0x" + Long.toHexString(ep - imageBase) + ")");
            DecompInterface di = new DecompInterface();
            di.openProgram(currentProgram);
            DecompileResults res = di.decompileFunction(f, 60, monitor);
            if (res != null && res.decompileCompleted()) {
                for (String line : res.getDecompiledFunction().getC().split("\n")) println(line);
            } else {
                println("  (decompile failed)");
            }
            di.dispose();
        }
    }
}
