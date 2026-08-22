// Locate the workshop ground-snap JBE by wildcarded byte pattern.
// Usage: -postScript FindGroundSnap.java
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;

public class FindGroundSnap extends GhidraScript {
    @Override
    public void run() throws Exception {
        long imageBase = currentProgram.getImageBase().getOffset();
        // movss xmm8,[r14+0x18] ; andps xmm0,[rip+X] ; comiss xmm0,[rip+Y] ; jbe rel32
        String pat = "\\xf3\\x41\\x0f\\x10\\x46\\x18\\x0f\\x54\\x05....\\x0f\\x2f\\x05....\\x0f\\x86";
        Address a = currentProgram.getMinAddress();
        int n = 0;
        while (a != null) {
            Address hit = find(a, pat.getBytes()); // fallback below if null
            hit = findBytes(a, pat);
            if (hit == null) break;
            long jcc = hit.getOffset() + 20;   // offset of the 0F of the JBE within the pattern
            println("### HIT at 0x" + Long.toHexString(hit.getOffset())
                    + "  (RVA 0x" + Long.toHexString(hit.getOffset() - imageBase) + ")"
                    + "  -> JBE 0F at RVA 0x" + Long.toHexString(jcc - imageBase));
            byte[] b = new byte[8];
            currentProgram.getMemory().getBytes(toAddr(jcc), b);
            StringBuilder sb = new StringBuilder();
            for (byte x : b) sb.append(String.format("%02X ", x));
            println("    bytes at JBE: " + sb);
            a = hit.add(1);
            if (++n > 20) break;
        }
        if (n == 0) println("### NO HIT for the ground-snap pattern");
    }
}
