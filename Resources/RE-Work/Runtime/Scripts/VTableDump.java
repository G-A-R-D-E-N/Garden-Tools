// Dump the raw function-pointer slots of a named vtable data symbol.
// Usage: -postScript VTableDump.java VTABLE_Name [VTABLE_Name2 ...]
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolTable;
import ghidra.program.model.mem.Memory;

import java.util.List;

public class VTableDump extends GhidraScript {
    @Override
    public void run() throws Exception {
        List<String> names = new java.util.ArrayList<>();
        if (getScriptArgs() != null) {
            for (String a : getScriptArgs()) names.add(a);
        }
        SymbolTable st = currentProgram.getSymbolTable();
        Memory mem = currentProgram.getMemory();
        Address imageBase = currentProgram.getImageBase();
        println("=== IMAGE BASE: " + imageBase + " ===");

        for (String name : names) {
            println("### VTABLE: " + name);
            Address vaddr = null;
            for (Symbol s : st.getAllSymbols(true)) {
                if (s.getName().equals(name)) { vaddr = s.getAddress(); break; }
            }
            if (vaddr == null) { println("  NOT FOUND"); continue; }
            println("  @ " + vaddr);
            for (int i = 0; i < 40; i++) {
                Address slot = vaddr.add((long) i * 8);
                long ptr;
                try { ptr = mem.getLong(slot); } catch (Exception e) { println("  [" + i + "] <read error>"); break; }
                if (ptr == 0) { println("  [" + i + "] 0x0 -- stop"); break; }
                Address target = imageBase.getNewAddress(ptr);
                Function f = getFunctionAt(target);
                if (f == null) {
                    // not a function start -- likely ran past the end of this vtable into the next symbol
                    println("  [" + i + "] -> 0x" + Long.toHexString(ptr) + "  <no function here, stopping>");
                    break;
                }
                println("  [" + i + "] -> 0x" + Long.toHexString(ptr) + "  " + f.getName(true) + "  :: " + f.getSignature());
            }
        }
    }
}
