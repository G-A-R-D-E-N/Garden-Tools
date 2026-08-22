// List every CALL inside the function containing an RVA, with resolved target name.
// Usage: -postScript ListCallsIn.java aRVAhex
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;

public class ListCallsIn extends GhidraScript {
    @Override
    public void run() throws Exception {
        long imageBase = currentProgram.getImageBase().getOffset();
        for (String arg : getScriptArgs()) {
            long rva = Long.parseLong(arg.replace("0x", ""), 16);
            Function f = currentProgram.getFunctionManager().getFunctionContaining(toAddr(imageBase + rva));
            if (f == null) { println("### no function at 0x" + arg); continue; }
            println("### CALLS IN " + f.getName() + " (RVA 0x"
                    + Long.toHexString(f.getEntryPoint().getOffset() - imageBase) + ")");
            InstructionIterator it = currentProgram.getListing().getInstructions(f.getBody(), true);
            while (it.hasNext()) {
                Instruction ins = it.next();
                if (!ins.getMnemonicString().toUpperCase().startsWith("CALL")) continue;
                Address[] flows = ins.getFlows();
                String tgt = "(indirect)";
                if (flows != null && flows.length > 0) {
                    Function g = currentProgram.getFunctionManager().getFunctionAt(flows[0]);
                    tgt = (g != null ? g.getName() : "sub") + " @0x"
                          + Long.toHexString(flows[0].getOffset() - imageBase);
                }
                println(String.format("  site RVA 0x%s  %s  -> %s",
                        Long.toHexString(ins.getAddress().getOffset() - imageBase),
                        ins.getMnemonicString(), tgt));
            }
        }
    }
}
