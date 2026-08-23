// Robust cluster analysis: raw-scan .data/.rdata for key-string pointers (descriptor
// records), then intersect record/pointer-table locations against ALL code references
// taken from every disassembled .text instruction (getReferencesFrom).
// usage: ClusterRefs.java
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;

import java.util.*;
import java.util.regex.*;

public class ClusterRefs extends GhidraScript {
    Pattern KEY = Pattern.compile("^[bsuifach][A-Za-z0-9_.]+:[A-Za-z0-9_. ]+$");

    @Override
    public void run() throws Exception {
        long base = currentProgram.getImageBase().getOffset();
        // --- 1) raw scan for key strings in .rdata
        MemoryBlock rdataBlock = null;
        for (MemoryBlock mb : currentProgram.getMemory().getBlocks()) {
            if (mb.getName().equals(".rdata")) { rdataBlock = mb; break; }
        }
        byte[] rb = new byte[(int) rdataBlock.getSize()];
        rdataBlock.getBytes(rdataBlock.getStart(), rb);
        StringBuilder rsb = new StringBuilder(rb.length);
        for (byte b : rb) rsb.append((char) (b & 0xff));
        Map<Long, String> keyAtRva = new HashMap<>();
        Matcher m = Pattern.compile("[bsuifach][A-Za-z0-9_.]+\\x3a[A-Za-z0-9_. ]+").matcher(rsb);
        while (m.find()) {
            String s = m.group();
            if (KEY.matcher(s).matches()) {
                keyAtRva.put(rdataBlock.getStart().add(m.start()).getOffset() - base, s);
            }
        }
        // --- 2) raw scan .data/.rdata qwords for pointers to key strings
        //        loc -> keyRva ; record start = loc - 0x10
        Map<Long, Long> ptrLocToKey = new LinkedHashMap<>();
        for (MemoryBlock mb : currentProgram.getMemory().getBlocks()) {
            String nm = mb.getName();
            if (!nm.equals(".data") && !nm.equals(".rdata")) continue;
            long sz = mb.getSize();
            byte[] buf = new byte[(int) Math.min(sz, 0x40000000L)];
            mb.getBytes(mb.getStart(), buf);
            for (long i = 0; i + 8 <= buf.length; i += 8) {
                long q = 0;
                for (int j = 7; j >= 0; j--) q = (q << 8) | (buf[(int) (i + j)] & 0xff);
                long r = q - base;
                if (keyAtRva.containsKey(r)) {
                    long locRva = mb.getStart().add(i).getOffset() - base;
                    ptrLocToKey.put(locRva, r);
                }
            }
        }
        println("### key strings: " + keyAtRva.size() + "  pointer locations: " + ptrLocToKey.size());
        // --- 3) build set of ALL code references (target rva -> source rva)
        Set<Long> codeRefTargets = new HashSet<>();
        Map<Long, Long> codeRefSrc = new HashMap<>(); // target -> one example source
        Listing listing = currentProgram.getListing();
        InstructionIterator ii = listing.getInstructions(true);
        int n = 0;
        while (ii.hasNext()) {
            Instruction ins = ii.next();
            long from = ins.getAddress().getOffset() - base;
            for (Reference r : ins.getReferencesFrom()) {
                long t = r.getToAddress().getOffset() - base;
                codeRefTargets.add(t);
                codeRefSrc.putIfAbsent(t, from);
            }
            n++;
        }
        println("### instructions: " + n + "  unique code-ref targets: " + codeRefTargets.size());
        // --- 4) classify each key
        // record rva = ptrLoc - 0x10
        int direct = 0, indirect = 0, none = 0;
        List<String> noneKeys = new ArrayList<>();
        for (Map.Entry<Long, Long> e : ptrLocToKey.entrySet()) {
            long loc = e.getKey();
            long rec = loc - 0x10;
            String key = keyAtRva.get(e.getValue());
            if (codeRefTargets.contains(rec)) { direct++; continue; }
            if (codeRefTargets.contains(loc)) { indirect++; continue; }
            none++;
            if (noneKeys.size() < 60) noneKeys.add(key);
        }
        println("### direct code ref to record: " + direct);
        println("### code ref to pointer-table slot: " + indirect);
        println("### NO code path: " + none);
        for (String k : noneKeys) println("  NOPATH " + k);
        // also print the indirect keys for verification
        int shown = 0;
        for (Map.Entry<Long, Long> e : ptrLocToKey.entrySet()) {
            long loc = e.getKey(); long rec = loc - 0x10;
            if (codeRefTargets.contains(loc) && !codeRefTargets.contains(rec) && shown++ < 15) {
                println("  INDIRECT " + keyAtRva.get(e.getValue()) + "  slotRva=0x" + Long.toHexString(loc)
                        + "  refFrom=0x" + Long.toHexString(codeRefSrc.get(loc)));
            }
        }
    }
}
