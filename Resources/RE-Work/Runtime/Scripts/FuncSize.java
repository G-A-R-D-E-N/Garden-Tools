// Report a function's byte size (body length) at a given RVA. Usage: -postScript FuncSize.java <rva>
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

public class FuncSize extends GhidraScript {
    @Override
    public void run() throws Exception {
        long imageBase = currentProgram.getImageBase().getOffset();
        long rva = Long.parseLong(getScriptArgs()[0].replace("0x", "").replace("0X", ""), 16);
        Address at = toAddr(imageBase + rva);
        Function f = currentProgram.getFunctionManager().getFunctionContaining(at);
        if (f == null) { println("no function at RVA 0x" + Long.toHexString(rva)); return; }
        long size = f.getBody().getNumAddresses();
        println("FUNC " + f.getName(true) + " @ RVA 0x" + Long.toHexString(f.getEntryPoint().getOffset() - imageBase)
                + "  size=" + size + " bytes  paramCount=" + f.getParameterCount());
    }
}
