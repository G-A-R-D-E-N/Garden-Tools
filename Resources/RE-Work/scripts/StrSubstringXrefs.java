// For every defined string whose value contains <substr>, list functions
// referencing it. Usage args: <substr>[,decompile]
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import java.util.TreeMap;

public class StrSubstringXrefs extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) { println("### usage: <substr>[,decompile]"); return; }
        boolean wantDecompile = args[0].endsWith(",decompile");
        String substr = args[0].replace(",decompile", "");
        long imageBase = currentProgram.getImageBase().getOffset();
        FunctionManager fm = currentProgram.getFunctionManager();

        TreeMap<String, Function[]> out = new TreeMap<>();
        DataIterator di = currentProgram.getListing().getDefinedData(true);
        while (di.hasNext()) {
            Data d = di.next();
            if (!d.hasStringValue()) continue;
            String v = d.getValue().toString();
            if (!v.contains(substr)) continue;
            TreeMap<Long, Function> fns = new TreeMap<>();
            for (var ref : getReferencesTo(d.getAddress())) {
                Function f = fm.getFunctionContaining(ref.getFromAddress());
                if (f != null) fns.put(f.getEntryPoint().getOffset(), f);
            }
            if (fns.isEmpty()) continue;
            long rva = d.getAddress().getOffset() - imageBase;
            out.put("STR 0x" + Long.toHexString(rva) + " = " + v,
                    fns.values().toArray(new Function[0]));
        }
        DecompInterface dec = null;
        if (wantDecompile) { dec = new DecompInterface(); dec.openProgram(currentProgram); }
        for (var e : out.entrySet()) {
            println(e.getKey());
            for (Function f : e.getValue()) {
                long ep = f.getEntryPoint().getOffset() - imageBase;
                println("    <- " + f.getName() + " RVA 0x" + Long.toHexString(ep)
                        + " size=" + f.getBody().getNumAddresses());
            }
        }
        println("=== " + out.size() + " string(s) matched ===");
        if (wantDecompile) {
            for (var e : out.entrySet()) {
                for (Function f : e.getValue()) {
                    println("### FUNC " + f.getName() + " :: "
                            + f.getSignature().getPrototypeString());
                    DecompileResults res = dec.decompileFunction(f, 120, monitor);
                    println(res != null && res.decompileCompleted()
                            ? res.getDecompiledFunction().getC() : "### decompile failed");
                }
            }
        }
    }
}
