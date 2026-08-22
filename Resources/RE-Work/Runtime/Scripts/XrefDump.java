// Dump the data globals (read/write/data refs) touched by the function containing each given RVA,
// with the target's primary symbol name and RVA. This is the xref trail behind a decompiled global
// access (e.g. which absolute address a `mov rax,[global]` actually loads) -- no offset assumptions.
// Usage: -postScript XrefDump.java aRVAhex [anotherRVAhex ...]
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.RefType;
import ghidra.program.model.symbol.Symbol;

public class XrefDump extends GhidraScript {
    @Override
    public void run() throws Exception {
        long imageBase = currentProgram.getImageBase().getOffset();
        for (String arg : getScriptArgs()) {
            String hex = arg.replace("0x", "").replace("0X", "");
            long rva = Long.parseLong(hex, 16);
            Address at = toAddr(imageBase + rva);
            Function f = currentProgram.getFunctionManager().getFunctionContaining(at);
            if (f == null) { println("### 0x" + hex + ": no function here"); continue; }
            println("### XREFS in " + f.getName(true)
                    + " @ RVA 0x" + Long.toHexString(f.getEntryPoint().getOffset() - imageBase));
            InstructionIterator it = currentProgram.getListing().getInstructions(f.getBody(), true);
            while (it.hasNext()) {
                Instruction ins = it.next();
                for (Reference ref : ins.getReferencesFrom()) {
                    RefType rt = ref.getReferenceType();
                    if (!(rt.isData() || rt.isRead() || rt.isWrite())) continue;
                    Address to = ref.getToAddress();
                    long toRva = to.getOffset() - imageBase;
                    Symbol s = currentProgram.getSymbolTable().getPrimarySymbol(to);
                    String kind = rt.isWrite() ? "W" : (rt.isRead() ? "R" : "D");
                    println(String.format("  %s  %-7s [%s] -> 0x%s (RVA 0x%s)  %s",
                        ins.getAddress(), ins.getMnemonicString(), kind,
                        Long.toHexString(to.getOffset()), Long.toHexString(toRva),
                        s != null ? s.getName(true) : "(unnamed)"));
                }
            }
        }
    }
}
