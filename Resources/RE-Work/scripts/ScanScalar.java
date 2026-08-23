// Walk all instructions; print those whose operand scalars equal
// imageBase+RVA. Usage args: find:0xRVA[,decompile]
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.scalar.Scalar;
import java.util.TreeMap;

public class ScanScalar extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1 || !args[0].startsWith("find:")) {
            println("### usage: find:0xRVA[,decompile]");
            return;
        }
        String rest = args[0].substring("find:".length());
        boolean wantDecompile = rest.endsWith(",decompile");
        long rva = Long.parseLong(rest.replace(",decompile", "")
                                       .replace("0x", "").replace("0X", ""), 16);
        long imageBase = currentProgram.getImageBase().getOffset();
        long want = imageBase + rva;
        FunctionManager fm = currentProgram.getFunctionManager();

        TreeMap<Long, Instruction> hits = new TreeMap<>();
        InstructionIterator it = currentProgram.getListing().getInstructions(true);
        while (it.hasNext()) {
            Instruction ins = it.next();
            int n = ins.getNumOperands();
            for (int i = 0; i < n; i++) {
                for (Object o : ins.getOpObjects(i)) {
                    if (o instanceof Scalar) {
                        if (((Scalar) o).getUnsignedValue() == want) {
                            hits.put(ins.getAddress().getOffset(), ins);
                        }
                    }
                }
            }
        }
        println("=== instructions referencing RVA 0x" + Long.toHexString(rva)
                + ": " + hits.size() + " ===");
        DecompInterface dec = null;
        if (wantDecompile) { dec = new DecompInterface(); dec.openProgram(currentProgram); }
        TreeMap<Long, Function> fns = new TreeMap<>();
        for (var e : hits.entrySet()) {
            println("  insn @ RVA 0x" + Long.toHexString(e.getKey() - imageBase)
                    + ": " + e.getValue());
            Function f = fm.getFunctionContaining(e.getValue().getAddress());
            if (f != null) fns.put(f.getEntryPoint().getOffset(), f);
        }
        for (var f : fns.values()) {
            println("### FUNC " + f.getName() + " RVA 0x"
                    + Long.toHexString(f.getEntryPoint().getOffset() - imageBase)
                    + " size=" + f.getBody().getNumAddresses());
            if (!wantDecompile) continue;
            DecompileResults res = dec.decompileFunction(f, 120, monitor);
            println(res != null && res.decompileCompleted()
                    ? res.getDecompiledFunction().getC() : "### decompile failed");
        }
    }
}
