// Label RTTI type_info structures and class vtables from the embedded MSVC RTTI name strings.
//
// The RTTI analyzer did not model type_info references, so this does a raw scan:
//   1. build a map: 8-byte-aligned qword in .rdata -> its address (only qwords that point
//      into .rdata, where the RTTI strings live)
//   2. each ".?AVClass@@" string address S -> all qword slots holding S are _m_d_name fields
//      of type_info objects (type_info = slot-8)
//   3. each type_info address T -> all qword slots holding T are the second slot of a class
//      vftable (vftable = slot-8)
// Labels:
//   type_info -> "typeinfo_<Class>",  vftable -> "vft_<Class>"
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.symbol.SymbolTable;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import java.util.HashMap;
import java.util.ArrayList;
import java.util.List;

public class RTTI_ClassNames extends GhidraScript {
    @Override
    public void run() throws Exception {
        SymbolTable st = currentProgram.getSymbolTable();
        long base = currentProgram.getImageBase().getOffset();
        Memory mem = currentProgram.getMemory();
        MemoryBlock rdata = mem.getBlock(".rdata");
        if (rdata == null) { println("no .rdata"); return; }
        long rdataStart = rdata.getStart().getOffset();
        long rdataSize = rdata.getSize();

        // 1. pointer index: qword -> list of addresses (only qwords pointing into .rdata)
        HashMap<Long, ArrayList<Long>> idx = new HashMap<>();
        byte[] buf = new byte[8];
        long nQ = rdataSize / 8;
        for (long i = 0; i < nQ; i++) {
            long a = rdataStart + i * 8;
            mem.getBytes(toAddr(a), buf);
            long v = 0;
            for (int k = 7; k >= 0; k--) v = (v << 8) | (buf[k] & 0xFFL);
            if (v >= rdataStart && v < rdataStart + rdataSize) {
                idx.computeIfAbsent(v, k -> new ArrayList<>()).add(a);
            }
        }
        println("indexed " + idx.size() + " unique rdata pointers");

        // 2. walk defined strings
        int n = 0, v = 0;
        DataIterator di = currentProgram.getListing().getDefinedData(true);
        while (di.hasNext()) {
            Data d = di.next();
            if (!d.hasStringValue()) continue;
            String s = d.getDefaultValueRepresentation();
            if (s == null || s.length() < 8) continue;
            if (!(s.startsWith(".?AV") || s.startsWith(".?AU"))) continue;
            String cls = s.substring(3);
            int at = cls.indexOf("@@");
            if (at <= 0) continue;
            cls = cls.substring(0, at).replace('@', ':');
            if (cls.length() > 140) continue;
            long sAddr = d.getAddress().getOffset();

            // type_info slots: qwords equal to sAddr
            List<Long> slots = idx.get(sAddr);
            if (slots == null) continue;
            for (long slot : slots) {
                long ti = slot - 8;
                String tid = "typeinfo_" + cls.replace(':', '_');
                if (st.getSymbol(tid, toAddr(ti), null) == null) {
                    st.createLabel(toAddr(ti), tid, SourceType.USER_DEFINED);
                    n++;
                }
                // 3. vftables: qwords equal to ti (vftable second slot)
                List<Long> vslots = idx.get(ti);
                if (vslots != null) {
                    for (long vslot : vslots) {
                        long vft = vslot - 8;
                        String vname = "vft_" + cls.replace(':', '_');
                        if (st.getSymbol(vname, toAddr(vft), null) == null) {
                            st.createLabel(toAddr(vft), vname, SourceType.USER_DEFINED);
                            v++;
                        }
                    }
                }
            }
            if (n % 500 == 0) println("processed " + n + " typeinfos, " + v + " vtables");
        }
        println("DONE: " + n + " type_info labeled, " + v + " vtables labeled");
    }
}
