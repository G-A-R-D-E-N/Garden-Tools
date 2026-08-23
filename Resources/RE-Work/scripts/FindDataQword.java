// Scan program memory for a byte pattern; optionally decompile containing
// functions. Usage args:
//   find:0xRVA[,decompile]      -> search for the 8-byte LE VA of imageBase+RVA
//   find:bytes:<hexpattern>     -> search for raw bytes
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.Memory;
import java.util.TreeMap;

public class FindDataQword extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1 || !args[0].startsWith("find:")) {
            println("### usage: find:0xRVA[,decompile] | find:bytes:<hex>[,decompile]");
            return;
        }
        String rest = args[0].substring("find:".length());
        boolean wantDecompile = rest.endsWith(",decompile");
        rest = rest.replace(",decompile", "");
        long imageBase = currentProgram.getImageBase().getOffset();
        Memory mem = currentProgram.getMemory();
        FunctionManager fm = currentProgram.getFunctionManager();

        byte[] pat;
        String label;
        if (rest.startsWith("bytes:")) {
            String h = rest.substring("bytes:".length());
            pat = new byte[h.length() / 2];
            for (int i = 0; i < pat.length; i++) {
                pat[i] = (byte) Integer.parseInt(h.substring(2 * i, 2 * i + 2), 16);
            }
            label = "bytes " + h;
        } else {
            long rva = Long.parseLong(rest.replace("0x", "").replace("0X", ""), 16);
            long want = imageBase + rva;
            pat = new byte[8];
            for (int i = 0; i < 8; i++) pat[i] = (byte) ((want >> (8 * i)) & 0xFF);
            label = "ptr->RVA 0x" + Long.toHexString(rva);
        }

        TreeMap<Long, Address> hits = new TreeMap<>();
        Address found = mem.findBytes(currentProgram.getMinAddress(), pat, null, true, monitor);
        while (found != null) {
            hits.put(found.getOffset() - imageBase, found);
            found = mem.findBytes(found.add(1), pat, null, true, monitor);
        }
        println("=== searching " + label + ": " + hits.size() + " hit(s) ===");
        DecompInterface dec = null;
        if (wantDecompile) {
            dec = new DecompInterface();
            dec.openProgram(currentProgram);
        }
        for (var e : hits.entrySet()) {
            println("  at RVA 0x" + Long.toHexString(e.getKey()));
            if (!wantDecompile) continue;
            Function f = fm.getFunctionContaining(e.getValue());
            if (f == null) continue;
            println("### FUNC " + f.getName() + " RVA 0x"
                    + Long.toHexString(f.getEntryPoint().getOffset() - imageBase)
                    + " size=" + f.getBody().getNumAddresses());
            DecompileResults res = dec.decompileFunction(f, 120, monitor);
            println(res != null && res.decompileCompleted()
                    ? res.getDecompiledFunction().getC() : "### decompile failed");
        }
    }
}
