// Define (disassemble+createFunction) then decompile the function at a given RVA (hex).
// Usage: -postScript DecompileDefine.java aRVAhex [anotherRVAhex ...]
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

public class DecompileDefine extends GhidraScript {
    @Override
    public void run() throws Exception {
        long imageBase = currentProgram.getImageBase().getOffset();
        for (String arg : getScriptArgs()) {
            String hex = arg.replace("0x", "").replace("0X", "");
            long rva = Long.parseLong(hex, 16);
            Address addr = toAddr(imageBase + rva);
            Function f = currentProgram.getFunctionManager().getFunctionContaining(addr);
            if (f == null) {
                try { disassemble(addr); } catch (Exception e) { println("  disasm err: " + e); }
                try { f = createFunction(addr, null); } catch (Exception e) { println("  createFn err: " + e); }
                if (f == null) f = currentProgram.getFunctionManager().getFunctionContaining(addr);
            }
            if (f == null) { println("### 0x" + hex + ": could not define function"); continue; }
            long ep = f.getEntryPoint().getOffset();
            println("### DECOMPILE " + f.getName(true) + " @ 0x" + Long.toHexString(ep)
                    + " (RVA 0x" + Long.toHexString(ep - imageBase) + ")");
            DecompInterface di = new DecompInterface();
            di.openProgram(currentProgram);
            DecompileResults res = di.decompileFunction(f, 120, monitor);
            if (res != null && res.decompileCompleted()) {
                for (String line : res.getDecompiledFunction().getC().split("\n")) println(line);
            } else {
                println("  (decompile failed)");
            }
            di.dispose();
        }
    }
}
