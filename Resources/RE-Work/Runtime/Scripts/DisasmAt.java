// Disassemble+define a function at RVA and print instructions (to see call targets).
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;

public class DisasmAt extends GhidraScript {
    @Override
    public void run() throws Exception {
        long imageBase = currentProgram.getImageBase().getOffset();
        for (String arg : getScriptArgs()) {
            String hex = arg.replace("0x", "").replace("0X", "");
            long rva = Long.parseLong(hex, 16);
            Address addr = toAddr(imageBase + rva);
            Function f = currentProgram.getFunctionManager().getFunctionContaining(addr);
            if (f == null) {
                try { disassemble(addr); } catch (Exception e) {}
                try { f = createFunction(addr, null); } catch (Exception e) {}
                if (f == null) f = currentProgram.getFunctionManager().getFunctionContaining(addr);
            }
            if (f == null) { println("### 0x" + hex + ": could not define"); continue; }
            println("### DISASM " + f.getName(true) + " @ 0x" + Long.toHexString(f.getEntryPoint().getOffset()));
            InstructionIterator it = currentProgram.getListing().getInstructions(f.getBody(), true);
            while (it.hasNext()) {
                Instruction ins = it.next();
                println("0x" + Long.toHexString(ins.getAddress().getOffset()) + "  " + ins.toString());
            }
        }
    }
}
