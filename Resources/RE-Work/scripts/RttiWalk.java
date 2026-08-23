// RttiWalk.java - resolve vftable from a TypeDescriptor name-string address.
// Usage: -postScript RttiWalk.java <hexTypeinfoAddr> [moreAddrs...] [slots=N]
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressIterator;
import ghidra.program.model.address.AddressSetView;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.mem.Memory;
import java.util.ArrayList;

public class RttiWalk extends GhidraScript {
    static long u64(byte[] b) {
        long v = 0;
        for (int i = 7; i >= 0; i--) v = (v << 8) | (b[i] & 0xffL);
        return v;
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) { println("usage: RttiWalk <hexAddr> [...] [slots=N]"); return; }
        int maxSlots = 28;
        long imageBase = currentProgram.getImageBase().getOffset();
        FunctionManager fm = currentProgram.getFunctionManager();
        Memory mem = currentProgram.getMemory();
        AddressSetView all = mem.getAllInitializedAddressSet();

        for (String arg : args) {
            if (arg.startsWith("slots=")) { maxSlots = Integer.parseInt(arg.substring(6)); continue; }
            long ti = Long.parseLong(arg.replaceFirst("^0x", ""), 16);
            println("TYPEINFO @0x" + Long.toHexString(ti));

            long tdRva = ti - 0x10 - imageBase;
            byte[] want = {(byte) tdRva, (byte) (tdRva >> 8), (byte) (tdRva >> 16), (byte) (tdRva >> 24)};
            ArrayList<Long> cols = new ArrayList<Long>();
            AddressIterator it = all.getAddresses(true);
            while (it.hasNext()) {
                Address a = it.next();
                byte[] b = new byte[4];
                if (mem.getBytes(a, b) == 4 && b[0] == want[0] && b[1] == want[1] && b[2] == want[2] && b[3] == want[3]) {
                    cols.add(a.getOffset() - 0xC);
                }
            }
            println("  COL candidates: " + cols.size());
            int found = 0;
            for (long col : cols) {
                AddressIterator it2 = all.getAddresses(true);
                while (it2.hasNext()) {
                    Address a2 = it2.next();
                    byte[] b2 = new byte[8];
                    if (mem.getBytes(a2, b2) == 8 && u64(b2) == col) {
                        StringBuilder sb = new StringBuilder();
                        Address va = a2.add(8 * 24);
                        try {
                            byte[] sl = new byte[8];
                            if (mem.getBytes(va, sl) == 8) {
                                long fn = u64(sl);
                                if (fn != 0) {
                                    Function f = fm.getFunctionContaining(currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(fn));
                                    sb.append("  [24] -> 0x").append(Long.toHexString(fn));
                                    if (f != null) sb.append(" ").append(f.getName());
                                }
                            }
                        } catch (Exception e) { /* skip */ }
                        println("VFTABLE @0x" + Long.toHexString(a2.getOffset() - imageBase)
                            + " (COL @0x" + Long.toHexString(col - imageBase) + ")" + sb.toString());
                        found++;
                        break;
                    }
                }
            }
            if (found == 0) println("  no vftable found");
        }
    }
}
