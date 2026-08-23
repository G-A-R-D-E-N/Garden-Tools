// FindVftable.java — locate a class vftable from its RTTI name and dump slots.
// Usage: analyzeHeadless ... -postScript FindVftable.java "<RTTI name>" [maxSlots]
// The RTTI name is the mangled TypeDescriptor string, e.g. ".?AVTESObjectCELL@@".
// Chain: typeinfo string -> RTTICompleteObjectLocator (COL) -> vftable (slot 0 -> COL).
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSetView;
import ghidra.program.model.address.AddressIterator;
import java.util.ArrayList;

public class FindVftable extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) { println("usage: FindVftable <rttiName> [maxSlots]"); return; }
        String name = args[0];
        int maxSlots = args.length > 1 ? Integer.parseInt(args[1]) : 80;
        long imageBase = currentProgram.getImageBase().getOffset();
        FunctionManager fm = currentProgram.getFunctionManager();

        // 1. find the TypeDescriptor string
        Address typeInfo = null;
        DataIterator ddi = currentProgram.getListing().getDefinedData(true);
        while (ddi.hasNext()) {
            Data d = ddi.next();
            if (d.hasStringValue()) {
                String s = d.getDefaultValueRepresentation().replace("\"", "");
                if (s.equals(name)) { typeInfo = d.getAddress(); break; }
            }
        }
        if (typeInfo == null) { println("typeinfo string not found: " + name); return; }
        println("TYPEINFO @" + typeInfo + " (RVA 0x" + Long.toHexString(typeInfo.getOffset() - imageBase) + ")");

        Memory mem = currentProgram.getMemory();
        AddressSetView all = mem.getAllInitializedAddressSet();

        // 2. pass 1: find every 4-byte value == TypeDescriptor-base RVA  (x64
        //    COL.pTypeDescriptor). TypeDescriptor: { pVFTable +0, spare +8,
        //    name +0x10 } so base = name address - 0x10. x64 COL layout:
        //    signature+0, offset+4, cdOffset+8, pTypeDescriptor+0xC (RVA).
        //    So the 4-byte value sits at COL+0xC -> COL base = a - 0xC.
        ArrayList<Long> colBases = new ArrayList<Long>();
        long tdRva = typeInfo.getOffset() - 0x10 - imageBase;
        byte[] want = new byte[4];
        want[0] = (byte) (tdRva & 0xff); want[1] = (byte) ((tdRva >> 8) & 0xff);
        want[2] = (byte) ((tdRva >> 16) & 0xff); want[3] = (byte) ((tdRva >> 24) & 0xff);
        AddressIterator it = all.getAddresses(true);
        while (it.hasNext()) {
            Address a = it.next();
            byte[] b = new byte[4];
            if (mem.getBytes(a, b) == 4 && b[0] == want[0] && b[1] == want[1] && b[2] == want[2] && b[3] == want[3]) {
                colBases.add(a.getOffset() - 0xC);
            }
        }
        if (colBases.isEmpty()) { println("no COL candidates found"); return; }

        // 3. pass 2: find vftables (slot 0 == colBase), dump slots
        int found = 0;
        for (long colBase : colBases) {
            AddressIterator it2 = all.getAddresses(true);
            while (it2.hasNext()) {
                Address a2 = it2.next();
                byte[] b2 = new byte[8];
                if (mem.getBytes(a2, b2) == 8 && u64(b2) == colBase) {
                    println("VFTABLE @0x" + Long.toHexString(a2.getOffset() - imageBase) + "  (COL @0x" + Long.toHexString(colBase - imageBase) + ")");
                    dumpVftable(a2, maxSlots, imageBase, fm);
                    found++;
                }
            }
        }
        if (found == 0) println("no vftable found (COL candidates: " + colBases.size() + ")");
    }

    static long u64(byte[] b) {
        long v = 0;
        for (int k = 7; k >= 0; k--) v = (v << 8) | (b[k] & 0xff);
        return v;
    }

    void dumpVftable(Address vt, int maxSlots, long imageBase, FunctionManager fm) throws Exception {
        Memory mem = currentProgram.getMemory();
        for (int i = 0; i < maxSlots; i++) {
            Address slot = vt.add(i * 8);
            byte[] b = new byte[8];
            if (mem.getBytes(slot, b) != 8) break;
            long val = u64(b);
            if (val == 0) continue;
            Address target = toAddr(val);
            Function f = fm.getFunctionAt(target);
            String fn = f != null ? f.getName() : "(no fn)";
            println(String.format("  [%2d] @0x%08x -> 0x%08x %s", i, slot.getOffset() - imageBase, val - imageBase, fn));
        }
    }
}
