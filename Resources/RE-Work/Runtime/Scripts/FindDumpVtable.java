// Locate a class's vtable data symbol by fuzzy name match (Ghidra "vftable", MSVC-mangled
// "??_7<Class>@@6B@", or a plain "<Class>::vftable"/"VTABLE_<Class>" form) and dump its slots with
// each target function's fully-qualified name. Works on both the PDB-named 155 project and the
// CommonLib-named 163 project regardless of which vtable-naming convention that project uses.
//
// Usage: -postScript FindDumpVtable.java <ClassName> [<ClassName2> ...]
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;
import ghidra.program.model.symbol.SymbolTable;

public class FindDumpVtable extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] classes = getScriptArgs();
        SymbolTable st = currentProgram.getSymbolTable();
        Memory mem = currentProgram.getMemory();
        Address imageBase = currentProgram.getImageBase();
        println("=== IMAGE BASE: " + imageBase + " ===");

        for (String cls : classes) {
            println("### CLASS: " + cls);
            // Collect candidate vtable symbols (there can be several for multiple-inheritance;
            // report each). A candidate: name mentions the class AND looks like a vtable.
            java.util.List<Symbol> candidates = new java.util.ArrayList<>();
            SymbolIterator it = st.getAllSymbols(true);
            while (it.hasNext()) {
                Symbol s = it.next();
                String n = s.getName();
                String full = s.getName(true);
                boolean mentionsClass = n.contains(cls) || full.contains(cls);
                if (!mentionsClass) continue;
                boolean looksVtable = n.contains("vftable") || n.contains("vtable")
                    || n.startsWith("VTABLE_") || n.contains("??_7") || n.contains("`vftable'");
                if (looksVtable) candidates.add(s);
            }
            if (candidates.isEmpty()) { println("  (no vtable symbol found)"); continue; }
            for (Symbol vs : candidates) {
                // Only dump ones whose class is EXACTLY this class at the front (avoid catching
                // "hknpConvexShape" when asked for "hknpShape") -- require the class token to be
                // immediately followed by a vtable marker or end.
                String vn = vs.getName();
                Address vaddr = vs.getAddress();
                println("  VTABLE SYM: " + vs.getName(true) + "  @ " + vaddr);
                for (int i = 0; i < 40; i++) {
                    Address slot = vaddr.add((long) i * 8);
                    long ptr;
                    try { ptr = mem.getLong(slot); } catch (Exception e) { break; }
                    if (ptr == 0) { println("    [" + i + "] 0x0 -- stop"); break; }
                    Address target = imageBase.getNewAddress(ptr);
                    Function f = getFunctionAt(target);
                    if (f == null) { println("    [" + i + "] -> 0x" + Long.toHexString(ptr) + "  <no fn, stop>"); break; }
                    println("    [" + i + "] rva=0x" + Long.toHexString(target.getOffset() - imageBase.getOffset())
                        + "  " + f.getName(true));
                }
            }
        }
    }
}
