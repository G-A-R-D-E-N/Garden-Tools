// Find functions referencing an address; optionally decompile them.
// Usage args:
//   addr:0xRVA            -> list functions with a reference to imageBase+RVA
//   addr:0xRVA,decompile  -> also decompile each
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.block.BasicBlockModel;
import ghidra.program.model.block.CodeBlock;
import java.util.TreeMap;

public class XrefsTo extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1 || !args[0].startsWith("addr:")) {
            println("### usage: addr:0xRVA[,decompile]");
            return;
        }
        String rest = args[0].substring("addr:".length());
        boolean wantDecompile = rest.endsWith(",decompile");
        String hex = rest.replace(",decompile", "").replace("0x", "").replace("0X", "");
        long rva = Long.parseLong(hex, 16);
        long imageBase = currentProgram.getImageBase().getOffset();
        Address target = toAddr(imageBase + rva);
        FunctionManager fm = currentProgram.getFunctionManager();

        TreeMap<Long, Function> hits = new TreeMap<>();
        for (var ref : getReferencesTo(target)) {
            Address from = ref.getFromAddress();
            Function f = fm.getFunctionContaining(from);
            if (f == null) continue;
            hits.put(f.getEntryPoint().getOffset(), f);
        }
        // also catch intra-function data reads without formal refs: scan basic blocks containing target
        println("=== xrefs to RVA 0x" + Long.toHexString(rva) + ": "
                + hits.size() + " function(s) ===");
        DecompInterface di = null;
        if (wantDecompile) {
            di = new DecompInterface();
            di.openProgram(currentProgram);
        }
        for (Function f : hits.values()) {
            long ep = f.getEntryPoint().getOffset() - imageBase;
            println("### FUNC " + f.getName() + " RVA 0x" + Long.toHexString(ep)
                    + " size=" + f.getBody().getNumAddresses()
                    + " :: " + f.getSignature().getPrototypeString());
            if (wantDecompile) {
                DecompileResults res = di.decompileFunction(f, 120, monitor);
                if (res != null && res.decompileCompleted()) {
                    println(res.getDecompiledFunction().getC());
                } else {
                    println("### decompile failed");
                }
            }
        }
    }
}
