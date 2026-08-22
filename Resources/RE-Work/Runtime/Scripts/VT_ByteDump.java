// One-off diagnostic: dump raw bytes at a given RVA from the CURRENT program's memory, to check
// whether the imported program bytes are sane plaintext code (vs. the on-disk exe file, which may
// have drifted/been replaced with a DRM-wrapped copy since the Ghidra project was built).
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.Memory;

public class VT_ByteDump extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        long imageBase = currentProgram.getImageBase().getOffset();
        Memory mem = currentProgram.getMemory();
        for (String arg : args) {
            String[] parts = arg.split(",");
            long rva = Long.parseLong(parts[0].replace("0x", "").replace("0X", ""), 16);
            int len = parts.length > 1 ? Integer.parseInt(parts[1]) : 32;
            Address addr = toAddr(imageBase + rva);
            byte[] buf = new byte[len];
            try {
                mem.getBytes(addr, buf);
                StringBuilder hex = new StringBuilder();
                for (byte b : buf) hex.append(String.format("%02x ", b));
                println("RVA 0x" + Long.toHexString(rva) + " (" + len + "B): " + hex.toString());
            } catch (Exception e) {
                println("RVA 0x" + Long.toHexString(rva) + ": ERROR " + e.getMessage());
            }
        }
    }
}
